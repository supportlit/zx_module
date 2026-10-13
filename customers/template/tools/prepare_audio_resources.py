#!/usr/bin/env python3
"""template audio resource preparation helper.

Copies the provided "Device Sound" assets into the layout expected by the
SoundBox firmware:

- Female prompts -> customers/template/res/FEMALE
- Male prompts   -> customers/template/res/MALE

Source files come from customers/template/info/Device Sound. Even though they use
an .amr extension, they already contain MP3 payloads, so the script simply
renames the extension to .mp3 and normalises filenames (lowercase, spaces ->
underscores, parentheses removed).
"""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path
from typing import Dict

VOICE_PREFIXES: Dict[str, str] = {
    "fm": "FEMALE",
    "ml": "MALE",
}


def normalise_name(name: str) -> str:
    """Convert `activation-device` etc. into filesystem-friendly names."""
    cleaned = name.strip().lower()
    for ch in " ()[]{}":
        cleaned = cleaned.replace(ch, "_")
    while "__" in cleaned:
        cleaned = cleaned.replace("__", "_")
    cleaned = cleaned.strip("_")
    return f"{cleaned}.mp3"


def copy_assets(source_root: Path, output_root: Path, dry_run: bool = False) -> int:
    if not source_root.exists():
        raise FileNotFoundError(f"Source folder not found: {source_root}")

    total = 0
    for entry in sorted(source_root.iterdir()):
        if not entry.is_file():
            continue
        if entry.suffix.lower() != ".amr":
            continue

        stem = entry.stem
        if "_" not in stem:
            print(f"[skip] {entry.name}: missing voice prefix")
            continue

        prefix, remainder = stem.split("_", 1)
        voice = VOICE_PREFIXES.get(prefix.lower())
        if not voice:
            print(f"[skip] {entry.name}: unknown voice prefix '{prefix}'")
            continue

        output_dir = output_root / voice
        output_dir.mkdir(parents=True, exist_ok=True)
        output_name = normalise_name(remainder)
        output_path = output_dir / output_name

        if dry_run:
            print(f"[dry] {entry.name} -> {voice}/{output_name}")
        else:
            shutil.copyfile(entry, output_path)
            print(f"[copy] {entry.name} -> {voice}/{output_name}")
        total += 1

    return total


def main() -> None:
    repo_root = Path(__file__).resolve().parents[1]
    default_source = repo_root / "info" / "Device Sound"
    default_output = repo_root / "res"

    parser = argparse.ArgumentParser(description="Prepare template audio assets")
    parser.add_argument("--source", type=Path, default=default_source,
                        help="Path to the Device Sound folder")
    parser.add_argument("--output", type=Path, default=default_output,
                        help="Destination root (res folder)")
    parser.add_argument("--dry-run", action="store_true",
                        help="Only display planned operations")
    args = parser.parse_args()

    total = copy_assets(args.source, args.output, dry_run=args.dry_run)
    print(f"Processed {total} audio fragments")


if __name__ == "__main__":
    main()
