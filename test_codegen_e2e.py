#!/usr/bin/env python3
"""ソース → lex → parse → analyze → generate → アセンブル → 実行。

実行: python3 calc_compile/test_codegen_e2e.py
Linux x86-64 と C コンパイラが必要。実装ファイルは変更しない。
"""
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parent
RUNNER = r'''
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    Source source = {argv[1], strlen(argv[1]), strlen(argv[1]) + 1};
    TokenArray *tokens = lex(&source);
    Program *program = parse(tokens);
    analyze(program);
    generate(program);
    return 0;
}
'''


@unittest.skipUnless(platform.system() == "Linux" and
                     platform.machine() in ("x86_64", "amd64"),
                     "generated assembly requires Linux x86-64")
class CodegenE2E(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="calc-codegen-e2e-")
        cls.failures = []
        cls.artifacts = None
        cls.addClassCleanup(cls.temp.cleanup)
        cls.work = Path(cls.temp.name)
        cls.cc = shlex.split(os.environ.get("CC", "cc"))
        # 既存テストと同じく、共通 .c の重複 include だけ一時ソースで除く。
        # calc4.c はまだ generate を呼んでいないため、上の main で各段階を接続する。
        sources = []
        for name in ("typedef.c", "util.c", "lex.c", "parser.c", "analyze.c", "generate.c"):
            source = (ROOT / name).read_text()
            source = re.sub(r'^\s*#\s*include\s*"(?:typedef|util)\.c"[^\n]*$',
                            '', source, flags=re.MULTILINE)
            sources.append(f'#line 1 "{ROOT / name}"\n' + source)
        combined = cls.work / "compiler.c"
        combined.write_text('\n'.join(sources) + '\n' + RUNNER)
        cls.compiler = cls.work / "compiler"
        result = subprocess.run(cls.cc + [
            "-std=c11", "-Wall", "-Wextra",
            "-Werror=implicit-function-declaration", "-Werror=incompatible-pointer-types",
            str(combined), "-o", str(cls.compiler),
        ], capture_output=True, text=True, timeout=30)
        if result.returncode:
            raise RuntimeError("compiler build failed:\n" + result.stderr)

    def compile_source(self, source):
        return subprocess.run([str(self.compiler), source], capture_output=True,
                              text=True, timeout=3)

    @classmethod
    def tearDownClass(cls):
        if cls.failures:
            print("\n失敗したケース:", flush=True)
            for name, source, stage, directory in cls.failures:
                print(f"  [WA] {name} [{stage}]\n"
                      f"       入力: {source!r}\n"
                      f"       詳細: {directory}", flush=True)

    def check_case(self, name, source, expected=None):
        directory = self.work / f"case-{len(list(self.work.glob('case-*'))) + 1:02d}"
        directory.mkdir()
        (directory / "source.calc").write_text(source)
        if expected is not None:
            (directory / "expected.txt").write_text(expected)
        stage = "コード生成"
        try:
            generated = self.compile_source(source)
            (directory / "compiler.stderr").write_text(generated.stderr)
            assembly = directory / "program.s"
            # デバッグ出力も含め、生成された標準出力をそのまま保存する。
            assembly.write_text(generated.stdout)
            if expected is None:
                stage = "エラー検出"
                self.assertEqual(generated.returncode, 1,
                                 f"expected exit(1), actual exit({generated.returncode})")
                self.assertTrue(generated.stderr.strip(), "error message is missing")
            else:
                self.assertEqual(generated.returncode, 0, generated.stderr)
                stage = "アセンブル・リンク"
                executable = directory / "program"
                linked = subprocess.run(self.cc + [str(assembly), "-o", str(executable)],
                                        capture_output=True, text=True, timeout=15)
                (directory / "link.stderr").write_text(linked.stderr)
                self.assertEqual(linked.returncode, 0, linked.stderr)
                stage = "実行"
                result = subprocess.run([str(executable)], capture_output=True,
                                        text=True, timeout=3)
                (directory / "actual.txt").write_text(result.stdout)
                (directory / "runtime.stderr").write_text(result.stderr)
                self.assertEqual(result.returncode, 0,
                                 f"execution status={result.returncode}\n{result.stderr}")
                stage = "出力比較"
                self.assertEqual(result.stdout, expected,
                                 f"expected={expected!r}, actual={result.stdout!r}")
        except (AssertionError, subprocess.TimeoutExpired) as error:
            # テスト終了後も失敗時の入力・アセンブリ・ログを読めるようにする。
            if self.__class__.artifacts is None:
                self.__class__.artifacts = Path(tempfile.mkdtemp(prefix="calc-codegen-failures-"))
            saved = self.artifacts / directory.name
            shutil.copytree(directory, saved)
            detail = f"{name} [{stage}]\ninput={source!r}\n{error}\n"
            (saved / "failure.txt").write_text(detail)
            self.failures.append((name, source, stage, saved))
            self.fail(f"{detail}保存先: {saved}")
        print(f"[AC] {name}", flush=True)

    def test_outputs(self):
        cases = [
            ("empty", "", ""),
            ("constant", "print(3);", "3\n"),
            ("zero", "print(0);", "0\n"),
            ("negative", "print(-3);", "-3\n"),
            ("addition", "print(1 + 2);", "3\n"),
            ("subtraction order", "print(2 - 9);", "-7\n"),
            ("precedence", "print(1 + 2 * 3);", "7\n"),
            ("nested temporaries", "print((1 + 2) * (3 + 4));", "21\n"),
            ("left associativity", "print(10 - 3 - 2);", "5\n"),
            ("division order", "print(20 / 3);", "6\n"),
            ("signed division", "print(-20 / 3);", "-6\n"),
            ("negative divisor", "print(20 / -3);", "-6\n"),
            ("consecutive unary minus", "print(--3);", "3\n"),
            ("multiple prints", "print(1); print(2); print(3);", "1\n2\n3\n"),
            ("one variable", "let a = 3; print(a);", "3\n"),
            ("two variables", "let a = 3; let b = a + 4; print(a); print(b);", "3\n7\n"),
            ("assignment", "let a = 3; a = a + 4; print(a);", "7\n"),
            ("block variable", "let a = 1; { let b = 2; print(a + b); } print(a);", "3\n1\n"),
            ("if true", "if (1) { print(2); } print(3);", "2\n3\n"),
            ("if false", "if (0) { print(2); } print(3);", "3\n"),
            ("if nonzero", "if (-2) { print(1); }", "1\n"),
            ("else", "if (0) { print(1); } else { print(2); }", "2\n"),
            ("while false", "while (0) { print(1); } print(2);", "2\n"),
            ("while updates variable", "let a = 0; while (a < 3) { print(a); a = a + 1; } print(a);", "0\n1\n2\n3\n"),
            ("nested control flow", "let a = 0; while (a < 3) { if (a == 1) { print(9); } else { print(a); } a = a + 1; }", "0\n9\n2\n"),
        ]
        # 比較の結果は0か1。真になる場合と偽になる場合を両方確認する。
        for op, left, right, false_left, false_right in (
                ("<", 1, 2, 2, 1), ("<=", 2, 2, 3, 2),
                (">", 3, 2, 2, 3), (">=", 2, 2, 1, 2),
                ("==", 2, 2, 1, 2), ("!=", 1, 2, 2, 2)):
            cases.append((f"comparison {op}",
                          f"print({left} {op} {right}); print({false_left} {op} {false_right});",
                          "1\n0\n"))
        for name, source, expected in cases:
            with self.subTest(name=name, source=source):
                self.check_case(name, source, expected)

    def test_rejected_sources(self):
        for source in ("print(missing);", "let a = 1", "print(1 +);",
                       "{ let a = 1; } print(a);", "if (1) { print(2);"):
            with self.subTest(source=source):
                self.check_case(f"reject {source}", source)


if __name__ == "__main__":
    unittest.main(verbosity=2)

