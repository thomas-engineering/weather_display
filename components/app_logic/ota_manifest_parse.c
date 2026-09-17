#include "ota_manifest_parse.h"

#include "cJSON.h"

#include <stdio.h>
#include <string.h>

static bool jstr(const cJSON *root, const char *key, char *out, size_t out_len) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsString(v) || !v->valuestring || v->valuestring[0] == '\0') return false;
    snprintf(out, out_len, "%s", v->valuestring);
    return true;
}

bool ota_manifest_parse(const char *json, ota_manifest_t *out) {
    if (!json || !out) return false;

    cJSON *root = cJSON_Parse(json);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }

    ota_manifest_t r = {0};
    bool ok = jstr(root, "version", r.version, sizeof r.version) &&
              jstr(root, "p4_sha256", r.p4_sha256, sizeof r.p4_sha256) &&
              jstr(root, "c6_version", r.c6_version, sizeof r.c6_version) &&
              jstr(root, "c6_sha256", r.c6_sha256, sizeof r.c6_sha256);

    cJSON_Delete(root);
    if (!ok) return false;

    *out = r;
    return true;
}
