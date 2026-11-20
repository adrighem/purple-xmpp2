/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file ticks.h Shared between ticks.c and its test hooks (ticks_test.c)
 */
#ifndef XMPP2_TICKS_TICKS_H_
#define XMPP2_TICKS_TICKS_H_

#include "conversation.h"
#include "plugin.h"

#include "tickbuf.h"

#define PRPL_ID "prpl-xmpp2"
#define LOG "xmpp2-ticks"

/**
 * Tick state of the view showing @a conv (an xmpp2 IM in Pidgin), created
 * on demand when @a create; NULL for any other conversation.
 */
X2TickBuf *x2_ticks_view(PurpleConversation *conv, gboolean create);

#ifdef XMPP2_TEST_HOOKS
/* ticks_test.c, built with -DXMPP2_TEST_HOOKS=ON only */
void x2_ticks_test_load(PurplePlugin *plugin);
void x2_ticks_test_rendered(PurpleConversation *conv, X2TickBuf *tb,
                            const char *id);
#endif

#endif /* XMPP2_TICKS_TICKS_H_ */
