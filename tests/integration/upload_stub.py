#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 purple-xmpp2 contributors
"""HTTP CONNECT proxy that fakes the upload server, for upload tests.

The harness (--proxy 127.0.0.1:PORT) sends everything through it.  CONNECT
requests to any other port are tunnelled to the real target (the XMPP
connection, the Prosody HTTP port of a download); CONNECT requests to
--upload-port are answered by this script as a misbehaving or slow HTTP
server (the PUT of an upload slot), selected by --mode:

  status:CODE   read the whole PUT, then answer CODE (500, 302, 201, ...)
  early:CODE    answer CODE right after the request head and close, while
                the client is still sending (a server that rejects at once)
  slow:BPS      read the body at BPS bytes per second (backpressure), then
                answer 201

Prints "PORT n" once listening, and "STUB put bytes=N of=M result=..." for
each PUT at its end (result is the status sent, or "closed" when the client
went away first).  Stdlib only.
"""
import argparse
import asyncio
import sys


async def read_head(reader):
    data = await reader.readuntil(b"\r\n\r\n")
    return data.decode("latin-1")


async def pipe(src, dst):
    try:
        while True:
            chunk = await src.read(65536)
            if not chunk:
                break
            dst.write(chunk)
            await dst.drain()
    except (ConnectionError, asyncio.CancelledError):
        pass
    finally:
        try:
            dst.close()
        except Exception:
            pass


async def serve_put(reader, writer, mode):
    head = await read_head(reader)
    length = 0
    for line in head.split("\r\n")[1:]:
        name, _, value = line.partition(":")
        if name.strip().lower() == "content-length":
            length = int(value.strip())
    kind, _, arg = mode.partition(":")
    got = 0
    result = "closed"

    def answer(code):
        extra = "Location: http://localhost/elsewhere\r\n" if code // 100 == 3 else ""
        writer.write(("HTTP/1.1 %d X\r\n%sContent-Length: 0\r\n"
                      "Connection: close\r\n\r\n" % (code, extra)).encode())

    try:
        if kind == "early":
            answer(int(arg))
            await writer.drain()
            result = arg
            # let it sit a moment so the client is mid-send, then close
            await asyncio.sleep(0.2)
            return
        while got < length:
            chunk = await reader.read(min(65536, length - got))
            if not chunk:
                return
            got += len(chunk)
            if kind == "slow":
                await asyncio.sleep(len(chunk) / float(arg))
        code = int(arg) if kind == "status" else 201
        answer(code)
        await writer.drain()
        result = str(code)
    except (ConnectionError, asyncio.IncompleteReadError):
        pass
    finally:
        print("STUB put bytes=%d of=%d result=%s" % (got, length, result), flush=True)
        writer.close()


async def handle(reader, writer, upload_port, mode):
    try:
        head = await read_head(reader)
        parts = head.split(" ")
        if parts[0] != "CONNECT":
            writer.close()
            return
        host, _, port = parts[1].rpartition(":")
        writer.write(b"HTTP/1.1 200 Connection established\r\n\r\n")
        await writer.drain()
        if int(port) == upload_port:
            await serve_put(reader, writer, mode)
            return
        up_reader, up_writer = await asyncio.open_connection(host, int(port))
        await asyncio.gather(pipe(reader, up_writer), pipe(up_reader, writer))
    except (ConnectionError, asyncio.IncompleteReadError):
        writer.close()


async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--upload-port", type=int, required=True)
    ap.add_argument("--mode", required=True)
    args = ap.parse_args()
    server = await asyncio.start_server(
        lambda r, w: handle(r, w, args.upload_port, args.mode),
        "127.0.0.1", 0)
    print("PORT %d" % server.sockets[0].getsockname()[1], flush=True)
    async with server:
        await server.serve_forever()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        sys.exit(0)
