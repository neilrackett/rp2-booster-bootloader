#!/usr/bin/env python3
"""
dnsredirect - a DNS server that sends chosen names to this machine.

Single-file, stdlib-only (Python >= 3.10). Answers an A query for each name
given with --name with this machine's address (or --address), and forwards
every other query, unchanged, to the real resolver (--upstream, by default
this machine's own, from /etc/resolv.conf). Point the device's DNS at this machine and it reaches
testserver.py under a real host name, with the URLs compiled into a
released firmware: an over-the-air upgrade can then be tested end to end
from any published version, without a special build.

Usage:
    python3 tools/dev/dnsredirect.py --name atarist.sidecartridge.com
    python3 tools/dev/dnsredirect.py --name a.example --name b.example \\
        --address 192.168.1.41 --upstream 192.168.1.1 --port 53

macOS lets any user bind port 53. Each query is printed, with what it got,
so the log shows when the device asked for the redirected name.
See tools/dev/README.md, "Testing a firmware upgrade end to end".
"""

from __future__ import annotations

import argparse
import socket
import struct
import sys


def system_resolver() -> str:
    """This machine's first IPv4 resolver, as the system uses it."""
    try:
        with open("/etc/resolv.conf") as conf:
            for line in conf:
                parts = line.split()
                if len(parts) == 2 and parts[0] == "nameserver" and \
                        parts[1].count(".") == 3:
                    return parts[1]
    except OSError:
        pass
    return "8.8.8.8"


def local_address(towards: str) -> str:
    """This machine's address on the interface that reaches `towards`."""
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        probe.connect((towards, 53))
        return probe.getsockname()[0]
    finally:
        probe.close()


def question(packet: bytes) -> tuple[str, int, int]:
    """The first question's name, type, and the offset just past it."""
    labels, i = [], 12
    while packet[i] != 0:
        length = packet[i]
        labels.append(packet[i + 1:i + 1 + length].decode("ascii", "replace"))
        i += 1 + length
    qtype = struct.unpack(">H", packet[i + 1:i + 3])[0]
    return ".".join(labels).lower(), qtype, i + 5


def answer(packet: bytes, end: int, address: str) -> bytes:
    """A reply to the query in `packet` with one A record for `address`."""
    header = packet[:2] + b"\x81\x80" + packet[4:6] + b"\x00\x01\x00\x00\x00\x00"
    record = (b"\xc0\x0c" + struct.pack(">HHIH", 1, 1, 60, 4) +
              socket.inet_aton(address))
    return header + packet[12:end] + record


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    ap.add_argument("--name", action="append", required=True,
                    help="host name to send here; repeat for more")
    ap.add_argument("--address", help="address to answer with "
                    "(default: this machine's)")
    ap.add_argument("--upstream", help="resolver for every other name "
                    "(default: this machine's, from /etc/resolv.conf)")
    ap.add_argument("--port", type=int, default=53)
    args = ap.parse_args()
    upstream = args.upstream or system_resolver()
    address = args.address or local_address(upstream)
    names = {n.lower().rstrip(".") for n in args.name}

    server = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    server.bind(("0.0.0.0", args.port))
    print(f"DNS on port {args.port}: {', '.join(sorted(names))} -> {address}; "
          f"everything else -> {upstream}", flush=True)
    while True:
        packet, client = server.recvfrom(512)
        try:
            name, qtype, end = question(packet)
        except (IndexError, struct.error):
            continue
        if name in names and qtype == 1:
            server.sendto(answer(packet, end, address), client)
            print(f"{client[0]}: {name} -> {address} (redirected)", flush=True)
            continue
        relay = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        relay.settimeout(3)
        try:
            relay.sendto(packet, (upstream, 53))
            reply, _ = relay.recvfrom(4096)
            server.sendto(reply, client)
            print(f"{client[0]}: {name} (forwarded)", flush=True)
        except OSError as exc:
            print(f"{client[0]}: {name} not forwarded: {exc}", flush=True)
        finally:
            relay.close()


if __name__ == "__main__":
    sys.exit(main())
