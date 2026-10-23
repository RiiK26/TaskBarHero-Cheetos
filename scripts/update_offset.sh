#!/bin/bash
set -euo pipefail

# Navigate to the project root directory
cd "$(dirname "$0")/.."

DUMP_FILE="resources/dump/dump.cs"
HEADER_FILE="src/core/Il2CppOffsets.h"
AC_FILE="src/features/AntiCheatBypass.h"
UPDATER_SCRIPT="resources/tools/UpdateOffsets.py"

echo "==========================================="
echo "   TaskBarHero - Offset Auto-Updater"
echo "==========================================="

if [ ! -f "$DUMP_FILE" ]; then
    echo "[!] Error: Dump file not found at '$DUMP_FILE'"
    echo "[!] Please run Il2CppDumper first and place 'dump.cs' in the resources/dump/ folder!"
    exit 1
fi

if [ ! -f "$UPDATER_SCRIPT" ]; then
    echo "[!] Error: Updater script not found at '$UPDATER_SCRIPT'"
    exit 1
fi

echo "[*] Found dump.cs! Extracting offsets..."
if ! python3 "$UPDATER_SCRIPT" "$DUMP_FILE" "$HEADER_FILE" "$AC_FILE"; then
    echo "[!] Offset update failed: one or more tagged entries are missing."
    exit 1
fi

echo ""
echo "[*] Done! Don't forget to rebuild the project."
