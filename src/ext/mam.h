/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file mam.h XEP-0313 archives: state shared by mam.c, mam_sync.c, mam_muc.c
 *
 * Private to the mam module.  mam.c holds the hooks, the inbound checks and
 * dedup; mam_sync.c the per-archive catch-up state machine (ARCHITECTURE
 * 7.2): queries, paging, item-not-found retry, page cap, watchdog and queue
 * flush; mam_muc.c the rooms: disco#info before the join, history
 * suppression and the catch-up on join (M4).
 */
#ifndef XMPP2_EXT_MAM_H_
#define XMPP2_EXT_MAM_H_

#include <time.h>
#include <glib.h>

#include "ext/ext.h"
#include "ext/statefile.h"

#define X2_MAM_DEBUG "xmpp2-mam"

/* Pages fetched at most per catch-up, last page included: account archive
 * (X2_MAM_PAGE_SIZE each) and a room (X2_MAM_ROOM_PAGE_SIZE each) */
#define X2_MAM_MAX_PAGES 20
#define X2_MAM_ROOM_MAX_PAGES 10

/* Live messages held back per archive while it syncs; one more ends the
 * catch-up as incomplete (above the offline flood of a 2000-message cap) */
#define X2_MAM_QUEUE_MAX 5000

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

typedef struct _X2MamState X2MamState;

/* One archive being caught up: the account archive or one joined room. */
typedef struct {
	X2MamState *st;
	char *jid;                 /* own bare JID, or the room bare JID */
	gboolean room;
	gboolean extended;         /* advertises urn:xmpp:mam:2#extended (ids) */

	X2MamPhase phase;
	X2MamQueryKind kind;
	char *queryid;             /* active query, NULL if none */
	char *iq_id;               /* IQ id of the active query */
	guint pages;               /* pages answered in this catch-up */
	gboolean from_cursor;      /* the catch-up started at after=cursor */
	gboolean retried;          /* item-not-found retry done */
	gboolean capped;           /* page cap reached: history was skipped */
	char *skip_id;             /* old cursor, skipped after the retry */
	time_t page_ts;            /* ts of the newest result of the page */
	GHashTable *gap_noticed;   /* conversations told "older history skipped" */
	gboolean frozen;           /* catch-up incomplete: live messages do
	                            * not move the cursor (next login refetches) */
	guint watchdog;
	GQueue queue;              /* live X2Msg dups held back while SYNCING */
	GQueue page;               /* results of a last-page query, held until
	                            * its <fin/> tells whether history was skipped */
} X2MamArchive;

struct _X2MamState {
	JabberStream *js;
	char *own;                 /* our bare JID: the account archive */
	X2StateFile *sf;

	gboolean supported;        /* own bare JID advertises urn:xmpp:mam:2 */
	gboolean carbons_offered;  /* server advertises urn:xmpp:carbons:2 */
	gboolean carbons_settled;  /* x2_ext_carbons_settled() ran */
	gboolean ready;            /* session_ready ran */

	X2MamArchive *acct;        /* the account archive */
	GHashTable *rooms;         /* room JID -> X2MamArchive, kept for the
	                            * whole stream (IQ callbacks point here) */
	GHashTable *room_info;     /* room JID -> X2_MAM_ROOM_* flags of its
	                            * disco#info (cached per stream) */
	GHashTable *held_joins;    /* room JID -> join waiting for disco#info */
};

/* Flags of a room's disco#info (X2MamState::room_info) */
#define X2_MAM_ROOM_MAM      (1 << 0)
#define X2_MAM_ROOM_EXTENDED (1 << 1)
#define X2_MAM_ROOM_KNOWN    (1 << 2)  /* answered, even without MAM */

/** The module (registry entry). */
extern const X2Ext x2_ext_mam;

X2MamState *x2_mam_state(JabberStream *js);

/* mam.c */
/** TRUE if @a key is in the dedup set or the archive's persisted ring. */
gboolean x2_mam_key_seen(X2MamArchive *a, const char *key);
/** Records the keys of a message that is going to be shown. */
void x2_mam_remember(X2MamArchive *a, const X2Msg *m);
/** Moves the cursor to a live message's trusted stanza-id. */
void x2_mam_advance_live(X2MamArchive *a, const X2Msg *m);
/**
 * Writes a system line: into the IM conversation with @a peer for the
 * account archive, into the room for a room archive (@a peer unused).
 */
void x2_mam_notice(X2MamArchive *a, const char *peer, const char *text);
/** Handles an archive result of @a a (already validated). */
X2Verdict x2_mam_inbound_result(X2MamArchive *a, X2Msg *m, xmlnode *inner,
                                const char *id);
/** TRUE if a disco#info <query/> lists the feature @a var. */
gboolean x2_mam_has_feature(xmlnode *query, const char *var);
/** The persisted state (cursors, rings). */
X2Store *x2_mam_store(X2MamState *st);

/* mam_sync.c: the per-archive state machine */
X2MamArchive *x2_mam_archive_new(X2MamState *st, const char *jid,
                                 gboolean room);
void x2_mam_archive_free(X2MamArchive *a);
/** Sends the first account query once MAM, session_ready and carbons allow. */
void x2_mam_sync_maybe_start(X2MamState *st);
/** Enters SYNCING and arms the watchdog (forgets an earlier catch-up). */
void x2_mam_sync_begin(X2MamArchive *a);
/** Restarts the watchdog after a result. */
void x2_mam_sync_kick(X2MamArchive *a);
/** Sends one query of @a a (to the room for a room archive). */
void x2_mam_sync_query(X2MamArchive *a, X2MamQueryKind kind,
                       const char *after, time_t start, const char *id);
/**
 * Ends the catch-up: LIVE, then the queue is delivered in arrival order.
 * @param complete  FALSE after an error, the watchdog or teardown.
 * @param notice    Optional system line: for each conversation in the queue
 *                  (account archive), or once into the room.
 */
void x2_mam_sync_finish(X2MamArchive *a, gboolean complete,
                        const char *notice);
/** Cancels timers and frees the queues without delivering them. */
void x2_mam_sync_reset(X2MamArchive *a);

/* mam_muc.c: rooms (M4) */
gboolean x2_mam_muc_chat_joining(JabberStream *js, JabberChat *chat);
void x2_mam_muc_join_presence(JabberStream *js, JabberChat *chat,
                              xmlnode *muc_x);
void x2_mam_muc_chat_joined(JabberStream *js, JabberChat *chat);
/** The first room query: after the cursor, or the last page (7.2). */
void x2_mam_muc_first_query(X2MamArchive *a);
/** Room archive of @a jid if one was started in this stream, else NULL. */
X2MamArchive *x2_mam_muc_archive(X2MamState *st, const char *jid);
/** Account option mam_muc_threshold_days and per-stream tables. */
void x2_mam_muc_account_options(GList **opts);
void x2_mam_muc_stream_new(X2MamState *st);
void x2_mam_muc_stream_free(X2MamState *st);

#endif /* XMPP2_EXT_MAM_H_ */
