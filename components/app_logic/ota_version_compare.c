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

/* Semver precedence for two pre-release strings: dot-separated identifiers
 * compare left to right; two numeric identifiers compare as numbers (so
 * "test.10" outranks "test.9"), a numeric one ranks below an alphanumeric
 * one, two alphanumeric ones compare as text, and the string with more
 * identifiers wins a tie on the common prefix. Returns <0, 0 or >0. */
static int compare_prerelease(const char *a, const char *b) {
    for (;;) {
        if (*a == '\0' || *b == '\0') return (*a != '\0') - (*b != '\0');

        size_t alen = strcspn(a, ".");
        size_t blen = strcspn(b, ".");
        bool a_num = true, b_num = true;
        for (size_t i = 0; i < alen; i++) if (!isdigit((unsigned char)a[i])) a_num = false;
        for (size_t i = 0; i < blen; i++) if (!isdigit((unsigned char)b[i])) b_num = false;

        int cmp;
        if (a_num && b_num) {
            /* Strip leading zeros, then a longer digit run is the larger number. */
            const char *an = a, *bn = b;
            size_t anlen = alen, bnlen = blen;
            while (anlen > 1 && *an == '0') { an++; anlen--; }
            while (bnlen > 1 && *bn == '0') { bn++; bnlen--; }
            cmp = anlen != bnlen ? (anlen > bnlen) - (anlen < bnlen)
                                 : strncmp(an, bn, anlen);
        } else if (a_num != b_num) {
            cmp = a_num ? -1 : 1;
        } else {
            size_t common = alen < blen ? alen : blen;
            cmp = strncmp(a, b, common);
            if (cmp == 0) cmp = (alen > blen) - (alen < blen);
        }
        if (cmp != 0) return cmp;

        a += alen; b += blen;
        if (*a == '.') a++;
        if (*b == '.') b++;
    }
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
    return compare_prerelease(r.pre, c.pre) > 0;
}
