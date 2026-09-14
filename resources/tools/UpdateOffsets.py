#!/usr/bin/env python3
import sys
import re
import os

def main():
    if len(sys.argv) < 3:
        print("Usage: python3 UpdateOffsets.py <path_to_dump.cs> <header1.h> [header2.h ...]")
        sys.exit(1)

    dump_path = sys.argv[1]
    header_paths = sys.argv[2:]

    if not os.path.exists(dump_path):
        print(f"Error: {dump_path} not found.")
        sys.exit(1)

    for h in header_paths:
        if not os.path.exists(h):
            print(f"Error: {h} not found.")
            sys.exit(1)

    # Regexes for extracting tags from headers
    tag_field_re = re.compile(r"//\s*@\[([^\]\.]+)\.([^\]]+)\]")
    tag_rva_re = re.compile(r"//\s*@RVA\[([^\]\.]+)\.([^\]]+)\]")

    needed_offsets = {} # (ClassName, FieldName) -> None
    needed_rvas = {}    # (ClassName, MethodName) -> None

    # Step 1: Read all headers and collect required tags
    for h in header_paths:
        print(f"[*] Reading header file: {h}")
        with open(h, 'r', encoding='utf-8') as f:
            content = f.read()

        for match in tag_field_re.finditer(content):
            needed_offsets[(match.group(1), match.group(2))] = None

        for match in tag_rva_re.finditer(content):
            needed_rvas[(match.group(1), match.group(2))] = None

    if not needed_offsets and not needed_rvas:
        print("[-] No @[Class.Field] or @RVA[Class.Method] tags found in header files.")
        sys.exit(0)

    print(
        f"[*] Found {len(needed_offsets)} field tags and {len(needed_rvas)} "
        f"RVA tags. Scanning dump.cs..."
    )

    # Step 2: Scan dump.cs
    current_class = None
    last_rva = None
    last_rva_line = None
    last_float_offset = None
    found_dict_a = False

    class_def_re = re.compile(
        r"^\s*(?:public|private|protected|internal)?\s*(?:sealed|abstract|static)?\s*(?:class|struct)\s+([^:\s]+)"
    )
    field_re = re.compile(
        r"^\s*(?:public|private|protected|internal|static|readonly)*\s+"
        r"(?:[A-Za-z0-9_<>\[\], ]+)\s+([^;\s]+)\s*;\s*//\s*(0x[0-9A-Fa-f]+)"
    )
    rva_comment_re = re.compile(r"//\s*RVA:\s*(0x[0-9A-Fa-f]+)")
    method_re = re.compile(r"\s+([^\(\s]+)\s*\(")

    with open(dump_path, 'r', encoding='utf-8') as f:
        for line in f:
            c_match = class_def_re.search(line)
            if c_match:
                current_class = c_match.group(1)
                last_rva = None
                last_float_offset = None
                continue

            if current_class:
                # Check for field
                f_match = field_re.search(line)
                if f_match:
                    fname = f_match.group(1)
                    offset = f_match.group(2)

                    # Heuristic for Monster.ExpHeuristic
                    if "float " in line:
                        last_float_offset = offset
                    if "EStageType " in line and current_class == "Monster":
                        if last_float_offset and ("Monster", "ExpHeuristic") in needed_offsets:
                            needed_offsets[("Monster", "ExpHeuristic")] = last_float_offset

                    # Heuristic for StatContainer (formerly bam/bat)
                    if "Dictionary<StatType, float>" in line:
                        if not found_dict_a:
                            if ("StatContainer", "StatsDictA") in needed_offsets:
                                needed_offsets[("StatContainer", "StatsDictA")] = offset
                            found_dict_a = True
                        else:
                            if ("StatContainer", "StatsDictB") in needed_offsets:
                                needed_offsets[("StatContainer", "StatsDictB")] = offset

                    # Heuristic for SaveManager (AccountSaveData / PlayerSaveData)
                    if "AccountSaveData " in line:
                        if ("SaveManager", "AccountSaveData") in needed_offsets:
                            needed_offsets[("SaveManager", "AccountSaveData")] = offset
                    if "PlayerSaveData " in line:
                        if ("SaveManager", "PlayerSaveData") in needed_offsets:
                            needed_offsets[("SaveManager", "PlayerSaveData")] = offset

                    # Heuristic for StageManager.ChestDropDict
                    if "Dictionary<ValueTuple<EBoxType, EContentType>, float>" in line and current_class == "StageManager":
                        if ("StageManager", "ChestDropDict") in needed_offsets:
                            needed_offsets[("StageManager", "ChestDropDict")] = offset

                    # Heuristic for wg (HeroRef)
                    if current_class == "wg":
                        if "HeroInfoData " in line:
                            if ("wg", "HeroInfoDataRef") in needed_offsets:
                                needed_offsets[("wg", "HeroInfoDataRef")] = offset
                        if "Hero " in line:
                            if ("wg", "HeroBackRef") in needed_offsets:
                                needed_offsets[("wg", "HeroBackRef")] = offset

                    # Heuristic for wn (StatContainer k__BackingField)
                    if current_class == "wn":
                        if "k__BackingField" in line:
                            if ("wn", "StatContainerBackingField") in needed_offsets:
                                needed_offsets[("wn", "StatContainerBackingField")] = offset

                    if (current_class, fname) in needed_offsets:
                        needed_offsets[(current_class, fname)] = offset
                    last_rva = None
                    last_rva_line = None
                    continue

                # Check for RVA comment
                r_match = rva_comment_re.search(line)
                if r_match:
                    last_rva = r_match.group(1)
                    last_rva_line = line
                    continue

                # Check for method if we have an RVA
                if last_rva:
                    m_match = method_re.search(line)
                    if m_match:
                        mname = m_match.group(1)
                        if (current_class, mname) in needed_rvas:
                            # If a class has multiple overloads, take the first one found.
                            if needed_rvas[(current_class, mname)] is None:
                                needed_rvas[(current_class, mname)] = last_rva

                        # Heuristic for DetectorCore
                        if current_class in ["InjectionDetector", "SpeedHackDetector", "TimeCheatingDetector", "ObscuredCheatingDetector", "WallHackDetector"]:
                            if last_rva_line and "Slot: 18" in last_rva_line:
                                if (current_class, "DetectorCore") in needed_rvas:
                                    needed_rvas[(current_class, "DetectorCore")] = last_rva
                    last_rva = None
                    last_rva_line = None

    # Step 3: Verify missing
    missing = False
    for (cls, fld), off in needed_offsets.items():
        if off is None:
            print(f"  [!] Missing field offset for {cls}.{fld}")
            missing = True
        else:
            print(f"  [+] Field {cls}.{fld} -> {off}")

    for (cls, mth), rva in needed_rvas.items():
        if rva is None:
            print(f"  [!] Missing RVA for {cls}.{mth}")
            missing = True
        else:
            print(f"  [+] RVA {cls}.{mth} -> {rva}")

    if missing:
        print("[-] Not all offsets/RVAs were found. Make sure names match exactly.")

    # Step 4: Patch headers
    print("[*] Updating header files...")

    # regex matches: = 0xABC; // @[Class.Field]
    field_replace_re = re.compile(
        r"(=\s*)(0x[0-9A-Fa-f]+)(\s*;\s*//\s*@\[[^\]\.]+\.[^\]]+\])"
        r"(?: \[(?:CHANGED|UNCHANGED)\])?"
    )
    # regex matches: , 0xABC}, // @RVA[Class.Method]
    rva_replace_re = re.compile(
        r"(,\s*)(0x[0-9A-Fa-f]+)(\s*\}\s*,\s*//\s*@RVA\[[^\]\.]+\.[^\]]+\])"
        r"(?: \[(?:CHANGED|UNCHANGED)\])?"
    )

    def field_replacer(match):
        prefix = match.group(1)
        old_hex = match.group(2)
        suffix = match.group(3)
        m = re.search(r"@\[([^\]\.]+)\.([^\]]+)\]", suffix)
        if m:
            new_hex = needed_offsets.get((m.group(1), m.group(2)))
            if new_hex:
                status = "CHANGED" if old_hex.lower() != new_hex.lower() else "UNCHANGED"
                return f"{prefix}{new_hex}{suffix} [{status}]"
        return match.group(0)

    def rva_replacer(match):
        prefix = match.group(1)
        old_hex = match.group(2)
        suffix = match.group(3)
        m = re.search(r"@RVA\[([^\]\.]+)\.([^\]]+)\]", suffix)
        if m:
            new_hex = needed_rvas.get((m.group(1), m.group(2)))
            if new_hex:
                status = "CHANGED" if old_hex.lower() != new_hex.lower() else "UNCHANGED"
                return f"{prefix}{new_hex}{suffix} [{status}]"
        return match.group(0)

    for h in header_paths:
        with open(h, 'r', encoding='utf-8') as f:
            content = f.read()

        new_content = field_replace_re.sub(field_replacer, content)
        new_content = rva_replace_re.sub(rva_replacer, new_content)

        temp_h = h + ".tmp"
        with open(temp_h, 'w', encoding='utf-8') as f:
            f.write(new_content)
        os.replace(temp_h, h)

        print(f"[*] Successfully updated {h}!")

if __name__ == "__main__":
    main()
