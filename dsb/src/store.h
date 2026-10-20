// store.h -- persistence for the task manager.
#pragma once

#include "model.h"

#include <string>
#include <vector>

struct LoadResult {
    bool        fileFound = false;   // tasks.dat existed
    bool        ok        = true;    // false -> file was unusable, data reset
    bool        legacyNoNotes = false;
    std::size_t tasksRead = 0;
    std::string error;                 // human readable, empty when ok
    std::vector<std::string> warnings;
};

// Persists task data.
//
//  * tasks.dat  keeps the exact byte layout of the original program:
//        size_t count; int nextId; bool hasNotes;
//        per task: int id; bool completed; time_t created; time_t completed;
//                  size_t descLen; desc; size_t notesLen; notes;
//    So an old task_manager_gui.exe can still read what we write.
//
//  * delay.dat  is a side file holding only the postponement state
//    (id -> delayedTime), so the postpone feature cannot corrupt the
//    legacy file if it is ever read by the original program.
class TaskStore {
public:
    explicit TaskStore(std::string dataDir);

    const std::string& dataDir() const { return dataDir_; }
    std::string taskPath() const;
    std::string delayPath() const;

    // Loads tasks.dat (if present) plus delay.dat.  Never throws.
    LoadResult load(std::vector<Task>& tasks, int& nextId);

    // Writes both files atomically (tmp file + rename).  Unchanged content is
    // skipped, so repeated saves cost nothing.  Returns false and fills `error`
    // on failure; the previous file is left intact in that case.
    bool save(const std::vector<Task>& tasks, int nextId, std::string& error);

private:
    std::string dataDir_;
    std::string lastTaskBlob_;
    std::string lastDelayBlob_;
};
