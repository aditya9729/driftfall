#!/usr/bin/env python3
# Copyright 2026 Aditya Gudal
# SPDX-License-Identifier: Apache-2.0
"""Serve a DRIFTFALL web build locally, with the headers the build requires.

The web build uses WebAssembly threads, which need SharedArrayBuffer, which
browsers only expose to a cross-origin-isolated page — meaning the server has
to send COOP and COEP headers. `python -m http.server` does not, so the page
loads and then fails at startup with a message about SharedArrayBuffer being
undefined. This is the same reason the web build cannot be hosted on GitHub
Pages: Pages cannot set response headers either.

It also serves .wasm as application/wasm. Without that, the browser refuses
the streaming compile path and falls back to a slower one, with a console
warning that looks alarming but is not fatal.

Usage:
    python3 web/serve.py [--dir build-web] [--port 8080]

Then open http://localhost:8080/index.html
"""

import argparse
import functools
import http.server
import pathlib
import socketserver
import sys


class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {
        **http.server.SimpleHTTPRequestHandler.extensions_map,
        ".wasm": "application/wasm",
        ".js": "text/javascript",
    }

    def end_headers(self):
        # The two headers that make the page cross-origin isolated, which is
        # what unlocks SharedArrayBuffer and therefore wasm threads.
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        # A dev server handing back a stale wasm after a rebuild is a very
        # confusing way to lose an hour.
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, fmt, *args):
        sys.stderr.write("  %s\n" % (fmt % args))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dir", default="build-web", help="directory to serve (default: build-web)")
    parser.add_argument("--port", type=int, default=8080, help="port to listen on (default: 8080)")
    args = parser.parse_args()

    root = pathlib.Path(args.dir).resolve()
    if not root.is_dir():
        sys.exit(f"{root} is not a directory — build the web target first, see README.md")
    if not (root / "index.html").is_file():
        sys.exit(f"no index.html in {root} — build the web target first, see README.md")

    handler = functools.partial(Handler, directory=str(root))

    # Without this, restarting the server after a crash spends a minute in
    # TIME_WAIT refusing to bind.
    socketserver.TCPServer.allow_reuse_address = True

    with socketserver.TCPServer(("0.0.0.0", args.port), handler) as httpd:
        print(f"DRIFTFALL: serving {root}")
        print(f"           http://localhost:{args.port}/index.html")
        print("           cross-origin isolated (COOP/COEP set), ctrl-c to stop")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print()


if __name__ == "__main__":
    main()
