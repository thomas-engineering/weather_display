#include "app_light.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_video_init.h"
#include "esp_video_device.h"
#include "light_policy.h"
#include "linux/videodev2.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
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
/* Gives the sensor's own AEC/AGC time to converge after the stream starts, so
 * the first sample isn't a too-dark pre-exposure frame — on hardware this
 * showed up as the backlight dropping to minimum on every boot with adaptive
 * mode already on, because the very first frame off a cold stream is
 * evaluated before the sensor has adjusted to the actual scene. */
#define SETTLE_MS            300

static bool s_available;         /* camera responded when app_light_init() probed it */
static volatile bool s_adaptive; /* requested by the UI; read by light_sensor_task */
static bool s_streaming;         /* owned entirely by light_sensor_task */
static int s_video_fd = -1;
static void *s_buf[BUF_COUNT];
static size_t s_buf_len[BUF_COUNT];
static int s_buf_mapped_count;   /* how many of s_buf[] are valid mmap()s, for cleanup */
static uint32_t s_buf_mem_type;
static app_light_brightness_cb_t s_cb;
static light_policy_t s_policy;
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

static void unmap_buffers(void) {
    for (int i = 0; i < s_buf_mapped_count; i++) {
        munmap(s_buf[i], s_buf_len[i]);
        s_buf[i] = NULL;
        s_buf_len[i] = 0;
    }
    s_buf_mapped_count = 0;
}

/* Opens the CSI device, negotiates RGB565, and queues BUF_COUNT mmap'd
 * buffers — but does not start the stream. Called only from
 * start_streaming(). On any failure, cleans up whatever it already
 * allocated (buffers mapped so far, then the fd) instead of leaking. */
static bool open_and_configure(void) {
    s_video_fd = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDONLY | O_NONBLOCK, 0);
    if (s_video_fd < 0) {
        ESP_LOGW(TAG, "failed to open CSI video device");
        return false;
    }

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(s_video_fd, VIDIOC_G_FMT, &fmt) != 0) {
        ESP_LOGW(TAG, "failed to read default format");
        goto fail;
    }
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
    if (ioctl(s_video_fd, VIDIOC_S_FMT, &fmt) != 0) {
        ESP_LOGW(TAG, "sensor won't do RGB565, can't average luma from it");
        goto fail;
    }
    /* FIX: a successful S_FMT does not guarantee the driver actually granted
     * what was asked for — V4L2 allows it to negotiate the closest format it
     * can do instead. average_luma() unconditionally reinterprets the buffer
     * as RGB565, so trusting an unchecked format here would silently feed it
     * garbage that still looks like a plausible luma value. */
    if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_RGB565) {
        ESP_LOGW(TAG, "sensor negotiated pixel format 0x%" PRIx32 " instead of RGB565, refusing",
                 (uint32_t)fmt.fmt.pix.pixelformat);
        goto fail;
    }
    ESP_LOGI(TAG, "negotiated %" PRIu32 "x%" PRIu32 " RGB565, %" PRIu32 " bytes/frame",
             (uint32_t)fmt.fmt.pix.width, (uint32_t)fmt.fmt.pix.height, (uint32_t)fmt.fmt.pix.sizeimage);

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
        void *mapped = mmap(NULL, buf.length, PROT_READ, MAP_SHARED, s_video_fd, buf.m.offset);
        if (mapped == MAP_FAILED) {
            ESP_LOGW(TAG, "mmap failed");
            goto fail;
        }
        s_buf[i] = mapped;
        s_buf_len[i] = buf.length;
        s_buf_mapped_count = i + 1; /* only now is this slot safe to munmap on cleanup */
        if (ioctl(s_video_fd, VIDIOC_QBUF, &buf) != 0) {
            ESP_LOGW(TAG, "qbuf failed");
            goto fail;
        }
    }

    return true;

fail:
    unmap_buffers();
    close(s_video_fd);
    s_video_fd = -1;
    return false;
}

/* Dequeues exactly one frame and requeues it immediately after reading its
 * luma — never holds more than one of the two buffers out of the driver's
 * rotation at a time.
 *
 * An earlier version of this function tried to drain every frame currently
 * queued and keep only the newest, on the theory that V4L2's oldest-first
 * DQBUF ordering would otherwise mean acting on a stale backlog. On this
 * driver that hung completely: with only BUF_COUNT=2 buffers and no backup
 * buffer, dequeuing one without immediately giving it back stalls the CSI
 * capture pipeline outright, so a second DQBUF issued before the first is
 * requeued never completes — confirmed on hardware, where the log went
 * silent forever right after the first successful dequeue. The self-limiting
 * effect of only ever having two buffers in flight already keeps the
 * oldest-available frame within roughly one frame period (~20ms at 50fps) of
 * current, since the driver simply stops capturing once both buffers are
 * full and waiting to be collected — there is no unbounded backlog to drain
 * in the first place. */
static bool capture_one_frame(uint8_t *out_luma) {
    struct v4l2_buffer buf;
    for (int attempt = 0; attempt < DQBUF_RETRIES; attempt++) {
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = s_buf_mem_type;
        if (ioctl(s_video_fd, VIDIOC_DQBUF, &buf) == 0) {
            *out_luma = average_luma(s_buf[buf.index], buf.bytesused ? buf.bytesused : buf.length);
            if (ioctl(s_video_fd, VIDIOC_QBUF, &buf) != 0) {
                ESP_LOGW(TAG, "requeue (qbuf) failed: errno=%d", errno);
                return false;
            }
            return true;
        }
        if (errno != EAGAIN) {
            ESP_LOGW(TAG, "dqbuf failed: errno=%d", errno);
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(DQBUF_RETRY_DELAY_MS));
    }
    ESP_LOGW(TAG, "no frame within %d ms, skipping this sample",
             DQBUF_RETRIES * DQBUF_RETRY_DELAY_MS);
    return false;
}

/* Only called from light_sensor_task. Opens the device, negotiates the
 * format, queues buffers, and starts the stream — i.e. everything that costs
 * PSRAM and DMA/CSI bandwidth happens here, not at app_light_init() time, so
 * a board with adaptive mode turned off (or the switch simply never touched)
 * doesn't pay for a continuously running 50 fps capture it never reads. */
static bool start_streaming(void) {
    if (s_streaming) return true;
    if (!open_and_configure()) return false;

    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(s_video_fd, VIDIOC_STREAMON, &type) != 0) {
        ESP_LOGW(TAG, "streamon failed");
        unmap_buffers();
        close(s_video_fd);
        s_video_fd = -1;
        return false;
    }

    /* Just the delay, deliberately not a "drain until empty" loop: with the
     * stream running continuously, a fresh frame becomes ready again within
     * milliseconds of draining the last one, so that loop never naturally
     * exits — it hung exactly like the original STREAMON/STREAMOFF cycling
     * bug this file's history already fixed once. drain_latest_frame() in
     * the normal per-sample path already always takes the newest frame, so
     * once SETTLE_MS has passed, the very next capture_one_frame() call
     * below is already guaranteed to see a post-settle frame. */
    vTaskDelay(pdMS_TO_TICKS(SETTLE_MS));

    light_policy_reset(&s_policy);
    s_streaming = true;
    ESP_LOGI(TAG, "capture stream started");
    return true;
}

/* Only called from light_sensor_task (and app_light_deinit(), which must not
 * run on that task). Safe to call whether or not a stream is actually up. */
static void stop_streaming(void) {
    if (s_video_fd >= 0) {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(s_video_fd, VIDIOC_STREAMOFF, &type);
        unmap_buffers();
        close(s_video_fd);
        s_video_fd = -1;
    }
    if (s_streaming) ESP_LOGI(TAG, "capture stream stopped");
    s_streaming = false;
}

static void light_sensor_task(void *arg) {
    (void)arg;
    for (;;) {
        /* Waits up to one sample interval, but app_light_set_adaptive() wakes
         * this immediately via a task notification — so turning the switch
         * on doesn't wait up to SAMPLE_INTERVAL_MS for the first reading, and
         * turning it off tears the stream down right away instead of leaving
         * the camera running for a flag nothing is reading anymore. */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(SAMPLE_INTERVAL_MS));

        bool want = s_adaptive;
        if (want && !s_streaming) {
            if (!start_streaming()) {
                ESP_LOGW(TAG, "failed to start capture stream, will retry next tick");
                continue;
            }
        } else if (!want && s_streaming) {
            stop_streaming();
            continue;
        }

        if (!want || !s_streaming) continue;

        uint8_t luma;
        if (!capture_one_frame(&luma)) continue;

        int pct;
        if (light_policy_sample(&s_policy, luma, &pct)) {
            ESP_LOGI(TAG, "luma=%u ema=%d -> brightness=%d%%", luma, s_policy.ema_luma, pct);
            if (s_cb) s_cb(pct);
        } else {
            ESP_LOGD(TAG, "luma=%u ema=%d -> brightness unchanged (below %d%% threshold)",
                     luma, s_policy.ema_luma, LIGHT_POLICY_CHANGE_THRESHOLD_PCT);
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

    /* Detect-only: open, probe, close immediately. No buffers are allocated
     * and nothing streams until adaptive mode is actually turned on — see
     * start_streaming(). */
    int fd = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDONLY | O_NONBLOCK, 0);
    if (fd < 0) {
        ESP_LOGI(TAG, "no CSI video device (no camera fitted?)");
        esp_video_deinit();
        return false;
    }
    struct v4l2_capability cap;
    bool responded = ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0;
    if (responded) {
        ESP_LOGI(TAG, "camera detected: %s", cap.card);
    } else {
        ESP_LOGI(TAG, "CSI device present but no sensor responded");
    }
    close(fd);

    s_available = responded;
    if (!s_available) {
        esp_video_deinit();
        return false;
    }

    light_policy_reset(&s_policy);
    xTaskCreatePinnedToCore(light_sensor_task, "light_sensor", 4096, NULL, 3, &s_task, 0);
    return true;
}

bool app_light_available(void) { return s_available; }

void app_light_set_callback(app_light_brightness_cb_t cb) { s_cb = cb; }

void app_light_set_adaptive(bool enabled) {
    if (!s_available) {
        /* Every camera-less board hits this exact path at boot (main.c always
         * calls this once with the saved preference) — that's the normal
         * case, not something worth a warning. Only trying to actually turn
         * it on without a camera is unusual enough to log. */
        if (enabled) ESP_LOGW(TAG, "adaptive mode requested but no camera is available, ignoring");
        return;
    }
    ESP_LOGI(TAG, "adaptive brightness %s", enabled ? "enabled" : "disabled");
    s_adaptive = enabled;
    if (s_task) xTaskNotifyGive(s_task);
}

void app_light_deinit(void) {
    if (s_task) {
        TaskHandle_t t = s_task;
        s_task = NULL;
        vTaskDelete(t);
    }
    stop_streaming();
    if (s_available) {
        esp_video_deinit();
        s_available = false;
    }
}
