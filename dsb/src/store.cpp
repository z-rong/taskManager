// store.cpp -- see store.h for the file format contract.

#include "store.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kDelayMagic = 0x314C4544u;  // 'DEL1', little endian "DEL1"

// Hard limits so that a corrupted file cannot make us allocate gigabytes.
constexpr std::uint64_t kMaxTasks     = 200000;
constexpr std::uint64_t kMaxStringLen = 16ull * 1024ull * 1024ull;

std::string readFile(const fs::path& path, bool& found) {
    found = false;
    std::error_code ec;
    if (!fs::exists(path, ec) || ec) return std::string();

    std::ifstream in(path, std::ios::binary);
    if (!in) return std::string();

    found = true;
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Serialises a value exactly the way the original program did: raw host
// representation (little endian x86_64, 8 byte size_t / time_t).
template <typename T>
void append(std::string& out, const T& value) {
    out.append(reinterpret_cast<const char*>(&value), sizeof(T));
}

// Byte cursor; every read is bounds checked so a damaged file can never
// produce a bogus allocation or an endless loop.
class Reader {
public:
    explicit Reader(const std::string& blob) : blob_(blob) {}

    bool        ok() const        { return ok_; }
    std::size_t remaining() const { return ok_ ? blob_.size() - pos_ : 0; }

    template <typename T>
    bool readValue(T& value) { return read(&value, sizeof(T)); }

    bool read(void* dst, std::size_t n) {
        if (!ok_ || n > remaining()) { ok_ = false; return false; }
        std::memcpy(dst, blob_.data() + pos_, n);
        pos_ += n;
        return true;
    }

    bool readString(std::uint64_t len, std::string& out) {
        if (len > kMaxStringLen || len > remaining()) { ok_ = false; return false; }
        out.assign(blob_, pos_, static_cast<std::size_t>(len));
        pos_ += static_cast<std::size_t>(len);
        return true;
    }

private:
    const std::string& blob_;
    std::size_t        pos_ = 0;
    bool               ok_  = true;
};

// ---------------------------------------------------------------------------
// delay.dat :  uint32 magic, uint32 count, then per entry int32 id, int64 when
// ---------------------------------------------------------------------------

std::string buildDelayBlob(const std::vector<Task>& tasks) {
    std::string blob;
    std::uint32_t count = 0;

    std::string body;
    for (const Task& t : tasks) {
        if (!t.delayed) continue;
        append(body, static_cast<std::int32_t>(t.id));
        append(body, static_cast<std::int64_t>(t.delayedTime));
        ++count;
    }

    append(blob, kDelayMagic);
    append(blob, count);
    blob += body;
    return blob;
}

void applyDelayBlob(const std::string& blob, std::vector<Task>& tasks,
                    std::vector<std::string>& warnings) {
    if (blob.empty()) return;

    Reader rd(blob);
    std::uint32_t magic = 0;
    if (!rd.readValue(magic) || magic != kDelayMagic) {
        warnings.push_back("delay.dat has an unknown header; postponements ignored");
        return;
    }

    std::uint32_t count = 0;
    if (!rd.readValue(count)) {
        warnings.push_back("delay.dat is truncated; postponements ignored");
        return;
    }
    if (count > kMaxTasks) {
        warnings.push_back("delay.dat claims an implausible entry count; ignored");
        return;
    }

    for (std::uint32_t i = 0; i < count; ++i) {
        std::int32_t  id = 0;
        std::int64_t  when = 0;
        if (!rd.readValue(id) || !rd.readValue(when)) {
            warnings.push_back("delay.dat is truncated; remaining postponements ignored");
            break;
        }
        auto it = std::find_if(tasks.begin(), tasks.end(),
                               [id](const Task& t) { return t.id == id; });
        if (it == tasks.end()) {
            warnings.push_back("delay.dat references unknown task id " +
                               std::to_string(id) + " (ignored)");
        } else {
            it->delayed     = true;
            it->delayedTime = static_cast<std::time_t>(when);
        }
    }
}

// ---------------------------------------------------------------------------
// Atomic file replacement: write "<file>.tmp", then rename over the target.
// A crash therefore leaves either the old or the new file, never a half file.
// ---------------------------------------------------------------------------

bool writeAtomic(const fs::path& path, const std::string& blob, std::string& error) {
    std::error_code ec;
    fs::path tmp = path;
    tmp += ".tmp";

    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "cannot open " + tmp.string() + " for writing";
            return false;
        }
        out.write(blob.data(), static_cast<std::streamsize>(blob.size()));
        out.flush();
        if (!out) {
            error = "write failed for " + tmp.string();
            out.close();
            fs::remove(tmp, ec);
            return false;
        }
    }

    fs::rename(tmp, path, ec);
    if (ec) {
        // Renaming over an existing file fails while another program holds the
        // file open.  Fall back to an in-place rewrite rather than losing the
        // user's edit; the tmp file is removed either way.
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        const bool wrote = static_cast<bool>(out);
        if (wrote) {
            out.write(blob.data(), static_cast<std::streamsize>(blob.size()));
            out.flush();
        }
        fs::remove(tmp, ec);
        if (!wrote) {
            error = "cannot replace " + path.string();
            return false;
        }
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------

TaskStore::TaskStore(std::string dataDir) : dataDir_(std::move(dataDir)) {}

std::string TaskStore::taskPath() const {
    return (fs::path(dataDir_) / "tasks.dat").string();
}

std::string TaskStore::delayPath() const {
    return (fs::path(dataDir_) / "delay.dat").string();
}

LoadResult TaskStore::load(std::vector<Task>& tasks, int& nextId) {
    LoadResult result;
    tasks.clear();
    nextId = 1;

    bool found = false;
    const std::string blob = readFile(taskPath(), found);
    result.fileFound = found;
    lastTaskBlob_ = blob;   // lets the first save() detect "nothing changed"

    if (found && !blob.empty()) {
        Reader rd(blob);

        std::uint64_t count = 0;
        std::int32_t  storedNextId = 1;
        if (!rd.readValue(count) || !rd.readValue(storedNextId)) {
            result.ok = false;
            result.error = "tasks.dat is truncated (header is incomplete)";
            return result;
        }

        if (count > kMaxTasks) {
            result.ok = false;
            result.error = "tasks.dat claims " + std::to_string(count) +
                           " tasks, which is implausible";
            return result;
        }

        // The notes flag was added after the first release; very old files end
        // right after nextId.
        bool hasNotes = false;
        if (rd.remaining() >= sizeof(bool)) {
            rd.readValue(hasNotes);
        } else {
            result.legacyNoNotes = true;
        }

        tasks.reserve(static_cast<std::size_t>(count));
        for (std::uint64_t i = 0; i < count; ++i) {
            Task t;
            std::int32_t  id = 0;
            bool          completed = false;
            std::int64_t  created = 0;
            std::int64_t  completedAt = 0;
            std::uint64_t descLen = 0;

            if (!rd.readValue(id) || !rd.readValue(completed) ||
                !rd.readValue(created) || !rd.readValue(completedAt) ||
                !rd.readValue(descLen) || !rd.readString(descLen, t.description)) {
                result.ok = false;
                result.error = "tasks.dat is damaged at task " + std::to_string(i + 1) +
                               " of " + std::to_string(count) + " - nothing was loaded";
                tasks.clear();
                return result;
            }

            t.id            = id;
            t.completed     = completed;
            t.createdTime   = static_cast<std::time_t>(created);
            t.completedTime = static_cast<std::time_t>(completedAt);

            if (hasNotes) {
                std::uint64_t notesLen = 0;
                if (!rd.readValue(notesLen) || !rd.readString(notesLen, t.notes)) {
                    result.ok = false;
                    result.error = "tasks.dat is damaged in the notes of task " +
                                   std::to_string(i + 1) + " - nothing was loaded";
                    tasks.clear();
                    return result;
                }
            }
            tasks.push_back(std::move(t));
        }

        // Anything after the last record is deliberately ignored: that is what
        // lets a future version append fields without breaking this one.
        if (rd.remaining() > 0) {
            result.warnings.push_back(std::to_string(rd.remaining()) +
                                      " trailing byte(s) in tasks.dat were ignored");
        }
        result.tasksRead = tasks.size();

        // nextId must stay ahead of every stored id, otherwise a newly added
        // task would collide with an existing one.
        int maxId = 0;
        for (const Task& t : tasks) maxId = std::max(maxId, t.id);
        if (storedNextId <= maxId) storedNextId = maxId + 1;
        nextId = storedNextId;
    }

    // Postponements live in their own file, so postponing never rewrites
    // tasks.dat and an old build of the program stays fully compatible.
    bool delayFound = false;
    const std::string delayBlob = readFile(delayPath(), delayFound);
    lastDelayBlob_ = delayBlob;
    applyDelayBlob(delayBlob, tasks, result.warnings);

    return result;
}

bool TaskStore::save(const std::vector<Task>& tasks, int nextId, std::string& error) {
    error.clear();

    std::string blob;
    blob.reserve(64 + tasks.size() * 64);

    append(blob, static_cast<std::uint64_t>(tasks.size()));
    append(blob, static_cast<std::int32_t>(nextId));
    append(blob, true);   // hasNotes

    for (const Task& t : tasks) {
        append(blob, static_cast<std::int32_t>(t.id));
        append(blob, static_cast<bool>(t.completed));
        append(blob, static_cast<std::int64_t>(t.createdTime));
        append(blob, static_cast<std::int64_t>(t.completedTime));

        append(blob, static_cast<std::uint64_t>(t.description.size()));
        blob += t.description;

        append(blob, static_cast<std::uint64_t>(t.notes.size()));
        blob += t.notes;
    }

    const std::string delayBlob = buildDelayBlob(tasks);

    // Turning "save after every keystroke" from an O(file) disk write into a
    // memory comparison.
    if (blob == lastTaskBlob_ && delayBlob == lastDelayBlob_) return true;

    std::string err;
    if (!writeAtomic(taskPath(), blob, err)) {
        error = err;
        return false;
    }
    lastTaskBlob_ = blob;

    constexpr std::size_t kEmptyDelaySize = sizeof(std::uint32_t) * 2;
    if (delayBlob.size() <= kEmptyDelaySize) {
        std::error_code ec;
        fs::remove(delayPath(), ec);   // no postponements -> no side file
        lastDelayBlob_.clear();
    } else {
        if (!writeAtomic(delayPath(), delayBlob, err)) {
            error = err;
            return false;
        }
        lastDelayBlob_ = delayBlob;
    }
    return true;
}
