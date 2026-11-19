/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file mam.h XEP-0313 account archive: state shared by mam.c and mam_sync.c
 *
 * Private to the mam module.  mam.c holds the hooks, the inbound checks and
 * dedup; mam_sync.c the catch-up state machine (ARCHITECTURE 7.2): queries,
 * paging, item-not-found retry, page cap, watchdog and queue flush.
 */
#ifndef XMPP2_EXT_MAM_H_
#define XMPP2_EXT_MAM_H_

#include <time.h>
#include <glib.h>

#include "ext/ext.h"
#include "ext/statefile.h"

#define X2_MAM_DEBUG "xmpp2-mam"

/* Pages of X2_MAM_PAGE_SIZE fetched at most per catch-up, last page included */
#define X2_MAM_MAX_PAGES 20

/* Seconds without a result or <fin/> before the archive goes LIVE anyway */
#define X2_MAM_WATCHDOG_SECS 30

typedef enum {
	X2_MAM_IDLE,     /* not known yet, or no MAM support */
	X2_MAM_SYNCING,  /* catch-up running: live and carbon messages queued */
	X2_MAM_LIVE      /* caught up: messages are delivered as they come */
} X2MamPhase;

typedef enum {
	X2_MAMQ_FORWARD,     /* after=cursor or start=..., paged forward */
	X2_MAMQ_LAST,        /* the newest page only (page cap reached) */
	X2_MAMQ_CURSOR_ONLY, /* newest id only, shown nothing: sets the cursor */
	X2_MAMQ_PROBE        /* is the cursor still in the archive? (ids=cursor) */
} X2MamQueryKind;

typedef struct {
	JabberStream *js;
	char *own;                 /* our bare JID: the account archive */
	X2StateFile *sf;

	gboolean supported;        /* own bare JID advertises urn:xmpp:mam:2 */
	gboolean extended;         /* ... and urn:xmpp:mam:2#extended (ids) */
	gboolean carbons_offered;  /* server advertises urn:xmpp:carbons:2 */
	gboolean carbons_settled;  /* x2_ext_carbons_settled() ran */
	gboolean ready;            /* session_ready ran */

	X2MamPhase phase;
	X2MamQueryKind kind;
	char *queryid;             /* active query, NULL if none */
	char *iq_id;               /* IQ id of the active query */
	guint pages;               /* pages answered in this catch-up */
	gboolean from_cursor;      /* the catch-up started at after=cursor */
	gboolean covered;          /* the archive answered for the whole range
	                            * (results, a found cursor, or a start=
	                            * query): offline copies are redundant */
	gboolean retried;          /* item-not-found retry done */
	char *skip_id;             /* old cursor, skipped after the retry */
	time_t page_ts;            /* ts of the newest result of the page */
	GHashTable *gap_noticed;   /* peers told "older history skipped" */
	gboolean frozen;           /* catch-up incomplete: live messages do
	                            * not move the cursor (next login refetches) */
	guint watchdog;
	GQueue queue;              /* X2Msg dups held back while SYNCING */
} X2MamState;

/** The module (registry entry). */
extern const X2Ext x2_ext_mam;

X2MamState *x2_mam_state(JabberStream *js);

/* mam.c */
/** TRUE if @a key is in the dedup set or the persisted ring. */
gboolean x2_mam_key_seen(X2MamState *st, const char *key);
/** Records the keys of a message that is going to be shown. */
void x2_mam_remember(X2MamState *st, const X2Msg *m);
/** Moves the cursor to a live message's trusted stanza-id. */
void x2_mam_advance_live(X2MamState *st, const X2Msg *m);
/** Writes a system line into the IM conversation with @a peer. */
void x2_mam_notice(X2MamState *st, const char *peer, const char *text);

/* mam_sync.c */
/** Sends the first query once MAM, session_ready and carbons allow it. */
void x2_mam_sync_maybe_start(X2MamState *st);
/** Enters SYNCING (pre_presence) and arms the watchdog. */
void x2_mam_sync_begin(X2MamState *st);
/** Restarts the watchdog after a result. */
void x2_mam_sync_kick(X2MamState *st);
/**
 * Ends the catch-up: LIVE, then the queue is delivered in arrival order.
 * @param complete  FALSE after an error, the watchdog or teardown.
 * @param notice    Optional system line for each conversation in the queue.
 */
void x2_mam_sync_finish(X2MamState *st, gboolean complete, const char *notice);
/** Cancels timers and frees the queue without delivering it. */
void x2_mam_sync_free(X2MamState *st);

#endif /* XMPP2_EXT_MAM_H_ */
