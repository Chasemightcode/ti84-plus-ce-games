#!/bin/sh
# Build the PC preview of Neon Slice (needs: python -m pip install ziglang).
# The game sources are compiled unchanged against mock GraphX/KeypadC/FileIOC.
set -e
cd "$(dirname "$0")/.."
mkdir -p preview/out
python -m ziglang cc -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter \
    -DNS_HOST -Dmain=game_main \
    -Ipreview/mock -Ipreview -include preview/host_compat.h \
    src/*.c preview/mock_platform.c preview/host_main.c \
    -o preview/slice_preview.exe
echo "built preview/slice_preview.exe"
