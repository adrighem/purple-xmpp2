/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file mam_proto.h XEP-0313 Message Archive Management (pure logic)
 *
 * Query building, <result/> validation and <fin/> parsing.  Only <glib.h>
 * and xmlnode.h, so every check that keeps forged archive results out is
 * unit tested (tests/unit/test_mam_proto.c).
 */
#ifndef XMPP2_CORE_MAM_PROTO_H_
#define XMPP2_CORE_MAM_PROTO_H_

#include <time.h>
#include <glib.h>
#include "xmlnode.h"

#define X2_NS_MAM "urn:xmpp:mam:2"
#define X2_NS_MAM_EXTENDED "urn:xmpp:mam:2#extended"
#define X2_NS_RSM "http://jabber.org/protocol/rsm"

/** RSM page size of the account archive catch-up. */
#define X2_MAM_PAGE_SIZE 100

/** RSM page size of a room archive catch-up, also its "last page". */
#define X2_MAM_ROOM_PAGE_SIZE 50

/** Parameters of one archive query (one RSM page). */
typedef struct {
	const char *queryid;   /* required, echoed in every <result/> */
	const char *with;      /* optional form field "with" */
	time_t start;          /* optional form field "start", 0 for none */
	const char *id;        /* optional form field "ids" (urn:xmpp:mam:2#extended):
	                        * only the message with this archive id */
	const char *after;     /* RSM <after/>, or NULL */
	gboolean last_page;    /* RSM empty <before/>: the newest page */
	guint max;             /* RSM <max/>, 0 for the server default */
} X2MamQuery;

/**
 * Builds the <query xmlns='urn:xmpp:mam:2'/> element of a query IQ.
 *
 * @return A new element, free with xmlnode_free() (or insert it into an IQ).
 */
xmlnode *x2_mam_query_new(const X2MamQuery *q);

/** Outcome of x2_mam_result_check(). */
typedef enum {
	X2_MAM_NOT_RESULT,  /* no <result xmlns='urn:xmpp:mam:2'/>: not ours */
	X2_MAM_VALID,       /* a result of the active query, safe to show */
	X2_MAM_FORGED       /* looks like a result but fails a check: drop */
} X2MamCheck;

/**
 * Validates a <message/> carrying an account-archive result (ARCHITECTURE 8).
 *
 * Accepted only if the outer from is absent or exactly @a own_bare, the
 * queryid equals @a queryid (no active query: always forged), the result has
 * an id and a forwarded <message/>, and the inner from or to bare JID is
 * @a own_bare.
 *
 * @param outer     The <message/> as received.
 * @param own_bare  Our bare JID, the archive that was queried.
 * @param queryid   The queryid of the active query, or NULL.
 * @param inner     Receives the forwarded message on X2_MAM_VALID.
 * @param id        Receives the archive id (result@id) on X2_MAM_VALID,
 *                  borrowed from @a outer.
 * @param why       Receives a static reason on X2_MAM_FORGED, for the log.
 */
X2MamCheck x2_mam_result_check(xmlnode *outer, const char *own_bare,
                               const char *queryid, xmlnode **inner,
                               const char **id, const char **why);

/**
 * Validates a <message/> carrying a room-archive result (ARCHITECTURE 8).
 *
 * Accepted only if the outer from is exactly @a room (never absent: that
 * would be our own server answering for the account archive), the queryid
 * equals @a queryid, the result has an id and a forwarded <message/>, and
 * the inner from bare JID is @a room.  On X2_MAM_VALID the inner type is
 * forced to groupchat: a room result is always delivered into the room,
 * never as a 1:1 conversation.
 *
 * Parameters as for x2_mam_result_check(), with @a room the room's bare JID.
 */
X2MamCheck x2_mam_room_result_check(xmlnode *outer, const char *room,
                                    const char *queryid, xmlnode **inner,
                                    const char **id, const char **why);

/** First query of a room archive catch-up (ARCHITECTURE 7.2). */
typedef enum {
	X2_MAM_ROOM_AFTER_CURSOR,  /* after=<cursor>, paged forward */
	X2_MAM_ROOM_LAST_PAGE      /* the newest X2_MAM_ROOM_PAGE_SIZE only */
} X2MamRoomStart;

/**
 * Chooses the first room query: after the cursor while it is newer than
 * @a threshold_days, else (no cursor, no timestamp, too old, threshold 0)
 * the last page only.
 *
 * @param cursor     The stored cursor of the room, or NULL.
 * @param cursor_ts  Its timestamp, 0 if unknown.
 */
X2MamRoomStart x2_mam_room_start(const char *cursor, gint64 cursor_ts,
                                 time_t now, int threshold_days);

/**
 * TRUE if the inner from or to bare JID of an account-archive message is
 * @a own_bare: the account archive only ever holds our own conversations.
 */
gboolean x2_mam_inner_is_own(const xmlnode *inner, const char *own_bare);


/** Fate of a live message held back during a catch-up, when it ends. */
typedef enum {
	X2_MAM_QUEUED_DELIVER,    /* show it now */
	X2_MAM_QUEUED_DUPLICATE,  /* its key was seen: shown already */
	X2_MAM_QUEUED_SKIPPED     /* archived, in the history the page cap
	                           * skipped on purpose */
} X2MamQueued;

/**
 * Decides what happens to a queued message at the end of a catch-up.
 *
 * A seen key is a duplicate.  An offline copy (XEP-0160: delayed, with a
 * trusted stanza-id) is dropped only after a complete catch-up that ended
 * on the newest page because of the page cap: it is archived, and showing
 * it after the newest page would break ordering.  In every other case it
 * is shown: if the archive delivered it too, the stanza-id dedup already
 * caught it, so dropping it could only lose it.
 */
X2MamQueued x2_mam_queued_fate(gboolean seen, gboolean complete,
                               gboolean last_page, gboolean delayed,
                               gboolean has_stanza_id);

/** Parsed <fin/> of a query result. */
typedef struct {
	gboolean complete;  /* complete='true': no more pages */
	char *first;        /* RSM <first/>, NULL if absent (empty page) */
	char *last;         /* RSM <last/>, NULL if absent (empty page) */
} X2MamFin;

/**
 * Parses the <fin/> of an IQ result (pass the <iq/> or the <fin/> itself).
 *
 * @return FALSE if there is no <fin xmlns='urn:xmpp:mam:2'/>; @a fin is then
 *         cleared.  Free the strings with x2_mam_fin_clear().
 */
gboolean x2_mam_fin_parse(const xmlnode *iq, X2MamFin *fin);

/** Frees the strings of a parsed <fin/> and zeroes it. */
void x2_mam_fin_clear(X2MamFin *fin);

/**
 * TRUE if an IQ error says the RSM <after/> id is unknown to the archive
 * (<item-not-found/>), the cue to retry with start=cursor_ts.
 */
gboolean x2_mam_error_is_item_not_found(const xmlnode *iq);

#endif /* XMPP2_CORE_MAM_PROTO_H_ */
