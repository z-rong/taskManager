# task_manager (dsb build)

An optimized rewrite of `task/task_manager_gui.cpp`: same idea, same file
format, but with header-click sorting, a **delay** list, a tested persistence
layer and a documented set of fixes for the problems found in the original.

**Nothing under `task/` was modified.** This directory only *reads*
`task/tasks.dat` (via the copy in this directory) and never writes to it.

```
task/
├── task_manager_gui.cpp     <- original, untouched
├── tasks.dat                <- original, untouched
└── dsb/                     <- everything below is new
    ├── src/                 the application (5 .cpp + 4 .h)
    ├── tests/               self tests (no mouse, no display needed)
    ├── tools/               helper scripts used to drive/screenshot the GUI,
    │                        plus the start-up measurement tools
    ├── screenshots/         the pictures referenced below
    ├── reference/           the untouched original source, plus the FLTK
    │                        1.4.5 sources that were read while porting
    ├── build.sh / build.cmd build scripts
    ├── OPTIMIZATION_ANALYSIS.md   what was wrong in the original, and why
    ├── tasks.dat            a copy of the original data file, for testing
    └── task_manager.exe     the built program
```

## Building

MSYS2 with the UCRT64 toolchain and FLTK 1.4 is expected (that is what is
installed on this machine).

```sh
# from an MSYS2 UCRT64 shell
./build.sh            # task_manager.exe       (needs the msys2 DLLs on PATH)
./build.sh static     # task_manager_static.exe (no msys2 DLLs needed)
./build.sh debug      # task_manager_debug.exe  (keeps symbols, for a debugger)
./build.sh test       # build and run both self tests
./build.sh clean
```

From a normal Windows shell:

```bat
build.cmd
build.cmd test
```

The build is warning-clean with `-Wall -Wextra`.

### Why the binary is the size it is

Built as-is the dynamic executable is 428 KB, which looks large next to the
original's 67 KB. Almost none of it is this program:

| Part | Bytes | Share |
| --- | ---: | ---: |
| loadable code + data | 142 504 | 33 % |
| DWARF debug info | 165 155 | 39 % |
| COFF symbol table + headers | 120 195 | 28 % |

Both of the last two are dead weight. MSYS2's runtime objects (`crt2.o`,
`libmingw32.a`, `libmingwex.a`) are **shipped with DWARF info compiled in**, so
the linker merges ~165 KB of `.debug_*` sections into every executable it
produces - a one-line `int main(){return 0;}` linked with this toolchain already
carries 74 KB of it. On top of that, without `-s` the COFF symbol table (3 059
symbols here) stays in the file.

The build therefore passes `-ffunction-sections -fdata-sections
-Wl,--gc-sections -s`, which drops unreachable code, the symbol table and the
debug sections:

| Build | Before | Now |
| --- | ---: | ---: |
| `task_manager.exe` | 427 854 | **143 360** |
| `task_manager_static.exe` | 4 652 027 | **2 031 616** |

`./build.sh debug` keeps everything when you actually want a stack trace
(445 KB). For a further ~30 % you can drop optimisation to size:
`OPT=-Os ./build.sh` gives 98 816 bytes - this program is I/O and UI bound, so
that is a reasonable trade if you want the smallest binary.

## Running

```
task_manager.exe [--data-dir <dir>]
```

The data directory defaults to the folder holding the executable, so the
program picks up `tasks.dat` next to it. Use `--data-dir` to point at
`task/` and work on the real data.

## Keys

| Key | Action |
| --- | --- |
| `i` | jump to the task entry field (`Esc` returns to the list) |
| `Enter` in the input | add the task |
| `Up` / `Down` | select a task |
| `Enter` in the list | open the notes editor for the selected task |
| `d` | mark the selected task finished |
| `c` | re-open a finished task |
| **`p`** | **postpone: move the selected task to the delay list** |
| **`q`** | **restore the selected task from the delay list** |
| **`b`** | **show the delay list** |
| `l` / `h` / `f` | pending / finished / all tasks |
| `v` | cycle sorting: created -> finished -> file order |
| click **Created Time** or **Finished Time** | sort by that column; click again to reverse |
| `Del` | delete the selected task (asks first) |
| `F1` or `?` | shortcut help |
| `Esc` | back to the list |

`Ctrl+F` / `Ctrl+L` / `Ctrl+H` / `Ctrl+B` do the same as `f` / `l` / `h` / `b`.

The current sort is shown in the column header (`^` ascending, `v`
descending); the status line reports the state counts (`pending / delay / done`).

### Delay list semantics

A task is in exactly one of three states: **pending**, **delayed** or
**done**.

* `p` on a pending task moves it to the delay list and records the moment it
  was postponed (shown in the *Delayed At* column).
* A postponed task is **not** shown in the pending list; it is only in the
  delay list (`b`).
* `q` moves it back to the pending list and clears the postponed timestamp.
* A finished task cannot be postponed; completing a postponed task removes it
  from the delay list.

## Data files

`tasks.dat` keeps the **exact byte layout the original program used**, so both
programs can read the same file:

```
size_t count; int nextId; bool hasNotes;
per task: int id; bool completed; time_t created; time_t completed;
          size_t descLen; desc; size_t notesLen; notes;
```

The postponement state lives in a separate `delay.dat` file
(`uint32 magic 'DEL1'; uint32 count; { int32 id; int64 delayedAt; }...`).
That way postponing a task never rewrites the legacy file, an old build of the
program stays fully compatible, and a damaged `delay.dat` can never cost the
task list. When no task is postponed, `delay.dat` is removed again.

Both files are written atomically (`<file>.tmp` + rename), and a save whose
content did not change is skipped entirely, so saving after every keystroke is
free. A damaged `tasks.dat` is refused as a whole - the program never loads
half a list.

## Tests

```sh
./build.sh test
```

* `tests/store_selftest.cpp` - persistence: byte-for-byte round trip of the
  real `tasks.dat` (read-only!), delay persistence across a restart, state
  transitions, next-id recovery, corrupt/truncated files, forward
  compatibility with trailing bytes, skip-unchanged saves.
* `tests/ui_selftest.cpp` - the table widget: which columns each list shows,
  synthetic header clicks reaching the sort callback, ascending/descending
  order, "never finished" sorting last, and the selection staying on the same
  *task* after re-sorting.

Current status: **101 + 29 checks, 0 failures.**

## Start-up performance

The program is not slow to start. Measured end to end (process creation -> the
window answers events) it takes about **55-75 ms**:

| Phase | Cost | What it is |
| --- | --- | --- |
| loader | ~16 ms | PE mapping, FLTK/msys2 DLL resolution and loading, CRT init |
| fltk | 3-8 ms | display, theme and font initialisation |
| data | 0.6 ms (30 tasks) / 7 ms (20 000 tasks) | reading `tasks.dat` |
| widgets | 0.05 ms | building the widget tree |
| `show()` | 30-45 ms | creating the Win32 window (the single biggest item) |
| first `Fl::wait()` | 2-7 ms | first paint, after which the window reacts |

`tools/probe_loader.exe` prints exactly this breakdown; it timestamps against
the *kernel's* process-creation time, so DLL loading before `main()` is
included. `tools/launch_bench.exe` measures the same thing from outside, by
creating the process and polling for a visible window with `EnumWindows`.

Both were written because the obvious measurement - PowerShell's
`Start-Process -PassThru` plus `Process.MainWindowHandle` - reports nonsense
here: it gave 1.7 s, 10 s, 32 s and outright "HANG" for the very same runs that
the native tools time at 32 ms. `Process.MainWindowHandle` caches its value, a
GUI child that inherits a captured pipe keeps the harness waiting until it
exits, and `ShellExecuteEx` reports `ERROR_CANCELLED` from a console host that
is not pumping messages. None of that is the program's cost.

Scaling check: 20 000 tasks (2.4 MB) add ~6 ms of reading and change nothing
else, so a big `tasks.dat` is not a start-up problem either.

## Verification screenshots

`tools/ui_driver.ps1` is the script used to drive and photograph the running
program on this machine while the work was done:

```powershell
# steps separated by ';' - key:<text>, click:<client x>,<client y>, shot:<file>
powershell -File tools/ui_driver.ps1 -Dir screenshots `
    -Steps "click:400,29;key:new task from dsb;key:{ENTER};shot:01-add-task.png"
```

It sends **real** keystrokes and mouse clicks (so the shortcuts and the header
clicks are exercised exactly as a user would), converts logical coordinates to
physical ones for the 200% display, and grabs the window with `PrintWindow`.
Windows refuses `SetForegroundWindow()` calls from a background process, so the
script retries and *verifies* the window really owns the keyboard before every
keystroke - otherwise the keys would silently land in whatever window happens to
be in front. It is a development helper, not part of the program.

| Screenshot | Shows |
| --- | --- |
| `screenshots/01-add-task.png` | a task added through the entry field |
| `screenshots/02-postpone-p.png` | `p` moved the selected task to the delay list (status line + counters) |
| `screenshots/03-delay-list-b.png` | `b` shows the delay list, with the *Delayed At* column filled in |
| `screenshots/04-restore-q.png` | after `q`: the delay list is empty again and the task is back in pending |
| `screenshots/05-all-columns.png` | the "all tasks" view with every column (S / ID / Description / Created / Finished / Delayed) |
| `screenshots/06-finished-sorted.png` | the finished list sorted by *Finished Time*, descending |
| `screenshots/07-header-click-sort.png` | clicking the *Created Time* header sorted the pending list by it |

The screenshots were taken on a 200%-scaled display, which is also how the
"status bar off-screen" problem of the original was found.
