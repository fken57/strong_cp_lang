"""Copy the shared LSP and compiler sources into the installable extension."""
from pathlib import Path
import shutil

extension = Path(__file__).resolve().parents[1]
root = extension.parents[1]
server = extension / "server"
server.mkdir(exist_ok=True)
for name in (
    "compiler.py", "language_service.py", "lsp_server.py", "fken_cpl.c",
    "typedef.c", "util.c", "lex.c", "parser.c", "analyze.c", "generate_cpp.c", "generate_plan.c", "runtime.hpp",
):
    shutil.copyfile(root / name, server / name)
(root / ".build").mkdir(exist_ok=True)
print("Bundled shared language server and compiler sources.")
