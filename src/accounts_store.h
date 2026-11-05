#ifndef ACCOUNTS_STORE_H
#define ACCOUNTS_STORE_H

#include "cJSON.h"
#include "spisor/lock.h"

/** One account store file, parsed and normalized, and the lock held over it while a change is made. */
typedef struct accounts_store {
    char *path;
    spisor_lock *guard;
    cJSON *document;
} accounts_store;

typedef enum accounts_store_status {
    ACCOUNTS_STORE_READY,
    ACCOUNTS_STORE_UNLOCKABLE,
    ACCOUNTS_STORE_UNREADABLE,
    ACCOUNTS_STORE_UNWRITABLE,
    ACCOUNTS_STORE_OUT_OF_MEMORY
} accounts_store_status;

/**
 * Opens the store at path as {version, accounts, activeIndex, activeIndexByLane}, an absent file being
 * an empty pool. With locked set it first creates the missing folders and takes `<path>.lock`, which
 * stays held until accounts_store_close; without it nothing is created or locked. On any status but
 * READY the store holds nothing and needs no close.
 *
 * @implNote The lock is the one the runtime registry takes (spisor_lock_file), an OS file lock on an
 * open handle, so a crash cannot leave it held. A file that is not a JSON object is UNREADABLE and is
 * never replaced: a write over it would destroy accounts the caller cannot see.
 */
accounts_store_status accounts_store_open(const char *path, int locked, accounts_store *store);

/** Replaces the file atomically with the document; the lock, when held, stays held. */
accounts_store_status accounts_store_save(const accounts_store *store);

void accounts_store_close(accounts_store *store);

/** The pool's accounts array, owned by the store. */
cJSON *accounts_store_accounts(const accounts_store *store);

#endif
