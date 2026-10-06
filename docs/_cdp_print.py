#!/usr/bin/env python3
"""
Renders one page to PDF via the Chrome DevTools Protocol (CDP), talking to
an already-running headless Chrome/headless-shell instance over its remote
debugging HTTP+WebSocket endpoint.

Used by build-pdf.sh as the Docker fallback when no local browser/PDF tool
is installed. Not meant to be run by hand, but it's a plain script: adjust
if you need a different CDP call (e.g. paperWidth/paperHeight for a
non-default page size).

Usage: _cdp_print.py <devtools-http-base> <page-url> <output.pdf>
  e.g.  _cdp_print.py http://127.0.0.1:9222 file:///docs/index.html out.pdf
"""
import sys
import json
import base64
import os
import socket
import struct
from urllib.parse import urlparse

import requests


def _recv_exact(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("websocket closed unexpectedly")
        buf += chunk
    return buf


def ws_handshake(host, port, path):
    key = base64.b64encode(os.urandom(16)).decode()
    req = (
        "GET {path} HTTP/1.1\r\n"
        "Host: {host}:{port}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: {key}\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n"
    ).format(path=path, host=host, port=port, key=key)
    sock = socket.create_connection((host, port), timeout=10)
    sock.sendall(req.encode())
    buf = b""
    while b"\r\n\r\n" not in buf:
        buf += sock.recv(4096)
    if b"101" not in buf.split(b"\r\n", 1)[0]:
        raise RuntimeError("websocket handshake failed: " + buf.decode(errors="replace"))
    return sock


def ws_send_text(sock, text):
    payload = text.encode()
    header = bytearray([0x81])  # FIN + text opcode
    length = len(payload)
    if length <= 125:
        header.append(0x80 | length)
    elif length <= 0xFFFF:
        header.append(0x80 | 126)
        header += struct.pack(">H", length)
    else:
        header.append(0x80 | 127)
        header += struct.pack(">Q", length)
    mask_key = os.urandom(4)
    masked = bytearray(payload)
    for i in range(len(masked)):
        masked[i] ^= mask_key[i % 4]
    sock.sendall(bytes(header) + mask_key + bytes(masked))


def ws_recv_message(sock, timeout=60):
    sock.settimeout(timeout)
    message = bytearray()
    while True:
        b0, b1 = _recv_exact(sock, 2)
        fin = b0 & 0x80
        op = b0 & 0x0F
        masked = b1 & 0x80
        length = b1 & 0x7F
        if length == 126:
            length = struct.unpack(">H", _recv_exact(sock, 2))[0]
        elif length == 127:
            length = struct.unpack(">Q", _recv_exact(sock, 8))[0]
        mask_key = _recv_exact(sock, 4) if masked else None
        payload = _recv_exact(sock, length)
        if mask_key:
            payload = bytes(b ^ mask_key[i % 4] for i, b in enumerate(payload))
        if op == 0x8:  # close frame
            return None
        if op in (0x0, 0x1, 0x2):
            message += payload
        if fin:
            return bytes(message)


def main():
    if len(sys.argv) != 4:
        print(__doc__, file=sys.stderr)
        return 1

    devtools_base, page_url, out_pdf = sys.argv[1], sys.argv[2], sys.argv[3]

    r = requests.get("{}/json/new?{}".format(devtools_base, page_url), timeout=15)
    r.raise_for_status()
    info = r.json()
    ws_url = info["webSocketDebuggerUrl"]
    target_id = info["id"]

    u = urlparse(ws_url)
    sock = ws_handshake(u.hostname, u.port, u.path)

    try:
        ws_send_text(sock, json.dumps({
            "id": 1,
            "method": "Page.printToPDF",
            "params": {"printBackground": True},
        }))

        while True:
            raw = ws_recv_message(sock)
            if raw is None:
                raise RuntimeError("websocket closed before a response arrived")
            msg = json.loads(raw.decode())
            if msg.get("id") == 1:
                break

        if "error" in msg:
            raise RuntimeError("CDP error: {}".format(msg["error"]))

        pdf_bytes = base64.b64decode(msg["result"]["data"])
        with open(out_pdf, "wb") as f:
            f.write(pdf_bytes)
    finally:
        sock.close()
        try:
            requests.get("{}/json/close/{}".format(devtools_base, target_id), timeout=10)
        except requests.RequestException:
            pass

    print("wrote {} ({} bytes)".format(out_pdf, os.path.getsize(out_pdf)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
