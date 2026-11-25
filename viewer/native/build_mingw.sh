#!/bin/sh
set -eu

source_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
output=${1:-"$source_dir/../../dist/AirConflicts_Viewer_Windows_x64.exe"}
mkdir -p -- "$(dirname -- "$output")"

"${ACPC_CC:-x86_64-w64-mingw32-gcc}" \
    -std=c11 -O2 -Wall -Wextra -Wpedantic -D_WIN32_WINNT=0x0601 \
    -static -s -municode -mwindows \
    "$source_dir/model.c" "$source_dir/texture.c" "$source_dir/render.c" "$source_dir/windows.c" \
    -Wl,--dynamicbase,--nxcompat,--high-entropy-va,--no-insert-timestamp \
    -lcomctl32 -lcomdlg32 -lshell32 -lgdi32 -lgdiplus -o "$output"
