/**
 * @file x2stream.h Per-stream state of the xmpp2 feature modules
 *
 * JabberStream::x2 points to one of these.  Every v1 module owns exactly one
 * pointer, declared here up front so that feature milestones never have to
 * edit this file: a module allocates its private struct in its stream_new
 * hook and frees it in stream_free.
 */
#ifndef XMPP2_EXT_X2STREAM_H_
#define XMPP2_EXT_X2STREAM_H_

#include "core/dedup.h"
#include "ext/x2types.h"

/* Opaque, defined in the module's own .c file */
struct X2Carbons;
struct X2Mam;
struct X2Receipts;
struct X2Markers;
struct X2Correction;
struct X2Upload;

struct _X2Stream {
	/* Keys of recently seen and sent messages, see core/dedup.h */
	X2Dedup *dedup;

	struct X2Carbons *carbons;
	struct X2Mam *mam;
	struct X2Receipts *receipts;
	struct X2Markers *markers;
	struct X2Correction *correction;
	struct X2Upload *upload;
};

#endif /* XMPP2_EXT_X2STREAM_H_ */
