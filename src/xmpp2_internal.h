/**
 * @file xmpp2_internal.h Local replacement for libpurple's internal.h
 *
 * libpurple does not install internal.h, so the plugin carries the few
 * pieces of it that the (jabber derived) sources actually rely on:
 * gettext macros, the common system includes and a couple of constants.
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
# define _(String) ((const char *)dgettext(XMPP2_PACKAGE, String))
#else
# define _(String) ((const char *)(String))
#endif
#define N_(String) (String)

#endif /* _XMPP2_INTERNAL_H_ */
