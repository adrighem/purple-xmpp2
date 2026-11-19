#!/usr/bin/env python3
"""Scripted XMPP peer client (slixmpp) used as the second party in tests.

Usage: peer.py --jid bob@localhost --password secret --port 15222 \
        --do "send alice@localhost hello" --do "wait hi back" ...

Actions (executed in order, after the session starts):
  send JID TEXT        send a chat message
  wait TEXT            wait until a message body containing TEXT arrives
  sleep SECONDS        pause
  mam [WITH_JID]       query the own MAM archive, print "PEER-MAM <from> <body>"
  disco [JID]          print the disco#info features of JID (default: server)
  streamfeatures       print negotiated stream features (e.g. stream_management)
  join ROOM NICK       join a MUC and wait for the self presence
  groupsend ROOM TEXT  send a groupchat message
  groupwait TEXT       wait for a groupchat message containing TEXT

Exit status 0 when every action succeeded, 1 otherwise. Output lines are
prefixed with "PEER" so test scripts can grep them.
"""
import argparse
import asyncio
import logging
import ssl
import sys

import slixmpp


class Peer(slixmpp.ClientXMPP):
    def __init__(self, jid: str, password: str, timeout: float) -> None:
        super().__init__(jid, password)
        self.timeout = timeout
        self.inbox: asyncio.Queue[tuple[str, str, str]] = asyncio.Queue()
        self.ok = True
        self.register_plugin("xep_0030")
        self.register_plugin("xep_0045")
        self.register_plugin("xep_0199")
        self.register_plugin("xep_0313")
        self.register_plugin("xep_0280")
        self.register_plugin("xep_0198")
        self.enable_direct_tls = False
        self.enable_starttls = True
        self.ssl_context.check_hostname = False
        self.ssl_context.verify_mode = ssl.CERT_NONE
        self.add_event_handler("message", self._on_message)

    def _on_message(self, msg: slixmpp.Message) -> None:
        if msg["type"] in ("chat", "normal", "groupchat") and msg["body"]:
            self.inbox.put_nowait(
                (msg["type"], str(msg["from"]), msg["body"]))
            print(f"PEER recv {msg['type']} {msg['from']}: {msg['body']}",
                  flush=True)

    async def wait_for(self, text: str, kinds: tuple[str, ...]) -> None:
        async def loop() -> None:
            while True:
                kind, _frm, body = await self.inbox.get()
                if kind in kinds and text in body:
                    return
        await asyncio.wait_for(loop(), self.timeout)

    async def run_action(self, line: str) -> None:
        cmd, _, rest = line.partition(" ")
        if cmd == "send":
            to, _, text = rest.partition(" ")
            self.send_message(mto=to, mbody=text, mtype="chat")
            print(f"PEER sent {to}: {text}", flush=True)
        elif cmd == "wait":
            await self.wait_for(rest, ("chat", "normal"))
            print(f"PEER matched {rest}", flush=True)
        elif cmd == "sleep":
            await asyncio.sleep(float(rest))
        elif cmd == "mam":
            kw = {"with_jid": slixmpp.JID(rest)} if rest else {}
            res = await self.plugin["xep_0313"].retrieve(**kw)
            for item in res["mam"]["results"]:
                fwd = item["mam_result"]["forwarded"]["stanza"]
                print(f"PEER-MAM {fwd['from']} {fwd['body']}", flush=True)
        elif cmd == "disco":
            info = await self.plugin["xep_0030"].get_info(jid=rest or None)
            ns = "{http://jabber.org/protocol/disco#info}feature"
            for feat in sorted(f.get("var") for f in info.xml.iter(ns)):
                print(f"PEER-FEATURE {feat}", flush=True)
        elif cmd == "streamfeatures":
            for feat in sorted(self.features):
                print(f"PEER-STREAM {feat}", flush=True)
        elif cmd == "join":
            room, _, nick = rest.partition(" ")
            await self.plugin["xep_0045"].join_muc_wait(
                slixmpp.JID(room), nick, timeout=self.timeout)
            print(f"PEER joined {room}", flush=True)
        elif cmd == "groupsend":
            room, _, text = rest.partition(" ")
            self.send_message(mto=room, mbody=text, mtype="groupchat")
        elif cmd == "groupwait":
            await self.wait_for(rest, ("groupchat",))
            print(f"PEER matched {rest}", flush=True)
        else:
            raise ValueError(f"unknown action: {line}")


async def main(args: argparse.Namespace) -> int:
    peer = Peer(args.jid, args.password, args.timeout)
    started = asyncio.Event()

    async def on_start(_event: object) -> None:
        peer.send_presence()
        await peer.get_roster()
        started.set()

    peer.add_event_handler("session_start", on_start)
    peer.add_event_handler(
        "failed_all_auth", lambda _e: (print("PEER auth failed"), sys.exit(1)))
    peer.connect(host=args.host, port=args.port)
    try:
        await asyncio.wait_for(started.wait(), args.timeout)
        print("PEER connected", flush=True)
        for action in args.do:
            await peer.run_action(action)
        status = 0
    except (asyncio.TimeoutError, ValueError) as exc:
        print(f"PEER FAIL {type(exc).__name__} {exc}", flush=True)
        status = 1
    peer.disconnect()
    await asyncio.sleep(0.3)
    return status


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--jid", required=True)
    ap.add_argument("--password", required=True)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=15222)
    ap.add_argument("--timeout", type=float, default=20.0)
    ap.add_argument("--do", action="append", default=[], metavar="ACTION")
    logging.basicConfig(level=logging.ERROR)
    sys.exit(asyncio.run(main(ap.parse_args())))
