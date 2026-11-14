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

    def run_source(self, source):
        env = dict(os.environ)
        env["CXXFLAGS"] = env.get("CXXFLAGS", "") + " " + shlex.join(SANITIZERS)
        return subprocess.run(
            [str(CLI), "--code", source, "--run"],
            capture_output=True, text=True, timeout=90, env=env,
        )

    def assert_output(self, source, expected):
        result = self.run_source(source)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, expected)

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
            command = shlex.split(os.environ.get("CXX", "c++"))
            includes = ["-I", str(ROOT)]
            for path in (os.environ.get("FKEN_BOOST_INCLUDE"),
                         ROOT / ".deps" / "boost" / "usr" / "include"):
                if path:
                    includes += ["-I", str(path)]
            built = subprocess.run(
                command + ["-std=c++20", *SANITIZERS, *includes, str(cpp),
                           "-o", str(work / "program")],
                capture_output=True, text=True, timeout=60,
            )
            self.assertEqual(built.returncode, 0, built.stderr)
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


if __name__ == "__main__":
    unittest.main(verbosity=2)
