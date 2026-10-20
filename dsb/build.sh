# Build script for the dsb task manager (run inside an MSYS2 UCRT64 shell).
#
#   ./build.sh            build task_manager.exe (dynamic, needs msys2 DLLs)
#   ./build.sh static     build task_manager_static.exe (no msys2 DLLs needed)
#   ./build.sh debug      build with symbols, for use in a debugger
#   ./build.sh test       build and run the store self test
#   ./build.sh clean      remove build output
#
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CXX="${CXX:-/ucrt64/bin/g++}"

SRC="$here/src/model.h $here/src/store.h $here/src/task_table.h $here/src/main_window.h"
SRC="$here/src/util.cpp $here/src/store.cpp $here/src/task_table.cpp $here/src/main_window.cpp $here/src/main.cpp"

FLTK_CFLAGS="-I/ucrt64/include"
FLTK_LIBS="-L/ucrt64/lib -lfltk -lole32 -lcomctl32 -lws2_32 -lwinspool -luuid -lgdiplus"
WARN="-Wall -Wextra -Wno-unused-parameter"
STD="-std=c++17"
OPT="${OPT:--O2}"
# The MSYS2 runtime objects (crt2.o, libmingw32.a, libmingwex.a) ship with DWARF
# debug info, so every binary linked here picks up ~165 KB of .debug_* sections
# plus a ~120 KB COFF symbol table that have nothing to do with this program.
#   -ffunction-sections/-fdata-sections + --gc-sections: drop unreachable code
#   -s: drop the symbol table and the debug sections
SIZE="-ffunction-sections -fdata-sections -Wl,--gc-sections"
STRIP="-s"

build() {
    local out="$1"; shift
    echo "==> $out"
    "$CXX" $STD $OPT $WARN $FLTK_CFLAGS \
        "$here/src/util.cpp" "$here/src/store.cpp" \
        "$here/src/task_table.cpp" "$here/src/main_window.cpp" "$here/src/main.cpp" \
        -o "$here/$out" -mwindows $SIZE $STRIP $FLTK_LIBS "$@"
}

build_tests() {
    local out="$here/tests/store_selftest.exe"
    echo "==> $out"
    "$CXX" $STD $OPT $WARN -I"$here/src" \
        "$here/src/util.cpp" "$here/src/store.cpp" "$here/tests/store_selftest.cpp" \
        -o "$out"

    local ui="$here/tests/ui_selftest.exe"
    echo "==> $ui"
    "$CXX" $STD $OPT $WARN -I"$here/src" \
        "$here/src/util.cpp" "$here/src/task_table.cpp" "$here/tests/ui_selftest.cpp" \
        -o "$ui" $FLTK_LIBS
}

run_tests() {
    build_tests
    echo
    "$here/tests/store_selftest.exe"
    local rc1=$?
    echo
    "$here/tests/ui_selftest.exe"
    local rc2=$?
    if [ $rc1 -ne 0 ] || [ $rc2 -ne 0 ]; then
        echo "TESTS FAILED (store=$rc1 ui=$rc2)"
        exit 1
    fi
    echo "all tests passed"
}

case "${1:-all}" in
    clean)
        rm -f "$here"/*.exe "$here"/*.o "$here"/*.tmp
        echo "cleaned"
        ;;
    test)
        run_tests
        ;;
    static)
        build task_manager_static.exe -static -lstdc++
        ;;
    debug)
        # Unstripped, for a debugger.  Noticeably bigger - that is the point.
        STRIP=""
        build task_manager_debug.exe
        ;;
    all)
        build task_manager.exe
        ;;
    *)
        build "$1"
        ;;
esac
