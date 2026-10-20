#include "model.h"

#include <cstdio>
#include <cstring>

namespace {

// Thread safe localtime.  The original code used localtime(), whose returned
// pointer is clobbered by the next call; this is both safer and faster.
bool localTime(std::time_t t, std::tm& out) {
#if defined(_WIN32)
    return localtime_s(&out, &t) == 0;
#else
    return localtime_r(&t, &out) != nullptr;
#endif
}

} // namespace

std::string formatTime(std::time_t t) {
    if (t == 0) return std::string();

    std::tm tmv;
    if (!localTime(t, tmv)) return std::string();

    char buffer[32];
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tmv) == 0)
        return std::string();
    return std::string(buffer);
}

std::string pluralCount(std::size_t n, const char* singular, const char* plural) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%llu %s", static_cast<unsigned long long>(n),
                  n == 1 ? singular : plural);
    return std::string(buf);
}

// ---------------------------------------------------------------------------
// State transitions
// ---------------------------------------------------------------------------

Task* findTask(std::vector<Task>& tasks, int id) {
    for (Task& t : tasks) {
        if (t.id == id) return &t;
    }
    return nullptr;
}

bool completeTask(std::vector<Task>& tasks, int id, std::time_t now) {
    Task* t = findTask(tasks, id);
    if (!t || t->completed) return false;

    t->completed = true;
    t->completedTime = now;
    // A finished task is no longer postponed: the delay list only holds work
    // that is still waiting to be done.
    t->delayed = false;
    t->delayedTime = 0;
    return true;
}

bool uncompleteTask(std::vector<Task>& tasks, int id) {
    Task* t = findTask(tasks, id);
    if (!t || !t->completed) return false;

    t->completed = false;
    t->completedTime = 0;
    return true;
}

bool postponeTask(std::vector<Task>& tasks, int id, std::time_t now) {
    Task* t = findTask(tasks, id);
    if (!t || t->completed || t->delayed) return false;

    t->delayed = true;
    t->delayedTime = now;
    return true;
}

bool restoreTask(std::vector<Task>& tasks, int id) {
    Task* t = findTask(tasks, id);
    if (!t || !t->delayed) return false;

    t->delayed = false;
    t->delayedTime = 0;
    return true;
}
