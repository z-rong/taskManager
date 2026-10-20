// Task manager (dsb build).
//
// Usage:  task_manager.exe [--data-dir <dir>]
//
// The data directory defaults to the directory holding the executable, so the
// program picks up the very same tasks.dat as the original version when it is
// copied next to it, while a copy in a different directory keeps its own file.

#include "main_window.h"

#include <FL/Fl.H>
#include <FL/fl_ask.H>

#include <cstring>
#include <filesystem>
#include <string>

#if defined(_WIN32)
#  include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {

// Directory of the running executable; used as the data directory so the
// original task/tasks.dat is picked up automatically.
std::string executableDir() {
#if defined(_WIN32)
    char buffer[MAX_PATH] = {0};
    const DWORD n = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        return fs::path(buffer).parent_path().string();
    }
#endif
    std::error_code ec;
    fs::path p = fs::current_path(ec);
    return ec ? std::string(".") : p.string();
}

} // namespace

int main(int argc, char** argv) {
    Fl::option(Fl::OPTION_SHOW_TOOLTIPS, true);

    std::string dataDir = executableDir();
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--data-dir") == 0 && i + 1 < argc) {
            dataDir = argv[++i];
        } else if (std::strcmp(argv[i], "--help") == 0) {
            std::fprintf(stdout, "usage: %s [--data-dir <dir>]\n", argv[0]);
            return 0;
        }
    }

    std::error_code ec;
    if (!fs::exists(dataDir, ec)) {
        fl_alert("The data directory does not exist:\n%s", dataDir.c_str());
        return 2;
    }

    TaskManagerApp app(dataDir);
    app.show();
    return Fl::run();
}
