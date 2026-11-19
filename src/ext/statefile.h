/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file statefile.h Persistence glue of the archive state (ARCHITECTURE 9)
 *
 * The state of one account lives in
 * purple_user_dir()/xmpp2/<escaped full username>.state, in a 0700
 * directory.  Writes are debounced by 2 s, flushed on close, and the file is
 * deleted when the account is removed.  The content is core/store.h.
 */
#ifndef XMPP2_EXT_STATEFILE_H_
#define XMPP2_EXT_STATEFILE_H_

#include <glib.h>
#include "account.h"

#include "core/store.h"

typedef struct _X2StateFile X2StateFile;

/** Path of the state file of an account. Free with g_free(). */
char *x2_statefile_path(PurpleAccount *account);

/** Loads (or starts) the state of an account. */
X2StateFile *x2_statefile_open(PurpleAccount *account);

/** The loaded state; owned by @a sf. */
X2Store *x2_statefile_store(X2StateFile *sf);

/** Schedules a debounced write if the state is dirty. */
void x2_statefile_touch(X2StateFile *sf);

/** Writes now if dirty, cancelling a pending debounced write. */
void x2_statefile_flush(X2StateFile *sf);

/** Flushes and frees. NULL is allowed. */
void x2_statefile_close(X2StateFile *sf);

/** Deletes the state file of an account (account-removed). */
void x2_statefile_remove(PurpleAccount *account);

#endif /* XMPP2_EXT_STATEFILE_H_ */
