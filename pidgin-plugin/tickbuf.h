/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file tickbuf.h Ticks on the sent lines of one GtkTextBuffer
 *
 * Plain GTK, no Pidgin: the conversation view of Pidgin 2 is a GtkIMHtml,
 * a GtkTextView that only ever appends.  x2_tick_buf_record() is called
 * right after a sent line was written and puts an anonymous left-gravity
 * mark at the end of the buffer, the end of that line: text appended later
 * (the next line, a typing notice) goes after it.  x2_tick_buf_update()
 * inserts or replaces " <glyph>" at that mark.
 *
 * Lines that go away take their ticks with them: a "delete-range" that
 * covers a mark (scrollback trimming, clearing the view) forgets that id,
 * so a late receipt never draws into some other line.  A range that starts
 * exactly at a mark leaves it alone, which is what the typing notice that
 * Pidgin appends and removes after the last line needs.
 */
#ifndef XMPP2_TICKS_TICKBUF_H_
#define XMPP2_TICKS_TICKBUF_H_

#include <gtk/gtk.h>

#include "tickmap.h"

/* Tag of the inserted glyphs, created once per tag table */
#define X2_TICK_TAG "xmpp2-tick"

/* Mark of Pidgin's typing notice (pidgin/gtkconv.c) */
#define X2_TICK_TYPING_MARK "typing-notification-start"

typedef struct _X2TickBuf X2TickBuf;

/** Tracks at most @a capacity sent lines of @a buffer (holds a ref). */
X2TickBuf *x2_tick_buf_new(GtkTextBuffer *buffer, guint capacity);

/** Removes its marks and handlers and drops the buffer ref. NULL is OK. */
void x2_tick_buf_free(X2TickBuf *tb);

/** Remembers @a id for the line that now ends the buffer. */
void x2_tick_buf_record(X2TickBuf *tb, const char *id);

/**
 * Shows @a state for @a id in @a style.  A state never goes back; a newer
 * glyph replaces the old one if that is still intact.
 *
 * @return FALSE if @a id is unknown (never recorded, evicted, or its line
 *         was removed), TRUE otherwise
 */
gboolean x2_tick_buf_update(X2TickBuf *tb, const char *id,
                            X2TickState state, X2TickStyle style);

/** Current state of @a id; X2_TICK_NONE when unknown. */
X2TickState x2_tick_buf_state(X2TickBuf *tb, const char *id);

/** Text of the buffer line holding @a id's mark (g_free), NULL if unknown. */
char *x2_tick_buf_line_text(X2TickBuf *tb, const char *id);

/** Forgets every id (the view was cleared). */
void x2_tick_buf_clear(X2TickBuf *tb);

/** Number of ids tracked. */
guint x2_tick_buf_size(const X2TickBuf *tb);

#endif /* XMPP2_TICKS_TICKBUF_H_ */
