#include "main_window.h"

#include <FL/Fl.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <cstdio>

namespace {

const char* const kLegend =
    "i input   Up/Down select   Enter detail   d done   c undo   p postpone   "
    "q restore   b delay list   Del delete   v sort   F1 help";

// Callback for the notes editor, so that any edit marks the session dirty.
void detailModified_cb(int, int, int, int, const char*, void*) {}

} // namespace

// ---------------------------------------------------------------------------
// MainWindow
// ---------------------------------------------------------------------------

MainWindow::MainWindow(TaskManagerApp* app, int W, int H, const char* L)
    : Fl_Window(W, H, L), app_(app) {}

void MainWindow::resize(int X, int Y, int W, int H) {
    Fl_Window::resize(X, Y, W, H);
    if (app_) app_->layout();
}

int MainWindow::handle(int event) {
    // Keys are handled here, before FLTK offers them to the focused widget.
    // This is what makes the single letter shortcuts work regardless of which
    // widget currently owns the focus, and it also lets the application decide
    // when a letter is text (entry field) and when it is a command.
    if (event == FL_KEYDOWN && app_) {
        if (app_->handleKey(Fl::event_key(), Fl::event_state())) return 1;
    }
    return Fl_Window::handle(event);
}

// ---------------------------------------------------------------------------
// TaskInput
// ---------------------------------------------------------------------------

TaskInput::TaskInput(TaskManagerApp* app, int X, int Y, int W, int H, const char* l)
    : Fl_Input(X, Y, W, H, l), app_(app) {}

int TaskInput::handle(int event) {
    // Esc and the arrow keys leave the entry field and return to the list;
    // every other key is ordinary text and is left to Fl_Input.
    if (event == FL_KEYDOWN && app_) {
        const int key = Fl::event_key();
        if (key == FL_Escape || key == FL_Up || key == FL_Down) {
            app_->focusList();
            if (key == FL_Up)   app_->moveListSelection(-1);
            if (key == FL_Down) app_->moveListSelection(+1);
            return 1;
        }
    }
    return Fl_Input::handle(event);
}

// ---------------------------------------------------------------------------
// TaskManagerApp -- construction
// ---------------------------------------------------------------------------

TaskManagerApp::TaskManagerApp(std::string dataDir) : store_(std::move(dataDir)) {
    const LoadResult res = store_.load(tasks_, nextId_);

    std::string startup;
    if (!res.ok) {
        startup = "PROBLEM: " + res.error + "   (the file was left untouched)";
    } else if (!res.warnings.empty()) {
        startup = res.warnings.front();
        if (res.warnings.size() > 1)
            startup += " (+" + std::to_string(res.warnings.size() - 1) + " more)";
    } else if (res.fileFound) {
        const std::string delayInfo =
            countOf(TaskState::Delayed) > 0
                ? std::string(", ") + pluralCount(countOf(TaskState::Delayed),
                                                  "task postponed", "tasks postponed")
                : std::string();
        startup = "Loaded " + pluralCount(res.tasksRead, "task", "tasks") + delayInfo +
                  " from " + store_.taskPath();
    } else {
        startup = "No task file found at " + store_.taskPath() +
                  " - add your first task below (i)";
    }

    buildWidgets(940, 620);
    applyView();
    setMessage(startup);
}

TaskManagerApp::~TaskManagerApp() {
    if (detailGroup_ && detailGroup_->visible()) closeDetail();
    save();
    delete detailBuffer_;
    delete window_;
}

void TaskManagerApp::show() {
    // Fit the window to the work area.
    //
    // Window sizes are expressed in FLTK units while Fl::screen_work_area()
    // reports physical pixels, so the work area has to be divided by the screen
    // scale.  Without this the 940x620 default becomes 1880x1240 physical on a
    // 200% display and the status bar lands off the bottom of the screen.
    int best = 0;
    long bestArea = -1;
    for (int i = 0; i < Fl::screen_count(); ++i) {
        int x = 0, y = 0, w = 0, h = 0;
        Fl::screen_work_area(x, y, w, h, i);
        const long area = static_cast<long>(w) * static_cast<long>(h);
        if (area > bestArea) { bestArea = area; best = i; }
    }

    int sx = 0, sy = 0, sw = 0, sh = 0;
    Fl::screen_work_area(sx, sy, sw, sh, best);

    float scale = Fl::screen_scale(best);
    if (scale <= 0.0f) scale = 1.0f;

    const int maxW = static_cast<int>(sw / scale) - 24;
    const int maxH = static_cast<int>(sh / scale) - 40;

    int w = window_->w();
    int h = window_->h();
    if (maxW > 320) w = std::min(w, maxW);
    if (maxH > 240) h = std::min(h, maxH);
    w = std::max(w, 560);
    h = std::max(h, 320);

    if (w != window_->w() || h != window_->h())
        window_->resize(window_->x(), window_->y(), w, h);

    // Let FLTK/Windows choose the position: window coordinates and
    // Fl::screen_work_area() do not share a unit (FLTK units vs. physical
    // pixels), so computing a centre by hand lands the window off-screen on
    // high DPI displays.  The default placement is always on a visible screen.
    window_->show();

    // Now that a screen has been chosen, pull the window fully into view.
    int wx = 0, wy = 0, ww = 0, wh = 0;
    Fl::screen_work_area(wx, wy, ww, wh, Fl::screen_num(window_->x(), window_->y()));
    const float sc = Fl::screen_scale(Fl::screen_num(window_->x(), window_->y()));
    const float fs = (sc > 0.0f) ? sc : 1.0f;
    const int wPhys = static_cast<int>(window_->w() * fs);
    const int hPhys = static_cast<int>(window_->h() * fs);

    int nx = window_->x();
    int ny = window_->y();
    if (nx * fs + wPhys > wx + ww) nx = static_cast<int>((wx + ww - wPhys) / fs);
    if (ny * fs + hPhys > wy + wh) ny = static_cast<int>((wy + wh - hPhys) / fs);
    if (nx * fs < wx) nx = static_cast<int>(wx / fs);
    if (ny * fs < wy) ny = static_cast<int>(wy / fs);
    if (nx != window_->x() || ny != window_->y()) window_->position(nx, ny);

    // Start with the list focused, not the entry field.  FLTK sends a key to
    // the focused widget first and only then to its parents, so a focused
    // Fl_Input would swallow every single-letter shortcut (d/c/p/q/b/v/...).
    // Pressing "i" moves to the entry field when a task has to be typed.
    if (tasks_.empty()) {
        input_->take_focus();
    } else {
        table_->focusFirstRow();
    }
}

// ---------------------------------------------------------------------------
// Construction / layout
// ---------------------------------------------------------------------------

void TaskManagerApp::buildWidgets(int W, int H) {
    window_ = new MainWindow(this, W, H, "Task Manager (dsb)");
    window_->color(FL_WHITE);
    window_->size_range(720, 380);
    window_->callback([](Fl_Widget* w, void* data) {
        auto* app = static_cast<TaskManagerApp*>(data);
        if (Fl::event() == FL_CLOSE) app->closeWindow();
    }, this);

    // ---- list view -------------------------------------------------------
    listGroup_ = new Fl_Group(0, 0, W, H);

    input_ = new Fl_Input(20, 12, W - 40, 34, "");
    input_->textsize(14);
    input_->tooltip("Type a task description and press Enter to add it "
                    "(Esc returns to the list)");
    input_->when(FL_WHEN_ENTER_KEY);
    input_->callback([](Fl_Widget* w, void* data) {
        auto* app = static_cast<TaskManagerApp*>(data);
        app->addTask(app->input_->value());
    }, this);

    table_ = new TaskTable(20, 58, W - 40, H - 122);
    table_->setTasks(&tasks_);
    table_->setSortRequestHandler([](void* user, SortKey key) {
        static_cast<TaskManagerApp*>(user)->requestSort(key);
    }, this);

    messageBox_ = new Fl_Box(20, H - 62, W - 40, 20);
    messageBox_->box(FL_FLAT_BOX);
    messageBox_->color(FL_WHITE);
    messageBox_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    messageBox_->labelsize(12);

    legendBox_ = new Fl_Box(20, H - 40, W - 40, 20);
    legendBox_->box(FL_FLAT_BOX);
    legendBox_->color(FL_WHITE);
    legendBox_->labelcolor(fl_rgb_color(80, 80, 80));
    legendBox_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    legendBox_->labelsize(11);
    legendBox_->copy_label(kLegend);

    listGroup_->end();

    // ---- detail view -----------------------------------------------------
    detailGroup_ = new Fl_Group(0, 0, W, H);
    detailGroup_->hide();

    detailTitle_ = new Fl_Box(20, 12, W - 40, 26);
    detailTitle_->labelfont(FL_BOLD);
    detailTitle_->labelsize(14);
    detailTitle_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    detailBuffer_ = new Fl_Text_Buffer();
    detailBuffer_->add_modify_callback(detailModified_cb, this);
    detailEditor_ = new Fl_Text_Editor(20, 46, W - 40, H - 96);
    detailEditor_->buffer(detailBuffer_);
    detailEditor_->textsize(13);
    detailEditor_->wrap_mode(Fl_Text_Editor::WRAP_AT_BOUNDS, 0);

    detailHint_ = new Fl_Box(20, H - 40, W - 40, 20,
                             "Esc: back to the list (notes are saved automatically)");
    detailHint_->labelsize(11);
    detailHint_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    detailGroup_->end();

    window_->end();
    window_->resizable(window_);
}

void TaskManagerApp::layout() {
    if (!window_ || !listGroup_ || !detailGroup_) return;   // called during construction

    const int w = window_->w();
    const int h = window_->h();

    listGroup_->resize(0, 0, w, h);
    input_->resize(20, 12, w - 40, 34);
    table_->resize(20, 58, w - 40, std::max(h - 122, 120));
    messageBox_->resize(20, h - 62, w - 40, 20);
    legendBox_->resize(20, h - 40, w - 40, 20);

    detailGroup_->resize(0, 0, w, h);
    detailTitle_->resize(20, 12, w - 40, 26);
    detailEditor_->resize(20, 46, w - 40, std::max(h - 96, 120));
    detailHint_->resize(20, h - 40, w - 40, 20);
}

// ---------------------------------------------------------------------------
// View state
// ---------------------------------------------------------------------------

void TaskManagerApp::applyView() {
    table_->setView(filter_, sort_);
    updateLegend();
}

void TaskManagerApp::setFilter(DisplayFilter f) {
    filter_ = f;
    applyView();
    table_->focusFirstRow();
    setMessage(std::string("Showing ") + filterName(filter_) + " (" +
               std::to_string(table_->displayedCount()) + ")");
}

void TaskManagerApp::requestSort(SortKey key) {
    if (key == SortKey::None) return;

    if (sort_.key == key) {
        sort_.ascending = !sort_.ascending;   // same column -> flip direction
    } else {
        sort_.key = key;
        sort_.ascending = (key == SortKey::Created);   // oldest first is the useful default
    }

    table_->setView(filter_, sort_);
    table_->focusFirstRow();
    setMessage(std::string("Sorted by ") + sortName() +
               (sort_.ascending ? " (ascending)" : " (descending)") +
               " - click the header again to reverse, press v to cycle");
}

void TaskManagerApp::cycleSort() {
    // v cycles: file order -> created -> finished -> file order.
    // This is the keyboard equivalent of clicking a column header.
    switch (sort_.key) {
        case SortKey::None:
            requestSort(SortKey::Created);
            break;
        case SortKey::Created:
            requestSort(SortKey::Completed);
            break;
        case SortKey::Completed:
        default:
            sort_.key = SortKey::None;
            sort_.ascending = true;
            table_->setView(filter_, sort_);
            table_->focusFirstRow();
            setMessage("Sorted in original (file) order");
            break;
    }
}

const char* TaskManagerApp::filterName(DisplayFilter f) const {
    switch (f) {
        case DisplayFilter::Pending: return "pending tasks (l)";
        case DisplayFilter::Done:    return "finished tasks (h)";
        case DisplayFilter::Delayed: return "delay list (b)";
        case DisplayFilter::All:
        default:                     return "all tasks (f)";
    }
}

const char* TaskManagerApp::sortName() const {
    switch (sort_.key) {
        case SortKey::Created:  return "Created Time";
        case SortKey::Completed: return "Finished Time";
        default:                return "file order";
    }
}

void TaskManagerApp::updateLegend() {
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "pending %d | delay %d | done %d   -   %s",
                  countOf(TaskState::Pending), countOf(TaskState::Delayed),
                  countOf(TaskState::Done), kLegend);
    legendBox_->copy_label(buf);
}

void TaskManagerApp::setMessage(const std::string& text) {
    messageBox_->copy_label(text.c_str());
    window_->redraw();
}

// ---------------------------------------------------------------------------
// Model operations
// ---------------------------------------------------------------------------

int TaskManagerApp::countOf(TaskState state) const {
    int n = 0;
    for (const Task& t : tasks_) if (t.state() == state) ++n;
    return n;
}

Task* TaskManagerApp::selectedTask() {
    const int idx = table_->selectedTaskIndex();
    if (idx < 0 || idx >= static_cast<int>(tasks_.size())) return nullptr;
    return &tasks_[idx];
}

void TaskManagerApp::save() {
    std::string error;
    if (!store_.save(tasks_, nextId_, error)) {
        setMessage("SAVE FAILED: " + error);
    }
}

void TaskManagerApp::addTask(const std::string& description) {
    std::string desc = description;
    // Trim surrounding whitespace; the original rejected only empty strings.
    const std::size_t first = desc.find_first_not_of(" \t\r\n");
    const std::size_t last  = desc.find_last_not_of(" \t\r\n");
    if (first == std::string::npos) {
        setMessage("A task description cannot be empty");
        return;
    }
    desc = desc.substr(first, last - first + 1);

    Task t;
    t.id = nextId_++;
    t.description = desc;
    t.createdTime = std::time(nullptr);
    tasks_.push_back(std::move(t));
    const int newId = tasks_.back().id;

    save();
    filter_ = DisplayFilter::Pending;
    applyView();
    table_->selectTaskId(newId);
    input_->value("");

    // Hand the keyboard back to the list, exactly like the original did: after
    // this the shortcuts (d/c/p/q/b/...) are active again, and "i" returns to
    // the entry field for the next task.
    focusList();
    setMessage("Added: " + desc);
}

void TaskManagerApp::completeSelected() {
    Task* t = selectedTask();
    if (!t) { setMessage("Select a task first (Up/Down)"); return; }

    const int id = t->id;
    const std::string desc = t->description;
    if (!completeTask(tasks_, id, std::time(nullptr))) {
        setMessage("That task is already finished");
        return;
    }
    save();
    applyView();
    table_->selectTaskId(id);
    table_->focusFirstRow();
    setMessage("Finished: " + desc);
    updateLegend();
}

void TaskManagerApp::uncompleteSelected() {
    Task* t = selectedTask();
    if (!t) { setMessage("Select a task first (Up/Down)"); return; }

    const int id = t->id;
    const std::string desc = t->description;
    if (!uncompleteTask(tasks_, id)) {
        setMessage("That task is not finished yet");
        return;
    }
    save();
    applyView();
    table_->selectTaskId(id);
    table_->focusFirstRow();
    setMessage("Re-opened: " + desc);
    updateLegend();
}

void TaskManagerApp::postponeSelected() {
    Task* t = selectedTask();
    if (!t) { setMessage("Select a task first (Up/Down)"); return; }

    const int id = t->id;
    const std::string desc = t->description;
    if (!postponeTask(tasks_, id, std::time(nullptr))) {
        setMessage(t->completed ? "A finished task cannot be postponed"
                                : "That task is already in the delay list");
        return;
    }
    save();
    applyView();
    table_->focusFirstRow();
    setMessage("Postponed: " + desc + "   (b opens the delay list, q restores)");
    updateLegend();
}

void TaskManagerApp::restoreSelected() {
    Task* t = selectedTask();
    if (!t) { setMessage("Select a task first (Up/Down)"); return; }

    const int id = t->id;
    const std::string desc = t->description;
    if (!restoreTask(tasks_, id)) {
        setMessage("That task is not in the delay list");
        return;
    }
    save();
    applyView();
    table_->focusFirstRow();
    setMessage("Restored to the pending list: " + desc);
    updateLegend();
}

void TaskManagerApp::deleteSelected() {
    const int idx = table_->selectedTaskIndex();
    if (idx < 0 || idx >= static_cast<int>(tasks_.size())) {
        setMessage("Select a task first (Up/Down)");
        return;
    }

    const Task& victim = tasks_[idx];
    const char* what = victim.state() == TaskState::Delayed ? "postponed task"
                     : victim.state() == TaskState::Done    ? "finished task"
                                                            : "task";
    // Delete is destructive and the original code did it without asking.
    if (!fl_choice("Delete this %s?\n\n%s", "Cancel", "Delete", nullptr,
                   what, victim.description.c_str())) {
        setMessage("Delete cancelled");
        return;
    }

    const std::string desc = victim.description;
    tasks_.erase(tasks_.begin() + idx);
    save();
    applyView();
    table_->focusFirstRow();
    setMessage("Deleted (" + std::string(what) + "): " + desc);
    updateLegend();
}

void TaskManagerApp::openDetail() {
    const int idx = table_->selectedTaskIndex();
    if (idx < 0 || idx >= static_cast<int>(tasks_.size())) {
        setMessage("Select a task first (Up/Down)");
        return;
    }

    detailTaskId_ = tasks_[idx].id;
    const std::string title = "#" + std::to_string(tasks_[idx].id) + "  " +
                              tasks_[idx].description;
    detailTitle_->copy_label(title.c_str());
    detailBuffer_->text(tasks_[idx].notes.c_str());

    listGroup_->hide();
    detailGroup_->show();
    detailEditor_->take_focus();
    window_->redraw();
}

void TaskManagerApp::closeDetail() {
    if (detailTaskId_ >= 0) {
        for (Task& t : tasks_) {
            if (t.id == detailTaskId_) {
                // Fl_Text_Buffer::text() returns a buffer owned by the widget;
                // the original code stored the pointer directly, which is only
                // valid until the next call.  Copy it.
                const char* raw = detailBuffer_->text();
                t.notes = raw ? raw : "";
                break;
            }
        }
        save();
    }
    detailTaskId_ = -1;

    detailGroup_->hide();
    listGroup_->show();
    table_->take_focus();
    window_->redraw();
    setMessage("Notes saved");
}

void TaskManagerApp::leaveDetail() { closeDetail(); }

void TaskManagerApp::closeWindow() {
    if (detailGroup_ && detailGroup_->visible()) closeDetail();
    save();          // the original called exit(0) and skipped this
    window_->hide();
}

void TaskManagerApp::focusList() {
    table_->focusFirstRow();
}

void TaskManagerApp::focusInput() {
    input_->take_focus();
    setMessage("Type a task and press Enter  (Esc: back to the list)");
}

void TaskManagerApp::moveListSelection(int delta) {
    table_->take_focus();
    table_->moveSelection(delta);
}

void TaskManagerApp::showHelp() {
    fl_message(
        "Keyboard shortcuts\n\n"
        "  Enter (in the input field)  add the task\n"
        "  i                           jump to the input field\n"
        "  Up / Down                   select a task\n"
        "  Enter (in the list)         open the notes editor\n"
        "  d                           mark the task finished\n"
        "  c                           re-open a finished task\n"
        "  p                           postpone (move to the delay list)\n"
        "  q                           restore from the delay list\n"
        "  b / l / h / f               delay list / pending / finished / all\n"
        "  v                           cycle sorting (created -> finished -> file order)\n"
        "  click a column header       sort by Created Time or Finished Time\n"
        "  Del                         delete the selected task (asks first)\n"
        "  Esc                         back to the list");
}

// ---------------------------------------------------------------------------
// Keyboard
// ---------------------------------------------------------------------------

int TaskManagerApp::handleKey(int key, int state) {
    const bool ctrl = (state & FL_CTRL) != 0;

    // Notes editor: typing wins, only Esc is a command.
    if (detailGroup_ && detailGroup_->visible()) {
        if (key == FL_Escape) { closeDetail(); return 1; }
        return 0;   // let the editor handle everything else
    }

    if (ctrl) {
        switch (key) {
            case 'f': case 'F': setFilter(DisplayFilter::All);      return 1;
            case 'l': case 'L': setFilter(DisplayFilter::Pending);  return 1;
            case 'h': case 'H': setFilter(DisplayFilter::Done);     return 1;
            case 'b': case 'B': setFilter(DisplayFilter::Delayed);  return 1;
            default: return 0;
        }
    }

    if (key == FL_F + 1 || key == FL_Help) { showHelp(); return 1; }

    // While the task entry field owns the keyboard, printable characters belong
    // to the text being typed.  Esc and the arrow keys leave the field instead.
    // (Focus is used rather than a flag of our own so that clicking into the
    // field with the mouse is handled correctly too.)
    if (Fl::focus() == input_) {
        switch (key) {
            case FL_Escape:
            case FL_Up:
            case FL_Down:
                focusList();
                if (key == FL_Up)   moveListSelection(-1);
                if (key == FL_Down) moveListSelection(+1);
                return 1;
            default:
                return 0;
        }
    }

    // In the list, Enter opens the notes of the selected task.
    if (key == FL_Enter || key == FL_KP_Enter) { openDetail(); return 1; }

    switch (key) {
        case 'i': case 'I': focusInput(); return 1;
        case 'l': case 'L': setFilter(DisplayFilter::Pending);  return 1;
        case 'h': case 'H': setFilter(DisplayFilter::Done);     return 1;
        case 'b': case 'B': setFilter(DisplayFilter::Delayed);  return 1;
        case 'f': case 'F': setFilter(DisplayFilter::All);      return 1;
        case 'd': case 'D': completeSelected();   return 1;
        case 'c': case 'C': uncompleteSelected(); return 1;
        case 'p': case 'P': postponeSelected();   return 1;
        case 'q': case 'Q': restoreSelected();    return 1;
        case 'v': case 'V': cycleSort();          return 1;
        case FL_Delete:     deleteSelected();     return 1;
        case '?':           showHelp();           return 1;
        default: break;
    }

    return 0;
}
