# Feature gap: Pidgin 2.14 jabber vs Gajim

Gajim column based on Gajim master `src/gajim/common/modules/` (checked
2026-11, e.g. `mam.py`, `carbons.py`, `receipts.py`, `chat_markers.py`,
`mds.py`, `httpupload.py`, `omemo.py`). Target: `done`, `v0.2`, `later`, `never`,
or `have` (inherited, kept).

| Feature | XEP | Pidgin 2.14 | Gajim | purple-xmpp2 |
|---|---|---|---|---|
| Message archive catch-up, 1:1 | 0313 | no | yes | done (v0.1.0) |
| Message archive catch-up, MUC | 0313 | no (legacy history replayed on every join) | yes | done (v0.1.0) |
| Unique and stable stanza ids, dedup | 0359 | no | yes | done (v0.1.0) |
| Message carbons | 0280 | no (third-party plugin) | yes | done (v0.1.0) |
| Stream management, acks | 0198 | partial (acks, no resume, bugs) | yes | done (v0.1.0: batched acks, no blind resend, unacked reported) |
| Stream management, resumption | 0198 | no | yes | v0.2 (M10, silent reconnect) |
| HTTP file upload | 0363 | no | yes | done (https by default; v0.2.0 streams from disk with progress) |
| Delivery receipts | 0184 | no | yes | done (v0.1.0: protocol, signal, optional notice; no ticks UI) |
| Chat markers | 0333 | no | yes | done (v0.1.0: 1:1, displayed on focus) |
| Last message correction | 0308 | no | yes | done (v0.1.0: receive as "corrected:" line, `/correct`) |
| Displayed sync across devices | 0490 | no | yes | later |
| Message retraction | 0424 | no | yes | later |
| Moderation (MUC) | 0425 | no | yes | later |
| Reactions | 0444 | no | yes | later |
| Replies | 0461 | no | yes | later |
| Occupant id | 0421 | no | yes | later (used if present for corrections) |
| Bookmarks 2 (PEP, autojoin sync) | 0402 | no (local blist only) | yes | later |
| Client state indication | 0352 | no | yes | later (cheap: Pidgin idle/away) |
| Direct TLS via SRV | 0368 | old-SSL option only | yes | later |
| SCRAM-SHA-256, SASL2, FAST | RFC 7677, 0388, 0484 | SCRAM-SHA-1 only | SHA-256 yes | later (SHA-256 is cheap) |
| Message styling | 0393 | no (XHTML-IM instead) | yes | later |
| Avatars (PEP, vCard) | 0084, 0153 | yes | yes | have |
| MUC | 0045 | yes | yes | have |
| Entity caps | 0115 | yes | yes | have |
| Chat states | 0085 | yes | yes | have |
| XHTML-IM | 0071 | yes | removed (deprecated) | have (keep for Pidgin formatting) |
| Ad-hoc commands | 0050 | yes | yes | have |
| In-band registration | 0077 | yes | yes | have |
| Blocking command | 0191 | yes | yes | have |
| Ping, entity time, version | 0199, 0202, 0092 | yes | yes | have |
| User mood, tune, nick | 0107, 0118, 0172 | yes | tune, nick | have |
| Attention (buzz) | 0224 | yes | no | have |
| SI file transfer (S5B, IBB) | 0096, 0065, 0047 | yes | Jingle FT only | have (fallback; removal candidate) |
| Jingle file transfer | 0234 | no | yes | later |
| OMEMO encryption | 0384 | no (lurch plugin) | yes | later (evaluate in v0.2) |
| OpenPGP | 0027, 0373 | no | 0373 yes | never (0027 obsolete) |
| Jingle audio/video | 0166/0167 | yes (no DTLS-SRTP) | yes | never in this plugin (GType clash, no interop; use prpl-jabber) |
| BOSH | 0124, 0206 | yes | no | never (dropped) |
| WebSocket transport | RFC 7395 | no | yes | never (desktop has TCP) |
| Google Talk extensions | n/a | yes | no | never (dropped, service gone) |
| Cyrus SASL / GSSAPI | n/a | optional | via SASL lib | never (dropped; use prpl-jabber) |
| DIGEST-MD5 | RFC 6331 | yes | no | never (dropped) |
| Local searchable archive DB | n/a | text/HTML logs | SQLite | never (Pidgin logs stay authoritative) |
