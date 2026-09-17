#ifndef OTA_MANIFEST_PARSE_H
#define OTA_MANIFEST_PARSE_H

#include <stdbool.h>

#include "ota_types.h"

/* Hardware-free parser for the firmware release manifest.json produced by
 * .github/workflows/Manual_Dual-Chip_Release_Build.yml:
 *   { "version": "...", "p4_sha256": "...", "c6_version": "...", "c6_sha256": "..." }
 * Shared by main/ota_update.c and the host tests. Returns false (and leaves
 * `out` untouched) if the JSON is malformed or missing/mistyped any of the
 * four required fields. */
bool ota_manifest_parse(const char *json, ota_manifest_t *out);

#endif
