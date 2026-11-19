/**
 * @file xmpp2_internal.h Local replacement for libpurple's internal.h
 *
 * libpurple does not install internal.h, so the plugin carries the few
 * pieces of it that the (jabber derived) sources actually rely on:
 * gettext macros, the common system includes and a couple of constants.
 */

/* purple-xmpp2
 *
 * Derived from internal.h of Pidgin/libpurple 2.14.14.
 *
 * Purple is the legal property of its developers, whose names are too numerous
 * to list here.  Please refer to the COPYRIGHT file distributed with the
 * Pidgin source distribution.
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
#ifndef _XMPP2_INTERNAL_H_
#define _XMPP2_INTERNAL_H_

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib-object.h>
#include <gmodule.h>

#include "glibcompat.h"

#include "account.h"
#include "connection.h"
#include "version.h"

/* upstream's config.h VERSION: the libpurple version, as a string literal */
#define XMPP2_STR_(x) #x
#define XMPP2_STR(x) XMPP2_STR_(x)
#ifndef VERSION
# define VERSION XMPP2_STR(PURPLE_MAJOR_VERSION) "." \
	XMPP2_STR(PURPLE_MINOR_VERSION) "." XMPP2_STR(PURPLE_MICRO_VERSION)
#endif

/* Identity of this plugin, see info in libxmpp.c. */
#define XMPP2_PRPL_ID "prpl-xmpp2"
#define XMPP2_WEBSITE "https://github.com/adrighem/purple-xmpp2"

/* Exported by libpurple.so but declared only in its private network.h. */
gboolean _purple_network_set_common_socket_flags(int fd);

/* Stands in for upstream's PACKAGE; also the gettext domain. */
#define XMPP2_PACKAGE "purple-xmpp2"

/*
 * Translations come from the plugin's own gettext domain.  Without an
 * installed catalog dgettext() simply returns the msgid.
 */

#ifdef ENABLE_NLS
# include <libintl.h>
/*
 * Most inherited strings are translated in Pidgin's own catalogs ("pidgin"
 * domain, bound by libpurple), so fall back to those for free.  dgettext()
 * returns its argument unchanged when there is no translation.
 */
static inline const char *
x2_gettext(const char *msgid)
{
	const char *tr = dgettext(XMPP2_PACKAGE, msgid);

	return tr != msgid ? tr : dgettext("pidgin", msgid);
}
# define _(String) x2_gettext(String)
#else
# define _(String) ((const char *)(String))
#endif
#define N_(String) (String)

#endif /* _XMPP2_INTERNAL_H_ */
