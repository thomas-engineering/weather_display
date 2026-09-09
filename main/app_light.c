#include "app_light.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_video_init.h"
#include "esp_video_device.h"
#include "linux/videodev2.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

static const char *TAG = "light";

#define SAMPLE_INTERVAL_MS   5000
#define SAMPLE_POINTS        2000   /* pixels averaged per frame, not the whole frame */
#define DQBUF_RETRIES        20
#define DQBUF_RETRY_DELAY_MS 50
#define BUF_COUNT            2
#define BRIGHTNESS_MIN       10
#define BRIGHTNESS_MAX       100
/* The OV5647's own AEC/AGC keeps the sensor's own exposure roughly correct
 * for whatever it's pointed at, which compresses average luma into a much
 * narrower band than the theoretical 0-255: on-hardware calibration (this
 * board, this lens) measured luma=1 covered, luma=60 in a dim room on an
 * overcast day, luma=117 pointed directly at a torch. Scaling against 255
 * would leave a merely bright room stuck around half brightness — scale
 * against this measured ceiling instead so typical bright-but-not-blinding
 * light reaches near BRIGHTNESS_MAX. Re-measure if the sensor, lens, or
 * mounting position changes. */
#define LUMA_CEILING         130
/* Report a new brightness only once it moves by this much, so sensor/AEC
 * noise doesn't chatter the backlight PWM every sample. */
#define CHANGE_THRESHOLD_PCT 3

static bool s_available;
static bool s_adaptive;
static int s_video_fd = -1;
static void *s_buf[BUF_COUNT];
static uint32_t s_buf_mem_type;
static app_light_brightness_cb_t s_cb;
static int s_last_reported_pct = -1;
static int s_ema_luma = -1;
static TaskHandle_t s_task;

/* RGB565, little-endian, sampled at a stride rather than every pixel — a
 * coarse ambient-brightness estimate doesn't need full-resolution accuracy. */
static uint8_t average_luma(const uint8_t *buf, size_t len) {
    size_t count = len / 2;
    if (count == 0) return 0;
    const uint16_t *px = (const uint16_t *)buf;
    size_t step = count / SAMPLE_POINTS;
    if (step < 1) step = 1;

    uint64_t sum = 0;
    size_t n = 0;
    for (size_t i = 0; i < count; i += step) {
        uint16_t v = px[i];
        uint8_t r = ((v >> 11) & 0x1F) * 255 / 31;
        uint8_t g = ((v >> 5) & 0x3F) * 255 / 63;
        uint8_t b = (v & 0x1F) * 255 / 31;
        sum += (r * 299 + g * 587 + b * 114) / 1000; /* Rec. 601 luma */
        n++;
    }
    return n ? (uint8_t)(sum / n) : 0;
}

static bool open_and_configure(void) {
    s_video_fd = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDONLY | O_NONBLOCK, 0);
    if (s_video_fd < 0) {
        ESP_LOGI(TAG, "no CSI video device (no camera fitted?)");
        return false;
    }

    struct v4l2_capability cap;
    if (ioctl(s_video_fd, VIDIOC_QUERYCAP, &cap) != 0) {
        ESP_LOGI(TAG, "CSI device present but no sensor responded");
        goto fail;
    }
    ESP_LOGI(TAG, "camera detected: %s", cap.card);

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(s_video_fd, VIDIOC_G_FMT, &fmt) != 0) {
        ESP_LOGW(TAG, "failed to read default format");
        goto fail;
    }
    if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_RGB565) {
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
        if (ioctl(s_video_fd, VIDIOC_S_FMT, &fmt) != 0) {
            ESP_LOGW(TAG, "sensor won't do RGB565, can't average luma from it");
            goto fail;
        }
    }

    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = BUF_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(s_video_fd, VIDIOC_REQBUFS, &req) != 0) {
        ESP_LOGW(TAG, "buffer request failed");
        goto fail;
    }
    s_buf_mem_type = req.memory;

    for (uint32_t i = 0; i < req.count; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ioctl(s_video_fd, VIDIOC_QUERYBUF, &buf) != 0) {
            ESP_LOGW(TAG, "querybuf failed");
            goto fail;
        }
        s_buf[i] = mmap(NULL, buf.length, PROT_READ, MAP_SHARED, s_video_fd, buf.m.offset);
        if (s_buf[i] == MAP_FAILED) {
            ESP_LOGW(TAG, "mmap failed");
            goto fail;
        }
        if (ioctl(s_video_fd, VIDIOC_QBUF, &buf) != 0) {
            ESP_LOGW(TAG, "qbuf failed");
            goto fail;
        }
    }

    /* FIX: this used to STREAMON/STREAMOFF around every single sample.
     * VIDIOC_STREAMOFF returns all queued buffers to the driver's dequeued
     * state, so the *next* STREAMON had nothing queued to fill until the
     * buffers were re-queued — and if that left the driver waiting on
     * something that never arrived, the blocking ioctl() call would hang with
     * no chance to log anything, which is exactly the silent hang observed on
     * hardware (camera detected, adaptive enabled, then nothing, ever).
     * Streaming continuously and only dequeuing/requeuing per sample avoids
     * that lifecycle entirely — it only needs a frame every few seconds, but
     * there's no V4L2-supported way to say that without stopping the stream. */
    ESP_LOGI(TAG, "starting continuous capture stream");
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(s_video_fd, VIDIOC_STREAMON, &type) != 0) {
        ESP_LOGW(TAG, "streamon failed");
        goto fail;
    }
    ESP_LOGI(TAG, "capture stream started");

    return true;

fail:
    close(s_video_fd);
    s_video_fd = -1;
    return false;
}

/* Dequeues whatever frame is newest, computes its luma, and immediately
 * requeues the buffer — the stream itself is already running continuously
 * (see the FIX note in open_and_configure()) so this never touches
 * STREAMON/STREAMOFF. */
static bool capture_one_frame(uint8_t *out_luma) {
    struct v4l2_buffer buf;
    bool got_frame = false;
    ESP_LOGD(TAG, "waiting for a frame (up to %d ms)", DQBUF_RETRIES * DQBUF_RETRY_DELAY_MS);
    for (int attempt = 0; attempt < DQBUF_RETRIES; attempt++) {
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = s_buf_mem_type;
        int r = ioctl(s_video_fd, VIDIOC_DQBUF, &buf);
        if (r == 0) {
            got_frame = true;
            break;
        }
        if (errno != EAGAIN) {
            ESP_LOGW(TAG, "dqbuf failed: errno=%d", errno);
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(DQBUF_RETRY_DELAY_MS));
    }

    if (!got_frame) {
        ESP_LOGW(TAG, "no frame within %d ms, skipping this sample",
                 DQBUF_RETRIES * DQBUF_RETRY_DELAY_MS);
        return false;
    }

    ESP_LOGD(TAG, "frame received (index=%u, bytesused=%u), computing luma",
             buf.index, buf.bytesused);
    *out_luma = average_luma(s_buf[buf.index], buf.bytesused ? buf.bytesused : buf.length);
    if (ioctl(s_video_fd, VIDIOC_QBUF, &buf) != 0) {
        ESP_LOGW(TAG, "requeue (qbuf) failed: errno=%d", errno);
        return false;
    }
    return true;
}

static void light_sensor_task(void *arg) {
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_INTERVAL_MS));
        if (!s_adaptive) {
            /* DEBUG, not INFO: this is the expected steady state whenever the
             * switch is off, so it would otherwise spam the log forever. Its
             * only purpose is to distinguish "off" from "task died" if asked. */
            ESP_LOGD(TAG, "adaptive mode off, skipping sample");
            continue;
        }

        uint8_t luma;
        if (!capture_one_frame(&luma)) continue;

        /* Exponential smoothing: the OV5647's own auto-exposure already fights
         * ambient changes somewhat (see app_light.h / the brightness README
         * note), so this is a coarse "did the room get darker/brighter" signal,
         * not a calibrated lux reading — smoothing keeps momentary shadows and
         * AEC settling from flickering the backlight. */
        s_ema_luma = (s_ema_luma < 0) ? luma : (s_ema_luma * 3 + luma) / 4;

        int pct = BRIGHTNESS_MIN + (s_ema_luma * (BRIGHTNESS_MAX - BRIGHTNESS_MIN)) / LUMA_CEILING;
        if (pct < BRIGHTNESS_MIN) pct = BRIGHTNESS_MIN;
        if (pct > BRIGHTNESS_MAX) pct = BRIGHTNESS_MAX;

        /* Previously silent on the success path — a working sample-and-apply
         * cycle produced zero log output, indistinguishable from adaptive
         * mode being off or every sample failing. Log every reading, note
         * separately when CHANGE_THRESHOLD_PCT suppresses actually applying it. */
        if (s_last_reported_pct < 0 || abs(pct - s_last_reported_pct) >= CHANGE_THRESHOLD_PCT) {
            ESP_LOGI(TAG, "luma=%u ema=%d -> brightness=%d%% (was %d%%)",
                     luma, s_ema_luma, pct, s_last_reported_pct);
            s_last_reported_pct = pct;
            if (s_cb) s_cb(pct);
        } else {
            ESP_LOGD(TAG, "luma=%u ema=%d -> brightness=%d%% (unchanged, below %d%% threshold)",
                      luma, s_ema_luma, pct, CHANGE_THRESHOLD_PCT);
        }
    }
}

bool app_light_init(i2c_master_bus_handle_t i2c_bus) {
    esp_video_init_csi_config_t csi_config = {
        .sccb_config = {
            .init_sccb = false,
            .i2c_handle = i2c_bus,
            .freq = 100000,
        },
        .reset_pin = -1,
        .pwdn_pin = -1,
        .dont_init_ldo = false,
    };
    esp_video_init_config_t cam_config = { .csi = &csi_config };

    esp_err_t err = esp_video_init(&cam_config);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "esp_video_init: %s (no camera fitted?)", esp_err_to_name(err));
        return false;
    }

    s_available = open_and_configure();
    if (!s_available) {
        esp_video_deinit();
        return false;
    }

    xTaskCreatePinnedToCore(light_sensor_task, "light_sensor", 4096, NULL, 3, &s_task, 0);
    return true;
}

bool app_light_available(void) { return s_available; }

void app_light_set_callback(app_light_brightness_cb_t cb) { s_cb = cb; }

void app_light_set_adaptive(bool enabled) {
    if (!s_available) {
        ESP_LOGW(TAG, "adaptive mode requested but no camera is available, ignoring");
        return;
    }
    ESP_LOGI(TAG, "adaptive brightness %s", enabled ? "enabled" : "disabled");
    s_adaptive = enabled;
    if (!enabled) {
        s_ema_luma = -1;
        s_last_reported_pct = -1;
    }
}
