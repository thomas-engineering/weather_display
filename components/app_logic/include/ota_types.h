#ifndef OTA_TYPES_H
#define OTA_TYPES_H

/* Hardware-free OTA types shared by ota_manifest_parse and the main/
 * ota_update.c adapter. No IDF headers here — see CLAUDE.md's
 * "Codeorganisation". */

typedef struct {
    char version[32];     /* e.g. "v0.1.0" — the P4 firmware version, semver-ish */
    char p4_sha256[65];    /* lowercase hex SHA-256 of firmware_p4.bin, + NUL */
    char c6_version[32];   /* informational only, not compared */
    char c6_sha256[65];    /* lowercase hex SHA-256 of esp32c6_hosted_slave.bin, + NUL */
} ota_manifest_t;

#endif
