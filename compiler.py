"""Shared native compiler build and include paths."""
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent
BUILD = Path(os.environ.get("FKEN_BUILD_DIR", ROOT / ".build"))
SOURCES = (
    "fken_cpl.c", "typedef.c", "util.c", "lex.c", "parser.c", "analyze.c",
    "generate_cpp.c", "runtime.hpp",
)


def build_compiler():
    BUILD.mkdir(parents=True, exist_ok=True)
    command = ([os.environ["FKEN_CC_PATH"]] if os.environ.get("FKEN_CC_PATH")
               else shlex.split(os.environ.get("CC", "gcc" if os.name == "nt" else "cc")))
    flags = shlex.split(os.environ.get("CFLAGS", ""))
    digest = hashlib.sha256(json.dumps(command + flags).encode())
    for name in SOURCES:
        digest.update((ROOT / name).read_bytes())
    digest.update(Path(__file__).read_bytes())
    fingerprint = digest.hexdigest()
    binary = BUILD / ("compiler.exe" if os.name == "nt" else "compiler")
    stamp = BUILD / "compiler.sha256"
    if binary.exists() and stamp.exists() and stamp.read_text() == fingerprint:
        return binary
    with tempfile.TemporaryDirectory(prefix="compiler-", dir=BUILD) as directory:
        work = Path(directory)
        # Embed the runtime so emitted C++ needs no project-local headers.
        (work / "runtime.inc").write_text("".join(
            json.dumps(line) + "\n"
            for line in (ROOT / "runtime.hpp").read_text().splitlines(keepends=True)
        ))
        subprocess.run(command + [
            "-std=c11", "-O2", "-Wall", "-Wextra",
            "-Wno-unused-parameter", *flags,
            "-I", str(work), str(ROOT / "fken_cpl.c"),
            "-o", str(work / binary.name),
        ], check=True, stdout=subprocess.PIPE)
        os.replace(work / binary.name, binary)
    stamp.write_text(fingerprint)
    return binary


def boost_flags():
    paths = []
    if os.environ.get("FKEN_BOOST_INCLUDE"):
        paths.append(Path(os.environ["FKEN_BOOST_INCLUDE"]))
    local = ROOT / ".deps" / "boost" / "usr" / "include"
    if local.is_dir():
        paths.append(local)
    return [flag for path in paths for flag in ("-I", str(path))]


