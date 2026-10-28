#!/usr/bin/env python3
"""
TaskBarHero — Cheetos Unified Update Runner.
Automates the end-to-end update workflow:
1. Detects Steam updates (build ID & assembly SHA-256 from appmanifest_3678970.acf)
2. Scans and validates  AOB signatures & updates RVA fallbacks
3. Validates and synchronizes struct field offsets against dump.cs (optional/if requested)
4. Invokes the project build system to produce release binaries
"""

import os
import re
import sys
import json
import time
import hashlib
import argparse
import platform
import subprocess

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "../.."))
sys.path.insert(0, SCRIPT_DIR)

from aobgenerator import find_unity_dll, cmd_scan, Image, to_regex
from validate_offsets import validate_and_sync

CONFIG_PATH = os.path.join(PROJECT_ROOT, "config.json")
APP_ID = "3678970"

# ANSI Colors
RED = "\033[0;31m"
GREEN = "\033[0;32m"
YELLOW = "\033[1;33m"
CYAN = "\033[0;36m"
PURPLE = "\033[0;35m"
BOLD = "\033[1m"
NC = "\033[0m"


def get_file_sha256(filepath):
    hasher = hashlib.sha256()
    with open(filepath, "rb") as f:
        while chunk := f.read(65536):
            hasher.update(chunk)
    return hasher.hexdigest()


def find_steam_manifest(dll_path):
    if not dll_path or not os.path.exists(dll_path):
        return None

    current = os.path.dirname(os.path.abspath(dll_path))
    for _ in range(4):
        parent = os.path.dirname(current)
        if (
            os.path.basename(current).lower() == "common"
            and os.path.basename(parent).lower() == "steamapps"
        ):
            manifest = os.path.join(parent, f"appmanifest_{APP_ID}.acf")
            if os.path.exists(manifest):
                return manifest
        current = parent

    # Fallback search in standard libraries
    for p in [
        "/data/SteamLibrary",
        os.path.expanduser("~/.steam/steam"),
        os.path.expanduser("~/.local/share/Steam"),
    ]:
        candidate = os.path.join(p, "steamapps", f"appmanifest_{APP_ID}.acf")
        if os.path.exists(candidate):
            return candidate
    return None


def read_manifest_info(manifest_path):
    info = {"buildid": "unknown", "installdir": "unknown"}
    if not manifest_path or not os.path.exists(manifest_path):
        return info

    try:
        with open(manifest_path, "r", encoding="utf-8") as f:
            content = f.read()

        for key in info.keys():
            match = re.search(rf'"{key}"\s+"([^"]+)"', content, re.IGNORECASE)
            if match:
                info[key] = match.group(1)
    except Exception:
        pass
    return info


def print_banner(title):
    print(f"{PURPLE}")
    print("=" * 68)
    print(f"  {title.center(64)}")
    print("=" * 68)
    print(f"{NC}")


def run_build(build_type="Release"):
    print(f"\n{CYAN}[*]{NC} Building project [{BOLD}{build_type}{NC}]...")
    build_dir = os.path.join(PROJECT_ROOT, "build")

    # Configure if needed
    if not os.path.exists(
        os.path.join(build_dir, "build.ninja")
    ) and not os.path.exists(os.path.join(build_dir, "Makefile")):
        toolchain_file = os.path.join(PROJECT_ROOT, "cmake", "mingw-toolchain.cmake")
        cfg_cmd = [
            "cmake",
            "-B",
            "build",
            "-S",
            ".",
            f"-DCMAKE_BUILD_TYPE={build_type}",
        ]
        if platform.system() != "Windows" and os.path.exists(toolchain_file):
            cfg_cmd.append(f"-DCMAKE_TOOLCHAIN_FILE={toolchain_file}")

        print(f"  > {' '.join(cfg_cmd)}")
        res = subprocess.run(cfg_cmd, cwd=PROJECT_ROOT)
        if res.returncode != 0:
            print(f"{RED}[!] CMake configuration failed!{NC}")
            return False

    build_cmd = ["cmake", "--build", "build", "-j4"]
    print(f"  > {' '.join(build_cmd)}")
    res = subprocess.run(build_cmd, cwd=PROJECT_ROOT)
    if res.returncode == 0:
        print(f"{GREEN}[+] Build succeeded! Artifacts ready in build/{NC}")
        return True
    else:
        print(f"{RED}[!] Build failed with exit code {res.returncode}{NC}")
        return False


def main():
    parser = argparse.ArgumentParser(
        description="TaskBarHero Cheetos Unified Update Runner"
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Dry run inspection without modifying files or building",
    )
    parser.add_argument(
        "--force", action="store_true", help="Force update even if build ID matches"
    )
    parser.add_argument(
        "--sync-fields",
        action="store_true",
        help="Synchronize struct field offsets from dump.cs",
    )
    parser.add_argument(
        "--build-type",
        default="Release",
        choices=["Release", "Debug"],
        help="Build configuration",
    )
    parser.add_argument("--dll", help="Manual path to GameAssembly.dll")

    args = parser.parse_args()
    print_banner("TaskBarHero Cheetos — Unified Update Runner")

    if not os.path.exists(CONFIG_PATH):
        sys.exit(f"{RED}[-] Error: config.json not found at {CONFIG_PATH}{NC}")

    with open(CONFIG_PATH, "r", encoding="utf-8") as f:
        cfg = json.load(f)

    dll_path = args.dll or find_unity_dll()
    if not dll_path or not os.path.exists(dll_path):
        sys.exit(f"{RED}[-] GameAssembly.dll not found. Specify with --dll <path>{NC}")

    manifest_path = find_steam_manifest(dll_path)
    manifest_info = read_manifest_info(manifest_path)
    current_build_id = manifest_info.get("buildid", "unknown")
    current_sha256 = get_file_sha256(dll_path)

    stored_build_id = cfg.get("steam-build-id", "unknown")
    stored_sha256 = cfg.get("assembly_sha256", "unknown")

    print(f"{CYAN}[*]{NC} Target Binary: {dll_path}")
    print(f"{CYAN}[*]{NC} Steam AppID:   {APP_ID}")
    print(
        f"{CYAN}[*]{NC} Current Build: {BOLD}{current_build_id}{NC} (stored: {stored_build_id})"
    )
    print(
        f"{CYAN}[*]{NC} SHA-256:       {current_sha256[:16]}... (stored: {stored_sha256[:16]}...)"
    )

    is_outdated = (current_build_id != stored_build_id) or (
        current_sha256 != stored_sha256
    )

    if not is_outdated and not args.force:
        print(
            f"\n{GREEN}[+] Project is up-to-date!{NC} Use --force to rescan signatures and rebuild."
        )
        if not args.check and not args.sync_fields:
            return

    # Scan AOB signatures
    print_banner("Step 1:  Signature Scan")
    scan_args = argparse.Namespace(dll=dll_path, update=(not args.check))
    scan_ok = cmd_scan(scan_args)

    if not scan_ok:
        print(f"\n{YELLOW}[!] Warning: Some signatures are broken or ambiguous.{NC}")
        print(
            f"Run {BOLD}python3 resources/tools/aobgenerator.py generate --update{NC} to regenerate."
        )

    # Optional struct offsets sync
    if args.sync_fields:
        print_banner("Step 2: Struct Offsets Sync")
        validate_and_sync(update=(not args.check))

    # Update metadata in config.json
    if not args.check:
        with open(CONFIG_PATH, "r", encoding="utf-8") as f:
            cfg = json.load(f)
        cfg["steam-build-id"] = current_build_id
        cfg["assembly_sha256"] = current_sha256
        with open(CONFIG_PATH, "w", encoding="utf-8") as f:
            json.dump(cfg, f, indent=2)
            f.write("\n")
        print(f"{GREEN}[+] Updated Steam metadata in config.json{NC}")

    # Build
    if not args.check:
        print_banner("Step 3: Compilation & Link")
        run_build(args.build_type)

    print_banner("Update Complete")


if __name__ == "__main__":
    main()
