/**
 * @file stream_management.h XEP-0198
 *
 * purple
 *
 * Purple is the legal property of its developers, whose names are too numerous
 * to list here.  Please refer to the COPYRIGHT file distributed with this
 * source distribution.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111-1301  USA
 */

#ifndef PURPLE_JABBER_STREAM_MANAGEMENT_H
#define PURPLE_JABBER_STREAM_MANAGEMENT_H

void jabber_sm_init(void);
void jabber_sm_uninit(void);

void jabber_sm_enable(JabberStream *js);
void jabber_sm_process_packet(JabberStream *js, xmlnode *packet);

void jabber_sm_ack_send(JabberStream *js);

/* Frees the account's SM state; call from jabber_close(). */
void jabber_sm_close(JabberStream *js);

void jabber_sm_outbound(JabberStream *js, xmlnode *packet);

/*
 * Counts the stanzas in raw XML about to be sent (XML console).  Returns
 * FALSE, counting nothing, if stream management is on and @a buf is not a
 * sequence of complete elements: the caller must not send it then.
 */
gboolean jabber_sm_outbound_raw(JabberStream *js, const char *buf, int len);
void jabber_sm_inbound(JabberStream *js, xmlnode *packet);

#endif /* PURPLE_JABBER_STREAM_MANAGEMENT_H */
