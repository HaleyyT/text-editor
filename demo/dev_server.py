#!/usr/bin/env python3
import http.client
import json
import os
from http import HTTPStatus
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse


ROOT = Path(__file__).resolve().parent.parent
STATIC_DIR = ROOT / "demo" / "static"
DEV_PORT = int(os.environ.get("DEMO_DEV_PORT", "8001"))
API_PORT = int(os.environ.get("DEMO_API_PORT", "8000"))


def latest_static_version():
    latest = 0
    for path in STATIC_DIR.rglob("*"):
        if path.is_file():
            latest = max(latest, int(path.stat().st_mtime_ns))
    return latest


def dev_reload_snippet():
    return """
<script>
(() => {
  let currentVersion = null;

  async function poll() {
    try {
      const response = await fetch('/__dev__/version', { cache: 'no-store' });
      const data = await response.json();
      if (currentVersion === null) {
        currentVersion = data.version;
      } else if (currentVersion !== data.version) {
        window.location.reload();
        return;
      }
    } catch (error) {
      console.warn('dev reload poll failed', error);
    }

    window.setTimeout(poll, 700);
  }

  poll();
})();
</script>
"""


class DevHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(STATIC_DIR), **kwargs)

    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path == "/__dev__/version":
            return self.respond_json({"version": latest_static_version()})
        if parsed.path.startswith("/api/"):
            return self.proxy_api()
        if parsed.path in {"/", "/index.html"}:
            return self.serve_index()
        return super().do_GET()

    def do_POST(self):
        parsed = urlparse(self.path)
        if parsed.path.startswith("/api/"):
            return self.proxy_api()
        self.send_error(HTTPStatus.NOT_FOUND)

    def serve_index(self):
        index_path = STATIC_DIR / "index.html"
        html = index_path.read_text(encoding="utf-8")
        html = html.replace("</body>", dev_reload_snippet() + "\n  </body>")
        encoded = html.encode("utf-8")
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)

    def proxy_api(self):
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length) if length else None

        try:
            connection = http.client.HTTPConnection("127.0.0.1", API_PORT, timeout=5)
            headers = {}
            if self.headers.get("Content-Type"):
                headers["Content-Type"] = self.headers["Content-Type"]
            connection.request(self.command, self.path, body=body, headers=headers)
            response = connection.getresponse()
            payload = response.read()
        except OSError:
            message = {
                "ok": False,
                "error": f"Demo API is not reachable on http://127.0.0.1:{API_PORT}. Start `make demo-ui` in another terminal.",
            }
            return self.respond_json(message, status=HTTPStatus.BAD_GATEWAY)

        self.send_response(response.status)
        self.send_header("Content-Type", response.getheader("Content-Type", "application/json"))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def respond_json(self, payload, status=HTTPStatus.OK):
        encoded = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, format, *args):
        return


def main():
    server = ThreadingHTTPServer(("127.0.0.1", DEV_PORT), DevHandler)
    print(f"Frontend dev server: http://127.0.0.1:{DEV_PORT}")
    print(f"Proxying /api requests to: http://127.0.0.1:{API_PORT}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    raise SystemExit(main())
