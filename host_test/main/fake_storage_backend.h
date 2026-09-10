#ifndef FAKE_STORAGE_BACKEND_H
#define FAKE_STORAGE_BACKEND_H

#include "storage_backend.h"
#include <stdbool.h>

/* In-memory storage_backend_t for host tests. Holds exactly one blob (tests
 * in this project only ever exercise one ns/key pair at a time) plus call
 * counters, so a test can assert e.g. "saving an unchanged record does not
 * call write()" — the actual wear-reduction behavior storage_record.c is
 * supposed to provide. */
typedef struct {
    unsigned char blob[256];
    size_t len;      /* 0 = nothing stored */
    int read_calls;
    int write_calls;
    int erase_calls;
    int commit_calls;
    bool fail_write; /* simulate a backend error on the next write() */
} fake_storage_backend_t;

void fake_storage_backend_reset(fake_storage_backend_t *fake);
storage_backend_t fake_storage_backend_as_backend(fake_storage_backend_t *fake);

#endif
