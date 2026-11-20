# Changelog

## [0.2.0](https://github.com/adrighem/purple-xmpp2/compare/v0.1.0...v0.2.0) (2026-11-20)


### Features

* **receipts:** emit xmpp2-sent-message for outgoing 1:1 messages ([598b066](https://github.com/adrighem/purple-xmpp2/commit/598b0665fa6f05e1898b190738c82fbb8879cda3))
* **sm:** resumption policy and resend computation (M10 core) ([dbc2b52](https://github.com/adrighem/purple-xmpp2/commit/dbc2b52cf7a0e4cd3c27dbf5b0cce3889769b55d))
* **sm:** XEP-0198 stream resumption as a silent reconnect (M10) ([7e56454](https://github.com/adrighem/purple-xmpp2/commit/7e564544065e41a6a6b316e6c0be32331cc00b74))
* **ticks:** add pidgin-xmpp2-ticks, delivery and read ticks in Pidgin ([e26a48c](https://github.com/adrighem/purple-xmpp2/commit/e26a48c04c09f4df608b0023808f41163cb222f7))
* **upload:** stream the file from disk with progress ([7f58f6f](https://github.com/adrighem/purple-xmpp2/commit/7f58f6feea1fe54b37c3ecdf0306e4cbf716d6d5))


### Bug Fixes

* **sm:** act on &lt;resumed/&gt; and &lt;failed/&gt; only after our &lt;resume/&gt; ([deeea2f](https://github.com/adrighem/purple-xmpp2/commit/deeea2f0e8dcafef00d40a6b0efc5f1956d061b0))
* **sm:** log only a short prefix of the resume id ([176d624](https://github.com/adrighem/purple-xmpp2/commit/176d624a8dddc1f06ed82118848d129ec0507555))
* **sm:** parse the h attribute strictly ([6f2c732](https://github.com/adrighem/purple-xmpp2/commit/6f2c73298fb48a745b4bb686d4261580503d8160))
* **test:** make peer.py bursts wait for the server ([6fd853c](https://github.com/adrighem/purple-xmpp2/commit/6fd853c70ff65202c88674b06cace460b2c8448f))
* **ticks:** name the room/nick conversation in xmpp2-sent-message ([5a025d9](https://github.com/adrighem/purple-xmpp2/commit/5a025d9699548a715afb7da9e02c74a6f6a0662f))
* **upload:** a 2xx before the whole body was sent is a failure ([6162245](https://github.com/adrighem/purple-xmpp2/commit/61622451f4a8f206a60d7f3a8c9d99db38efcc61))
* **upload:** fail a PUT that stalls for 60 seconds ([e0a165b](https://github.com/adrighem/purple-xmpp2/commit/e0a165b9f486c78bbcc269067a938222864de858))
* **upload:** treat EINTR like EAGAIN in the PUT client ([aaa7cc5](https://github.com/adrighem/purple-xmpp2/commit/aaa7cc54a1aede943a6a643953b854cbcdfc292a))

## 0.1.0 (2026-11-20)


### Features

* **carbons:** XEP-0280 message carbons with forged-carbon checks (M2) ([81d57fd](https://github.com/adrighem/purple-xmpp2/commit/81d57fd8cdb8ca9660755a5dcc8dbdc1936ac37a))
* **correct:** XEP-0308 last message correction and /correct command ([4532cc8](https://github.com/adrighem/purple-xmpp2/commit/4532cc878813b4a38565c5b2f18e48ae663ea61f))
* extension layer, message pipeline and core helpers (M1) ([f173c9d](https://github.com/adrighem/purple-xmpp2/commit/f173c9d382324e143e3fd0a79d5d01e62ced15b9))
* **mam:** XEP-0313 catch-up for rooms (M4) ([d8c9017](https://github.com/adrighem/purple-xmpp2/commit/d8c9017aa99bc6ad78ced2c6df1ce11867e46c29))
* **mam:** XEP-0313 catch-up of the account archive (M3) ([35e9baa](https://github.com/adrighem/purple-xmpp2/commit/35e9baad6c673903a7bf55e274effcdfbda5ddcd))
* **migrate:** copy prpl-jabber accounts to prpl-xmpp2 ([4efcfbf](https://github.com/adrighem/purple-xmpp2/commit/4efcfbf37814d15ac84b0a19ffc5ae5cd5ccd6db))
* **receipts:** delivery receipts (XEP-0184) and chat markers (XEP-0333) ([b7f0a12](https://github.com/adrighem/purple-xmpp2/commit/b7f0a126e177a9f3ed9b49480f32b51f56aff64a))
* **upload:** HTTP file upload (XEP-0363) for send_file and rooms ([a1dc558](https://github.com/adrighem/purple-xmpp2/commit/a1dc55852b0e0df0a05087208cd374911e7df90d))
* **upload:** XEP-0363 protocol core with unit tests ([9575b7f](https://github.com/adrighem/purple-xmpp2/commit/9575b7f3530baeb7bb9ebbf3bdf9f9b2ab0a3d11))


### Bug Fixes

* **correct:** trust occupant-id only from rooms that advertise it ([cd67129](https://github.com/adrighem/purple-xmpp2/commit/cd67129ba1e332b1dfc05f1c39f0a92b4ec9ddad))
* harden message pipeline trust, delay parsing and ownership (M1.1) ([8ff3fca](https://github.com/adrighem/purple-xmpp2/commit/8ff3fcae23c1ab1adbae91ab3a7cf41b3819ae30))
* **mam:** accept complete="1" in &lt;fin/&gt; ([ba53d31](https://github.com/adrighem/purple-xmpp2/commit/ba53d31ba34bd656badf50518a5d69d30a0c6009))
* **mam:** always one account-level "history incomplete" notice ([c51ffa8](https://github.com/adrighem/purple-xmpp2/commit/c51ffa857dc442d5a1eed9d519ff1bbe536f9cff))
* **mam:** bound the last page, scope stanza-id keys by archive ([dd12305](https://github.com/adrighem/purple-xmpp2/commit/dd12305e1d091bc8cdc9d6ee72f2a692472443f0))
* **mam:** compile the stall test hook only with XMPP2_TEST_HOOKS ([48d49ff](https://github.com/adrighem/purple-xmpp2/commit/48d49ff677fc3bc676cdcbb6af220d7677896773))
* **mam:** connect account-removed once libpurple registered it ([ef0271a](https://github.com/adrighem/purple-xmpp2/commit/ef0271a0620563078e4d537af62d357617eac088))
* **mam:** drop queued offline copies only after a page-cap skip ([c58c54e](https://github.com/adrighem/purple-xmpp2/commit/c58c54e6babe2304fb0ac8ede3fba2558901f782))
* **mam:** keep keys in the ring while the cursor is frozen ([b4355e5](https://github.com/adrighem/purple-xmpp2/commit/b4355e58ed254c330f2d94f52bab3e395b437a96))
* **mam:** key own sends by the same peer as inbound messages ([05fa0ff](https://github.com/adrighem/purple-xmpp2/commit/05fa0ffb8a97fe1fddde43f217fd5d100a58dff1))
* **mam:** pass bodyless messages during catch-up, cap the queue ([2fb3100](https://github.com/adrighem/purple-xmpp2/commit/2fb31000bbd45e0cc6ed4bd645897385e411e758))
* **mam:** retry from cursor_ts when the cursor probe fails ([4225552](https://github.com/adrighem/purple-xmpp2/commit/42255521c93ba0bc31ab429f482933df62371e3e))
* **mam:** show a live message live when the catch-up also returns it ([967b274](https://github.com/adrighem/purple-xmpp2/commit/967b2742744e2f2d7feb21b31e77a012dbe55833))
* **migrate:** copy the account avatar ([ed660e9](https://github.com/adrighem/purple-xmpp2/commit/ed660e91fcb544ad7e8ff5280875254d16cd38be))
* **migrate:** reachable Tools action, bare-JID twins, test-only offer hook ([35dfad6](https://github.com/adrighem/purple-xmpp2/commit/35dfad6d0e960e612e6a399ae9b1bd20d43eaa99))
* **receipts,markers:** answer offline-stored requests, gate displayed on subscription ([890db98](https://github.com/adrighem/purple-xmpp2/commit/890db982b5652b7ffd506efc189a9871da49db9d))
* **receipts:** answer requests before the MAM catch-up can drop them ([77dd084](https://github.com/adrighem/purple-xmpp2/commit/77dd084de906bb23cf19198ceb35792f68d378ae))
* **sm:** batched acks, modular h, no blind resend, report unacked messages (M9) ([e878144](https://github.com/adrighem/purple-xmpp2/commit/e878144c4f722264bf3eb4d7c2ab28d617bfa051))
* **sm:** count stanzas sent as raw XML (XML console) ([44b5905](https://github.com/adrighem/purple-xmpp2/commit/44b590575b22e0ecec2407cd9de407f92f295019))
* **sm:** no new conversation for the sign-off notice ([1152616](https://github.com/adrighem/purple-xmpp2/commit/115261699cea085dee1e849ada985360db719124))
* **sm:** plain enable, queue only messages, report unacked on sign-off ([1212aba](https://github.com/adrighem/purple-xmpp2/commit/1212aba2cd53010f719b4f5eec60d4d8482e2bc0))
* **upload:** accept 200, 201 and 204 as a successful PUT ([8e37a69](https://github.com/adrighem/purple-xmpp2/commit/8e37a697f8a59cf1e18c6bd5839420347116919a))
* **upload:** accept only 201, bound the response, free the PUT early ([964d5a1](https://github.com/adrighem/purple-xmpp2/commit/964d5a172bc10024fa779603ca965340bad663a7))
* **upload:** cancel transfers in stream_closing, not stream_free ([706d6e0](https://github.com/adrighem/purple-xmpp2/commit/706d6e03672faab00eaa7ba7cfcd367403d5abb7))

## Changelog
