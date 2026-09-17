#include "ota_version_compare.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int major, minor, patch;
    char pre[32];   /* "" if no pre-release suffix */
    bool parsed;    /* false if the string didn't start with a digit (after an optional 'v') */
} ver_t;

static ver_t parse_version(const char *s) {
    ver_t v = {0};
    if (!s) return v;
    if (*s == 'v' || *s == 'V') s++;
    if (!isdigit((unsigned char)*s)) return v;

    v.parsed = true;
    char *end;
    v.major = (int)strtol(s, &end, 10);
    s = end;
    if (*s == '.') { v.minor = (int)strtol(s + 1, &end, 10); s = end; }
    if (*s == '.') { v.patch = (int)strtol(s + 1, &end, 10); s = end; }
    if (*s == '-' || *s == '+') {
        snprintf(v.pre, sizeof v.pre, "%s", s + 1);
    }
    return v;
}

bool ota_is_newer(const char *current, const char *remote) {
    if (!current || !remote) return false;

    ver_t c = parse_version(current);
    ver_t r = parse_version(remote);

    /* Neither parses as a version: fail closed rather than update on garbage input. */
    if (!c.parsed || !r.parsed) return false;

    if (r.major != c.major) return r.major > c.major;
    if (r.minor != c.minor) return r.minor > c.minor;
    if (r.patch != c.patch) return r.patch > c.patch;

    bool c_pre = c.pre[0] != '\0';
    bool r_pre = r.pre[0] != '\0';
    if (c_pre != r_pre) return c_pre && !r_pre; /* "1.0.0" > "1.0.0-pre1" */
    if (!c_pre && !r_pre) return false;         /* identical release version */
    return strcmp(r.pre, c.pre) > 0;
}
