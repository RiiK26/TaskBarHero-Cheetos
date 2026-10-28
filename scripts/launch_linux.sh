#!/bin/bash
EXE_NAME="TBH-Cheetos.exe"
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ -f "$DIR/$EXE_NAME" ]; then
    EXE_PATH="$DIR/$EXE_NAME"
elif [ -f "$DIR/build/$EXE_NAME" ]; then
    EXE_PATH="$DIR/build/$EXE_NAME"
elif [ -f "$DIR/../build/$EXE_NAME" ]; then
    EXE_PATH="$DIR/../build/$EXE_NAME"
else
    echo "Error: $EXE_NAME not found! Please build the project or place this script next to the executable."
    exit 1
fi

if ! command -v protontricks &> /dev/null; then
    echo "Error: protontricks not found. Please install protontricks."
    exit 1
fi

# The Steam App ID for TaskBarHero
APP_ID=3678970
EXE_DIR="$(cd "$(dirname "$EXE_PATH")" && pwd)"
echo "Launching $EXE_NAME from $EXE_DIR via protontricks (AppID: $APP_ID)..."
cd "$EXE_DIR"
PROTON_DEBUG=1 protontricks -c "wine \"$EXE_NAME\"" $APP_ID
