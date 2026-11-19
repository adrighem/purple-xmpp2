/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file x2types.h Forward declarations shared by the upstream files and ext/
 *
 * Kept free of includes so that jabber.h and message.h can use it.
 */
#ifndef XMPP2_EXT_X2TYPES_H_
#define XMPP2_EXT_X2TYPES_H_

typedef struct _X2Stream X2Stream;   /* per-stream state, x2stream.h */
typedef struct _X2Msg X2Msg;         /* inbound message context, msgctx.h */
typedef struct _X2Out X2Out;         /* outbound message info, ext.h */

#endif /* XMPP2_EXT_X2TYPES_H_ */
