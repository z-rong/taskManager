// model.h -- domain model for the task manager (no FLTK dependency).
#pragma once

#include <ctime>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Task
// ---------------------------------------------------------------------------
//
// A task is either pending, postponed (the "delay" list) or done.  The three
// states are mutually exclusive and derived from two flags, which keeps the
// stored representation minimal and backwards compatible with the original
// tasks.dat layout.
//
enum class TaskState {
    Pending,
    Delayed,
    Done
};

struct Task {
    int         id            = 0;
    std::string description;
    bool        completed     = false;
    std::time_t createdTime   = 0;
    std::time_t completedTime = 0;   // 0 when not completed
    std::string notes;

    // Postponement.  Persisted in a side file (delay.dat) so that tasks.dat
    // keeps the exact byte layout the original program wrote and read.
    bool        delayed     = false;
    std::time_t delayedTime = 0;     // when the task was postponed

    TaskState state() const {
        if (completed) return TaskState::Done;
        if (delayed)   return TaskState::Delayed;
        return TaskState::Pending;
    }
};

// ---------------------------------------------------------------------------
// View configuration
// ---------------------------------------------------------------------------

enum class DisplayFilter {
    Pending,   // not done, not postponed
    Done,      // completed
    Delayed,   // postponed ("delay" list)
    All
};

enum class SortKey {
    None,       // file order (the order tasks were created in)
    Created,
    Completed
};

struct SortSpec {
    SortKey key       = SortKey::None;
    bool    ascending = true;
};

// Human readable, locale independent formatting.  Returns "" for 0.
std::string formatTime(std::time_t t);

// "3 tasks" / "1 task"
std::string pluralCount(std::size_t n, const char* singular, const char* plural);

// ---------------------------------------------------------------------------
// State transitions (policy, kept out of the GUI so it can be tested)
// ---------------------------------------------------------------------------
//
// Every function returns false when the requested change does not apply, e.g.
// completing a task that is already finished.  The caller can use that to show
// an explanation instead of silently doing nothing.

bool completeTask(std::vector<Task>& tasks, int id, std::time_t now);
bool uncompleteTask(std::vector<Task>& tasks, int id);

// Moves a task to the delay list.  `now` is recorded so the list can show when
// the task was postponed.
bool postponeTask(std::vector<Task>& tasks, int id, std::time_t now);

// Moves a postponed task back to the pending list.
bool restoreTask(std::vector<Task>& tasks, int id);

// Finds a task by id; returns nullptr when it does not exist.
Task* findTask(std::vector<Task>& tasks, int id);
