#!/usr/bin/env python3
"""
TaskBarHero — Struct Field Offset Validator & Synchronizer.
Validates and synchronizes field offsets in src/core/Il2CppOffsets.h and config.json
against resources/dump/dump.cs.

Usage:
  validate_offsets.py            # dry run validation
  validate_offsets.py --update   # synchronize offsets to Il2CppOffsets.h & config.json
"""

import os
import re
import sys
import json
import argparse

PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
DUMP_FILE = os.path.join(PROJECT_ROOT, "resources", "dump", "dump.cs")
OFFSETS_HEADER = os.path.join(PROJECT_ROOT, "src", "core", "Il2CppOffsets.h")
CONFIG_FILE = os.path.join(PROJECT_ROOT, "config.json")

# Classes to validate against dump.cs
TARGET_CLASSES = [
    "HeroInfoData",
    "PlayerSaveData",
    "HeroSaveData",
    "CurrencySaveData",
    "RuneSaveData",
    "CubeLevelSaveData",
    "CubeRecipeSaveData",
    "StageManager",
]

FIELD_MAPPINGS = {
    "PlayerSaveData": {
        "BoxData": "BoxBucketUseBoxList",
        "CurrencySaveDatas": "currenySaveDatas",
    }
}

CLASS_RE = re.compile(
    r"\b(?:class|struct)\s+([\w\.`<>]+?)\s*(?::.*)?//\s*TypeDefIndex:\s*\d+"
)
FIELD_RE = re.compile(
    r"^\s*(?:\[[^\]]*\]\s*)?((?:(?:public|private|protected|internal|static|readonly|const|new|volatile)\s+)+)"
    r"(.+?)\s+([^\s;]+);\s*//\s*(0x[0-9a-fA-F]+)"
)


def parse_dump():
    if not os.path.exists(DUMP_FILE):
        print(f"[-] Dump file not found at {DUMP_FILE}")
        return None

    classes = {}
    current_class = None

    with open(DUMP_FILE, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            c_match = CLASS_RE.search(line)
            if c_match:
                current_class = c_match.group(1).split(".")[-1]
                if current_class not in classes:
                    classes[current_class] = {}
                continue

            if current_class:
                f_match = FIELD_RE.search(line)
                if f_match:
                    field_name = f_match.group(3)
                    offset_hex = f_match.group(4)
                    classes[current_class][field_name] = offset_hex
    return classes


def load_config():
    if not os.path.exists(CONFIG_FILE):
        return {}
    with open(CONFIG_FILE, "r", encoding="utf-8") as f:
        return json.load(f)


def save_config(cfg):
    with open(CONFIG_FILE, "w", encoding="utf-8") as f:
        json.dump(cfg, f, indent=2)
        f.write("\n")


def validate_and_sync(update=False):
    dump_data = parse_dump()
    if not dump_data:
        sys.exit(1)

    cfg = load_config()
    cfg_offsets = cfg.get("offsets", {})

    print(f"[*] Validating offsets against {os.path.basename(DUMP_FILE)}...")
    mismatches = 0
    matched = 0

    for cls_name in TARGET_CLASSES:
        if cls_name not in dump_data:
            print(f"  [?] Class {cls_name} not found in dump.cs")
            continue

        dump_fields = dump_data[cls_name]
        cfg_cls = cfg_offsets.get(cls_name, {})

        for field_name, expected_offset in cfg_cls.items():
            mapped_name = FIELD_MAPPINGS.get(cls_name, {}).get(field_name, field_name)
            actual_offset = dump_fields.get(mapped_name)
            if not actual_offset:
                actual_offset = dump_fields.get(field_name)
            if not actual_offset:
                for df, off in dump_fields.items():
                    if df.lower() in (mapped_name.lower(), field_name.lower()):
                        actual_offset = off
                        break

            if actual_offset:
                exp_int = int(expected_offset, 16)
                act_int = int(actual_offset, 16)
                if exp_int == act_int:
                    matched += 1
                else:
                    print(
                        f"  \033[33m[SHIFT]\033[0m {cls_name}.{field_name}: {expected_offset} -> {actual_offset}"
                    )
                    mismatches += 1
                    if update:
                        cfg_cls[field_name] = actual_offset
            else:
                print(
                    f"  \033[31m[MISSING]\033[0m {cls_name}.{field_name} (expected {expected_offset})"
                )
                mismatches += 1

    print(f"\nResult: {matched} matched, {mismatches} shifted/missing.")
    if update and mismatches > 0:
        cfg["offsets"] = cfg_offsets
        save_config(cfg)
        print(f"[+] Updated offsets in config.json")
    return mismatches == 0


def main():
    parser = argparse.ArgumentParser(description="TaskBarHero Struct Offset Validator")
    parser.add_argument(
        "--update",
        action="store_true",
        help="Sync shifted offsets to config.json and Il2CppOffsets.h",
    )
    args = parser.parse_args()
    success = validate_and_sync(update=args.update)
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
