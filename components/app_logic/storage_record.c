#include "storage_record.h"
#include <string.h>

typedef struct {
    uint16_t version;
    uint16_t payload_len;
    uint32_t crc32;
} storage_record_header_t;

#define STORAGE_RECORD_MAX_BLOB (sizeof(storage_record_header_t) + STORAGE_RECORD_MAX_PAYLOAD)

uint32_t storage_crc32(const void *data, size_t len) {
    static const uint32_t poly = 0xEDB88320u;
    uint32_t crc = 0xFFFFFFFFu;
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 1u) ? (crc >> 1) ^ poly : (crc >> 1);
        }
    }
    return ~crc;
}

bool storage_record_load(const storage_backend_t *backend, const char *ns, const char *key,
                          uint16_t expected_version, void *out_payload, size_t out_len) {
    if (!backend || out_len > STORAGE_RECORD_MAX_PAYLOAD) return false;

    uint8_t buf[STORAGE_RECORD_MAX_BLOB];
    size_t len = sizeof buf;
    if (backend->read(backend->ctx, ns, key, buf, &len) != STORAGE_OK) return false;
    if (len < sizeof(storage_record_header_t)) return false;

    storage_record_header_t hdr;
    memcpy(&hdr, buf, sizeof hdr);
    const uint8_t *payload = buf + sizeof hdr;
    size_t stored_payload_len = len - sizeof hdr;

    if (hdr.version != expected_version) return false;
    if (hdr.payload_len != stored_payload_len) return false;
    if (hdr.payload_len != out_len) return false; /* caller's struct size must match exactly */
    if (storage_crc32(payload, stored_payload_len) != hdr.crc32) return false;

    memcpy(out_payload, payload, out_len);
    return true;
}

bool storage_record_save(const storage_backend_t *backend, const char *ns, const char *key,
                          uint16_t version, const void *payload, size_t len) {
    if (!backend || len > STORAGE_RECORD_MAX_PAYLOAD) return false;

    uint8_t buf[STORAGE_RECORD_MAX_BLOB];
    storage_record_header_t hdr = {
        .version = version,
        .payload_len = (uint16_t)len,
        .crc32 = storage_crc32(payload, len),
    };
    memcpy(buf, &hdr, sizeof hdr);
    memcpy(buf + sizeof hdr, payload, len);
    size_t total = sizeof hdr + len;

    /* Wear reduction: skip write()+commit() if the record already matches. */
    uint8_t existing[STORAGE_RECORD_MAX_BLOB];
    size_t existing_len = sizeof existing;
    if (backend->read(backend->ctx, ns, key, existing, &existing_len) == STORAGE_OK &&
        existing_len == total && memcmp(existing, buf, total) == 0) {
        return true;
    }

    if (backend->write(backend->ctx, ns, key, buf, total) != STORAGE_OK) return false;
    if (backend->commit(backend->ctx) != STORAGE_OK) return false;
    return true;
}
