#!/usr/bin/env python3
"""Local C++20 backend, automatic integer promotion, and CLI regression tests."""
import math
import os
from pathlib import Path
import random
import shlex
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parent
CLI = ROOT / "fken_cpl"
SANITIZERS = ["-O0", "-fsanitize=undefined", "-fno-sanitize-recover=undefined"]


class CppE2E(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        result = subprocess.run(
            [str(CLI), "--code", ""], capture_output=True, text=True, timeout=30
        )
        if result.returncode:
            raise RuntimeError(result.stderr)
        cls.compiler = ROOT / ".build" / "compiler"

    def run_source(self, source, input_text=None):
        env = dict(os.environ)
        env["CXXFLAGS"] = env.get("CXXFLAGS", "") + " " + shlex.join(SANITIZERS)
        return subprocess.run(
            [str(CLI), "--code", source, "--run"],
            capture_output=True, text=True, timeout=90, env=env, input=input_text,
        )

    def assert_output(self, source, expected, input_text=None):
        result = self.run_source(source, input_text)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, expected)

    def compile_cpp(self, cpp, binary):
        command = shlex.split(os.environ.get("CXX", "c++"))
        includes = ["-I", str(ROOT)]
        for path in (os.environ.get("FKEN_BOOST_INCLUDE"),
                     ROOT / ".deps" / "boost" / "usr" / "include"):
            if path:
                includes += ["-I", str(path)]
        built = subprocess.run(
            command + ["-std=c++20", *SANITIZERS, *includes, str(cpp), "-o", str(binary)],
            capture_output=True, text=True, timeout=60,
        )
        self.assertEqual(built.returncode, 0, built.stderr)

    def test_submission_arithmetic_graphs_and_associative_containers(self):
        source = r"""
// Preserve a source comment with a C++ line-continuation marker: \
int twice(int a) { return a + a; }
void add(map<string,mint<13>>& scores) { scores["cat"] += 4; }
map<string,mint<13>> scores;
add(scores);
print(scores["cat"].val());
mint<998244353> a = -2;
mint998 b = a.pow(2) / 2;
b++;
print(b.val()); print(twice(gcd(12, -16))); print(-17 % 5); print(pow_mod(-2,3,11));
graph G(4);
G.add(0,1,5); G.add(1,2,2); G.add(0,2,8);
print(G.bfs(0)); print(G.dijkstra(0)); print(G.components());
unordered_map<int,string> u; u[9223372036854775808] = "large"; print(u[9223372036854775808]);
multi_map<int,int> m; m.insert(2,5); m.insert(2,6); print(m[2]);
unordered_multi_map<char,char> c; c.insert('k','x'); c.insert('k','y'); print(c['k']);
set<int> s; s.insert(5); s.insert(1); print(s.keys());
unordered_set<int> us; us.insert(5); print(us[5]);
multi_set<int> ms; ms.insert(6); ms.insert(6); print(ms[6]);
unordered_multi_set<char> ums; ums.insert('a'); ums.insert('a'); print(ums.erase('a'));
"""
        result = subprocess.run([str(CLI), "--code", source], capture_output=True, text=True, check=True)
        self.assertTrue(result.stdout.startswith("// fken_cpl: https://github.com/fken57/strong_cp_lang\n"))
        self.assertIn("// int twice(int a) { return a + a; }", result.stdout)
        self.assert_output(source, "4\n3\n8\n-2\n3\n0 1 1 -1\n0 5 7 -1\n0 0 0 1\nlarge\n5 6\nxy\n1 5\n1\n2\n2\n")

    def test_char_string_and_string_vectors(self):
        self.assert_output(r"""
string s = "かな";
s.push_back('!');
char[] t = ['a', 'b'];
t[1] = 'x';
t += "word";
char[][] words = [s, t];
words[1].push_back('?');
words.push("new");
cout << words[0] << ' ' << words[1] << '\n';
print(words);
print(s.size());
print(t[0] == 'a');
char c;
string word;
cin >> c >> word;
cout << c << ' ' << word << '\n';
for (char ch : "ab") { cout << ch; }
""", "かな! axword?\nかな! axword? new\n7\n1\na bc\nab", "abc\n")

    def test_structured_binding_and_tree_diameter(self):
        self.assert_output("""
struct Sample { int count; int[] data; }
fn make(calls: &int) -> Sample {
    calls++;
    return Sample{count: 9, data: [4, 5]};
}
int calls;
auto [count, data] = make(calls);
print(calls); print(count); print(data);
auto [value, times] = [6,6,6].run_length()[0];
print(value); print(times);
graph G(3);
G.add(0, 1, 9223372036854775808);
G.add(1, 2, 8);
auto cnt = G.diameter;
auto [d, a, b] = G.diameter;
print(cnt.dist); print(d); print(a); print(b);
graph singleton(1);
auto [zero, first, last] = singleton.diameter;
print(zero); print(first); print(last);
""", "1\n9\n4 5\n6\n3\n9223372036854775816\n9223372036854775816\n2\n0\n0\n0\n0\n")

    def test_array_boundaries_and_run_length(self):
        self.assert_output("""
int[] a = [3, 1, 3, 9223372036854775808, 3];
pair<int,int>[] runs = a.run_length();
for (pair<int,int> run : runs) { cout << run.first << ' ' << run.second << '\\n'; }
a.sort();
cout << a.at_least(3) << ' ' << a.more_than(3) << ' '
     << a.at_most(3) << ' ' << a.less_than(3) << '\\n';
cout << a.at_least(9223372036854775809) << ' ' << a.more_than(9223372036854775808)
     << ' ' << a.at_most(0) << ' ' << a.less_than(1) << '\\n';
int[] empty;
cout << empty.at_least(0) << ' ' << empty.more_than(0) << ' '
     << empty.at_most(0) << ' ' << empty.less_than(0) << ' ' << empty.run_length().size() << '\\n';
""", "3 1\n1 1\n3 1\n9223372036854775808 1\n3 1\n1 4 3 0\n5 5 -1 -1\n0 0 -1 -1 0\n")

    def test_record_graph_streams(self):
        self.assert_output((ROOT / "example_graph.fken").read_text(), "2 11\n3 9223372036854775809\n",
                           "3 2   1 2 10   1 3 9223372036854775808")
        self.assert_output("""
struct P {
    int x;
    fn grow(a: &P[]) { modify(self, a); }
}
fn modify(p: &P, a: &P[]) -> void { a.resize(12); p.x++; }
P[] a(1);
a[0].grow(a);
P p = a[0]; p.x++;
graph G(2);
G.add(0,1); G.add(1,1); G.add(0,1,-2);
digraph D(2); D.add(0,1);
print(p.x); print(a[0].x); print(G.edge_count()); print(G[1].size());
for(edge e : G[1]) { print(e.id); }
print(D[1].size());
""", "2\n1\n3\n4\n0\n1\n1\n2\n0\n")

    def test_functions_recursion_and_forward_calls(self):
        source = r'''
print(add(7, 8));
print(fact(100));
print(fib(12));
print(even(10));
print(odd(10));
print(even(9));
print(encode(1, 2, 3));
print(class(3));
print(main());
print(branch(0));
print(branch(1));
fn add(a, b) { return a + b; }
fn fact(n: int) -> int {
    if (n <= 1) { return 1; }
    return n * fact(n - 1);
}
fn fib(n) {
    if (n <= 1) { return n; }
    return fib(n - 1) + fib(n - 2);
}
fn even(n) { if (n == 0) { return 1; } return odd(n - 1); }
fn odd(n) { if (n == 0) { return 0; } return even(n - 1); }
fn encode(a, b, c) { return a * 100 + b * 10 + c; }
fn class(template) { return template + 1; }
fn main() { return 77; }
fn branch(n) { if (n) { return 11; } else { return 22; } }
'''
        self.assert_output(source, f"15\n{math.factorial(100)}\n144\n1\n0\n0\n123\n4\n77\n22\n11\n")

    def test_function_arrays_references_and_void(self):
        source = r'''
fn sum(a: int[]) {
    let total = 0;
    rep(i, 0, len(a)) { total += a[i]; }
    return total;
}
fn mutate_copy(a: int[]) -> int[] { a[0] += 10; return a; }
fn shift(a: &int[], delta) -> void { rep(i, 0, len(a)) { a[i] += delta; } }
fn nested(a: &int[]) -> void { shift(a, 2); }
fn fill(a: &int[], n, value) -> void { a = array(n, value); }
fn bump(x: &int) -> void { x++; return; }
fn swap(a: &int, b: &int) -> void { let tmp = a; a = b; b = tmp; }
fn make(n) -> int[] {
    let a = array(n);
    rep(i, 0, n) { a[i] = i * i; }
    return a;
}
fn empty() -> int[] { return []; }
fn early_stop(a: &int[]) -> void { if (len(a) == 0) { return; } a[0] = -1; }
let a = [1, 2, 3];
print(mutate_copy(a));
print(a);
shift(a, 5);
print(sum(a));
nested(a);
print(a);
fill(a, 2, 9);
print(a);
let x = 9223372036854775807;
bump(x);
print(x);
let y = 3;
swap(x, y);
print(x);
print(y);
let z = [];
early_stop(z);
print(len(z));
early_stop(a);
print(a);
print(empty());
print(make(4));
print(make(3)[1]);
'''
        self.assert_output(
            source, "11 2 3\n1 2 3\n21\n8 9 10\n9 9\n9223372036854775808\n"
                    "3\n9223372036854775808\n0\n-1 9\n\n0 1 4 9\n1\n"
        )

    def test_function_call_order_and_for_clauses(self):
        source = r'''
fn encode(a, b, c) { return a * 100 + b * 10 + c; }
fn mark(x: &int, value) { x = value; return value; }
fn mix(a, b) { return a * 10 + b; }
fn mutate(a: &int[], value) { a[0] = value; return value; }
fn copied(a: int[], value) { return a[0] * 10 + value; }
fn referenced(a: &int[], value) { return a[0] * 10 + value; }
fn reset(i: &int) -> void { i = 0; }
fn next(i: &int) -> void { i++; }
fn show(a, b) -> void { print(a); print(b); }
print(encode(read_int(), read_int(), read_int()));
let x = 4;
print(mix(x, mark(x, 7)));
print(x);
let a = [2];
print(copied(a, mutate(a, 8)));
print(a);
print(referenced(a, mutate(a, 9)));
let i = 0;
for (; i < 3; next(i)) { print(i); }
for (reset(i); i < 2; next(i)) { print(i); }
read_int();
print(read_int());
show(read_int(), read_int());
'''
        self.assert_output(source, "123\n47\n7\n28\n8\n99\n0\n1\n2\n0\n1\n5\n6\n7\n",
                           "1 2 3 4 5 6 7")

    def test_function_compile_errors(self):
        cases = [
            "fn", "fn f", "fn f(", "fn f(,) { return 1; }", "fn f(a,) { return a; }",
            "fn f(a b) { return a; }", "fn f()",
            "fn f(a:) { return 1; }", "fn f(a: &) { return 1; }",
            "fn f(a: void) { return 1; }", "fn f(a: float) { return 1; }",
            "fn f(a: int[) { return 1; }", "fn f() -> {}", "fn f() -> &int { return 1; }",
            "fn f() -> float { return 1; }", "fn f() { return 1 }",
            "return;", "return 1;", "{ return 1; }",
            "fn f() {}", "fn f() { print(1); }", "fn f(n) { if (n) { return 1; } }",
            "fn f() { while (1) { return 1; } }", "fn f() { return; }",
            "fn f() { return []; }", "fn f() -> int[] { return 1; }",
            "fn f() -> void { return 1; }",
            "fn f() { return 1; } fn f(a) { return a; }", "fn f(a, a) { return a; }",
            "fn f(a) { let a = 1; return a; }",
            "fn read_int() { return 1; }", "fn array(n) -> int[] { return []; }",
            "fn f() { return missing(); }", "fn f() { return missing; }",
            "fn f(n) { return n; } print(f());", "fn f(n) { return n; } f(1, 2);",
            "fn f(a: int[]) { return len(a); } print(f(1));",
            "fn f(a) { return a; } print(f([]));",
            "fn f() -> void {} let x = f();", "fn f() -> void {} print(f());",
            "fn f() -> void {} print(f() + 1);", "fn f() -> void {} if (f()) {}",
            "fn f() -> void {} let a = [f()];",
            "fn f() -> void {} fn g(n) { return n; } g(f());",
            "fn f() -> void {} fn g() { return f(); }",
            "fn outer() { fn inner() { return 1; } return 1; }",
            "let global = 1; fn f() { return global; }",
            "fn f() { let local = 1; return local; } print(local);",
            "fn f(n) { return n; } print(n);",
            "fn f(x: &int) -> void { x++; } f(1);",
            "fn f(x: &int) -> void { x++; } let a = [1]; f(a[0]);",
            "fn f(x: &int) -> void { x++; } let x = 1; f(x + 1);",
            "fn f(a: &int[]) -> void {} f([]);",
            "fn f(a: &int[]) -> void {} f(array(1));",
        ]
        for source in cases:
            with self.subTest(source=source):
                result = subprocess.run([str(self.compiler), "--code", source],
                                        capture_output=True, text=True, timeout=5)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertEqual(result.stdout, "")
                self.assertRegex(result.stderr, r"\d+:\d+: error:")

    def test_function_runtime_errors(self):
        source = r'''
fn divide(n, d) { return n / d; }
fn element(a: int[], index) { return a[index]; }
fn read_one() { return read_int(); }
let mode = read_int();
if (mode == 0) { print(divide(1, 0)); }
if (mode == 1) { print(element([1], 1)); }
if (mode == 2) { print(read_one()); }
'''
        with tempfile.TemporaryDirectory(prefix="fken-function-errors-") as directory:
            work = Path(directory)
            cpp, binary = work / "program.cpp", work / "program"
            emitted = subprocess.run([str(CLI), "--code", source, "-o", str(cpp)],
                                     capture_output=True, text=True, timeout=30)
            self.assertEqual(emitted.returncode, 0, emitted.stderr)
            self.compile_cpp(cpp, binary)
            for data, message in [("0", "division by zero"), ("1", "array index"),
                                  ("2", "expected integer input")]:
                with self.subTest(input=data):
                    result = subprocess.run([str(binary)], input=data, capture_output=True,
                                            text=True, timeout=5)
                    self.assertEqual(result.returncode, 1, result.stderr)
                    self.assertIn("runtime error: " + message, result.stderr)
                    self.assertNotIn("UndefinedBehaviorSanitizer", result.stderr)

    def test_integer_input_and_arrays(self):
        values = [(1 << 63) - 1, 1, -3, 10**80, -(10**70)]
        source = r'''
let n = read_int();
let a = read_vec(n);
let total = 0;
for (let i = 0; i < n; i++) { total += a[i]; }
print(total);
print(a);
let b = a;
b[0] += 10;
print(a[0]);
print(b[0]);
let z = array(3);
z[1] = 7;
z[0]++;
z[1] += 2;
z[2]--;
print(z);
let filled = array(2, -5);
print(filled);
filled = [1, 2, 3];
print(len(filled));
print(filled);
print([4, 5, 6][1] - [1][0]);
print(len([]));
print(array(0));
let grid = array(4);
for (let row = 0; row < 2; row++) {
    for (let col = 0; col < 2; col++) {
        grid[row * 2 + col] = row * 10 + col;
    }
}
print(grid);
'''
        expected = (
            f"{sum(values)}\n{' '.join(map(str, values))}\n{values[0]}\n{values[0] + 10}\n"
            "1 9 -1\n-5 -5\n3\n1 2 3\n4\n0\n\n0 1 10 11\n"
        )
        # Whitespace, signs, leading zeroes, and huge input all use decimal semantics.
        data = f"+0005\n{values[0]} +01 -03 {values[3]}\t{values[4]}\n"
        self.assert_output(source, expected, data)

    def test_c_style_for_and_updates(self):
        source = r'''
let outside = 8;
for (let outside = 0; outside < 3; outside++) { print(outside); }
print(outside);
for (let i = 4; i > 0; i -= 2) { print(i); }
for (let i = 0; i < 5; i += 2) { print(i); }
for (let i = 1; i < 10; i *= 2) { print(i); }
for (let i = 0; i < 2; ++i) { print(i); }
for (let i = 2; i > 0; --i) { print(i); }
let j = 0;
for (; j < 2;) { print(j); j += 1; }
for (j = 0; j < 4; j = j + 2) { print(j); }
print(j);
let limit = 5;
for (let i = 0; i < limit; i++) { print(i); limit -= 2; }
for (let i = 9; i < 0; i--) { print(999); }
for (let i = 9223372036854775806; i <= 9223372036854775808; i++) { print(i); }
let n = 9223372036854775807;
n++;
n += 2;
n -= 1;
n *= 2;
n /= 2;
n--;
print(n);
let lo = -9223372036854775808;
lo--;
print(lo);
lo += 1;
lo /= -1;
print(lo);
let a = [0];
for (; a[0] < 3; a[0]++) { print(a[0]); }
++a[0];
a[0] *= 2;
a[0] /= 4;
print(a);
'''
        expected = [
            0, 1, 2, 8, 4, 2, 0, 2, 4, 1, 2, 4, 8, 0, 1, 2, 1, 0, 1, 0, 2, 4,
            0, 1, 9223372036854775806, 9223372036854775807, 9223372036854775808,
            9223372036854775808, -9223372036854775809, 9223372036854775808,
            0, 1, 2, 2,
        ]
        self.assert_output(source, "\n".join(map(str, expected)) + "\n")

    def test_rep_shorthand(self):
        source = r'''
let i = 7;
rep(i, 0, 3) { print(i); }
print(i);
rep(i, -2, 1) { print(i); }
rep(i, 5, 5) { print(999); }
rep(i, 5, 2) { print(999); }
let total = 0;
rep(row, 0, 2) { rep(col, 0, 3) { total += row * 10 + col; } }
print(total);
rep(index, 1, len([1, 2, 3])) { print(index); }
let limit = 5;
rep(i, 0, limit) { print(i); limit -= 2; }
rep(i, 9223372036854775806, 9223372036854775809) { print(i); }
'''
        expected = [
            0, 1, 2, 7, -2, -1, 0, 36, 1, 2, 0, 1,
            9223372036854775806, 9223372036854775807, 9223372036854775808,
        ]
        self.assert_output(source, "\n".join(map(str, expected)) + "\n")

    def test_input_evaluation_order(self):
        source = r'''
print(read_int() - read_int());
print(read_int() / read_int());
print(array(read_int(), read_int()));
print(read_vec(2)[read_int()]);
let a = array(2);
a[read_int()] = read_int();
a[read_int()] += read_int();
print(a);
print([read_int(), read_int()]);
for (let i = read_int(); i < 4; i++) { print(i); }
print(read_vec(read_int()));
'''
        self.assert_output(
            source, "5\n6\n7 7\n20\n42 5\n8 9\n2\n3\n11 12\n",
            "9 4 20 3 2 7 10 20 1 0 42 1 5 8 9 2 2 11 12",
        )

    def test_array_for_and_update_compile_errors(self):
        cases = [
            "rep", "rep() {}", "rep(i, 0) {}", "rep(1, 0, 3) {}",
            "rep(i, 0, 3, 4) {}", "rep(i, [], 3) {}", "rep(i, 0, []) {}",
            "rep(i, 0, 1) {} print(i);",
            "let x = read_int(1);", "let a = read_vec();",
            "let a = array();", "let a = array(1, 2, 3);", "print(len(1));",
            "print(unknown());", "let a = [1]; print(a + 1);", "let x = 1; print(x[0]);",
            "let a = [1]; print(a[[]]);", "let a = [1]; a[0] = [];",
            "let a = []; a = 1;", "let x = 1; x = [];",
            "if ([]) {}", "while ([]) {}", "let a = [1,];", "let a = [1;",
            "print(read_int(,));", "let a = array([)]);",
            "for {}", "for () {}", "for (let i = 0; i < 3) {}", "for (let i = 0; i < 3; i++",
            "for (let i = 0; []; i++) {}", "for (let i = 0; i < 3; let j = 1) {}",
            "for (let i = 0; i < 1; i++) {} print(i);",
            "for (let i = 0; i < 1; missing++) {}", "for (let i = 0; i < 1; i++) {} i++;",
            "let a = []; a++;", "let a = []; a += [];", "let i = 1; i += [];",
            "missing++;", "let a = [1]; a[]++;", "let i = 0; i +=;",
            "let i = 0; i+++;", "let x = 0; print(x++);",
            "for (let i = 0; i < 2; body++) { let body = 1; }",
        ]
        for source in cases:
            with self.subTest(source=source):
                result = subprocess.run([str(self.compiler), "--code", source],
                                        capture_output=True, text=True, timeout=5)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertEqual(result.stdout, "")
                self.assertRegex(result.stderr, r"\d+:\d+: error:")

    def test_input_and_array_runtime_errors(self):
        source = r'''
let mode = read_int();
if (mode == 0) { print(len(array(read_int()))); }
if (mode == 1) { let a = [1]; print(a[read_int()]); }
if (mode == 2) { let a = [0]; a[read_int()] += read_int(); print(a); }
if (mode == 3) { print(read_vec(2)); }
if (mode == 4) { print(read_int()); }
if (mode == 5) { let a = [1]; a[0] /= 0; }
if (mode == 6) { for (;;) { print(1 / 0); } }
'''
        cases = [
            ("0 -1", "array size"), ("0 9223372036854775808", "array size"),
            ("1 -1", "array index"), ("1 1", "array index"),
            ("1 1000000000000000000000000000000000", "array index"),
            ("2 1", "array index"), ("3 1", "expected integer input"),
            ("4 1x", "invalid integer"), ("4 -", "invalid integer"),
            ("4 +", "invalid integer"), ("4", "expected integer input"),
            ("5", "division by zero"), ("6", "division by zero"),
        ]
        with tempfile.TemporaryDirectory(prefix="fken-errors-") as directory:
            work = Path(directory)
            cpp, binary = work / "program.cpp", work / "program"
            emitted = subprocess.run([str(CLI), "--code", source, "-o", str(cpp)],
                                     capture_output=True, text=True, timeout=30)
            self.assertEqual(emitted.returncode, 0, emitted.stderr)
            self.compile_cpp(cpp, binary)
            for data, message in cases:
                with self.subTest(input=data):
                    result = subprocess.run([str(binary)], input=data, capture_output=True,
                                            text=True, timeout=5)
                    self.assertEqual(result.returncode, 1, result.stderr)
                    self.assertIn("runtime error: " + message, result.stderr)
                    self.assertNotIn("UndefinedBehaviorSanitizer", result.stderr)

    def test_arithmetic_matches_python(self):
        lo, hi = -(1 << 63), (1 << 63) - 1
        pairs = [
            (hi, 1), (lo, -1), (lo, 1), (hi, hi), (lo, lo),
            (1 << 32, 1 << 32), (0, 10**100), (10**100, -3),
        ]
        rng = random.Random(571)
        for _ in range(32):
            a = rng.getrandbits(rng.choice([16, 63, 64, 150, 256]))
            b = rng.getrandbits(rng.choice([16, 63, 64, 150, 256])) or 1
            pairs.append((a if rng.randrange(2) else -a, b if rng.randrange(2) else -b))
        expressions, expected = [], []
        for a, b in pairs:
            values = [
                ("+", a + b), ("-", a - b), ("*", a * b),
                ("==", int(a == b)), ("!=", int(a != b)),
                ("<", int(a < b)), ("<=", int(a <= b)),
                (">", int(a > b)), (">=", int(a >= b)),
            ]
            if b:
                quotient = abs(a) // abs(b)
                if (a < 0) != (b < 0):
                    quotient = -quotient
                values.append(("/", quotient))
            for op, value in values:
                expressions.append(f"print(({a}) {op} ({b}));")
                expected.append(str(value))
        expressions.extend([f"print(-({lo}));", f"print(-({10**100}));"])
        expected.extend([str(-lo), str(-(10**100))])
        self.assert_output("\n".join(expressions), "\n".join(expected) + "\n")

    def test_control_flow_scopes_and_literals(self):
        long_name = "v1_" + "x" * 300
        source = f"""
// C++ keyword names and long identifiers are valid.
let class = 7;
let {long_name} = 9;
print(class + {long_name});
let x = 10;
{{
    let x = x + 1;
    print(x);
    {{ x = x + 1; }}
    print(x);
}}
print(x);
let f = 1;
let i = 1;
while (i <= 100) {{ f = f * i; i = i + 1; }}
if (f > 9223372036854775807) {{ print(f); }} else {{ print(0); }}
print(000000000000000000000000000001234);
print(000000100000000000000000000000000000000000009);
if (0) {{ print(1 / 0); }}
if (-1) {{ print(1 + 2 * 3); }}
"""
        self.assert_output(
            source, f"16\n11\n12\n10\n{math.factorial(100)}\n1234\n"
                    "100000000000000000000000000000000000009\n7\n"
        )

    def test_runtime_uses_small_storage_and_promotes(self):
        source = r'''
#include "runtime.hpp"
#include <cassert>
int main() {
    using fken::Int;
    auto hi = Int::parse("9223372036854775807");
    auto lo = Int::parse("-9223372036854775808");
    assert(!hi.is_big() && !lo.is_big());
    assert(!(Int(20) + Int(22)).is_big());
    assert(!(Int(100) * Int(100)).is_big());
    assert((hi + Int(1)).is_big());
    assert((lo - Int(1)).is_big());
    assert((hi * Int(2)).is_big());
    assert((-lo).is_big());
    assert((lo / Int(-1)).is_big());
    assert(!((hi + Int(1)) - Int(1)).is_big());
    assert(Int::parse("00000000000000000000000042") == Int(42));
    assert(Int::parse("0000001000000000000000000000000") ==
           Int::parse("1000000000000000000000000"));
    assert(Int::parse("-0") == Int(0));
    bool rejected = false;
    try { (void)Int::parse("12a"); } catch (const std::invalid_argument &) { rejected = true; }
    assert(rejected);
}
'''
        with tempfile.TemporaryDirectory(prefix="fken-runtime-") as directory:
            work = Path(directory)
            cpp = work / "runtime_test.cpp"
            cpp.write_text(source)
            self.compile_cpp(cpp, work / "program")
            result = subprocess.run([str(work / "program")], capture_output=True,
                                    text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_division_by_zero_is_runtime_error(self):
        result = self.run_source("let x = 100000000000000000000000000; print(x / 0);")
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("runtime error: division by zero", result.stderr)
        self.assertNotIn("runtime error: signed integer overflow", result.stderr)

    def test_invalid_sources_have_diagnostics(self):
        cases = [
            "let", "let a", "let a =", "print", "print();", "if", "while",
            "let a = a;", "let a = 1; let a = 2;", "print(missing);",
            "{ let a = 1; } print(a);", "print(1 +);", "if (1) { print(2);",
            "print(1@2);", "print(1!2);", "print(1\x002);",
        ]
        for source in cases:
            with self.subTest(source=source):
                with tempfile.TemporaryDirectory(prefix="fken-invalid-") as directory:
                    path = Path(directory) / "source.fken"
                    path.write_text(source)
                    result = subprocess.run([str(self.compiler), str(path)],
                                            capture_output=True, text=True, timeout=5)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertEqual(result.stdout, "")
                self.assertRegex(result.stderr, r"\d+:\d+: error:")
        result = subprocess.run(
            [str(self.compiler), "--code", "let a = 1;\nprint(missing);"],
            capture_output=True, text=True, timeout=5,
        )
        self.assertIn("2:7: error:", result.stderr)

    def test_file_stdin_and_failed_output(self):
        with tempfile.TemporaryDirectory(prefix="fken cli ") as directory:
            work = Path(directory)
            path, output = work / "hello world.fken", work / "hello world.cpp"
            path.write_text("print(42);")
            result = subprocess.run([str(CLI), str(path), "-o", str(output)],
                                    capture_output=True, text=True, timeout=30, cwd=work)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, "")
            self.assertIn("int main()", output.read_text())
            self.assertNotIn('#include "runtime.hpp"', output.read_text())
            original = output.read_bytes()
            path.write_text("print(missing);")
            result = subprocess.run([str(CLI), str(path), "-o", str(output)],
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 1)
            self.assertEqual(output.read_bytes(), original)
        result = subprocess.run([str(CLI), "-"], input="print(3);",
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("int main()", result.stdout)

    def test_typed_vectors_and_methods(self):
        self.assert_output(r"""
int zero;
int huge = 9223372036854775807;
huge += 1;
int[] a;
a.push(huge); a.push(20);
print(zero); print(a.size()); print(a.back()); print(a.pop()); print(a);
a.resize(3, 7); print(a);
a.resize(1); print(a); a.clear(); print(a.size());
int n = 2;
int[][] grid(n, 2);
grid[0][1] = 5;
grid[0].push(9);
grid.push([3, 4]);
print(grid); print(grid.back()); print(grid.pop());
print(grid[0][2]); print(len(grid));
int[][] copy = grid;
copy[0][0] = 99;
print(grid[0][0]);
int[][][] cube(1, 2, 3);
cube[0][1][2] = huge;
print(cube[0][1][2]);
int[][][] empty = [];
empty.push([]); empty[0].push([]); empty[0][0].push(42);
print(empty[0][0]);
let legacy = [[1], []];
legacy[1].push(2); print(legacy);
for (int i = 0; i < 2; i++) { a.push(i); }
for (let j = 2; j < 4; j++) { a.push(j); }
print(a);
int[][] alias[1][2]; print(alias[0].size());
int[][] unused(0, 9223372036854775807);
print(unused.size());
""", "0\n2\n20\n20\n9223372036854775808\n9223372036854775808 7 7\n9223372036854775808\n0\n[0 5 9] [0 0] [3 4]\n3 4\n3 4\n9\n2\n0\n9223372036854775808\n42\n[1] [2]\n0 1 2 3\n2\n0\n")

    def test_vector_functions_and_evaluation_order(self):
        self.assert_output(r"""
fn make(n) -> int[][] { int[][] result(n, 2); return result; }
fn grow(g: &int[][], x) -> void { g[0].push(x); }
fn copy(g: int[][]) -> int[][] { g[0].push(99); return g; }
fn next(counter: &int) { counter++; return counter; }
fn push(x) { return x + 1; }
int counter;
int[][] g(next(counter), next(counter));
print(g.size()); print(g[0].size()); print(counter);
grow(g, 7); print(copy(g)); print(g);
print(make(2)[1].size()); print(push(5));
int[][] moved;
moved.push(g.pop()); print(moved); print(g.size());
int[] a = [10, 20];
a.push(a.pop()); print(a);
int[][] b = [[1,2]];
b.push(b[0]); b[0][0] = 3; print(b);
int[][][] inferred = [[], [[4]]];
print(inferred[1][0][0]);
""", "1\n2\n2\n[0 0 7 99]\n[0 0 7]\n2\n6\n[0 0 7]\n0\n10 20\n[3 2] [1 2]\n4\n")

    def test_vector_reallocation_preserves_update_paths(self):
        source = r"""
fn grow(a: &int[]) { a.resize(100000, 7); return 2; }
fn grow_outer(g: &int[][]) { g.resize(10000); return 5; }
fn index(g: &int[][], calls: &int) { calls++; g.resize(10000); return 0; }
int[] a = [1];
a[0] += grow(a); print(a[0]);
int[][] g = [[10]];
g[0].push(grow_outer(g)); print(g[0]);
g[0][0] += grow_outer(g); print(g[0][0]);
g = [[3]];
int calls;
g[index(g, calls)][0] += 2;
print(calls); print(g[0][0]);
g = [[4]];
g[0][index(g, calls)] = 8;
print(calls); print(g[0][0]);
"""
        with patch.dict(os.environ, {"CXXFLAGS": os.environ.get("CXXFLAGS", "") + " -fsanitize=address -fno-omit-frame-pointer"}):
            self.assert_output(source, "3\n10 5\n15\n1\n5\n2\n8\n")

    def test_break_in_for_while_and_rep(self):
        self.assert_output(r"""
fn bump(x: &int) -> void { x++; }
int total;
for (int i = 0; i < 10; i++) { if(i == 3) {break;} total += i; }
print(total);
int i;
while (1) { i++; if (i == 2) { break; } }
print(i);
rep(j, 0, 10) { if (j == 4) {break;} total += j; }
print(total);
int count;
rep(outer, 0, 3) {
    while (1) { count++; break; }
    rep(inner, 0, 3) { count++; break; }
}
print(count);
int updates;
for (;;bump(updates)) { break; }
print(updates);
fn first(a: int[]) { rep(i,0,a.size()) { if(a[i] == 2) {break;} } return 9; }
print(first([1,2,3]));
""", "3\n2\n9\n6\n0\n9\n")

    def test_vector_and_break_compile_errors(self):
        cases = [
            "int x = [];", "int[] a = 1;", "int[][] a = [1];",
            "int[] a(1,2);", "int[][] a(1);", "int x(2);",
            "int[] a([]);", "int[];", "int[2] a;", "int[][] a = [[1],2];",
            "int[] a; a.push();", "int[] a; a.push(1,2);", "int[] a; a.push([]);",
            "int[][] a; a.push(1);", "int[] a; a.pop(1);", "int[] a; a.back(0);",
            "int x; x.push(1);", "int[] a; a.unknown();", "int[] a; a.push;",
            "int[] a; a.resize();", "int[] a; a.resize([]);", "int[] a; a.resize(2,[]);",
            "int[][] a; a.resize(2,1);", "int[] a; int x = a.clear();",
            "int[] a; int x = a.push(1);", "array(2).push(1);",
            "int[] a; a.back().push(1);", "int[][] a; a[0][0] = [];",
            "int[] a; a += [];", "int[][] a; a[0]++;", "int[] a; a[] = 1;",
            "break;", "if(1){break;}", "fn f(){break; return 1;}",
            "while(1){break 1;}", "while(1){break}",
            "for(int i=0;i<1;i++){} print(i);",
        ]
        for source in cases:
            with self.subTest(source=source):
                result = subprocess.run([str(self.compiler), "--code", source], capture_output=True, text=True, timeout=5)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertEqual(result.stdout, "")

    def test_vector_runtime_errors(self):
        source = r"""
fn shrink(a: &int[]) { a.clear(); return 1; }
fn shrink_outer(g: &int[][]) { g.clear(); return 1; }
int mode = read_int();
int[] a;
int[][] g = [[1]];
if(mode == 0){print(a.pop());}
if(mode == 1){print(a.back());}
if(mode == 2){a.resize(-1);}
if(mode == 3){int[][] bad(1, -1);}
if(mode == 4){print(g[1][0]);}
if(mode == 5){a = [1]; a[0] += shrink(a);}
if(mode == 6){g[0].push(shrink_outer(g));}
if(mode == 7){a.resize(999999999999999999999999999999);}
"""
        with tempfile.TemporaryDirectory() as directory:
            cpp, binary = Path(directory)/"main.cpp", Path(directory)/"main"
            generated = subprocess.run([str(CLI), "--code", source], capture_output=True, text=True, timeout=10)
            self.assertEqual(generated.returncode, 0, generated.stderr)
            cpp.write_text(generated.stdout)
            self.compile_cpp(cpp, binary)
            for mode, message in enumerate(("pop from empty", "back of empty", "vector size", "array size", "array index", "array index", "array index", "vector size")):
                with self.subTest(mode=mode):
                    result = subprocess.run([str(binary)], input=str(mode), capture_output=True, text=True, timeout=5)
                    self.assertEqual(result.returncode, 1, result.stderr)
                    self.assertIn(message, result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
