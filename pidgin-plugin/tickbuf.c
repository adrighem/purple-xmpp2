/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file tickbuf.c Ticks on the sent lines of one GtkTextBuffer
 */
#include "tickbuf.h"

#include <string.h>

/* Tag of a "displayed" glyph; delivered ones use X2_TICK_TAG */
#define X2_TICK_TAG_READ "xmpp2-tick-read"

struct _X2TickBuf {
	GtkTextBuffer *buffer;  /* ref held */
	X2TickMap *lines;       /* id -> Line */
	gulong delete_id;       /* "delete-range" handler */
	gboolean busy;          /* our own edit: ignore our delete-range */
};

typedef struct {
	GtkTextMark *mark;      /* end of the sent line, left gravity; ref held */
	X2TickState state;
	char *shown;            /* text we inserted at the mark, or NULL */
} Line;

static void
line_free(gpointer data)
{
	Line *line = data;

	if (!gtk_text_mark_get_deleted(line->mark))
		gtk_text_buffer_delete_mark(gtk_text_mark_get_buffer(line->mark),
		                            line->mark);
	g_object_unref(line->mark);
	g_free(line->shown);
	g_free(line);
}

typedef struct {
	gint start;
	gint end;
} Range;

/* A line is gone when the deleted range swallows its end: start < mark <= end */
static gboolean
line_in_range(gpointer key, gpointer value, gpointer data)
{
	Line *line = value;
	Range *r = data;
	GtkTextIter iter;
	gint at;

	if (gtk_text_mark_get_deleted(line->mark))
		return TRUE;
	gtk_text_buffer_get_iter_at_mark(gtk_text_mark_get_buffer(line->mark),
	                                 &iter, line->mark);
	at = gtk_text_iter_get_offset(&iter);
	return r->start < at && at <= r->end;
}

/* Runs before the default handler, while the marks are still in place. */
static void
delete_range_cb(GtkTextBuffer *buffer, GtkTextIter *start, GtkTextIter *end,
                gpointer data)
{
	X2TickBuf *tb = data;
	Range r;

	if (tb->busy)
		return;
	r.start = gtk_text_iter_get_offset(start);
	r.end = gtk_text_iter_get_offset(end);
	if (r.start > r.end) {
		gint tmp = r.start;

		r.start = r.end;
		r.end = tmp;
	}
	if (r.start == r.end)
		return;
	x2_tick_map_foreach_remove(tb->lines, line_in_range, &r);
}

X2TickBuf *
x2_tick_buf_new(GtkTextBuffer *buffer, guint capacity)
{
	X2TickBuf *tb;

	g_return_val_if_fail(GTK_IS_TEXT_BUFFER(buffer), NULL);
	g_return_val_if_fail(capacity > 0, NULL);

	tb = g_new0(X2TickBuf, 1);
	tb->buffer = g_object_ref(buffer);
	tb->lines = x2_tick_map_new(capacity, line_free);
	tb->delete_id = g_signal_connect(buffer, "delete-range",
	                                 G_CALLBACK(delete_range_cb), tb);
	return tb;
}

void
x2_tick_buf_free(X2TickBuf *tb)
{
	if (tb == NULL)
		return;
	g_signal_handler_disconnect(tb->buffer, tb->delete_id);
	x2_tick_map_free(tb->lines);
	g_object_unref(tb->buffer);
	g_free(tb);
}

void
x2_tick_buf_record(X2TickBuf *tb, const char *id)
{
	GtkTextIter end;
	Line *line;

	g_return_if_fail(tb != NULL && id != NULL);

	gtk_text_buffer_get_end_iter(tb->buffer, &end);
	line = g_new0(Line, 1);
	line->mark = g_object_ref(gtk_text_buffer_create_mark(tb->buffer, NULL,
	                                                      &end, TRUE));
	x2_tick_map_insert(tb->lines, id, line);
}

static GtkTextTag *
get_tag(GtkTextBuffer *buffer, X2TickState state)
{
	const char *name = state == X2_TICK_DISPLAYED ? X2_TICK_TAG_READ
	                                              : X2_TICK_TAG;
	GtkTextTag *tag;

	tag = gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buffer),
	                                name);
	if (tag != NULL)
		return tag;
	return gtk_text_buffer_create_tag(buffer, name,
		"foreground", state == X2_TICK_DISPLAYED ? "#3478c6" : "#808080",
		"scale", PANGO_SCALE_SMALL,
		NULL);
}

/* Is the text we inserted still right after the mark? */
static gboolean
shown_intact(GtkTextBuffer *buffer, const Line *line, GtkTextIter *start,
             GtkTextIter *end)
{
	char *text;
	gboolean same;
	glong len;

	if (line->shown == NULL)
		return FALSE;
	len = g_utf8_strlen(line->shown, -1);
	gtk_text_buffer_get_iter_at_mark(buffer, start, line->mark);
	*end = *start;
	gtk_text_iter_forward_chars(end, len);
	if (gtk_text_iter_get_offset(end) - gtk_text_iter_get_offset(start) != len)
		return FALSE;
	text = gtk_text_buffer_get_slice(buffer, start, end, TRUE);
	same = strcmp(text, line->shown) == 0;
	g_free(text);
	return same;
}

static void
draw(X2TickBuf *tb, Line *line, const char *text, X2TickState state)
{
	GtkTextBuffer *buffer = tb->buffer;
	GtkTextMark *typing;
	GtkTextIter iter, end;
	gboolean typing_here = FALSE;

	tb->busy = TRUE;
	if (shown_intact(buffer, line, &iter, &end))
		gtk_text_buffer_delete(buffer, &iter, &end);
	gtk_text_buffer_get_iter_at_mark(buffer, &iter, line->mark);

	/* The typing notice starts where the last line ends; keep the glyph
	 * out of the range Pidgin deletes when the notice goes away. */
	typing = gtk_text_buffer_get_mark(buffer, X2_TICK_TYPING_MARK);
	if (typing != NULL) {
		GtkTextIter at;

		gtk_text_buffer_get_iter_at_mark(buffer, &at, typing);
		typing_here = gtk_text_iter_equal(&at, &iter);
	}
	gtk_text_buffer_insert_with_tags(buffer, &iter, text, -1,
	                                 get_tag(buffer, state), NULL);
	if (typing_here)
		gtk_text_buffer_move_mark(buffer, typing, &iter);
	tb->busy = FALSE;

	g_free(line->shown);
	line->shown = g_strdup(text);
}

gboolean
x2_tick_buf_update(X2TickBuf *tb, const char *id, X2TickState state,
                   X2TickStyle style)
{
	Line *line;
	const char *glyph;
	char *text;
	X2TickState merged;

	g_return_val_if_fail(tb != NULL, FALSE);

	line = x2_tick_map_lookup(tb->lines, id);
	if (line == NULL)
		return FALSE;
	if (gtk_text_mark_get_deleted(line->mark)) {
		x2_tick_map_remove(tb->lines, id);
		return FALSE;
	}

	merged = x2_tick_state_merge(line->state, state);
	glyph = x2_tick_glyph(style, merged);
	if (glyph != NULL) {
		GtkTextIter s, e;

		text = g_strconcat(" ", glyph, NULL);
		if (merged != line->state ||
		    !shown_intact(tb->buffer, line, &s, &e) ||
		    strcmp(text, line->shown) != 0)
			draw(tb, line, text, merged);
		g_free(text);
	}
	line->state = merged;
	return TRUE;
}

X2TickState
x2_tick_buf_state(X2TickBuf *tb, const char *id)
{
	Line *line;

	g_return_val_if_fail(tb != NULL, X2_TICK_NONE);
	line = x2_tick_map_lookup(tb->lines, id);
	return line ? line->state : X2_TICK_NONE;
}

char *
x2_tick_buf_line_text(X2TickBuf *tb, const char *id)
{
	GtkTextIter start, end;
	Line *line;

	g_return_val_if_fail(tb != NULL, NULL);
	line = x2_tick_map_lookup(tb->lines, id);
	if (line == NULL || gtk_text_mark_get_deleted(line->mark))
		return NULL;
	gtk_text_buffer_get_iter_at_mark(tb->buffer, &start, line->mark);
	end = start;
	gtk_text_iter_set_line_offset(&start, 0);
	if (!gtk_text_iter_ends_line(&end))
		gtk_text_iter_forward_to_line_end(&end);
	return gtk_text_buffer_get_text(tb->buffer, &start, &end, FALSE);
}

void
x2_tick_buf_clear(X2TickBuf *tb)
{
	g_return_if_fail(tb != NULL);
	x2_tick_map_clear(tb->lines);
}

guint
x2_tick_buf_size(const X2TickBuf *tb)
{
	g_return_val_if_fail(tb != NULL, 0);
	return x2_tick_map_size(tb->lines);
}
