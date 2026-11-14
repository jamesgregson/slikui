"""Every source, test, tool, doc and generated text file is plain ASCII.

Non-ASCII breaks MSVC builds without /utf-8 and Windows consoles (cp1252
stdout), so the project uses none: write symbols in prose as ASCII ("->",
"-", "...") and test data as escapes ("\\u00e9", "\\xC3\\xA9"). Also covers
the generated files (dist/slikui.hpp, the type stub, doc snippets).
"""

import pathlib

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]

SKIP_DIRS = {".git", ".venv", "venv", "build", "site", "third_party", "dist-info",
             "__pycache__", ".pytest_cache", ".mypy_cache", "node_modules"}
BINARY = {".png", ".gif", ".jpg", ".jpeg", ".ppm", ".ttf", ".otf", ".so", ".dylib",
          ".dll", ".pyd", ".pyc", ".a", ".o", ".obj", ".exe", ".whl", ".gz", ".zip"}


def text_files():
    for path in sorted(ROOT.rglob("*")):
        rel = path.relative_to(ROOT)
        if any(part in SKIP_DIRS or part.endswith(".egg-info") for part in rel.parts):
            continue
        if path.is_file() and path.suffix.lower() not in BINARY:
            yield path, rel


def test_repository_is_ascii():
    if not (ROOT / "include" / "slikui").is_dir():
        pytest.skip("source tree not available (installed package only)")
    bad = []
    n = 0
    for path, rel in text_files():
        n += 1
        for lineno, line in enumerate(path.read_bytes().splitlines(), 1):
            if any(b > 0x7F for b in line):
                bad.append(f"{rel}:{lineno}: {line.decode('ascii', 'backslashreplace')[:100]}")
    assert n > 100, f"scanned only {n} files - wrong root? {ROOT}"
    assert not bad, f"{len(bad)} non-ASCII line(s):\n" + "\n".join(bad[:40])
