"""Compiler-backed language features, real LSP framing, and local Web API checks."""
import io
import json
import os
from pathlib import Path
import subprocess
import threading
import tempfile
import unittest
from http.client import HTTPConnection
from unittest.mock import patch

from compiler import ROOT
from language_service import LanguageService, MAX_SOURCE_BYTES, apply_changes, offset_at, position_at
from lsp_server import LspServer, read_message, write_message
from web_server import make_server


class LanguageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.service = LanguageService()

    def completion(self, marked):
        offset = marked.index("|")
        source = marked.replace("|", "")
        return {item["label"]: item for item in self.service.completions(source, position_at(source, offset))["items"]}

    def test_diagnostics_and_compile_use_real_compiler(self):
        source = "// 日本語🙂\nprint(missing);"
        errors = self.service.diagnostics(source)
        self.assertEqual(errors[0]["range"], {"start": {"line": 1, "character": 6},
                                             "end": {"line": 1, "character": 13}})
        self.assertIn("undefined variable", errors[0]["message"])
        self.assertEqual(self.service.diagnostics("print(42);"), [])
        self.assertEqual(self.service.diagnostics("🙂")[0]["range"]["end"]["character"], 2)
        eof = "let x=1"
        self.assertEqual(self.service.diagnostics(eof)[0]["range"]["start"], position_at(eof, len(eof)))
        result = self.service.compile("print(42);")
        cli = subprocess.run([str(ROOT / "fken_cpl"), "--code", "print(42);"],
                             capture_output=True, text=True, timeout=10)
        self.assertEqual(result["cpp"], cli.stdout)
        self.assertEqual(result["diagnostics"], [])
        self.assertEqual(self.service.compile("print(missing);")["cpp"], "")

    def test_completion_prefix_snippets_and_incomplete_code(self):
        items = self.completion("rea|")
        self.assertEqual(set(items), {"read_int", "read_vec"})
        self.assertIn("$", items["read_vec"]["textEdit"]["newText"])
        result = self.service.completions("rea", {"line": 0, "character": 3}, snippets=False)
        self.assertTrue(all("$" not in item["textEdit"]["newText"] for item in result["items"]))
        items = self.completion("cu|\nfn custom(a: &int[], n) -> int[] { return a; }")
        self.assertIn("custom", items)
        self.assertIn("a: &int[]", items["custom"]["detail"])
        items = self.completion("let amount = 1;\nam|")
        self.assertEqual(items["amount"]["kind"], 6)
        self.assertEqual(self.completion("// rea|"), {})
        self.assertIn("amount", self.completion("let amount = 1;\n🙂\nam|"))
        self.assertIn("arg", self.completion("fn unfinished(arg) { ar|"))
        self.assertEqual(self.completion("rea|()")['read_int']["textEdit"]["newText"], "read_int")

    def test_completion_respects_scopes(self):
        inside = self.completion(
            "let outside=1;\nfn grow(data: &int[], step) -> int[] {\n"
            "let copy=data;\nfor(let index=0; index<len(copy); index++) { let inner=step;\n| }\n"
            "return copy;\n}")
        for name in ("data", "step", "copy", "index", "inner", "grow"):
            self.assertIn(name, inside)
        self.assertNotIn("outside", inside)
        self.assertIn("int[]", inside["copy"]["detail"])
        after = self.completion("for(let index=0; index<2; index++) { let inner=1; }\n|")
        self.assertNotIn("index", after)
        self.assertNotIn("inner", after)
        self.assertNotIn("hidden", self.completion("{ let hidden=1; }\n|"))
        self.assertIn("int[]", self.completion("let x=1; {let x=[];\n|}")["x"]["detail"])
        self.assertIn("i", self.completion("rep(i, 0, 2) { | }"))
        self.assertNotIn("i", self.completion("rep(i, 0, 2) { } |"))

    def test_completion_infers_array_values_without_confusing_calls(self):
        items = self.completion(
            "fn values() -> int[] {return [];}\n"
            "let a=values(); let b=a[0]; let array=3; let c=array;\n|")
        self.assertIn("int[]", items["a"]["detail"])
        self.assertEqual(items["b"]["detail"], "int b")
        self.assertEqual(items["c"]["detail"], "int c")
        self.assertEqual(self.completion("let a=read_vec(2)[0];\n|")["a"]["detail"], "int a")

    def test_utf16_changes_hover_and_limits(self):
        text = "//🙂\r\nlet x=1;\r\nprint(x);"
        self.assertEqual(offset_at(text, {"line": 0, "character": 4}), 3)
        self.assertEqual(position_at(text, 3), {"line": 0, "character": 4})
        changed = apply_changes(text, [{"range": {"start": {"line": 1, "character": 6},
                                                  "end": {"line": 1, "character": 7}}, "text": "20"}])
        self.assertIn("let x=20;", changed)
        self.assertEqual(self.service.hover(changed, {"line": 2, "character": 7})["contents"]["value"], "int x")
        self.assertIsNone(self.service.hover("unknown", {"line": 0, "character": 2}))
        with patch("language_service.subprocess.run", side_effect=AssertionError("must not spawn")):
            self.assertTrue(self.service.diagnostics("a" * (MAX_SOURCE_BYTES + 1)))

    def test_typed_vector_methods_and_break_completion(self):
        items = self.completion("int[][] grid(2,3);\ngrid.|")
        self.assertEqual(set(items), {"push", "pop", "back", "size", "resize", "clear"})
        self.assertEqual(items["push"]["kind"], 2)
        self.assertIn("value: int[]", items["push"]["detail"])
        self.assertIn("value: int)", self.completion("int[][] grid; grid[0].pu|")["push"]["detail"])
        self.assertEqual(self.completion("int x; x.|"), {})
        self.assertIn("push", self.completion("fn make()->int[][]{return [];} make()[0].|"))
        self.assertIn("push", self.completion("int[][] grid; (grid[0]).|"))
        self.assertIn("int[][]", self.completion("int[][] grid; gr|")["grid"]["detail"])
        self.assertIn("int[]", self.completion("let a=[[1]]; let b=a.pop(); b|")["b"]["detail"])
        self.assertNotIn("push", self.completion("int[] a; a[0].|"))
        self.assertIn("break", self.completion("while(1){br|}"))
        self.assertNotIn("break", self.completion("br|"))
        self.assertNotIn("i", self.completion("for(int i=0;i<3;i++){} |"))
        source = "int[][] grid(2,3); grid.push([1]);"
        hover = self.service.hover(source, position_at(source, source.index("push") + 1))
        self.assertIn("push(value: int[])", hover["contents"]["value"])
        self.assertEqual(self.service.diagnostics("int[][] g(2,3); g[0].push(4); while(1){break;}"), [])
        self.assertIn("break is only allowed", self.service.diagnostics("break;")[0]["message"])

    def test_record_and_graph_members(self):
        self.assertEqual(set(self.completion("graph G(2); G.|")), {"size", "add", "edge_count"})
        self.assertEqual(set(self.completion("graph G(2); G[0][0].|")), {"to", "cost", "id"})
        self.assertIn("cost", self.completion("graph G(2); for(edge e : G[0]) { e.| }"))
        point = "struct P { int x; fn twice() -> int {return self.x*2;} } "
        self.assertEqual(set(self.completion(point + "P[] a(1); a[0].|")), {"x", "twice"})
        self.assertIn("x", self.completion("struct P { int x; fn get() -> int { return self.|; } }"))
        for source in ("graph G(1); G[0].clear();", "graph G(1); G[0][0].cost = 1;",
                       point + "P q = P{x: 1, x: 2};", point + "P q; q.missing = 1;"):
            with self.subTest(source=source):
                self.assertTrue(self.service.diagnostics(source))

    def test_check_cli(self):
        result = subprocess.run([str(ROOT / "fken_cpl"), "--check", "--code", "print(1);"],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "")
        result = subprocess.run([str(ROOT / "fken_cpl"), "--check", "--code", "print(x);"],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 1)
        self.assertIn("undefined variable", result.stderr)


class LspTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.service = LanguageService()

    def test_stdio_lifecycle_changes_and_completion(self):
        uri = "file:///tmp/日本語.fken"
        messages = [
            {"id": 1, "method": "initialize", "params": {"capabilities": {"textDocument": {
                "completion": {"completionItem": {"snippetSupport": True}}}}}},
            {"method": "initialized", "params": {}},
            {"method": "textDocument/didOpen", "params": {"textDocument": {
                "uri": uri, "languageId": "fken_cpl", "version": 1, "text": "//🙂\nlet amount=1;\nam"}}},
            {"id": 2, "method": "textDocument/completion", "params": {
                "textDocument": {"uri": uri}, "position": {"line": 2, "character": 2}}},
            {"method": "textDocument/didChange", "params": {"textDocument": {"uri": uri, "version": 2},
                "contentChanges": [{"range": {"start": {"line": 2, "character": 0},
                                              "end": {"line": 2, "character": 2}}, "text": "print(amount);"}]}},
            {"id": 3, "method": "textDocument/hover", "params": {
                "textDocument": {"uri": uri}, "position": {"line": 2, "character": 8}}},
            {"id": 4, "method": "textDocument/diagnostic", "params": {"textDocument": {"uri": uri}}},
            {"id": 6, "method": "fken_cpl/compile", "params": {"source": "print(987654321);", "version": 7}},
            {"id": 7, "method": "fken_cpl/compile", "params": {"source": "print(missing);", "version": 8}},
            {"id": 8, "method": "fken_cpl/compile", "params": {"source": 42}},
            {"method": "textDocument/didClose", "params": {"textDocument": {"uri": uri}}},
            {"id": 5, "method": "shutdown"}, {"method": "exit"},
        ]
        stream = io.BytesIO()
        for message in messages:
            write_message(stream, {"jsonrpc": "2.0", **message})
        result = subprocess.run([str(ROOT / "fken_cpl"), "--lsp"], input=stream.getvalue(),
                                capture_output=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        output, replies = io.BytesIO(result.stdout), []
        while True:
            message = read_message(output)
            if message is None:
                break
            replies.append(message)
        by_id = {item["id"]: item for item in replies if "id" in item}
        self.assertEqual(by_id[1]["result"]["capabilities"]["positionEncoding"], "utf-16")
        self.assertIn("amount", [item["label"] for item in by_id[2]["result"]["items"]])
        self.assertEqual(by_id[3]["result"]["contents"]["value"], "int amount")
        self.assertEqual(by_id[4]["result"]["items"], [])
        self.assertIsNone(by_id[5]["result"])
        self.assertIn("987654321", by_id[6]["result"]["cpp"])
        self.assertEqual(by_id[6]["result"]["version"], 7)
        self.assertEqual(by_id[6]["result"]["diagnostics"], [])
        self.assertEqual(by_id[7]["result"]["cpp"], "")
        self.assertEqual(by_id[7]["result"]["version"], 8)
        self.assertIn("undefined variable", by_id[7]["result"]["diagnostics"][0]["message"])
        self.assertEqual(by_id[8]["error"]["code"], -32602)
        pushes = [item["params"] for item in replies if item.get("method") == "textDocument/publishDiagnostics"]
        self.assertTrue(pushes[0]["diagnostics"])
        self.assertEqual(pushes[0]["version"], 1)
        self.assertEqual(pushes[1]["diagnostics"], [])
        self.assertEqual(pushes[1]["version"], 2)
        self.assertEqual(pushes[2]["diagnostics"], [])

    def test_errors_versions_and_message_framing(self):
        server = LspServer(self.service)
        self.assertEqual(server.handle({"id": 0, "method": "anything"})[0]["error"]["code"], -32002)
        server.handle({"id": 1, "method": "initialize"})
        self.assertEqual(server.handle({"id": 2, "method": "unknown"})[0]["error"]["code"], -32601)
        self.assertEqual(server.handle({"id": 3, "method": "textDocument/completion",
                                      "params": {"textDocument": {"uri": "missing"}}})[0]["error"]["code"], -32602)
        server.handle({"method": "textDocument/didOpen", "params": {"textDocument": {
            "uri": "test", "version": 2, "text": "print(1);"}}})
        self.assertEqual(server.handle({"method": "textDocument/didChange", "params": {
            "textDocument": {"uri": "test", "version": 1}, "contentChanges": [{"text": "bad"}]}}), [])
        self.assertEqual(server.documents["test"]["text"], "print(1);")
        for body in (b"Bad\r\n\r\n", b"Content-Length: -1\r\n\r\n",
                     b"Content-Length: 20\r\n\r\n{}"):
            with self.subTest(body=body), self.assertRaises(ValueError):
                read_message(io.BytesIO(body))
        payload = io.BytesIO(b"Content-Length: 1\r\n\r\n{")
        payload.seek(0, io.SEEK_END)
        write_message(payload, {"id": 1, "method": "initialize"})
        write_message(payload, {"id": 2, "method": "shutdown"})
        write_message(payload, {"method": "exit"})
        result = subprocess.run([str(ROOT / "fken_cpl"), "--lsp"], input=payload.getvalue(),
                                capture_output=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertEqual(read_message(io.BytesIO(result.stdout))["error"]["code"], -32700)

    def test_external_build_cache_and_compiler_with_spaces(self):
        with tempfile.TemporaryDirectory(prefix="fken cache ") as directory:
            work = Path(directory)
            cc = work / "C compiler"
            cc.symlink_to("/usr/bin/cc")
            env = {**os.environ, "FKEN_BUILD_DIR": str(work / "nested" / "cache"), "FKEN_CC_PATH": str(cc)}
            result = subprocess.run([str(ROOT / "fken_cpl"), "--check", "--code", "print(42);"],
                                    env=env, capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue((work / "nested" / "cache" / "compiler").is_file())


class WebTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = make_server(0)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.port = cls.server.server_port

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join(timeout=3)

    def request(self, method, path, data=None, headers=None):
        connection = HTTPConnection("127.0.0.1", self.port, timeout=10)
        body = json.dumps(data).encode() if data is not None else None
        headers = {"Content-Type": "application/json", **(headers or {})}
        connection.request(method, path, body=body, headers=headers)
        response = connection.getresponse()
        status, content_type, content = response.status, response.getheader("Content-Type"), response.read()
        connection.close()
        return status, content_type, content

    def test_assets_and_language_endpoints(self):
        status, content_type, body = self.request("GET", "/")
        self.assertEqual(status, 200)
        self.assertIn("text/html", content_type)
        self.assertIn(b'id="source"', body)
        self.assertEqual(json.loads(self.request("GET", "/api/health")[2])["status"], "ready")
        self.assertTrue(json.loads(self.request("GET", "/api/examples")[2]))
        status, _, body = self.request("POST", "/api/analyze", {"source": "print(x);", "version": 2})
        self.assertEqual(status, 200)
        self.assertEqual(json.loads(body)["version"], 2)
        self.assertIn("undefined variable", json.loads(body)["diagnostics"][0]["message"])
        status, _, body = self.request("POST", "/api/complete", {
            "source": "rea", "position": {"line": 0, "character": 3}})
        self.assertEqual(status, 200)
        self.assertEqual({i["label"] for i in json.loads(body)["items"]}, {"read_int", "read_vec"})
        status, _, body = self.request("POST", "/api/compile", {"source": "print(42);"})
        self.assertEqual(status, 200)
        self.assertIn("int main()", json.loads(body)["cpp"])
        self.assertEqual(json.loads(body)["diagnostics"], [])

    def test_http_boundaries(self):
        self.assertEqual(self.request("GET", "/../README.md")[0], 404)
        self.assertEqual(self.request("GET", "/", headers={"Host": "example.invalid"})[0], 403)
        self.assertEqual(self.request("POST", "/api/compile", {"source": ""},
                                      {"Origin": "https://example.invalid"})[0], 403)
        self.assertEqual(self.request("POST", "/api/compile", {"source": 2})[0], 400)
        self.assertEqual(self.request("POST", "/api/compile", {"source": ""},
                                      {"Content-Type": "text/plain"})[0], 415)
        self.assertEqual(self.request("POST", "/api/compile", {},
                                      {"Content-Length": str(MAX_SOURCE_BYTES * 6 + 1)})[0], 413)
        self.assertEqual(self.request("POST", "/api/nope", {"source": ""})[0], 404)


class PublicWebTests(unittest.TestCase):
    def test_proxy_host_and_https_origin(self):
        server = make_server(0, host="0.0.0.0", allowed_hosts={"public.example"})
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            cases = [
                ({"Host": "public.example", "Origin": "https://public.example"}, 200),
                ({"Host": "public.example", "Origin": "https://public.example:443"}, 200),
                ({"Host": "public.example", "Origin": "https://foreign.invalid"}, 403),
                ({"Host": "unknown.invalid"}, 403),
                ({"Host": "public.example:bad"}, 403),
                ({"Host": "user@public.example"}, 403),
                ({"Host": "public.example", "Origin": "null"}, 403),
                ({"Host": "public.example", "Origin": "https://public.example/path"}, 403),
            ]
            for headers, expected in cases:
                with self.subTest(headers=headers):
                    connection = HTTPConnection("127.0.0.1", server.server_port, timeout=5)
                    connection.request("POST", "/api/compile", body=json.dumps({"source": "print(1);"}),
                                       headers={"Content-Type": "application/json", **headers})
                    response = connection.getresponse()
                    self.assertEqual(response.status, expected, response.read())
                    connection.close()
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=3)


if __name__ == "__main__":
    unittest.main(verbosity=2)
