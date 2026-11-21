"""Local Web editor and compiler-backed language endpoints."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
import threading
from pathlib import Path
from urllib.parse import urlsplit

from compiler import ROOT
from language_service import LanguageService, MAX_SOURCE_BYTES

ASSETS = {"/": ("index.html", "text/html; charset=utf-8"),
          "/app.js": ("app.js", "text/javascript; charset=utf-8"),
          "/runner.js": ("runner.js", "text/javascript; charset=utf-8"),
          "/style.css": ("style.css", "text/css; charset=utf-8"),
          "/favicon.svg": ("favicon.svg", "image/svg+xml")}
EXAMPLES = [("vectors", "多次元 vector と break", "example_vectors.fken"),
            ("graph", "構造体と重み付きグラフ", "example_graph.fken"),
            ("functions", "関数と再帰", "example_functions.fken"),
            ("sum", "入力と配列", "example_sum.fken"),
            ("bigint", "整数の自動昇格", "example_bigint.fken")]


class BoundedHTTPServer(ThreadingHTTPServer):
    daemon_threads = True
    request_queue_size = 32

    def __init__(self, *args, **kwargs):
        self.slots = threading.BoundedSemaphore(16)
        super().__init__(*args, **kwargs)

    def process_request(self, request, address):
        if not self.slots.acquire(blocking=False):
            try:
                request.sendall(b"HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
            except OSError:
                pass
            self.shutdown_request(request)
            return
        try:
            super().process_request(request, address)
        except BaseException:
            self.slots.release()
            raise

    def process_request_thread(self, request, address):
        try:
            super().process_request_thread(request, address)
        finally:
            self.slots.release()


def make_server(port=8765, service=None, host="127.0.0.1", allowed_hosts=None):
    language = service or LanguageService()
    configured = os.environ.get("FKEN_ALLOWED_HOSTS", "") if allowed_hosts is None else allowed_hosts
    if isinstance(configured, str):
        configured = configured.split(",")
    configured = {name.strip().lower() for name in configured if name.strip()}

    class Handler(BaseHTTPRequestHandler):
        def setup(self):
            super().setup()
            self.connection.settimeout(5)

        def handle(self):
            try:
                super().handle()
            except (BrokenPipeError, ConnectionResetError, TimeoutError):
                pass

        def send_bytes(self, code, content, content_type="application/json; charset=utf-8"):
            self.send_response(code)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(content)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header("Content-Security-Policy",
                             "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; "
                             "connect-src 'self'; img-src 'self'; frame-ancestors 'none'; base-uri 'none'")
            self.end_headers()
            self.wfile.write(content)

        def send_json(self, code, value):
            self.send_bytes(code, json.dumps(value, ensure_ascii=False).encode("utf-8"))

        def allowed(self):
            port = self.server.server_port
            hosts = {f"127.0.0.1:{port}", f"localhost:{port}", *configured}
            authority = self.headers.get("Host", "").lower()
            try:
                if not authority or any(c in authority for c in "/\\?#@, \t\r\n"):
                    raise ValueError("invalid Host")
                target = urlsplit("//" + authority)
                if not target.hostname:
                    raise ValueError("missing hostname")
                target.port  # Validate the port even when all public hosts are allowed.
                if "*" not in hosts and authority not in hosts and target.hostname not in hosts:
                    raise ValueError("host is not allowed")
            except ValueError:
                self.send_json(403, {"error": "Invalid host"})
                return False
            origin = self.headers.get("Origin")
            if origin:
                try:
                    parsed = urlsplit(origin)
                    default_port = 443 if parsed.scheme == "https" else 80
                    valid = (parsed.scheme in ("http", "https") and not parsed.username and
                             not parsed.password and not parsed.path and not parsed.query and not parsed.fragment and
                             parsed.hostname == target.hostname and
                             (parsed.port or default_port) == (target.port or default_port))
                except ValueError:
                    valid = False
                if not valid:
                    self.send_json(403, {"error": "Invalid origin"})
                    return False
            return True

        def do_GET(self):
            if not self.allowed():
                return
            path = urlsplit(self.path).path
            if path == "/api/examples":
                self.send_json(200, [{"id": key, "label": label, "source": (ROOT / filename).read_text()}
                                     for key, label, filename in EXAMPLES])
            elif path == "/api/health":
                self.send_json(200, {"name": "fken_cpl", "version": "0.9.0", "status": "ready"})
            elif path in ASSETS:
                filename, content_type = ASSETS[path]
                self.send_bytes(200, (ROOT / "web" / filename).read_bytes(), content_type)
            else:
                self.send_json(404, {"error": "Not found"})

        def do_POST(self):
            if not self.allowed():
                return
            try:
                if self.headers.get("Content-Type", "").split(";")[0] != "application/json":
                    self.send_json(415, {"error": "Expected application/json"})
                    return
                size = int(self.headers.get("Content-Length", "0"))
                if size <= 0 or size > MAX_SOURCE_BYTES * 6:
                    self.send_json(413, {"error": "Request is too large"})
                    return
                data = json.loads(self.rfile.read(size))
                source = data["source"]
                if not isinstance(source, str):
                    raise ValueError("source must be a string")
                path = urlsplit(self.path).path
                if path == "/api/analyze":
                    result = {"diagnostics": language.diagnostics(source)}
                elif path == "/api/complete":
                    result = language.completions(source, data["position"])
                elif path == "/api/hover":
                    result = {"hover": language.hover(source, data["position"])}
                elif path == "/api/plan":
                    result = language.execution_plan(source)
                elif path == "/api/compile":
                    result = language.compile(source)
                else:
                    self.send_json(404, {"error": "Not found"})
                    return
                self.send_json(200, {**result, "version": data.get("version")})
            except (ValueError, KeyError, TypeError, AttributeError) as error:
                self.send_json(400, {"error": str(error)})
            except (OSError, RuntimeError) as error:
                self.send_json(503, {"error": str(error)})

        def log_message(self, format, *args):
            # Keep routine keystroke requests out of the terminal.
            if args and str(args[1] if len(args) > 1 else "").startswith("5"):
                super().log_message(format, *args)

    return BoundedHTTPServer((host, port), Handler)


def serve(port=8765, host=None):
    host = host or os.environ.get("FKEN_WEB_HOST", "127.0.0.1")
    with make_server(port, host=host) as server:
        print(f"fken_cpl Web: http://{host}:{server.server_port}", flush=True)
        print("Ctrl+C で終了します。", flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="Local fken_cpl Web editor")
    parser.add_argument("--port", type=int, default=int(os.environ.get("PORT", "8765")))
    parser.add_argument("--host", default=os.environ.get("FKEN_WEB_HOST", "127.0.0.1"))
    args = parser.parse_args()
    if not 0 <= args.port <= 65535:
        parser.error("--port must be between 0 and 65535")
    serve(args.port, args.host)
