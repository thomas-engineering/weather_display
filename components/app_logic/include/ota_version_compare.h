#ifndef OTA_VERSION_COMPARE_H
#define OTA_VERSION_COMPARE_H

#include <stdbool.h>

/* Hardware-free semver-ish comparison for the "vMAJOR.MINOR.PATCH[-preN]"
 * shape used by version.txt (e.g. "0.0.1-pre1", "v1.2.0"). A leading 'v'/'V'
 * is ignored on either side. Numeric major/minor/patch compare first;
 * missing components are treated as 0. If major.minor.patch tie, a
 * pre-release suffix ranks lower than none (standard semver precedence:
 * "1.0.0" > "1.0.0-pre1"), and two differing pre-release suffixes fall back
 * to a plain string compare. If neither string parses as at least one
 * leading digit, falls back to a plain strcmp (not-equal treated as "not
 * newer", to fail closed rather than update on garbage input).
 *
 * Returns true iff `remote` is strictly newer than `current`. */
bool ota_is_newer(const char *current, const char *remote);

#endif
