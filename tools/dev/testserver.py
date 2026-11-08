#!/usr/bin/env python3
"""A catalog and download server for Booster, with every failure it must
survive, over HTTP and HTTPS.

    python3 tools/dev/testserver.py [--root DIR] [--http-port 80]
                                    [--https-port 443] [--ecdsa-port 8443]

Run it on a machine in the same LAN as the SidecarTridge and enter one of its
catalogs as the custom catalog URL on Booster's Config page, for example
http://<this machine>/apps.json. Default ports are 80 and 443 (macOS lets any
user bind them).

Files under --root (default tools/dev/builds/testserver) are served by name.
Put real microfirmwares there as <uuid>-<version>.uf2 and they appear in
apps.json. Put Booster's own SIDECARTVERSION, upgrade.bin and upgrade.md5 there
to serve a firmware update (the device fetches them from the URL compiled into
it, so the update test also needs DNS pointing that host here).

Generated catalogs:
  apps.json            every .uf2 under --root, with its MD5
  failures/apps.json   one app per failure route below, a wrong MD5, and
                       pattern.uf2 as the control: what the device does with
                       each download
  many-N/apps.json     N apps with distinct UUIDs (N up to 200), for the
                       config-sector and catalog-size limits. Each binary is
                       placeholder.uf2 from --root when there is one, a
                       firmware that hands straight back to Booster and is
                       safe to launch; otherwise pattern.uf2, which is not a
                       firmware: install those, never launch them

Generated routes, for any name:
  pattern.uf2          64 KB of a fixed pattern (not a firmware)
  fail-404*            404 with an HTML body
  fail-500*            500
  fail-html200*        200 with an HTML error page (a proxy's or captive
                       portal's), which must not become an app or a version
  fail-truncated*      a Content-Length the body never reaches, then close
  fail-stall*          headers, a little of the body, then nothing
  fail-loop*           a redirect to itself
  *slow-*.uf2          the pattern in 4 KB pieces, one a second: a download
                       that lasts 16 s and completes
  *slow-long-*.uf2     the same, one piece every 10 s: 160 s, long enough to
                       pull the SD card by hand in the middle of it
  302, 301, rel, cd, 302cd, tohttps   the redirect cases of md-browser's
                       server, ending at file.bin (64 KB, printed MD5)

HTTPS uses a self-signed certificate made with openssl on first run, in
tools/dev/builds/testserver-cert/: Booster encrypts but does not verify.
Port 443 has an RSA 2048 certificate, --ecdsa-port an ECDSA P-256 one (the
other handshake, https://<this machine>:8443/...); 0 turns that one off.
Every request is logged to stdout and to tools/dev/logs/testserver.log, so a
test can check what the device asked for.

Adapted from md-rom-emulator's tools/dev/testserver.py, itself an extension
of md-browser's redirect and TLS test server: the routes and the TLS set-up
are theirs, the catalogs are Booster's JSON format.
"""

import argparse
import datetime
import hashlib
import json
import os
import re
import ssl
import subprocess
import sys
import threading
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
PAYLOAD = bytes(range(256)) * 256          # file.bin: 64 KB, deterministic
TARGET = b"relative-redirect-target-payload\n" * 64
PATTERN = bytes((i * 7 + (i >> 8)) & 0xFF for i in range(64 * 1024))
CERT_DIR = os.path.join(HERE, "builds", "testserver-cert")
LOG = os.path.join(HERE, "logs", "testserver.log")
PORTS = {"http": 80, "https": 443}
CERT_KEYS = {"rsa": ["-newkey", "rsa:2048"],
             "ecdsa": ["-newkey", "ec", "-pkeyopt",
                       "ec_paramgen_curve:prime256v1"]}
ROOT = os.path.join(HERE, "builds", "testserver")
LOG_LOCK = threading.Lock()
MANY_MAX = 200
UUID_RE = re.compile(r"^([0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-"
                     r"[0-9a-f]{12})(?:-v?([0-9][\w.]*))?\.uf2$")

FAILURES = ["fail-404.uf2", "fail-500.uf2", "fail-html200.uf2",
            "fail-truncated.uf2", "fail-stall.uf2", "fail-loop.uf2",
            "302", "301", "rel", "cd", "302cd", "tohttps", "slow-pattern.uf2",
            "slow-long-pattern.uf2"]


def md5(data: bytes) -> str:
    return hashlib.md5(data).hexdigest()


def test_uuid(seed: str) -> str:
    """A version 4 UUID, the same for the same seed."""
    h = hashlib.sha1(seed.encode()).hexdigest()
    return f"{h[:8]}-{h[8:12]}-4{h[13:16]}-{'89ab'[int(h[16], 16) % 4]}{h[17:20]}-{h[20:32]}"


def app(uuid: str, name: str, description: str, binary: str, checksum: str,
        version: str = "0.0.1test") -> dict:
    return {"uuid": uuid, "name": name, "description": description,
            "image": "https://placehold.co/256x256/png", "tags": ["Test"],
            "devices": ["ST", "STE", "MegaST", "MegaSTE"], "binary": binary,
            "md5": checksum, "version": version, "previous_versions": [],
            "official": False, "creator": "testserver"}


def catalog(apps: list[dict]) -> bytes:
    return json.dumps({"creators": [{"id": "testserver",
                                     "name": "Booster test server"}],
                       "apps": apps}, indent=1).encode()


def root_file(name: str) -> bytes | None:
    path = os.path.realpath(os.path.join(ROOT, name))
    if os.path.dirname(path) == os.path.realpath(ROOT) and os.path.isfile(path):
        with open(path, "rb") as f:
            return f.read()
    return None


def route_body(name: str) -> bytes:
    """What a route serves when it completes, for its catalog MD5."""
    if name in ("rel", "cd", "302cd"):
        return TARGET
    if name in ("302", "301", "tohttps"):
        return PAYLOAD
    return PATTERN


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def scheme(self):
        return "https" if isinstance(self.connection, ssl.SSLSocket) else "http"

    def base(self) -> str:
        # lwIP's client sends Host without the port: absolute URLs are
        # rebuilt from the ports this server listens on.
        host = self.headers.get("Host", "localhost").split(":")[0]
        return f"{self.scheme()}://{host}:{PORTS[self.scheme()]}"

    def send(self, code, body=b"", ctype="application/octet-stream",
             length=None, extra=()):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length",
                         str(len(body) if length is None else length))
        for name, value in extra:
            self.send_header(name, value)
        self.end_headers()
        self.wfile.write(body)

    def redirect(self, code, location):
        self.send(code, extra=[("Location", location)])

    def apps_catalog(self) -> bytes:
        apps = []
        for name in sorted(os.listdir(ROOT)):
            m = UUID_RE.match(name.lower())
            body = root_file(name) if name.endswith(".uf2") else None
            if body is None:
                continue
            uuid = m.group(1) if m else test_uuid(name)
            version = (m.group(2) if m and m.group(2) else "0.0.1test")
            apps.append(app(uuid, name[:-4], f"{name} from the test server.",
                            f"{self.base()}/{urllib.parse.quote(name)}",
                            md5(body), version))
        return catalog(apps)

    def failures_catalog(self) -> bytes:
        apps = [app(test_uuid(n), f"Route {n}", "A failure or redirect route.",
                    f"{self.base()}/{n}", md5(route_body(n)))
                for n in FAILURES]
        apps.append(app(test_uuid("bad-md5"), "Wrong MD5",
                        "pattern.uf2 with a catalog MD5 that does not match.",
                        f"{self.base()}/pattern.uf2", "0" * 32))
        apps.append(app(test_uuid("control"), "pattern (control)",
                        "Served whole, MD5 matches. Not a firmware.",
                        f"{self.base()}/pattern.uf2", md5(PATTERN)))
        return catalog(apps)

    def many_catalog(self, count: int) -> bytes:
        placeholder = root_file("placeholder.uf2")
        if placeholder is not None:
            binary, checksum, note = ("placeholder.uf2", md5(placeholder),
                                      "The placeholder: safe to launch.")
        else:
            binary, checksum, note = ("pattern.uf2", md5(PATTERN),
                                      "Not a firmware: do not launch.")
        return catalog([app(test_uuid(f"many-{i}"), f"Test app {i:03d}", note,
                            f"{self.base()}/{binary}", checksum)
                        for i in range(1, count + 1)])

    def do_GET(self):
        name = urllib.parse.unquote(self.path.split("?")[0].lstrip("/"))
        html = b"<html><body><h1>Something went wrong</h1></body></html>"
        many = re.fullmatch(r"many-(\d+)/apps\.json", name)
        if name.startswith("fail-404"):
            self.send(404, html, "text/html")
        elif name.startswith("fail-500"):
            self.send(500, html, "text/html")
        elif name.startswith("fail-html200"):
            self.send(200, html, "text/html")
        elif name.startswith("fail-truncated"):
            self.send(200, PATTERN[:10000], length=len(PATTERN))
            self.close_connection = True
        elif name.startswith("fail-stall"):
            self.send_response(200)
            self.send_header("Content-Length", str(len(PATTERN)))
            self.end_headers()
            self.wfile.write(PATTERN[:1000])
            self.wfile.flush()
            time.sleep(600)
        elif name.startswith("fail-loop"):
            self.redirect(302, "/" + name)
        elif "slow-" in name and name.endswith(".uf2"):
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(PATTERN)))
            self.end_headers()
            pause = 10 if "slow-long-" in name else 1
            for start in range(0, len(PATTERN), 4096):
                self.wfile.write(PATTERN[start:start + 4096])
                self.wfile.flush()
                time.sleep(pause)
        elif name == "pattern.uf2":
            self.send(200, PATTERN)
        elif name == "file.bin":
            self.send(200, PAYLOAD)
        elif name == "302":
            self.redirect(302, "/file.bin")
        elif name == "301":
            self.redirect(301, f"{self.base()}/file.bin")
        elif name == "rel":
            self.redirect(302, "sub/target.bin")
        elif name == "sub/target.bin":
            self.send(200, TARGET)
        elif name == "cd":
            self.send(200, TARGET, extra=[("Content-Disposition",
                                           'attachment; filename="REALNAME.UF2"')])
        elif name == "302cd":
            self.redirect(302, "/cd")
        elif name == "tohttps":
            host = self.headers.get("Host", "localhost").split(":")[0]
            self.redirect(302, f"https://{host}:{PORTS['https']}/file.bin")
        elif name == "apps.json":
            self.send(200, self.apps_catalog(), "application/json")
        elif name == "failures/apps.json":
            self.send(200, self.failures_catalog(), "application/json")
        elif many and 1 <= int(many.group(1)) <= MANY_MAX:
            self.send(200, self.many_catalog(int(many.group(1))),
                      "application/json")
        else:
            body = root_file(name)
            if body is None:
                self.send(404, html, "text/html")
            else:
                ctype = ("application/json" if name.endswith(".json") else
                         "text/plain" if name in ("SIDECARTVERSION", "upgrade.md5")
                         else "application/octet-stream")
                self.send(200, body, ctype)

    def log_message(self, fmt, *args):
        line = (f"{datetime.datetime.now():%Y-%m-%d %H:%M:%S.%f}"[:23] +
                f" [{self.scheme()}] {self.address_string()} {fmt % args}")
        print(line, flush=True)
        with LOG_LOCK, open(LOG, "a") as f:
            f.write(line + "\n")


def ensure_cert(kind: str = "rsa") -> tuple[str, str]:
    os.makedirs(CERT_DIR, exist_ok=True)
    suffix = "" if kind == "rsa" else f"-{kind}"
    cert, key = (os.path.join(CERT_DIR, n + suffix + ".pem")
                 for n in ("cert", "key"))
    if not (os.path.exists(cert) and os.path.exists(key)):
        print(f"generating a self-signed {kind} certificate (openssl)...")
        subprocess.run(["openssl", "req", "-x509", *CERT_KEYS[kind],
                        "-keyout", key, "-out", cert, "-days", "3650",
                        "-nodes", "-subj", "/CN=testserver"],
                       check=True, capture_output=True)
    return cert, key


def tls_server(port: int, kind: str) -> ThreadingHTTPServer:
    server = ThreadingHTTPServer(("0.0.0.0", port), Handler)
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(*ensure_cert(kind))
    server.socket = ctx.wrap_socket(server.socket, server_side=True)
    return server


def main() -> int:
    global ROOT
    ap = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    ap.add_argument("--root", default=ROOT)
    ap.add_argument("--http-port", type=int, default=PORTS["http"])
    ap.add_argument("--https-port", type=int, default=PORTS["https"])
    ap.add_argument("--ecdsa-port", type=int, default=8443)
    args = ap.parse_args()
    ROOT = os.path.abspath(args.root)
    PORTS.update(http=args.http_port, https=args.https_port)
    os.makedirs(ROOT, exist_ok=True)
    os.makedirs(os.path.dirname(LOG), exist_ok=True)
    print(f"file.bin md5 {md5(PAYLOAD)} (65536 bytes); pattern.uf2 md5 "
          f"{md5(PATTERN)}; serving {ROOT}")
    httpd = ThreadingHTTPServer(("0.0.0.0", args.http_port), Handler)
    httpsd = tls_server(args.https_port, "rsa")
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    if args.ecdsa_port:
        ecdsad = tls_server(args.ecdsa_port, "ecdsa")
        threading.Thread(target=ecdsad.serve_forever, daemon=True).start()
    print(f"http on port {args.http_port}, https on port {args.https_port}"
          + (f", https (ECDSA) on port {args.ecdsa_port}"
             if args.ecdsa_port else ""), flush=True)
    httpsd.serve_forever()
    return 0


if __name__ == "__main__":
    sys.exit(main())
