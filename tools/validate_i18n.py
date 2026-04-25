#!/usr/bin/env python3
"""Validate the small built-in UI localization table."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "src/xenia/ui/i18n.h"
SOURCE = ROOT / "src/xenia/ui/i18n.cc"


def main() -> int:
    header = HEADER.read_text(encoding="utf-8")
    source = SOURCE.read_text(encoding="utf-8")

    enum_match = re.search(r"enum class TextId \{(?P<body>.*?)\n\};", header, re.S)
    if not enum_match:
        print("TextId enum not found", file=sys.stderr)
        return 1
    text_ids = [
        line.strip().rstrip(",")
        for line in enum_match.group("body").splitlines()
        if line.strip().startswith("k") and line.strip().rstrip(",") != "kCount"
    ]

    table_match = re.search(
        r"constexpr std::array<LocalizedText, size_t\(TextId::kCount\)> "
        r"kLocalizedText = \{\{(?P<body>.*?)\n\}\};",
        source,
        re.S,
    )
    if not table_match:
        print("localized text table not found", file=sys.stderr)
        return 1

    rows = re.findall(r"\{\s*\"(?:[^\"\\]|\\.)*\"", table_match.group("body"))
    if len(rows) != len(text_ids):
        print(
            f"translation count mismatch: enum has {len(text_ids)}, "
            f"table has {len(rows)}",
            file=sys.stderr,
        )
        return 1

    for language in ("en", "sv"):
        if re.search(rf"\{{\s*\"\"\s*,\s*\"", table_match.group("body")):
            print(f"empty {language} translation found", file=sys.stderr)
            return 1

    print(f"validated {len(text_ids)} localized UI strings")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
