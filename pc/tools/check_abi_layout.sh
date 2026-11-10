#!/bin/sh
# Build the layout probe for the Linux server ABI and for armeabi-v7a, then diff them.
# A mismatch exits 1. The Android Gradle build runs this before packaging.
set -e
ROOT="$(CDPATH= cd "$(dirname "$0")/../.." && pwd)"
NDK="${ANDROID_NDK_HOME:-/tmp/android-sdk/ndk/26.1.10909125}"
CLANG="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/armv7a-linux-androideabi26-clang"
SYSROOT="$NDK/toolchains/llvm/prebuilt/linux-x86_64/sysroot"
OUT="${1:-/tmp/abi-layout}"
mkdir -p "$OUT"
INC="-I$ROOT/include -I$ROOT/pc/include -I$ROOT/pc/lib/glad/include"
DEFS="-DTARGET_PC -DVERSION=0 -DF3DEX_GBI_2 -D_LANGUAGE_C -DNDEBUG"
SRC="$ROOT/pc/tools/abi_layout.c"
echo "linux i386 -malign-double" >&2
gcc -m32 -malign-double $DEFS $INC -c -o "$OUT/abi_linux.o" "$SRC"
gcc -m32 -malign-double $DEFS $INC -o "$OUT/abi_linux" "$SRC"
echo "armeabi-v7a" >&2
"$CLANG" --sysroot="$SYSROOT" $DEFS $INC -c -o "$OUT/abi_arm.o" "$SRC"
OBJCOPY="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-objcopy"
"$OBJCOPY" -O binary --only-section=.abi_nums "$OUT/abi_linux.o" "$OUT/linux.bin"
"$OBJCOPY" -O binary --only-section=.abi_nums "$OUT/abi_arm.o" "$OUT/arm.bin"
"$OUT/abi_linux" > "$OUT/linux.txt"
if ! cmp -s "$OUT/linux.bin" "$OUT/arm.bin"; then
    echo "ABI LAYOUT MISMATCH: armeabi-v7a does not match the 32-bit server" >&2
    echo "---- linux server ----" >&2
    od -An -t u4 "$OUT/linux.bin" >&2
    echo "---- armeabi-v7a ----" >&2
    od -An -t u4 "$OUT/arm.bin" >&2
    exit 1
fi
echo "ABI layout matches (.abi_nums identical)"
cat "$OUT/linux.txt"
