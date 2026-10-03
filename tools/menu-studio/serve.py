#!/usr/bin/env python3
"""Serve Menu Studio locally, exposing only editor files and curated branding."""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import mimetypes
from pathlib import Path
from urllib.parse import unquote, urlsplit

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
ALLOWED = {"index.html", "studio.css", "app.js", "model.js", "templates.js",
           "render.js", "storage.js", "catalog.json"}
ASSETS = {
    "/repo-assets/launcher-icon.png": HERE / "assets/launcher-icon.png",
    "/repo-assets/icon.png": ROOT / "services/lobbies/public/favicon-64.png",
    "/repo-assets/banner.png": ROOT / "docs/banner.png",
    "/repo-assets/native-ui.ttf": HERE / "assets/native-ui.ttf",
}


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        path = unquote(urlsplit(self.path).path)
        name = path.lstrip("/") or "index.html"
        if path in ASSETS:
            file = ASSETS[path]
        elif name in ALLOWED:
            file = HERE / name
        else:
            self.send_error(404)
            return
        if not file.is_file():
            self.send_error(404)
            return
        content = file.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", mimetypes.guess_type(file.name)[0] or "application/octet-stream")
        self.send_header("Content-Length", str(len(content)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Content-Security-Policy", "default-src 'self'; img-src 'self' data: blob:; font-src 'self' data:; style-src 'self' 'unsafe-inline'; script-src 'self'; connect-src 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'none'")
        self.end_headers()
        self.wfile.write(content)

    def do_POST(self):
        self.send_error(405, "This tool has no upload endpoint.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8800)
    parser.add_argument("--host", default="127.0.0.1")
    args = parser.parse_args()
    server = ThreadingHTTPServer((args.host, args.port), Handler)
    print(f"Menu Studio listening on {args.host}:{args.port}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        server.server_close()
