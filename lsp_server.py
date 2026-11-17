"""LSP 3.17 stdio transport. No third-party Python packages are required."""
import json
import sys

from language_service import LanguageService, apply_changes

MAX_MESSAGE_BYTES = 2 * 1024 * 1024


def read_message(stream):
    length = None
    while True:
        line = stream.readline(8193)
        if not line:
            if length is None:
                return None
            raise ValueError("unexpected end of LSP headers")
        if len(line) > 8192:
            raise ValueError("LSP header is too long")
        if line in (b"\r\n", b"\n"):
            break
        key, separator, value = line.partition(b":")
        if not separator:
            raise ValueError("invalid LSP header")
        if key.strip().lower() == b"content-length":
            if length is not None:
                raise ValueError("duplicate Content-Length")
            length = int(value.strip())
    if length is None or length < 0 or length > MAX_MESSAGE_BYTES:
        raise ValueError("invalid LSP Content-Length")
    body = bytearray()
    while len(body) < length:
        part = stream.read(length - len(body))
        if not part:
            raise ValueError("unexpected end of LSP body")
        body.extend(part)
    return json.loads(body.decode("utf-8"))


def write_message(stream, message):
    body = json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    stream.write(f"Content-Length: {len(body)}\r\n\r\n".encode("ascii") + body)
    stream.flush()


class LspServer:
    def __init__(self, service=None):
        self.service = service or LanguageService()
        self.documents = {}
        self.initialized = False
        self.shutdown = False
        self.should_exit = False
        self.snippets = False

    def _document(self, params):
        uri = params["textDocument"]["uri"]
        if not isinstance(uri, str) or uri not in self.documents:
            raise ValueError("document is not open")
        return uri, self.documents[uri]

    def _publish(self, uri):
        doc = self.documents[uri]
        return {"jsonrpc": "2.0", "method": "textDocument/publishDiagnostics",
                "params": {"uri": uri, "version": doc["version"],
                           "diagnostics": self.service.diagnostics(doc["text"])}}

    def handle(self, message):
        if not isinstance(message, dict) or not isinstance(message.get("method"), str):
            return [{"jsonrpc": "2.0", "id": message.get("id") if isinstance(message, dict) else None,
                     "error": {"code": -32600, "message": "Invalid request"}}]
        method, params = message["method"], message.get("params") or {}
        request = "id" in message
        response = {"jsonrpc": "2.0", "id": message.get("id")}
        try:
            if method == "exit":
                self.should_exit = True
                return []
            if method == "initialize":
                if self.initialized:
                    raise ValueError("server is already initialized")
                capabilities = params.get("capabilities", {}).get("textDocument", {})
                self.snippets = capabilities.get("completion", {}).get("completionItem", {}).get("snippetSupport", False)
                self.initialized = True
                result = {"capabilities": {
                    "positionEncoding": "utf-16",
                    "textDocumentSync": {"openClose": True, "change": 2, "save": {"includeText": True}},
                    "completionProvider": {"resolveProvider": False, "triggerCharacters": ["_", "."]},
                    "hoverProvider": True,
                    "diagnosticProvider": {"identifier": "fken_cpl", "interFileDependencies": False,
                                           "workspaceDiagnostics": False}},
                    "serverInfo": {"name": "fken_cpl", "version": "0.6.0"}}
            elif not self.initialized:
                return [dict(response, error={"code": -32002, "message": "Server not initialized"})] if request else []
            elif method == "shutdown":
                self.shutdown = True
                result = None
            elif self.shutdown:
                return [dict(response, error={"code": -32600, "message": "Server has shut down"})] if request else []
            elif method == "textDocument/didOpen":
                document = params["textDocument"]
                if not isinstance(document["text"], str) or not isinstance(document["uri"], str):
                    raise ValueError("invalid document")
                self.documents[document["uri"]] = {"text": document["text"], "version": document["version"]}
                return [self._publish(document["uri"])]
            elif method == "textDocument/didChange":
                uri, doc = self._document(params)
                version = params["textDocument"]["version"]
                if version <= doc["version"]:
                    return []
                text = apply_changes(doc["text"], params["contentChanges"])
                self.documents[uri] = {"text": text, "version": version}
                return [self._publish(uri)]
            elif method == "textDocument/didSave":
                uri, doc = self._document(params)
                if "text" in params:
                    if not isinstance(params["text"], str):
                        raise ValueError("invalid document")
                    doc["text"] = params["text"]
                return [self._publish(uri)]
            elif method == "textDocument/didClose":
                uri = params["textDocument"]["uri"]
                self.documents.pop(uri, None)
                return [{"jsonrpc": "2.0", "method": "textDocument/publishDiagnostics",
                         "params": {"uri": uri, "diagnostics": []}}]
            elif method == "textDocument/completion":
                _, doc = self._document(params)
                result = self.service.completions(doc["text"], params["position"], self.snippets)
            elif method == "textDocument/hover":
                _, doc = self._document(params)
                result = self.service.hover(doc["text"], params["position"])
            elif method == "textDocument/diagnostic":
                _, doc = self._document(params)
                result = {"kind": "full", "resultId": str(doc["version"]),
                          "items": self.service.diagnostics(doc["text"])}
            elif method == "fken_cpl/compile":
                # Compile the captured editor contents, including unsaved edits.
                if not isinstance(params["source"], str):
                    raise ValueError("source must be a string")
                result = {**self.service.compile(params["source"]), "version": params.get("version")}
            elif method in ("initialized", "$/cancelRequest", "$/setTrace"):
                return []
            else:
                return [dict(response, error={"code": -32601, "message": "Method not found"})] if request else []
            return [dict(response, result=result)] if request else []
        except (ValueError, KeyError, TypeError, AttributeError) as error:
            return [dict(response, error={"code": -32602, "message": str(error)})] if request else []
        except (OSError, RuntimeError) as error:
            if request:
                return [dict(response, error={"code": -32603, "message": str(error)})]
            print(f"fken_cpl LSP: {error}", file=sys.stderr)
            return []


def run_stdio():
    server = LspServer()
    while not server.should_exit:
        try:
            message = read_message(sys.stdin.buffer)
            if message is None:
                break
            for response in server.handle(message):
                write_message(sys.stdout.buffer, response)
        except (json.JSONDecodeError, UnicodeDecodeError):
            write_message(sys.stdout.buffer, {"jsonrpc": "2.0", "id": None,
                                              "error": {"code": -32700, "message": "Parse error"}})
        except ValueError as error:
            print(f"fken_cpl LSP: {error}", file=sys.stderr)
            return 1
    return 0 if server.shutdown else 1


if __name__ == "__main__":
    sys.exit(run_stdio())
