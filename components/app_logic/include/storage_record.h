#ifndef STORAGE_RECORD_H
#define STORAGE_RECORD_H

#include "storage_backend.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* Consistency and wear-reduction logic for persisted records, hardware-free
 * (see CLAUDE.md's Codeorganisation) so it runs and is tested on the host —
 * ./scripts/host-test.sh, host_test/main/test_storage_record.c.
 *
 * Every record is written as ONE backend key: {version, payload_len, crc32,
 * payload}. A caller with several related fields (e.g. a city name plus its
 * coordinates) must pack them into one struct and save it as one record, not
 * as separate backend keys — a backend with per-key atomicity (true of NVS)
 * then covers the whole record, not just one field of it. A power loss
 * mid-write leaves either the old record or the new one, never a mix.
 */

/* Largest payload a record may hold. Both app records today (prefs, wifi
 * credentials) are well under this; raise it if a future record needs more,
 * it only affects the stack buffers inside storage_record.c. */
#define STORAGE_RECORD_MAX_PAYLOAD 192

/* Loads the record at ns/key, validates it (matching version, matching
 * payload_len against out_len, matching CRC32), and copies the payload into
 * *out_payload (out_len bytes) only on success. Returns false — out_payload
 * left untouched — if the key is missing, truncated, wrong version, or
 * checksum-mismatched. These cases are deliberately not distinguished: the
 * caller's existing default-fallback path is expected to treat "missing"
 * and "corrupt" identically. */
bool storage_record_load(const storage_backend_t *backend, const char *ns, const char *key,
                          uint16_t expected_version, void *out_payload, size_t out_len);

/* Encodes payload (len bytes) into a versioned+checksummed record and writes
 * it as a single backend key. Reads back whatever is currently stored first
 * and skips write()+commit() entirely if it is already byte-identical to
 * what would be written — this is the actual wear reduction, verifiable in
 * a host test by asserting the fake backend's write() was not called.
 * Returns true if the record now matches (whether or not a write
 * happened), false on a backend error. */
bool storage_record_save(const storage_backend_t *backend, const char *ns, const char *key,
                          uint16_t version, const void *payload, size_t len);

/* Plain CRC32 (IEEE 802.3 / zlib polynomial), self-contained so host tests
 * and firmware compute byte-identical checksums without depending on
 * esp_rom_crc32_le() or any other IDF-only implementation. */
uint32_t storage_crc32(const void *data, size_t len);

#endif
