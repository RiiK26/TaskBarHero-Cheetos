#!/usr/bin/env python3
"""
TaskBarHero — AOB Signature Engine for GameAssembly.dll.
Based on the architecture of PixelGun3D-Cheetos and Evitania.

Commands:
  aobgenerator.py scan [--dll PATH] [--update]
      Scan every signature in config.json against the DLL and verify uniqueness.
      With --update, syncs shifted RVAs to config.json and src/core/Signatures.h.

  aobgenerator.py generate [--dll PATH] [--update]
      Disassemble code at target RVAs using Capstone, mask dynamic operands
      (RIP-relative displacements, relative calls/branches), and generate unique AOBs.
      With --update, writes signatures to config.json and src/core/Signatures.h.
"""

import os
import re
import sys
import glob
import json
import argparse
import platform

import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_OP_MEM, CS_OP_IMM
from capstone.x86 import X86_REG_RIP

PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
CONFIG_FILE = os.path.join(PROJECT_ROOT, "config.json")
SIGNATURES_HEADER = os.path.join(PROJECT_ROOT, "src", "core", "Signatures.h")

APP_ID = "3678970"
MIN_SIG_BYTES = 16
MAX_SIG_BYTES = 256
BRANCH_GROUPS = {"jump", "call", "branch_relative"}


def find_unity_dll(app_id=APP_ID):
    steam_paths = []
    if platform.system() == "Windows":
        try:
            import winreg

            with winreg.OpenKey(
                winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam"
            ) as key:
                steam_path = winreg.QueryValueEx(key, "SteamPath")[0]
                steam_paths.append(steam_path)
        except Exception:
            pass
        for p in [r"C:\Program Files (x86)\Steam", r"C:\Program Files\Steam"]:
            if os.path.exists(p) and p not in steam_paths:
                steam_paths.append(p)
    elif platform.system() == "Linux":
        for p in [
            "/data/SteamLibrary",
            os.path.expanduser("~/.steam/steam"),
            os.path.expanduser("~/.local/share/Steam"),
        ]:
            if os.path.exists(p):
                steam_paths.append(p)

    library_folders = []
    for sp in steam_paths:
        if sp not in library_folders:
            library_folders.append(sp)
        vdf_path = os.path.join(sp, "steamapps", "libraryfolders.vdf")
        if os.path.exists(vdf_path):
            try:
                with open(vdf_path, "r", encoding="utf-8") as f:
                    content = f.read()
                    paths = re.findall(r'"path"\s+"([^"]+)"', content)
                    for p in paths:
                        clean_path = p.replace("\\\\", "\\")
                        if clean_path not in library_folders:
                            library_folders.append(clean_path)
            except Exception:
                pass

    for lib in library_folders:
        manifest_path = os.path.join(lib, "steamapps", f"appmanifest_{app_id}.acf")
        if os.path.exists(manifest_path):
            try:
                with open(manifest_path, "r", encoding="utf-8") as f:
                    content = f.read()
                    match = re.search(
                        r'"installdir"\s+"([^"]+)"', content, re.IGNORECASE
                    )
                    if match:
                        install_dir = match.group(1)
                        dll_path = os.path.join(
                            lib, "steamapps", "common", install_dir, "GameAssembly.dll"
                        )
                        if os.path.exists(dll_path):
                            print(
                                f"[+] Found GameAssembly.dll via AppID {app_id} at: {dll_path}"
                            )
                            return dll_path
            except Exception as e:
                print(f"[-] Error reading manifest: {e}")

    # Fallback to current directory or standard relative locations
    fallback_path = os.path.join(PROJECT_ROOT, "GameAssembly.dll")
    if os.path.exists(fallback_path):
        return fallback_path
    return None


class Image:
    def __init__(self, path):
        print(f"[*] Loading PE image: {path}")
        self.pe = pefile.PE(path, fast_load=True)
        self.code = []
        for s in self.pe.sections:
            if s.Characteristics & 0x20000000:  # IMAGE_SCN_MEM_EXECUTE
                raw = bytes(s.get_data())
                size = min(len(raw), max(s.Misc_VirtualSize, 1))
                self.code.append(
                    (s.VirtualAddress, s.VirtualAddress + size, raw[:size])
                )
        if not self.code:
            sys.exit("[-] No executable section found in binary")

    def read(self, rva, size):
        for start, end, raw in self.code:
            if start <= rva < end:
                off = rva - start
                return raw[off : off + size]
        return None

    def find(self, regex, limit=2):
        hits = []
        for start, _, raw in self.code:
            for m in regex.finditer(raw):
                hits.append(start + m.start())
                if len(hits) >= limit:
                    return hits
        return hits


def to_regex(sig):
    parts = []
    for tok in sig.split():
        if tok in ("?", "??"):
            parts.append(b".")
        else:
            parts.append(re.escape(bytes([int(tok, 16)])))
    return re.compile(b"".join(parts), re.DOTALL)


def fmt(mask_bytes):
    return " ".join("??" if b is None else f"{b:02X}" for b in mask_bytes)


def masked_insn_bytes(insn):
    out = list(insn.bytes)

    def wipe(off, size):
        for i in range(off, min(off + size, len(out))):
            out[i] = None

    is_branch = any(insn.group_name(g) in BRANCH_GROUPS for g in insn.groups)
    for op in insn.operands:
        if op.type == CS_OP_MEM and insn.disp_size:
            if op.mem.base == X86_REG_RIP:
                wipe(insn.disp_offset, insn.disp_size)
        elif op.type == CS_OP_IMM and is_branch and insn.imm_size:
            wipe(insn.imm_offset, insn.imm_size)
    if insn.imm_size == 8:
        wipe(insn.imm_offset, 8)
    return out


def generate_sig(img, md, rva):
    code = img.read(rva, MAX_SIG_BYTES + 32)
    if not code:
        return None, "RVA not in an executable section"

    sig = []
    for insn in md.disasm(code, rva):
        sig.extend(masked_insn_bytes(insn))
        concrete = sum(b is not None for b in sig)
        if len(sig) >= MIN_SIG_BYTES and concrete >= 8:
            hits = img.find(to_regex(fmt(sig)))
            if hits == [rva]:
                while sig and sig[-1] is None:
                    sig.pop()
                return fmt(sig), None
        if len(sig) >= MAX_SIG_BYTES:
            break
    return None, f"not unique within {MAX_SIG_BYTES} bytes"


def load_config():
    if not os.path.exists(CONFIG_FILE):
        sys.exit(f"[-] Error: config.json not found at {CONFIG_FILE}")
    with open(CONFIG_FILE, "r", encoding="utf-8") as f:
        return json.load(f)


def save_config(cfg):
    with open(CONFIG_FILE, "w", encoding="utf-8") as f:
        json.dump(cfg, f, indent=2)
        f.write("\n")
    print(f"[+] Updated {CONFIG_FILE}")


def sync_signatures_header(cfg):
    sigs = cfg.get("signatures", {})
    lines = [
        "#pragma once",
        "#include <cstdint>",
        "",
        "/* =========================================================================",
        " * AOB Signatures & Fallback RVAs",
        f" * Target: {cfg.get('project', 'TaskBarHero')} (AppID: {APP_ID})",
        " *",
        " * Generated automatically by resources/tools/aobgenerator.py.",
        " * Wildcard pattern: RIP displacements and rel32 calls/jmps masked with '??'.",
        " * ========================================================================= */",
        "",
        "namespace Signatures",
        "{",
        "  struct TargetSignature",
        "  {",
        "    const char* name;",
        "    const char* aob;",
        "    uintptr_t   rva;",
        "  };",
        "",
    ]

    for name, data in sigs.items():
        sig_str = data.get("signature", "")
        rva_str = data.get("rva", "0x0")
        clean_name = name.replace(".", "_")
        lines.append(f"  inline constexpr TargetSignature {clean_name} = {{")
        lines.append(f'    "{name}",')
        lines.append(f'    "{sig_str}",')
        lines.append(f"    {rva_str}")
        lines.append("  };")
        lines.append("")

    lines.append("}")
    lines.append("")

    with open(SIGNATURES_HEADER, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print(f"[+] Synchronized {SIGNATURES_HEADER}")


def cmd_scan(args):
    dll_path = args.dll or find_unity_dll()
    if not dll_path or not os.path.exists(dll_path):
        sys.exit("[-] GameAssembly.dll not found. Specify with --dll <path>")

    img = Image(dll_path)
    cfg = load_config()
    sigs = cfg.get("signatures", {})

    print(
        f"\n[*] Scanning {len(sigs)} signatures against {os.path.basename(dll_path)}..."
    )
    ok_count = 0
    moved_count = 0
    broken_count = 0

    for name, data in sigs.items():
        pattern = data.get("signature", "")
        prev_rva = int(data.get("rva", "0"), 16)
        if not pattern:
            print(f"  [EMPTY]  {name}")
            broken_count += 1
            continue

        rx = to_regex(pattern)
        hits = img.find(rx)

        if len(hits) == 0:
            print(f"  \033[31m[BROKEN]\033[0m {name} — pattern not found")
            broken_count += 1
        elif len(hits) > 1:
            print(
                f"  \033[31m[AMBIG]\033[0m  {name} — matched {len(hits)} locations ({[hex(h) for h in hits]})"
            )
            broken_count += 1
        else:
            hit = hits[0]
            if hit == prev_rva:
                print(f"  \033[32m[OK]\033[0m      {name} @ 0x{hit:X}")
                ok_count += 1
            else:
                print(
                    f"  \033[33m[MOVED]\033[0m   {name} @ 0x{prev_rva:X} -> 0x{hit:X}"
                )
                data["rva"] = f"0x{hit:x}"
                moved_count += 1

    summary = {
        "total": len(sigs),
        "ok": ok_count,
        "moved": moved_count,
        "broken": broken_count,
    }
    cfg["signatures_summary"] = summary
    print(
        f"\nSummary: {ok_count} OK, {moved_count} moved, {broken_count} broken / {len(sigs)} total."
    )

    if args.update and (moved_count > 0 or broken_count == 0):
        save_config(cfg)
        sync_signatures_header(cfg)

    return broken_count == 0


def cmd_generate(args):
    dll_path = args.dll or find_unity_dll()
    if not dll_path or not os.path.exists(dll_path):
        sys.exit("[-] GameAssembly.dll not found. Specify with --dll <path>")

    img = Image(dll_path)
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True

    cfg = load_config()
    sigs = cfg.get("signatures", {})

    print(f"\n[*] Generating signatures for {len(sigs)} targets...")
    generated = 0
    failed = 0

    for name, data in sigs.items():
        rva = int(data.get("rva", "0"), 16)
        if rva == 0:
            print(f"  [-] Skipping {name}: no RVA")
            failed += 1
            continue

        pattern, err = generate_sig(img, md, rva)
        if pattern:
            print(
                f"  \033[32m[+] Generated\033[0m {name} ({len(pattern.split())} bytes)"
            )
            data["signature"] = pattern
            generated += 1
        else:
            print(f"  \033[31m[-] Failed\033[0m    {name}: {err}")
            failed += 1

    print(f"\nResult: {generated} generated, {failed} failed.")
    if args.update:
        save_config(cfg)
        sync_signatures_header(cfg)


def main():
    parser = argparse.ArgumentParser(description="AOB Signature Tool for TaskBarHero")
    subparsers = parser.add_subparsers(dest="cmd")

    scan_parser = subparsers.add_parser(
        "scan", help="Scan signatures and report status"
    )
    scan_parser.add_argument("--dll", help="Path to GameAssembly.dll")
    scan_parser.add_argument(
        "--update",
        action="store_true",
        help="Sync moved RVAs to config.json and Signatures.h",
    )

    gen_parser = subparsers.add_parser(
        "generate", help="Generate unique signatures using Capstone"
    )
    gen_parser.add_argument("--dll", help="Path to GameAssembly.dll")
    gen_parser.add_argument(
        "--update",
        action="store_true",
        help="Write generated signatures to config.json and Signatures.h",
    )

    args = parser.parse_args()
    if args.cmd == "scan":
        cmd_scan(args)
    elif args.cmd == "generate":
        cmd_generate(args)
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
