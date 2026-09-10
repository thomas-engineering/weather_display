#include "fake_storage_backend.h"
#include <string.h>

void fake_storage_backend_reset(fake_storage_backend_t *fake) {
    memset(fake, 0, sizeof *fake);
}

static int fake_read(void *ctx, const char *ns, const char *key, void *buf, size_t *inout_len) {
    (void)ns; (void)key;
    fake_storage_backend_t *f = ctx;
    f->read_calls++;
    if (f->len == 0) return STORAGE_NOT_FOUND;
    if (*inout_len < f->len) return STORAGE_ERR;
    memcpy(buf, f->blob, f->len);
    *inout_len = f->len;
    return STORAGE_OK;
}

static int fake_write(void *ctx, const char *ns, const char *key, const void *buf, size_t len) {
    (void)ns; (void)key;
    fake_storage_backend_t *f = ctx;
    f->write_calls++;
    if (f->fail_write) return STORAGE_ERR;
    if (len > sizeof f->blob) return STORAGE_ERR;
    memcpy(f->blob, buf, len);
    f->len = len;
    return STORAGE_OK;
}

static int fake_erase(void *ctx, const char *ns, const char *key) {
    (void)ns; (void)key;
    fake_storage_backend_t *f = ctx;
    f->erase_calls++;
    f->len = 0;
    return STORAGE_OK;
}

static int fake_commit(void *ctx) {
    fake_storage_backend_t *f = ctx;
    f->commit_calls++;
    return STORAGE_OK;
}

storage_backend_t fake_storage_backend_as_backend(fake_storage_backend_t *fake) {
    storage_backend_t b = {
        .read = fake_read,
        .write = fake_write,
        .erase = fake_erase,
        .commit = fake_commit,
        .ctx = fake,
    };
    return b;
}
