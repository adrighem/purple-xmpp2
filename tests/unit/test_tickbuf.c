/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/*
 * Unit tests of pidgin-plugin/tickbuf.c on a plain GtkTextBuffer (no
 * display needed), written the way pidgin/gtkconv.c writes a conversation:
 * "\n" before every line but the first, a typing notice between two marks
 * after the last line, scrollback trimmed from the top, clear = delete all.
 */
#include <gtk/gtk.h>
#include <string.h>

#include "tickbuf.h"

#define CHECK "\xe2\x9c\x93"

static void
append_line(GtkTextBuffer *buffer, const char *text)
{
	GtkTextIter end;

	gtk_text_buffer_get_end_iter(buffer, &end);
	if (gtk_text_buffer_get_char_count(buffer) > 0)
		gtk_text_buffer_insert(buffer, &end, "\n", -1);
	gtk_text_buffer_insert(buffer, &end, text, -1);
}

static char *
all_text(GtkTextBuffer *buffer)
{
	GtkTextIter start, end;

	gtk_text_buffer_get_bounds(buffer, &start, &end);
	return gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
}

static void
assert_text(GtkTextBuffer *buffer, const char *expected)
{
	char *text = all_text(buffer);

	g_assert_cmpstr(text, ==, expected);
	g_free(text);
}

static void
assert_line(X2TickBuf *tb, const char *id, const char *expected)
{
	char *line = x2_tick_buf_line_text(tb, id);

	g_assert_cmpstr(line, ==, expected);
	g_free(line);
}

static void
test_delivered_then_displayed(void)
{
	GtkTextBuffer *buffer = gtk_text_buffer_new(NULL);
	X2TickBuf *tb = x2_tick_buf_new(buffer, 10);

	append_line(buffer, "(12:00) me: one");
	x2_tick_buf_record(tb, "a");
	append_line(buffer, "(12:01) me: two");
	x2_tick_buf_record(tb, "b");
	append_line(buffer, "(12:02) bob: reply");

	g_assert_true(x2_tick_buf_update(tb, "a", X2_TICK_DELIVERED,
	                                 X2_TICK_STYLE_CHECKS));
	assert_line(tb, "a", "(12:00) me: one " CHECK);
	g_assert_true(x2_tick_buf_update(tb, "a", X2_TICK_DISPLAYED,
	                                 X2_TICK_STYLE_CHECKS));
	/* a late receipt does not take the read state back */
	g_assert_true(x2_tick_buf_update(tb, "a", X2_TICK_DELIVERED,
	                                 X2_TICK_STYLE_CHECKS));
	g_assert_cmpint(x2_tick_buf_state(tb, "a"), ==, X2_TICK_DISPLAYED);
	g_assert_true(x2_tick_buf_update(tb, "b", X2_TICK_DELIVERED,
	                                 X2_TICK_STYLE_CHECKS));
	assert_text(buffer, "(12:00) me: one " CHECK CHECK "\n"
	            "(12:01) me: two " CHECK "\n(12:02) bob: reply");

	/* lines written after a tick stay on their own line */
	append_line(buffer, "(12:03) me: three");
	assert_line(tb, "b", "(12:01) me: two " CHECK);

	g_assert_false(x2_tick_buf_update(tb, "nope", X2_TICK_DELIVERED,
	                                  X2_TICK_STYLE_CHECKS));
	g_assert_cmpint(x2_tick_buf_state(tb, "nope"), ==, X2_TICK_NONE);
	g_assert_null(x2_tick_buf_line_text(tb, "nope"));

	x2_tick_buf_free(tb);
	g_object_unref(buffer);
}

/* The last line: the tick goes at the very end of the buffer. */
static void
test_last_line(void)
{
	GtkTextBuffer *buffer = gtk_text_buffer_new(NULL);
	X2TickBuf *tb = x2_tick_buf_new(buffer, 10);

	append_line(buffer, "me: only");
	x2_tick_buf_record(tb, "a");
	x2_tick_buf_update(tb, "a", X2_TICK_DELIVERED, X2_TICK_STYLE_ASCII);
	assert_text(buffer, "me: only +");
	append_line(buffer, "me: next");
	x2_tick_buf_update(tb, "a", X2_TICK_DISPLAYED, X2_TICK_STYLE_ASCII);
	assert_text(buffer, "me: only ++\nme: next");

	x2_tick_buf_free(tb);
	g_object_unref(buffer);
}

static void
test_style_change_replaces(void)
{
	GtkTextBuffer *buffer = gtk_text_buffer_new(NULL);
	X2TickBuf *tb = x2_tick_buf_new(buffer, 10);

	append_line(buffer, "me: hi");
	x2_tick_buf_record(tb, "a");
	append_line(buffer, "bob: hey");
	x2_tick_buf_update(tb, "a", X2_TICK_DELIVERED, X2_TICK_STYLE_CHECKS);
	x2_tick_buf_update(tb, "a", X2_TICK_DELIVERED, X2_TICK_STYLE_WORDS);
	assert_text(buffer, "me: hi (delivered)\nbob: hey");
	x2_tick_buf_update(tb, "a", X2_TICK_DISPLAYED, X2_TICK_STYLE_WORDS);
	assert_text(buffer, "me: hi (read)\nbob: hey");

	x2_tick_buf_free(tb);
	g_object_unref(buffer);
}

/* Pidgin's typing notice sits right after the last line and is deleted
 * from its start mark; the tick must survive that. */
static void
test_typing_notice(void)
{
	GtkTextBuffer *buffer = gtk_text_buffer_new(NULL);
	X2TickBuf *tb = x2_tick_buf_new(buffer, 10);
	GtkTextMark *st, *en;
	GtkTextIter iter, end;

	append_line(buffer, "me: hi");
	x2_tick_buf_record(tb, "a");

	gtk_text_buffer_get_end_iter(buffer, &iter);
	gtk_text_buffer_create_mark(buffer, X2_TICK_TYPING_MARK, &iter, TRUE);
	gtk_text_buffer_insert(buffer, &iter, "\n bob is typing", -1);
	gtk_text_buffer_get_end_iter(buffer, &iter);
	gtk_text_buffer_create_mark(buffer, "typing-notification-end", &iter,
	                            TRUE);

	x2_tick_buf_update(tb, "a", X2_TICK_DELIVERED, X2_TICK_STYLE_CHECKS);
	assert_text(buffer, "me: hi " CHECK "\n bob is typing");

	/* update_typing_message(): delete [start mark, end mark] */
	st = gtk_text_buffer_get_mark(buffer, X2_TICK_TYPING_MARK);
	en = gtk_text_buffer_get_mark(buffer, "typing-notification-end");
	gtk_text_buffer_get_iter_at_mark(buffer, &iter, st);
	gtk_text_buffer_get_iter_at_mark(buffer, &end, en);
	gtk_text_buffer_delete_mark(buffer, st);
	gtk_text_buffer_delete_mark(buffer, en);
	gtk_text_buffer_delete(buffer, &iter, &end);

	assert_text(buffer, "me: hi " CHECK);
	g_assert_cmpuint(x2_tick_buf_size(tb), ==, 1);
	x2_tick_buf_update(tb, "a", X2_TICK_DISPLAYED, X2_TICK_STYLE_CHECKS);
	assert_text(buffer, "me: hi " CHECK CHECK);

	x2_tick_buf_free(tb);
	g_object_unref(buffer);
}

/* Scrollback trimming deletes from the top to the start of a line. */
static void
test_trimmed_lines_are_forgotten(void)
{
	GtkTextBuffer *buffer = gtk_text_buffer_new(NULL);
	X2TickBuf *tb = x2_tick_buf_new(buffer, 10);
	GtkTextIter start, end;

	append_line(buffer, "me: one");
	x2_tick_buf_record(tb, "a");
	append_line(buffer, "me: two");
	x2_tick_buf_record(tb, "b");
	append_line(buffer, "me: three");
	x2_tick_buf_record(tb, "c");
	x2_tick_buf_update(tb, "b", X2_TICK_DELIVERED, X2_TICK_STYLE_CHECKS);

	gtk_text_buffer_get_start_iter(buffer, &start);
	gtk_text_buffer_get_iter_at_line(buffer, &end, 2);
	gtk_text_buffer_delete(buffer, &start, &end);

	g_assert_cmpuint(x2_tick_buf_size(tb), ==, 1);
	g_assert_false(x2_tick_buf_update(tb, "a", X2_TICK_DELIVERED,
	                                  X2_TICK_STYLE_CHECKS));
	g_assert_false(x2_tick_buf_update(tb, "b", X2_TICK_DISPLAYED,
	                                  X2_TICK_STYLE_CHECKS));
	g_assert_true(x2_tick_buf_update(tb, "c", X2_TICK_DELIVERED,
	                                 X2_TICK_STYLE_CHECKS));
	assert_text(buffer, "me: three " CHECK);

	x2_tick_buf_free(tb);
	g_object_unref(buffer);
}

/* gtk_imhtml_clear(): everything goes, also the last line's mark. */
static void
test_cleared_view(void)
{
	GtkTextBuffer *buffer = gtk_text_buffer_new(NULL);
	X2TickBuf *tb = x2_tick_buf_new(buffer, 10);
	GtkTextIter start, end;

	append_line(buffer, "me: one");
	x2_tick_buf_record(tb, "a");
	append_line(buffer, "me: two");
	x2_tick_buf_record(tb, "b");
	x2_tick_buf_update(tb, "b", X2_TICK_DELIVERED, X2_TICK_STYLE_CHECKS);

	gtk_text_buffer_get_bounds(buffer, &start, &end);
	gtk_text_buffer_delete(buffer, &start, &end);
	g_assert_cmpuint(x2_tick_buf_size(tb), ==, 0);

	/* a receipt that arrives afterwards draws nothing */
	append_line(buffer, "me: new");
	g_assert_false(x2_tick_buf_update(tb, "b", X2_TICK_DISPLAYED,
	                                  X2_TICK_STYLE_CHECKS));
	assert_text(buffer, "me: new");

	/* explicit clear (cleared-message-history) */
	x2_tick_buf_record(tb, "c");
	x2_tick_buf_clear(tb);
	g_assert_cmpuint(x2_tick_buf_size(tb), ==, 0);

	x2_tick_buf_free(tb);
	g_object_unref(buffer);
}

/* Someone else removed our glyph: redraw once, touch nothing else. */
static void
test_glyph_removed_elsewhere(void)
{
	GtkTextBuffer *buffer = gtk_text_buffer_new(NULL);
	X2TickBuf *tb = x2_tick_buf_new(buffer, 10);
	GtkTextIter start, end;

	append_line(buffer, "me: hi");
	x2_tick_buf_record(tb, "a");
	append_line(buffer, "bob: x");
	x2_tick_buf_update(tb, "a", X2_TICK_DELIVERED, X2_TICK_STYLE_CHECKS);

	gtk_text_buffer_get_iter_at_offset(buffer, &start, 6);
	gtk_text_buffer_get_iter_at_offset(buffer, &end, 8);
	gtk_text_buffer_delete(buffer, &start, &end);
	assert_text(buffer, "me: hi\nbob: x");

	x2_tick_buf_update(tb, "a", X2_TICK_DISPLAYED, X2_TICK_STYLE_CHECKS);
	assert_text(buffer, "me: hi " CHECK CHECK "\nbob: x");

	x2_tick_buf_free(tb);
	g_object_unref(buffer);
}

/* Many messages: the oldest lines are dropped and their marks deleted. */
static void
test_bounded(void)
{
	GtkTextBuffer *buffer = gtk_text_buffer_new(NULL);
	X2TickBuf *tb = x2_tick_buf_new(buffer, 5);
	GtkTextMark *first = NULL;
	guint i;

	for (i = 0; i < 50; i++) {
		char *id = g_strdup_printf("id-%u", i);
		GtkTextIter end;

		append_line(buffer, id);
		if (i == 0) {
			gtk_text_buffer_get_end_iter(buffer, &end);
			first = gtk_text_buffer_create_mark(buffer, NULL, &end, TRUE);
		}
		x2_tick_buf_record(tb, id);
		g_free(id);
	}
	g_assert_cmpuint(x2_tick_buf_size(tb), ==, 5);
	g_assert_false(x2_tick_buf_update(tb, "id-44", X2_TICK_DELIVERED,
	                                  X2_TICK_STYLE_CHECKS));
	g_assert_true(x2_tick_buf_update(tb, "id-45", X2_TICK_DELIVERED,
	                                 X2_TICK_STYLE_CHECKS));
	assert_line(tb, "id-45", "id-45 " CHECK);

	/* the evicted mark is gone from the buffer: only @first is left at
	 * the end of the first line */
	{
		GtkTextIter at;
		GSList *marks;

		gtk_text_buffer_get_iter_at_mark(buffer, &at, first);
		marks = gtk_text_iter_get_marks(&at);
		g_assert_cmpuint(g_slist_length(marks), ==, 1);
		g_slist_free(marks);
	}

	x2_tick_buf_free(tb);
	g_object_unref(buffer);
}

/* After free, the buffer lives on without our handler or marks. */
static void
test_free_detaches(void)
{
	GtkTextBuffer *buffer = gtk_text_buffer_new(NULL);
	X2TickBuf *tb = x2_tick_buf_new(buffer, 10);
	GtkTextIter start, end, at;
	GSList *marks;

	append_line(buffer, "me: hi");
	x2_tick_buf_record(tb, "a");
	x2_tick_buf_update(tb, "a", X2_TICK_DELIVERED, X2_TICK_STYLE_CHECKS);
	x2_tick_buf_free(tb);

	gtk_text_buffer_get_iter_at_offset(buffer, &at, 6);
	marks = gtk_text_iter_get_marks(&at);
	g_assert_cmpuint(g_slist_length(marks), ==, 0);
	g_slist_free(marks);

	gtk_text_buffer_get_bounds(buffer, &start, &end);
	gtk_text_buffer_delete(buffer, &start, &end);
	g_object_unref(buffer);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/tickbuf/delivered-then-displayed",
	                test_delivered_then_displayed);
	g_test_add_func("/tickbuf/last-line", test_last_line);
	g_test_add_func("/tickbuf/style-change", test_style_change_replaces);
	g_test_add_func("/tickbuf/typing-notice", test_typing_notice);
	g_test_add_func("/tickbuf/trimmed", test_trimmed_lines_are_forgotten);
	g_test_add_func("/tickbuf/cleared", test_cleared_view);
	g_test_add_func("/tickbuf/glyph-removed", test_glyph_removed_elsewhere);
	g_test_add_func("/tickbuf/bounded", test_bounded);
	g_test_add_func("/tickbuf/free-detaches", test_free_detaches);
	return g_test_run();
}
