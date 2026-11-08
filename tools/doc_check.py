#!/usr/bin/env python3
"""tools/doc_check.py — keep the docs honest.

Parses headers and the python package as text (nothing is compiled) and
verifies:

  1. every public widget class is covered by docs/reference/index.md
     (or is on the internal allowlist below);
  2. every `slikui::Name` mentioned in the docs is a real C++ name;
  3. every `slikui.name` mentioned in the docs is exported by the python
     package's __all__;
  3b. every `slikui.examples.<name>` mentioned in the docs names a real
     shipped example module (python/slikui/examples/<name>.py);
  4. every image referenced from the docs exists (orphans are reported
     as notes, not errors).

Docs CI runs this before `mkdocs build --strict`, so drift fails the build.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DOCS = ROOT / "docs"
INCLUDE = ROOT / "include" / "slikui"
PY_INIT = ROOT / "python" / "slikui" / "__init__.py"

# Not user-facing widget classes: internal helpers or aliases covered by
# another documented name. Keyed by class name, value says why.
INTERNAL_CPP = {
    "ColorPopup": "internal: ColorEdit's picker popup",
    "PropertyRow": "row type returned by PropertyGrid.add_property",
    "VecNEdit": "base of the Vec2Edit/Vec3Edit/Vec4Edit aliases",
    "TooltipLabel": "internal tooltip widget",
    "TooltipBox": "internal tooltip widget",
}

# Namespace-level names the docs may reference beyond widget classes.
CPP_EXTRAS = {
    "Widget", "Container", "Context", "Theme", "Color", "Event", "Time",
    "Constraints", "Size", "Rect", "Vec2", "Padding", "CrossMode", "Cursor",
    "Icon", "Key", "Mod", "ModCtrl", "ModShift", "ModAlt", "ModSuper",
    "IRenderSink", "DrawList", "DrawCmd", "TexUpload", "PaintEnv", "Anim",
    "software", "opengl", "glfw", "text",
    "theme_presets", "apply_preset", "use_theme", "find_system_font",
    "shortcut_label",
}

# Python-side modules referenced by name in prose (not part of __all__)
# plus non-python tokens the dotted regex happens to catch ("slikui.hpp").
PY_INTERNAL = {"_slikui", "_py", "hpp", "examples"}

EXAMPLES_DIR = ROOT / "python" / "slikui" / "examples"


def example_modules() -> set[str]:
    """Shipped example module names: python/slikui/examples/*.py."""
    return {p.stem for p in sorted(EXAMPLES_DIR.glob("*.py")) if p.stem != "__main__"}


def cpp_classes() -> set[str]:
    """Public classes and aliases from the widget headers + context.hpp."""
    classes: set[str] = set()
    files = sorted((INCLUDE / "widgets").glob("*.hpp"))
    files.append(INCLUDE / "context.hpp")
    for h in files:
        text = h.read_text()
        for m in re.finditer(r"^class (\w+)\b", text, re.M):
            classes.add(m.group(1))
        for m in re.finditer(r"^using (\w+) = ", text, re.M):
            classes.add(m.group(1))  # e.g. using Vec3Edit = VecNEdit<3>
    return classes


def python_exports() -> set[str]:
    text = PY_INIT.read_text()
    m = re.search(r"^__all__ = \[(.*?)^\]", text, re.S | re.M)
    if not m:
        raise SystemExit(f"doc_check: cannot parse __all__ in {PY_INIT}")
    return set(re.findall(r'"(\w+)"', m.group(1)))


def main() -> int:
    errors: list[str] = []
    classes = cpp_classes()
    exports = python_exports()

    # 1. coverage: index.md (widget families) + context.md (the Context API).
    coverage = (DOCS / "reference" / "index.md").read_text() + \
               (DOCS / "reference" / "context.md").read_text()
    for c in sorted(classes):
        if c in INTERNAL_CPP:
            continue
        if not re.search(rf"\b{c}\b", coverage):
            errors.append(
                f"undocumented class: {c} — add to docs/reference/index.md "
                f"or the allowlist in tools/doc_check.py")

    docs_text = "\n".join(p.read_text() for p in sorted(DOCS.rglob("*.md")))

    # 2. C++ names.
    known = classes | CPP_EXTRAS | set(INTERNAL_CPP)
    for m in sorted(set(re.findall(r"slikui::(\w+)", docs_text))):
        if m not in known:
            errors.append(f"unknown C++ name in docs: slikui::{m}")

    # 3. python names.
    for m in sorted(set(re.findall(r"slikui\.(\w+)", docs_text))):
        if m not in exports and m not in PY_INTERNAL:
            errors.append(
                f"slikui.{m} is used in the docs but not exported — "
                f"add it to python/slikui/__init__.py __all__ or fix the name")

    # 3b. shipped example modules: `python -m slikui.examples.<name>`.
    ex_modules = example_modules()
    for m in sorted(set(re.findall(r"slikui\.examples\.(\w+)", docs_text))):
        if m not in ex_modules:
            errors.append(
                f"unknown shipped example in docs: slikui.examples.{m} "
                f"(known: {', '.join(sorted(ex_modules)) or 'none yet'})")

    # 4. snippet includes — docs quote extracted regions of the shipped
    #    examples (tools/extract_snippets.py); a target must exist and name a
    #    source whose # doc: markers are still live.
    include_re = re.compile(r'--8<--\s*"([^"]+)"')
    src_head = re.compile(r"<!--\s*source:\s*(\S+?)#([\w-]+)")
    for p in sorted(DOCS.rglob("*.md")):
        for m in include_re.finditer(p.read_text()):
            target = DOCS / m.group(1)
            if not target.exists():
                errors.append(
                    f"missing snippet include: {m.group(1)} "
                    f"(referenced from {p.relative_to(DOCS)}; "
                    f"run tools/extract_snippets.py)")
                continue
            head = src_head.match(target.read_text())
            if not head:
                errors.append(
                    f"snippet include without source header: {m.group(1)}")
                continue
            source = ROOT / head.group(1)
            text = source.read_text() if source.exists() else ""
            if (f"# doc: {head.group(2)}" not in text
                    or "# doc: end" not in text):
                errors.append(
                    f"snippet source stale: {head.group(1)}#{head.group(2)} "
                    f"— markers missing (referenced from "
                    f"{p.relative_to(DOCS)})")

    # 5. images.
    referenced: set[str] = set()
    for p in sorted(DOCS.rglob("*.md")):
        for m in re.finditer(r"!\[[^\]]*\]\(([^)\s]+)\)", p.read_text()):
            target = (p.parent / m.group(1)).resolve()
            referenced.add(str(target))
            if not target.exists():
                errors.append(
                    f"missing image: {m.group(1)} (referenced from "
                    f"{p.relative_to(ROOT)})")
    for img in sorted((DOCS / "images").glob("*.png")):
        if str(img.resolve()) not in referenced:
            print(f"note: unreferenced image {img.name}")
        twin = ROOT / "python" / "examples" / img.name
        if twin.exists():
            print(f"note: {img.name} exists in both docs/images and "
                  f"python/examples — make sure the docs copy is the one the "
                  f"snippet renders (or write docs/images directly)")

    if errors:
        print(f"doc_check: {len(errors)} error(s)")
        for e in errors:
            print("  -", e)
        return 1
    includes = sum(1 for _ in (DOCS / "includes" / "examples").glob("*.md")) \
        if (DOCS / "includes" / "examples").exists() else 0
    print(f"doc_check: OK — {len(classes)} classes, {len(exports)} python "
          f"exports, {len(referenced)} image references, "
          f"{includes} snippet includes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
