"""Compiler-backed diagnostics and tolerant completion shared by stdio LSP and Web."""
from functools import lru_cache
import json
import re
import subprocess
import threading

from compiler import build_compiler

MAX_SOURCE_BYTES = 256 * 1024
TIMEOUT_SECONDS = 2

MAP_TYPES = {"map", "unordered_map", "multi_map", "unordered_multi_map", "multimap", "unordered_multimap"}
SET_TYPES = {"set", "unordered_set", "multi_set", "unordered_multi_set", "multiset", "unordered_multiset"}
MOD_TYPES = {"mint", "mint998", "mint107"}
DECLARED_TYPES = MAP_TYPES | SET_TYPES | MOD_TYPES | {"int", "char", "string", "pair", "void"}

CATALOG = {
    "gcd": (3, "gcd(a, b) -> int", "gcd(${1:a}, ${2:b})", "最大公約数を返します。"),
    "lcm": (3, "lcm(a, b) -> int", "lcm(${1:a}, ${2:b})", "最小公倍数を返します。"),
    "pow_mod": (3, "pow_mod(a, n, m) -> int", "pow_mod(${1:a}, ${2:n}, ${3:m})", "a の n 乗を法 m で計算します。"),
    "inv_mod": (3, "inv_mod(a, m) -> int", "inv_mod(${1:a}, ${2:m})", "法 m における a の逆元を求めます。"),
    "mint998": (14, "mint998", "mint998", "法 998244353 の剰余環型です。"),
    "mint107": (14, "mint107", "mint107", "法 1000000007 の剰余環型です。"),
    "mint": (14, "mint<M>", "mint<${1:M}>", "正の整数リテラル M を法にします。"),
    "char": (14, "char", "char", "1 バイトの文字型です。"),
    "string": (14, "string = char[]", "string", "可変長の文字列型です。"),
    "read_char": (3, "read_char() -> char", "read_char()", "空白を飛ばして 1 バイト読みます。"),
    "read_string": (3, "read_string() -> string", "read_string()", "空白区切りの文字列を読みます。"),
    "auto": (14, "auto name = value;", "auto ", "初期値から型を推論します。auto [a, b] で構造体のフィールドを分解できます。"),
    "struct": (15, "struct Name { ... }", "struct ${1:Point} {\n    int ${2:x};\n}", "フィールドとメソッドを定義します。"),
    "graph": (14, "graph G(n);", "graph ${1:G}(${2:n});", "無向グラフを作成します。"),
    "digraph": (14, "digraph D(n);", "digraph ${1:D}(${2:n});", "有向グラフを作成します。"),
    "edge": (14, "edge", "edge", "辺の to、cost、id を持つ値です。"),
    "cin": (14, "cin >> value;", "cin >> ${1:value};", "整数、文字、文字列を読み込みます。"),
    "cout": (14, "cout << value;", "cout << ${1:value};", "整数、文字、文字列を出力します。"),
    "read_int": (3, "read_int() -> int", "read_int()", "空白区切りの整数を一つ読みます。64bit を超える値は自動昇格します。"),
    "read_vec": (3, "read_vec(n: int) -> int[]", "read_vec(${1:n})", "整数を n 個読み、配列にします。"),
    "array": (3, "array(n: int, value: int = 0) -> int[]", "array(${1:n})", "長さ n の整数配列を作ります。第2引数で初期値を指定できます。"),
    "len": (3, "len(a: int[]) -> int", "len(${1:a})", "配列の長さを返します。"),
    "print": (3, "print(value) -> void", "print(${1:value});", "値または配列を改行付きで出力します。"),
    "rep": (15, "rep(i, start, end)", "rep(${1:i}, ${2:0}, ${3:n}) {\n    $0\n}", "終点を含まない for の短縮形です。"),
    "for": (15, "for (init; condition; step)", "for (int i = ${1:0}; i < ${2:n}; i++) {\n    $0\n}", "初期化・条件・更新を指定するループです。各節を省略できます。"),
    "fn": (15, "fn name() -> int", "int ${1:name}() {\n    return ${2:0};\n}", "トップレベルに関数を定義します。型を省略すると int です。"),
    "if": (15, "if (condition)", "if (${1:condition}) {\n    $0\n}", "条件に応じてブロックを実行します。"),
    "while": (15, "while (condition)", "while (${1:condition}) {\n    $0\n}", "条件が真の間、ブロックを繰り返します。"),
    "let": (14, "let name = value;", "let ", "変数を宣言します。旧構文です。既存コード向けに型推論を維持します。"),
    "else": (14, "else { ... }", "else {\n    $0\n}", "if の条件が偽の場合のブロックです。"),
    "return": (14, "return value;", "return ${1:value};", "関数から値を返します。void 関数では return; と書きます。"),
    "int": (14, "int", "int", "通常は符号付き64bit、範囲を超えた値は任意精度に自動昇格します。"),
    "int[]": (14, "int[]", "int[]", "整数の可変長配列です。int[] a(n); で n 個の 0 を作ります。"),
    "int[][]": (14, "int[][]", "int[][]", "二次元の可変長配列です。int[][] a(n, m); で 0 初期化します。"),
    "break": (14, "break;", "break;", "最も内側の for / while / rep を抜けます。"),
    "void": (14, "void", "void", "戻り値のない関数の型です。"),
}


for name in sorted(MAP_TYPES | SET_TYPES):
    types = "<${1:int}, ${2:int}>" if name in MAP_TYPES else "<${1:int}>"
    CATALOG[name] = (14, name + "<...>", name + types, "添字でアクセスできる連想コンテナです。")


VECTOR_METHODS = {
    "push": ("push(value: {element}) -> void", "push(${1:value})", "末尾へ値を追加します。配列の値はコピーします。"),
    "pop": ("pop() -> {element}", "pop()", "末尾を削除して返します。空の配列は実行時エラーです。"),
    "back": ("back() -> {element}", "back()", "末尾の値をコピーして返します。空の配列は実行時エラーです。"),
    "size": ("size() -> int", "size()", "この次元の要素数を返します。"),
    "resize": ("resize(n: int, value: {element} = default) -> void", "resize(${1:n})", "サイズを変更します。追加要素は 0、内側の配列は空になります。第2引数で初期値を指定できます。"),
    "clear": ("clear() -> void", "clear()", "この配列の全要素を削除します。"),
}


INT_VECTOR_METHODS = {
    "sort": ("sort() -> void", "sort()", "整数を昇順に並べ替えます。"),
    "at_least": ("at_least(x: int) -> int", "at_least(${1:x})", "x 以上の最初の添字。該当なしは size()。昇順配列用。"),
    "more_than": ("more_than(x: int) -> int", "more_than(${1:x})", "x より大きい最初の添字。該当なしは size()。昇順配列用。"),
    "at_most": ("at_most(x: int) -> int", "at_most(${1:x})", "x 以下の最後の添字。該当なしは -1。昇順配列用。"),
    "less_than": ("less_than(x: int) -> int", "less_than(${1:x})", "x 未満の最後の添字。該当なしは -1。昇順配列用。"),
    "run_length": ("run_length() -> pair<int,int>[]", "run_length()", "連続区間を (値, 個数) のペアに変換します。"),
}


def method_items(receiver_type):
    if not receiver_type.endswith("[]"):
        return {}
    methods = VECTOR_METHODS
    if receiver_type == "int[]":
        methods = VECTOR_METHODS | INT_VECTOR_METHODS
    elif receiver_type == "char[]":
        methods = VECTOR_METHODS | {"push_back": ("push_back(c: char) -> void", "push_back(${1:c})", "末尾に文字を 1 バイト追加します。")}
    return {name: {"label": name, "kind": 2, "detail": detail.format(element=receiver_type[:-2]),
                   "insert": insert, "documentation": doc, "priority": "0"}
            for name, (detail, insert, doc) in methods.items()}


def utf16_length(text):
    return len(text.encode("utf-16-le", errors="replace")) // 2


def position_at(source, offset):
    offset = max(0, min(offset, len(source)))
    start = source.rfind("\n", 0, offset) + 1
    return {"line": source.count("\n", 0, offset), "character": utf16_length(source[start:offset])}


def offset_at(source, position):
    line, character = position.get("line"), position.get("character")
    if not isinstance(line, int) or not isinstance(character, int) or line < 0 or character < 0:
        raise ValueError("invalid document position")
    lines = source.split("\n")
    if line >= len(lines):
        raise ValueError("line is outside the document")
    text = lines[line].removesuffix("\r")
    offset = sum(len(part) + 1 for part in lines[:line])
    units = 0
    for i, char in enumerate(text):
        width = utf16_length(char)
        if units + width > character:
            return offset + i
        units += width
        if units == character:
            return offset + i + 1
    return offset + len(text)


def apply_changes(source, changes):
    for change in changes:
        if "range" not in change:
            source = change["text"]
            continue
        start = offset_at(source, change["range"]["start"])
        end = offset_at(source, change["range"]["end"])
        if end < start:
            raise ValueError("invalid edit range")
        source = source[:start] + change["text"] + source[end:]
    return source


def plain_snippet(text):
    return re.sub(r"\$\{[0-9]+:([^}]*)\}|\$[0-9]+",
                  lambda match: match[1] or "", text)


def byte_index(source):
    mapping = []
    for i, char in enumerate(source):
        mapping.extend([i] * len(char.encode("utf-8", errors="replace")))
    mapping.append(len(source))
    return mapping


def diagnostic(source, message, start=0, end=None):
    end = min(len(source), start + 1) if end is None else end
    return {"range": {"start": position_at(source, start), "end": position_at(source, end)},
            "severity": 1, "source": "fken_cpl", "message": message}


class DocumentIndex:
    """Only declarations/scopes are indexed here; the C compiler validates semantics."""
    def __init__(self, source, raw_tokens):
        self.source = source
        mapping = byte_index(source)
        self.tokens = [
            {**token, "start": mapping[min(token["offset"], len(mapping) - 1)],
             "end": mapping[min(token["offset"] + token["length"], len(mapping) - 1)]}
            for token in raw_tokens if token["kind"] != "TK_EOF"
        ]
        tokens = self.tokens
        self.pairs = {}
        stack = []
        closes = {"TK_RPAREN": "TK_LPAREN", "TK_RBRACKET": "TK_LBRACKET", "TK_RBRACE": "TK_LBRACE"}
        for i, token in enumerate(tokens):
            kind = token["kind"]
            if kind in closes.values():
                stack.append(i)
            elif kind in closes and stack and tokens[stack[-1]]["kind"] == closes[kind]:
                opened = stack.pop()
                self.pairs[opened] = i
        self.reverse_pairs = {close: opened for opened, close in self.pairs.items()}
        eof = len(source) + 1
        self.blocks = [(0, eof)]
        for i, token in enumerate(tokens):
            if token["kind"] == "TK_LBRACE":
                close = self.pairs.get(i)
                self.blocks.append((token["start"], tokens[close]["start"] if close is not None else eof))
        self.method_scopes = {}
        self.members = {"graph": {}, "edge": {}, "pair<int,int>": {}}
        for name in ("first", "second"):
            self.members["pair<int,int>"][name] = self.member(name, 5, "int " + name, name, "int")
        for name, args in (("add", "u, v, cost = 1"), ("size", ""), ("edge_count", "")):
            insert = "add(${1:u}, ${2:v})" if name == "add" else name + "()"
            self.members["graph"][name] = self.member(name, 2, f"{name}({args}) -> int", insert, "int")
        for name in ("bfs", "dijkstra"):
            self.members["graph"][name] = self.member(name, 2, name + "(source: int) -> int[]",
                                                      name + "(${1:source})", "int[]")
        self.members["digraph"] = self.members["graph"].copy()
        self.members["graph"]["diameter"] = self.member(
            "diameter", 5, "diameter → (dist, one, two)", "diameter", "<tree diameter>")
        self.members["<tree diameter>"] = {
            name: self.member(name, 5, "int " + name, name, "int") for name in ("dist", "one", "two")
        }
        self.members["graph"]["components"] = self.member("components", 2, "components() -> int[]", "components()", "int[]")
        for name in ("to", "cost", "id"):
            self.members["edge"][name] = self.member(name, 5, "int " + name, name, "int")
        for i in range(len(tokens)-2):
            if tokens[i]["text"] != "struct" or tokens[i+2]["kind"] != "TK_LBRACE":
                continue
            members = self.members.setdefault(tokens[i+1]["text"], {})
            j, limit = i+3, self.pairs.get(i+2, len(tokens))
            while j+1 < limit:
                method = self.read_function(j, limit, method=True)
                if method:
                    name, signature_end, body, result, params, _, detail = method
                    insert = self.function_insert(name["text"], params)
                    members[name["text"]] = self.member(name["text"], 2, detail, insert, result)
                    self.method_scopes[tokens[j]["start"]] = tokens[i+1]["text"]
                    j = body + 1
                else:
                    result, j = self.read_type(j, limit)
                    if j >= limit or tokens[j]["kind"] != "TK_IDENT":
                        break
                    name = tokens[j]["text"]
                    members[name] = self.member(name, 5, result + " " + name, name, result)
                    j += 2
        self.functions = []
        for i, token in enumerate(tokens):
            if token["start"] not in self.method_scopes and any(start < token["start"] < end for start, end in self.blocks[1:]):
                continue
            parsed = self.read_function(i, len(tokens))
            if not parsed:
                continue
            name, opening, close, result, params, _, detail = parsed
            body = (tokens[opening]["start"], tokens[close]["start"] if close < len(tokens) else eof)
            self.functions.append({"name": name["text"], "token": name, "params": params,
                                   "body": body, "return": result, "owner": token["start"], "detail": detail})
        self.variables = []
        for function in self.functions:
            if function["body"]:
                start, end = function["body"]
                params = function["params"]
                if function["owner"] in self.method_scopes:
                    params = [*params, {"name": "self", "type": self.method_scopes[function["owner"]]}]
                for param in params:
                    self.variables.append({"name": param["name"], "type": param["type"],
                                           "start": start, "end": end, "visible": start,
                                           "owner": function["owner"], "scope": start})
        loops = []
        for i, token in enumerate(tokens):
            if token["kind"] not in ("TK_FOR", "TK_REP", "TK_WHILE") or i + 1 >= len(tokens):
                continue
            close = self.pairs.get(i + 1)
            if close is None:
                continue
            body = close + 1
            end = eof
            if body < len(tokens) and tokens[body]["kind"] == "TK_LBRACE":
                closed = self.pairs.get(body)
                end = tokens[closed]["start"] if closed is not None else eof
            loops.append((i, close, end))
            if token["kind"] == "TK_REP" and i + 2 < close and tokens[i + 2]["kind"] == "TK_IDENT":
                name = tokens[i + 2]
                start = tokens[body]["start"] if body < len(tokens) else eof
                self.variables.append({"name": name["text"], "type": "int", "start": start, "end": end,
                                       "visible": start, "scope": start, "owner": self.owner(token["start"])})
        self.loop_ranges = [(tokens[close]["end"], end) for _, close, end in loops]
        for i, token in enumerate(tokens):
            if token["kind"] not in ("TK_LET", "TK_AUTO", "TK_INT") and token["text"] not in self.members and token["text"] not in DECLARED_TYPES:
                continue
            if token["kind"] == "TK_AUTO" and i + 1 in self.pairs and tokens[i+1]["kind"] == "TK_LBRACKET":
                close = self.pairs[i+1]
                if close + 2 >= len(tokens) or tokens[close+1]["kind"] != "TK_ASSIGN":
                    continue
                end = next((j for j in range(close+2, len(tokens)) if tokens[j]["kind"] == "TK_SEMICOLON"), None)
                if end is None:
                    continue
                value_type = self.infer_type(close+2, end, token["start"])
                fields = [f for f in self.members.get(value_type, {}).values() if f["kind"] == 5]
                scope = max((b for b in self.blocks if b[0] <= token["start"] <= b[1]), key=lambda b: (b[0], -b[1]))
                for opened, loop_close, loop_end in loops:
                    if opened < i < loop_close:
                        scope = (tokens[opened]["start"], loop_end)
                for j, field in zip(range(i+2, close, 2), fields):
                    if tokens[j]["kind"] == "TK_IDENT":
                        self.variables.append({"name": tokens[j]["text"], "type": field["result"],
                                               "start": scope[0], "end": scope[1], "visible": tokens[end]["end"],
                                               "owner": self.owner(token["start"]), "scope": scope[0]})
                continue
            declared_type, j = self.read_type(i, len(tokens))
            if j >= len(tokens) or tokens[j]["kind"] != "TK_IDENT":
                continue
            name, value_start = tokens[j]["text"], j + 1
            scope = max((b for b in self.blocks if b[0] <= token["start"] <= b[1]), key=lambda b: (b[0], -b[1]))
            for opened, close, end in loops:
                if opened < i < close:
                    scope = (tokens[opened]["start"], end)
            if value_start < len(tokens) and tokens[value_start]["text"] == ":" and any(a < i < b for a,b,_ in loops):
                self.variables.append({"name": name, "type": declared_type, "start": scope[0], "end": scope[1],
                                       "visible": tokens[j]["end"], "owner": self.owner(token["start"]), "scope": scope[0]})
                continue
            semi_index = next((j for j in range(value_start, len(tokens)) if tokens[j]["kind"] in
                               ("TK_SEMICOLON", "TK_LBRACE", "TK_RBRACE")), None)
            while semi_index in self.pairs and tokens[semi_index]["kind"] == "TK_LBRACE":
                semi_index = self.pairs[semi_index] + 1
                if semi_index >= len(tokens): semi_index = None
            if semi_index is None or tokens[semi_index]["kind"] != "TK_SEMICOLON":
                continue
            semicolon = tokens[semi_index]
            owner = self.owner(token["start"])
            if token["kind"] not in ("TK_LET", "TK_AUTO"):
                value_type = declared_type
            elif value_start < semi_index and tokens[value_start]["kind"] == "TK_ASSIGN":
                value_type = self.infer_type(value_start + 1, semi_index, token["start"])
            else:
                continue
            if value_type != "void":
                self.variables.append({"name": name, "type": value_type, "start": scope[0], "end": scope[1],
                                       "visible": semicolon["end"], "owner": owner, "scope": scope[0]})

    @staticmethod
    def function_insert(name, params):
        arguments = ", ".join("${%d:%s}" % (i+1, p["name"]) for i, p in enumerate(params))
        return name + "(" + arguments + ")"

    def read_function(self, start, limit, method=False):
        tokens = self.tokens
        token = tokens[start]
        legacy = token["kind"] == "TK_FN"
        if legacy:
            result, pos = ("void" if method or token["start"] in self.method_scopes else "int"), start + 1
        elif token["text"] in DECLARED_TYPES or token["text"] in self.members:
            result, pos = self.read_type(start, limit)
        else:
            return None
        if pos + 1 >= limit or tokens[pos]["kind"] != "TK_IDENT" or tokens[pos+1]["kind"] != "TK_LPAREN":
            return None
        name = tokens[pos]
        close = self.pairs.get(pos + 1)
        if close is None:
            return None
        j, params = pos + 2, []
        while j < close:
            kind, ref = "int", False
            type_first = not legacy or (tokens[j]["text"] in DECLARED_TYPES and
                                         j+1 < close and tokens[j+1]["text"] not in (":", ","))
            if type_first:
                kind, j = self.read_type(j, close)
                if j < close and tokens[j]["kind"] == "TK_AMP":
                    ref, j = True, j+1
            if j >= close or tokens[j]["kind"] != "TK_IDENT":
                return None
            param = tokens[j]
            j += 1
            if not type_first and j+1 < close and tokens[j]["kind"] == "TK_COLON":
                j += 1
                if tokens[j]["kind"] == "TK_AMP":
                    ref, j = True, j+1
                if j >= close:
                    return None
                kind, j = self.read_type(j, close)
            params.append({"name": param["text"], "token": param, "type": kind, "ref": ref})
            if j < close:
                if tokens[j]["kind"] != "TK_COMMA":
                    return None
                j += 1
        opening = close + 1
        if legacy and opening+1 < limit and tokens[opening]["kind"] == "TK_ARROW":
            result, opening = self.read_type(opening+1, limit)
        if opening >= limit or tokens[opening]["kind"] != "TK_LBRACE":
            return None
        end = self.pairs.get(opening, limit)
        detail = self.source[token["start"]:tokens[opening]["start"]].strip()
        if legacy and "->" not in detail:
            detail += " -> " + result
        return name, opening, end, result, params, legacy, detail

    def read_type(self, start, end):
        tokens = self.tokens
        name = tokens[start]["text"]
        if name == "string": name = "char[]"
        pos = start + 1
        if name in (MAP_TYPES | SET_TYPES | {"mint", "pair"}) and pos + 1 < end and tokens[pos]["text"] == "<":
            params = []
            cursor = pos + 1
            while cursor < end:
                argument, cursor = self.read_type(cursor, end)
                params.append(argument)
                if cursor == end or tokens[cursor]["text"] != ",":
                    break
                cursor += 1
            if cursor < end and tokens[cursor]["text"] == ">":
                name += "<" + ",".join(params) + ">"
                pos = cursor + 1
        while pos + 1 < end and tokens[pos]["kind"] == "TK_LBRACKET" and tokens[pos+1]["kind"] == "TK_RBRACKET":
            name += "[]"
            pos += 2
        return name, pos

    @staticmethod
    def member(name, kind, detail, insert, result):
        return {"label": name, "kind": kind, "detail": detail, "insert": insert,
                "documentation": detail, "priority": "0", "result": result}

    @staticmethod
    def container_parts(receiver):
        outer, sep, parts = receiver.partition("<")
        if not sep or not receiver.endswith(">") or outer not in MAP_TYPES | SET_TYPES:
            return None
        key, _, value = parts[:-1].partition(",")
        return outer, key, value

    def member_items(self, receiver):
        if receiver in self.members:
            return {k: v.copy() for k, v in self.members[receiver].items()}
        if receiver in MOD_TYPES - {"mint"} or re.fullmatch(r"mint<[0-9]+>", receiver):
            return {"val": self.member("val", 2, "val() -> int", "val()", "int"),
                    "pow": self.member("pow", 2, "pow(exponent: int)", "pow(${1:exponent})", receiver)}
        parts = self.container_parts(receiver)
        if parts:
            outer, key, value = parts
            insert = "insert(${1:key}, ${2:value})" if value else "insert(${1:key})"
            members = {"keys": self.member("keys", 2, f"keys() -> {key}[]", "keys()", key+"[]"),
                       "size": self.member("size", 2, "size() -> int", "size()", "int"),
                       "insert": self.member("insert", 2, "insert(key" + (", value" if value else "") + ")", insert, "void")}
            for name in ("count", "contains", "erase"):
                members[name] = self.member(name, 2, f"{name}(key: {key}) -> int", name + "(${1:key})", "int")
            return members
        return method_items(receiver)

    def expression_start(self, end):
        if end <= 0:
            return 0
        i = end - 1
        token = self.tokens[i]
        if token["kind"] in ("TK_RPAREN", "TK_RBRACKET"):
            opening = self.reverse_pairs.get(i)
            if opening is None:
                return i
            if opening > 0 and self.tokens[opening - 1]["kind"] in ("TK_IDENT", "TK_RPAREN", "TK_RBRACKET"):
                return self.expression_start(opening)
            return opening
        if token["kind"] == "TK_IDENT" and i > 0 and self.tokens[i - 1]["kind"] == "TK_DOT":
            return self.expression_start(i - 1)
        return i

    def receiver_type(self, dot, offset):
        return self.infer_type(self.expression_start(dot), dot, offset)

    def infer_type(self, begin, end, offset):
        tokens = self.tokens
        if begin >= end:
            return "int"
        value, pos = tokens[begin], begin + 1
        result = "int"
        if value["kind"] == "TK_CHAR":
            result = "char"
        elif value["kind"] == "TK_STRING":
            result = "char[]"
        elif value["kind"] == "TK_LBRACKET":
            close = self.pairs.get(begin)
            if close is None or close >= end:
                return "int[]"
            element, start, j = "int", begin + 1, begin + 1
            while j <= close:
                if j == close or tokens[j]["kind"] == "TK_COMMA":
                    current = self.infer_type(start, j, offset)
                    if start < j and current.count("[]") >= element.count("[]"):
                        element = current
                    start = j + 1
                elif tokens[j]["kind"] in ("TK_LPAREN", "TK_LBRACKET"):
                    j = self.pairs.get(j, j)
                j += 1
            result, pos = element + "[]", close + 1
        elif value["kind"] == "TK_LPAREN" and begin in self.pairs:
            close = self.pairs[begin]
            result, pos = self.infer_type(begin + 1, close, offset), close + 1
        elif value["kind"] == "TK_IDENT":
            if pos < end and tokens[pos]["kind"] == "TK_LPAREN" and pos in self.pairs:
                if value["text"] in ("array", "read_vec"):
                    result = "int[]"
                elif value["text"] in ("read_char", "read_string"):
                    result = "char" if value["text"] == "read_char" else "char[]"
                else:
                    result = next((f["return"] for f in self.functions if f["name"] == value["text"]), "int")
                pos = self.pairs[pos] + 1
            elif pos in self.pairs and tokens[pos]["kind"] == "TK_LBRACE" and value["text"] in self.members:
                result, pos = value["text"], self.pairs[pos] + 1
            else:
                result = self.visible(offset).get(value["text"], {}).get("type", "int")
        while pos < end:
            if tokens[pos]["kind"] == "TK_LBRACKET" and pos in self.pairs:
                parts = self.container_parts(result)
                if parts:
                    outer, _, value = parts
                    result = (value + ("[]" if "multi" in outer else "")) if value else "int"
                else:
                    result = "edge[]" if result in ("graph", "digraph") else result[:-2] if result.endswith("[]") else "int"
                pos = self.pairs[pos] + 1
            elif (tokens[pos]["kind"] == "TK_DOT" and pos + 2 < end and
                  tokens[pos + 2]["kind"] == "TK_LPAREN" and pos + 2 in self.pairs):
                method = tokens[pos + 1]["text"]
                members = self.member_items(result)
                if method in members and "result" in members[method]:
                    result = members[method]["result"]
                elif method in ("pop", "back"):
                    result = result[:-2] if result.endswith("[]") else "int"
                elif result == "int[]" and method in INT_VECTOR_METHODS:
                    result = INT_VECTOR_METHODS[method][0].split(" -> ")[1]
                elif method == "size":
                    result = "int"
                else:
                    result = "void"
                pos = self.pairs[pos + 2] + 1
            elif tokens[pos]["kind"] == "TK_DOT" and pos+1 < end:
                result = self.members.get(result, {}).get(tokens[pos+1]["text"], {}).get("result", "int")
                pos += 2
            else:
                return "int"
        return result

    def owner(self, offset):
        for function in self.functions:
            if function["body"] and function["body"][0] < offset <= function["body"][1]:
                return function["owner"]
        return None

    def visible(self, offset):
        candidates = [v for v in self.variables if v["owner"] == self.owner(offset) and
                      v["start"] <= offset <= v["end"] and v["visible"] <= offset]
        result = {}
        for item in sorted(candidates, key=lambda v: (v["scope"], v["visible"])):
            result[item["name"]] = item
        return result


class LanguageService:
    def __init__(self):
        self.compiler = build_compiler()
        self.workers = threading.BoundedSemaphore(2)

    def _run(self, source, mode):
        if not isinstance(source, str):
            raise ValueError("source must be a string")
        encoded = source.encode("utf-8", errors="replace")
        if len(encoded) > MAX_SOURCE_BYTES:
            # ponytail: bounded local documents; raise after profiling larger programs.
            return {"ok": False, "diagnostics": [diagnostic(source, "ソースは 256 KiB 以内にしてください。")]}
        flags = ["--json-diagnostics"]
        if mode:
            flags.append(mode)
        if not self.workers.acquire(timeout=1):
            raise RuntimeError("変換器が使用中です。少し待って再試行してください。")
        try:
            result = subprocess.run([str(self.compiler), *flags, "-"], input=encoded,
                                    capture_output=True, timeout=TIMEOUT_SECONDS)
        except subprocess.TimeoutExpired:
            return {"ok": False, "diagnostics": [diagnostic(source, "解析がタイムアウトしました。式や入れ子を短くしてください。")]}
        finally:
            self.workers.release()
        if result.returncode == 0:
            return {"ok": True, "output": result.stdout.decode("utf-8"), "diagnostics": []}
        try:
            error = json.loads(result.stderr.decode("utf-8"))
            mapping = byte_index(source)
            start_byte = min(max(0, error["offset"]), len(mapping) - 1)
            end_byte = min(start_byte + error["length"], len(mapping) - 1)
            start, end = mapping[start_byte], mapping[end_byte]
            if error["length"] and end == start and start < len(source):
                end = start + 1
            item = diagnostic(source, error["message"], start, end)
        except (ValueError, KeyError, TypeError):
            item = diagnostic(source, "変換器が解析を完了できませんでした。")
        return {"ok": False, "diagnostics": [item]}

    @lru_cache(maxsize=16)
    def diagnostics(self, source):
        return self._run(source, "--check")["diagnostics"]

    @lru_cache(maxsize=8)
    def index(self, source):
        result = self._run(source, "--tokens")
        return DocumentIndex(source, json.loads(result["output"]) if result["ok"] else [])

    def execution_plan(self, source):
        result = self._run(source, "--plan")
        return {"plan": json.loads(result["output"]) if result["ok"] else None,
                "diagnostics": result["diagnostics"]}

    def compile(self, source):
        result = self._run(source, None)
        return {"cpp": result.get("output", "") if result["ok"] else "",
                "diagnostics": result["diagnostics"]}

    def completions(self, source, position, snippets=True):
        offset = offset_at(source, position)
        line_start = source.rfind("\n", 0, offset) + 1
        quoted_or_comment = r""" "(?:\\.|[^"\\])*" | '(?:\\.|[^'\\])*' | // """
        if any(m[0] == "//" for m in re.finditer(quoted_or_comment, source[line_start:offset], re.VERBOSE)):
            return {"isIncomplete": False, "items": []}
        start, end = offset, offset
        while start > line_start and re.match(r"[A-Za-z0-9_]", source[start - 1]):
            start -= 1
        while end < len(source) and re.match(r"[A-Za-z0-9_]", source[end]):
            end += 1
        prefix = source[start:offset].lower()
        edit_range = {"start": position_at(source, start), "end": position_at(source, end)}
        index = self.index(source)
        items = {}
        for name, (kind, detail, insert, doc) in CATALOG.items():
            if name == "break" and not any(start <= offset <= end for start, end in index.loop_ranges):
                continue
            if name == "return" and index.owner(offset) is None:
                continue
            if name == "fn" and index.owner(offset) is not None:
                continue
            items[name] = {"label": name, "kind": kind, "detail": detail, "insert": insert,
                           "documentation": doc, "priority": "3" if name == "let" else "2"}
        for name in index.members.keys() - {"graph", "digraph", "edge", "pair<int,int>", "<tree diameter>"}:
            items[name] = {"label": name, "kind": 22, "detail": "struct " + name, "insert": name,
                           "documentation": "ユーザー定義構造体", "priority": "1"}
        for function in index.functions:
            if function["owner"] in index.method_scopes: continue
            insert = index.function_insert(function["name"], function["params"])
            items[function["name"]] = {"label": function["name"], "kind": 3, "detail": function["detail"],
                                      "insert": insert,
                                      "documentation": "ユーザー定義関数。引数は左から右に評価されます。", "priority": "1"}
        for name, variable in index.visible(offset).items():
            items[name] = {"label": name, "kind": 6, "detail": variable["type"] + " " + name,
                           "insert": name, "documentation": "この位置で参照できる変数です。", "priority": "0"}
        before = [i for i, token in enumerate(index.tokens) if token["end"] <= start]
        if before and index.tokens[before[-1]]["kind"] == "TK_DOT":
            items = index.member_items(index.receiver_type(before[-1], offset))
        result = []
        for name, item in items.items():
            if not name.lower().startswith(prefix):
                continue
            insert = item.pop("insert")
            if (item["kind"] in (2, 3) or name == "rep") and source[end:].lstrip().startswith("("):
                insert = name
            item["sortText"] = item.pop("priority") + name
            item["insertTextFormat"] = 2 if snippets else 1
            if not snippets:
                insert = plain_snippet(insert)
            item["textEdit"] = {"range": edit_range, "newText": insert}
            result.append(item)
        expansion = self.binding_expansion(index, offset, line_start)
        if expansion:
            result.append(expansion)
        return {"isIncomplete": False, "items": sorted(result, key=lambda item: item["sortText"])}

    def binding_expansion(self, index, offset, line_start):
        tokens = index.tokens
        last = next((i for i in range(len(tokens)-1, -1, -1)
                     if line_start <= tokens[i]["start"] and tokens[i]["end"] == offset), None)
        if last is None:
            return None
        end = last if tokens[last]["kind"] == "TK_SEMICOLON" else last + 1
        following = tokens[last+1] if last + 1 < len(tokens) else None
        if end > last and following and following["kind"] != "TK_SEMICOLON":
            if "\n" not in index.source[offset:following["start"]]:
                return None
        # Skip paired expressions while looking for the start of this statement.
        declaration = end - 1
        while declaration >= 0 and tokens[declaration]["start"] >= line_start:
            token = tokens[declaration]
            if token["kind"] == "TK_AUTO":
                break
            if token["kind"] in ("TK_SEMICOLON", "TK_LBRACE"):
                return None
            declaration = index.reverse_pairs.get(declaration, declaration) - 1
        else:
            return None
        if declaration + 3 >= end:
            return None
        keyword, variable, assign = tokens[declaration:declaration+3]
        if variable["kind"] != "TK_IDENT" or assign["kind"] != "TK_ASSIGN":
            return None
        value_type = index.infer_type(declaration+3, end, keyword["start"])
        names = [name for name, field in index.members.get(value_type, {}).items() if field["kind"] == 5]
        if not names:
            return None
        used = set(index.visible(keyword["start"]))
        prefix = "diameter" if value_type == "<tree diameter>" else variable["text"]
        bindings = []
        for name in names:
            base = prefix + "_" + name if value_type == "<tree diameter>" or name in used else name
            name = base
            suffix = 2
            while name in used:
                name = f"{base}_{suffix}"
                suffix += 1
            used.add(name)
            bindings.append(name)
        pattern = "[" + ", ".join(bindings) + "]"
        original = index.source[keyword["start"]:offset]
        initializer = index.source[variable["end"]:offset]
        if end > last and (not following or following["kind"] != "TK_SEMICOLON"):
            initializer += ";"
        return {"label": "分解して展開 " + pattern, "kind": 15, "detail": "構造体を分解する auto 宣言に置き換えます。",
                "sortText": "!bindings", "filterText": original, "insertTextFormat": 1,
                "textEdit": {"range": {"start": position_at(index.source, keyword["start"]),
                                       "end": position_at(index.source, offset)},
                             "newText": "auto " + pattern + initializer}}

    def hover(self, source, position):
        offset = offset_at(source, position)
        start, end = offset, offset
        while start > 0 and re.match(r"[A-Za-z0-9_]", source[start - 1]):
            start -= 1
        while end < len(source) and re.match(r"[A-Za-z0-9_]", source[end]):
            end += 1
        name = source[start:end]
        index = self.index(source)
        visible = index.visible(offset)
        if name in visible:
            value = visible[name]["type"] + " " + name
        elif name in CATALOG:
            value = CATALOG[name][1] + "\n\n" + CATALOG[name][3]
        else:
            value = next((f["detail"] for f in index.functions if f["name"] == name), None)
        token_index = next((i for i, token in enumerate(index.tokens) if token["start"] == start), None)
        if token_index and index.tokens[token_index - 1]["kind"] == "TK_DOT":
            methods = index.member_items(index.receiver_type(token_index - 1, offset))
            method = methods.get(name)
            value = method["detail"] + "\n\n" + method["documentation"] if method else None
        if value is None:
            return None
        return {"contents": {"kind": "plaintext", "value": value},
                "range": {"start": position_at(source, start), "end": position_at(source, end)}}
