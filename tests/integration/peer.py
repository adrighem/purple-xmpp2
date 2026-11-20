#!/usr/bin/env python3
"""Scripted XMPP peer client (slixmpp) used as the second party in tests.

Usage: peer.py --jid bob@localhost --resource peer --password secret \
        --port 15222 [--script FILE] --do "send alice@localhost hello" ...

Actions run in order after the session starts (from --do and/or --script,
one per line, '#' comments). Output lines are prefixed "PEER" for grep.

Sending:
  send JID TEXT             chat message (prints id)
  sendr JID TEXT            chat message with receipt request + chat marker
                            markable (prints id)
  sendmany JID N PREFIX [GAP_MS]   N messages "PREFIX-1" .. "PREFIX-N"
  correct JID ID TEXT       XEP-0308 correction of message ID
  raw XML                   send a hand-written stanza verbatim
  groupsend ROOM TEXT       groupchat message
  groupsendmany ROOM N PREFIX [GAP_MS]   N groupchat messages "PREFIX-1" ..
  befriend JID              presence subscribe (auto-approves inbound requests)
Waiting and assertions (regex for stanza actions, substring for wait):
  wait TEXT [SECS]          wait for a chat body containing TEXT (history counts)
  nowait TEXT [SECS]        assert no such body arrives (default 3 s)
  groupwait TEXT            wait for a groupchat body containing TEXT
  expect-stanza REGEX [SECS]  an inbound stanza (any kind) matches REGEX
  no-stanza REGEX [SECS]    no inbound stanza matches (history counts)
  count-stanza REGEX N      exactly N inbound stanzas match so far
  expect-out REGEX [SECS]   same for stanzas this peer sent
  bind NAME REGEX [SECS]    wait for an inbound stanza matching REGEX and store
                            its group 1; "${NAME}" in later actions expands to it
  clear                     forget recorded messages and stanzas
  sleep SECS
Settings:
  receipts on|off           auto-reply <received/> to receipt requests (off)
  markers on|off            auto-reply <displayed/> to markable messages (off)
  carbons on|off            XEP-0280 enable/disable for this resource
Queries:
  mam [WITH_JID]            print "PEER-MAM <from> <body>" for each archived msg
  disco [JID]               print "PEER-FEATURE <ns>" (default: the server)
  streamfeatures            print negotiated stream features
  join ROOM NICK / leave ROOM
Files (no aiohttp needed):
  upload PATH [DOMAIN]      XEP-0363 slot request + PUT, prints "PEER-URL <get url>"
  sha256 URL                GET URL, prints "PEER-SHA256 <hex>"
  filesha PATH              prints "PEER-SHA256 <hex>" of a local file
Connection:
  abort                     drop the TCP connection without closing the stream

Options: --feature NS advertises a feature in caps and disco (repeatable).

Exit status 0 when every action succeeded, 1 otherwise. With --stay SECS the
peer keeps the session open after the last action (for background peers).
"""
import argparse
import asyncio
import hashlib
import logging
import re
import ssl
import sys
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path

import slixmpp
from slixmpp.stanza import Message

NS_RECEIPTS = "urn:xmpp:receipts"
NS_MARKERS = "urn:xmpp:chat-markers:0"
NS_UPLOAD = "urn:xmpp:http:upload:0"
NS_REPLACE = "urn:xmpp:message-correct:0"
NS_DISCO = "http://jabber.org/protocol/disco#info"


class Peer(slixmpp.ClientXMPP):
    def __init__(self, jid: str, password: str, timeout: float) -> None:
        super().__init__(jid, password)
        self.timeout = timeout
        self.msgs: list[tuple[str, str, str]] = []   # (type, from, body)
        self.stanzas_in: list[str] = []
        self.stanzas_out: list[str] = []
        self.auto_receipts = False
        self.auto_markers = False
        self.wakeup = asyncio.Event()
        self.vars: dict[str, str] = {}
        for plug in ("xep_0030", "xep_0045", "xep_0199", "xep_0313",
                     "xep_0280", "xep_0198"):
            self.register_plugin(plug)
        self.enable_direct_tls = False
        self.enable_starttls = True
        self.ssl_context.check_hostname = False
        self.ssl_context.verify_mode = ssl.CERT_NONE
        self.add_filter("in", self._filter_in)
        self.add_filter("out", self._filter_out)

    # ---- recording ----
    def _filter_in(self, stanza):
        text = str(stanza)
        self.stanzas_in.append(text)
        if isinstance(stanza, Message):
            self._record_message(stanza)
        self.wakeup.set()
        return stanza

    def _filter_out(self, stanza):
        self.stanzas_out.append(str(stanza))
        self.wakeup.set()
        return stanza

    def _record_message(self, msg: Message) -> None:
        if msg["body"] and msg["type"] in ("chat", "normal", "groupchat"):
            self.msgs.append((msg["type"], str(msg["from"]), msg["body"]))
            print(f"PEER recv {msg['type']} {msg['from']}: {msg['body']}",
                  flush=True)
        xml = msg.xml
        sender, mid = str(msg["from"]), msg["id"]
        if not sender or not mid or msg["type"] not in ("chat", "normal"):
            return
        if self.auto_receipts and xml.find(f"{{{NS_RECEIPTS}}}request") is not None:
            self.send_raw(
                f"<message to='{sender}'><received xmlns='{NS_RECEIPTS}' "
                f"id='{mid}'/></message>")
        if self.auto_markers and xml.find(f"{{{NS_MARKERS}}}markable") is not None:
            self.send_raw(
                f"<message to='{sender}' type='chat'><displayed "
                f"xmlns='{NS_MARKERS}' id='{mid}'/></message>")

    async def _poll(self, check, secs: float, want: bool) -> bool:
        """Wait until check() == want; True on success, False on timeout."""
        loop = asyncio.get_running_loop()
        end = loop.time() + secs
        while check() != want:
            left = end - loop.time()
            if left <= 0:
                return False
            self.wakeup.clear()
            try:
                await asyncio.wait_for(self.wakeup.wait(), min(left, 0.2))
            except asyncio.TimeoutError:
                pass
        return True

    @staticmethod
    def _args_secs(rest: str, default: float) -> tuple[str, float]:
        head, _, tail = rest.rpartition(" ")
        if head and re.fullmatch(r"\d+(\.\d+)?", tail):
            return head, float(tail)
        return rest, default

    # ---- message builders ----
    def _chat(self, to: str, text: str, extra: list[str] = ()) -> str:
        msg = self.make_message(mto=to, mbody=text, mtype="chat")
        for frag in extra:
            msg.xml.append(ET.fromstring(frag))
        msg.send()
        print(f"PEER sent {to} id={msg['id']}: {text}", flush=True)
        return msg["id"]

    # ---- actions ----
    async def run_action(self, line: str) -> None:
        for name, val in self.vars.items():
            line = line.replace("${%s}" % name, val)
        cmd, _, rest = line.strip().partition(" ")
        handler = getattr(self, "act_" + cmd.replace("-", "_"), None)
        if handler is None:
            raise ValueError(f"unknown action: {line}")
        await handler(rest)

    async def act_send(self, rest: str) -> None:
        to, _, text = rest.partition(" ")
        self._chat(to, text)

    async def act_sendr(self, rest: str) -> None:
        to, _, text = rest.partition(" ")
        self._chat(to, text, [f"<request xmlns='{NS_RECEIPTS}'/>",
                              f"<markable xmlns='{NS_MARKERS}'/>"])

    async def _server_caught_up(self) -> None:
        """Round trip to the server: it answers only after it handled every
        stanza sent before.  Without it the peer can exit (and close the
        stream) while part of a big burst is still unsent or unprocessed,
        which lost messages in the bulk MAM tests."""
        await self.plugin["xep_0199"].ping(timeout=self.timeout)

    async def act_sendmany(self, rest: str) -> None:
        to, n, prefix, *gap = rest.split()
        for i in range(1, int(n) + 1):
            self.send_message(mto=to, mbody=f"{prefix}-{i}", mtype="chat")
            if gap:
                await asyncio.sleep(int(gap[0]) / 1000)
            elif i % 50 == 0:
                await asyncio.sleep(0.05)
        await self._server_caught_up()
        print(f"PEER sent {n} to {to}", flush=True)

    async def act_correct(self, rest: str) -> None:
        to, mid, text = rest.split(" ", 2)
        self._chat(to, text, [f"<replace xmlns='{NS_REPLACE}' id='{mid}'/>"])

    async def act_raw(self, rest: str) -> None:
        self.send_raw(rest)

    async def act_groupsend(self, rest: str) -> None:
        room, _, text = rest.partition(" ")
        self.send_message(mto=room, mbody=text, mtype="groupchat")

    async def act_groupsendmany(self, rest: str) -> None:
        room, n, prefix, *gap = rest.split()
        for i in range(1, int(n) + 1):
            self.send_message(mto=room, mbody=f"{prefix}-{i}",
                              mtype="groupchat")
            if gap:
                await asyncio.sleep(int(gap[0]) / 1000)
            elif i % 50 == 0:
                await asyncio.sleep(0.05)
        await self._server_caught_up()
        print(f"PEER sent {n} to {room}", flush=True)

    async def act_befriend(self, rest: str) -> None:
        self.send_presence(pto=rest, ptype="subscribe")

    async def act_wait(self, rest: str) -> None:
        text, secs = self._args_secs(rest, self.timeout)
        ok = await self._poll(lambda: any(
            text in m[2] and m[0] in ("chat", "normal") for m in self.msgs),
            secs, True)
        if not ok:
            raise asyncio.TimeoutError(f"no message containing {text!r}")
        print(f"PEER matched {text}", flush=True)

    async def act_nowait(self, rest: str) -> None:
        text, secs = self._args_secs(rest, 3.0)
        seen = await self._poll(
            lambda: any(text in m[2] for m in self.msgs), secs, True)
        if seen:
            raise ValueError(f"unexpected message containing {text!r}")
        print(f"PEER absent {text}", flush=True)

    async def act_groupwait(self, rest: str) -> None:
        ok = await self._poll(lambda: any(
            rest in m[2] and m[0] == "groupchat" for m in self.msgs),
            self.timeout, True)
        if not ok:
            raise asyncio.TimeoutError(f"no groupchat containing {rest!r}")
        print(f"PEER matched {rest}", flush=True)

    @staticmethod
    def _count(pattern: str, pool: list[str]) -> int:
        rx = re.compile(pattern, re.S)
        return sum(1 for s in pool if rx.search(s))

    async def act_expect_stanza(self, rest: str) -> None:
        rx, secs = self._args_secs(rest, self.timeout)
        if not await self._poll(
                lambda: self._count(rx, self.stanzas_in) > 0, secs, True):
            raise asyncio.TimeoutError(f"no inbound stanza matching {rx!r}")
        print(f"PEER matched stanza {rx}", flush=True)

    async def act_expect_out(self, rest: str) -> None:
        rx, secs = self._args_secs(rest, self.timeout)
        if not await self._poll(
                lambda: self._count(rx, self.stanzas_out) > 0, secs, True):
            raise asyncio.TimeoutError(f"no outbound stanza matching {rx!r}")
        print(f"PEER matched out {rx}", flush=True)

    async def act_no_stanza(self, rest: str) -> None:
        rx, secs = self._args_secs(rest, 3.0)
        if await self._poll(
                lambda: self._count(rx, self.stanzas_in) > 0, secs, True):
            raise ValueError(f"unexpected inbound stanza matching {rx!r}")
        print(f"PEER absent stanza {rx}", flush=True)

    async def act_count_stanza(self, rest: str) -> None:
        rx, _, want = rest.rpartition(" ")
        got = self._count(rx, self.stanzas_in)
        if got != int(want):
            raise ValueError(f"stanza {rx!r}: want {want} got {got}")
        print(f"PEER count {rx} = {got}", flush=True)

    async def act_bind(self, rest: str) -> None:
        name, _, rest = rest.partition(" ")
        rx, secs = self._args_secs(rest, self.timeout)
        comp = re.compile(rx, re.S)

        def found() -> bool:
            return any(comp.search(s) for s in self.stanzas_in)
        if not await self._poll(found, secs, True):
            raise asyncio.TimeoutError(f"no inbound stanza matching {rx!r}")
        hit = next(m for s in self.stanzas_in if (m := comp.search(s)))
        self.vars[name] = hit.group(1)
        print(f"PEER bound {name}={self.vars[name]}", flush=True)

    async def act_clear(self, rest: str) -> None:
        self.msgs.clear()
        self.stanzas_in.clear()
        self.stanzas_out.clear()

    async def act_sleep(self, rest: str) -> None:
        await asyncio.sleep(float(rest))

    async def act_receipts(self, rest: str) -> None:
        self.auto_receipts = rest.strip() == "on"

    async def act_markers(self, rest: str) -> None:
        self.auto_markers = rest.strip() == "on"

    async def act_carbons(self, rest: str) -> None:
        if rest.strip() == "on":
            await self.plugin["xep_0280"].enable()
        else:
            await self.plugin["xep_0280"].disable()
        print(f"PEER carbons {rest.strip()}", flush=True)

    async def act_mam(self, rest: str) -> None:
        kw = {"with_jid": slixmpp.JID(rest)} if rest else {}
        res = await self.plugin["xep_0313"].retrieve(**kw)
        for item in res["mam"]["results"]:
            fwd = item["mam_result"]["forwarded"]["stanza"]
            print(f"PEER-MAM {fwd['from']} {fwd['body']}", flush=True)

    async def act_disco(self, rest: str) -> None:
        info = await self.plugin["xep_0030"].get_info(
            jid=rest or self.boundjid.domain)
        for feat in sorted(f.get("var") for f in info.xml.iter(f"{{{NS_DISCO}}}feature")):
            print(f"PEER-FEATURE {feat}", flush=True)

    async def act_streamfeatures(self, rest: str) -> None:
        for feat in sorted(self.features):
            print(f"PEER-STREAM {feat}", flush=True)

    async def act_join(self, rest: str) -> None:
        room, _, nick = rest.partition(" ")
        await self.plugin["xep_0045"].join_muc_wait(
            slixmpp.JID(room), nick, timeout=self.timeout)
        print(f"PEER joined {room}", flush=True)

    async def act_leave(self, rest: str) -> None:
        room = slixmpp.JID(rest)
        nick = self.plugin["xep_0045"].our_nicks.get(room, "peer")
        self.plugin["xep_0045"].leave_muc(room, nick)

    async def act_filesha(self, rest: str) -> None:
        digest = hashlib.sha256(Path(rest).read_bytes()).hexdigest()
        print(f"PEER-SHA256 {digest}", flush=True)

    async def act_sha256(self, rest: str) -> None:
        def get() -> str:
            with urllib.request.urlopen(rest, timeout=self.timeout) as resp:
                return hashlib.sha256(resp.read()).hexdigest()
        print(f"PEER-SHA256 {await asyncio.to_thread(get)}", flush=True)

    async def act_upload(self, rest: str) -> None:
        path, _, domain = rest.partition(" ")
        domain = domain or f"upload.{self.boundjid.domain}"
        data = Path(path).read_bytes()
        iq = self.make_iq_get(ito=domain)
        iq.xml.append(ET.fromstring(
            f"<request xmlns='{NS_UPLOAD}' filename='{Path(path).name}' "
            f"size='{len(data)}' content-type='application/octet-stream'/>"))
        reply = await iq.send(timeout=self.timeout)
        slot = reply.xml.find(f"{{{NS_UPLOAD}}}slot")
        put = slot.find(f"{{{NS_UPLOAD}}}put")
        headers = {h.get("name"): h.text or ""
                   for h in put.findall(f"{{{NS_UPLOAD}}}header")}

        def do_put() -> None:
            req = urllib.request.Request(
                put.get("url"), data=data, method="PUT", headers=headers)
            req.add_header("Content-Type", "application/octet-stream")
            urllib.request.urlopen(req, timeout=self.timeout).read()
        await asyncio.to_thread(do_put)
        print(f"PEER-URL {slot.find(f'{{{NS_UPLOAD}}}get').get('url')}", flush=True)

    async def act_abort(self, rest: str) -> None:
        self.transport.abort()


async def main(args: argparse.Namespace) -> int:
    jid = args.jid if "/" in args.jid else f"{args.jid}/{args.resource}"
    peer = Peer(jid, args.password, args.timeout)
    for ns in args.feature:
        peer["xep_0030"].add_feature(ns)
    started = asyncio.Event()

    async def on_start(_event: object) -> None:
        peer.send_presence()
        await peer.get_roster()
        started.set()

    peer.add_event_handler("session_start", on_start)
    peer.connect(host=args.host, port=args.port)
    actions = list(args.do)
    if args.script:
        actions += [ln.strip() for ln in Path(args.script).read_text().splitlines()
                    if ln.strip() and not ln.lstrip().startswith("#")]
    status = 0
    try:
        await asyncio.wait_for(started.wait(), args.timeout)
        print("PEER connected", flush=True)
        for action in actions:
            await peer.run_action(action)
        if args.stay:
            await asyncio.sleep(args.stay)
    except (asyncio.TimeoutError, ValueError, TypeError, AttributeError,
            OSError) as exc:
        print(f"PEER FAIL {type(exc).__name__} {exc}", flush=True)
        status = 1
    peer.disconnect()
    await asyncio.sleep(0.3)
    return status


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--jid", required=True)
    ap.add_argument("--resource", default="peer")
    ap.add_argument("--password", required=True)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=15222)
    ap.add_argument("--timeout", type=float, default=20.0)
    ap.add_argument("--stay", type=float, default=0.0)
    ap.add_argument("--script")
    ap.add_argument("--do", action="append", default=[], metavar="ACTION")
    ap.add_argument("--feature", action="append", default=[], metavar="NS",
                    help="advertise a disco/caps feature (repeatable)")
    logging.basicConfig(level=logging.ERROR)
    sys.exit(asyncio.run(main(ap.parse_args())))
