#include "task_table.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {

const char* stateLabel(const Task& t) {
    switch (t.state()) {
        case TaskState::Done:    return "[x]";
        case TaskState::Delayed: return "[~]";
        default:                 return "[ ]";
    }
}

} // namespace

TaskTable::TaskTable(int X, int Y, int W, int H, const char* l)
    : Fl_Table_Row(X, Y, W, H, l) {
    col_header(1);
    col_header_height(22);
    col_resize(1);
    col_resize_min(40);
    row_header(0);
    row_resize(0);
    type(SELECT_SINGLE);
    color(FL_WHITE);
    selection_color(fl_rgb_color(205, 224, 245));
    end();
}

// ---------------------------------------------------------------------------
// filtering / sorting
// ---------------------------------------------------------------------------

bool TaskTable::visible(const Task& t) const {
    switch (filter_) {
        case DisplayFilter::Pending: return t.state() == TaskState::Pending;
        case DisplayFilter::Done:    return t.state() == TaskState::Done;
        case DisplayFilter::Delayed: return t.state() == TaskState::Delayed;
        case DisplayFilter::All:
        default:                     return true;
    }
}

void TaskTable::setView(DisplayFilter filter, const SortSpec& sort) {
    filter_ = filter;
    sort_   = sort;
    buildColumnSet();
    refresh();
}

void TaskTable::buildColumnSet() {
    activeCols_.clear();
    if (filter_ == DisplayFilter::All) {
        activeCols_ = {C_STATUS, C_ID, C_DESC, C_CREATED, C_FINISHED, C_DELAYED};
    } else if (filter_ == DisplayFilter::Done) {
        activeCols_ = {C_DESC, C_CREATED, C_FINISHED};
    } else if (filter_ == DisplayFilter::Delayed) {
        activeCols_ = {C_DESC, C_CREATED, C_DELAYED};
    } else {
        activeCols_ = {C_DESC, C_CREATED};
    }
}

void TaskTable::sortDisplayed() {
    if (sort_.key == SortKey::None || !tasks_) return;

    const std::vector<Task>& tasks = *tasks_;
    const bool asc = sort_.ascending;

    std::stable_sort(displayed_.begin(), displayed_.end(),
                     [&tasks, this, asc](int a, int b) {
        const Task& ta = tasks[a];
        const Task& tb = tasks[b];

        std::time_t ka = 0;
        std::time_t kb = 0;
        if (sort_.key == SortKey::Created) {
            ka = ta.createdTime;
            kb = tb.createdTime;
        } else {
            // Tasks that were never finished sort last in either direction:
            // an empty finish time carries no information.
            ka = ta.completedTime;
            kb = tb.completedTime;
            if (ka == 0 || kb == 0) {
                if (ka != kb) return ka != 0;
            }
        }
        if (ka != kb) return asc ? (ka < kb) : (ka > kb);
        return ta.id < tb.id;   // stable, predictable tie break
    });
}

void TaskTable::rebuildTextCache() {
    const std::size_t n = tasks_ ? tasks_->size() : 0;
    createdText_.resize(n);
    finishedText_.resize(n);
    delayedText_.resize(n);

    for (std::size_t i = 0; i < n; ++i) {
        const Task& t = (*tasks_)[i];
        createdText_[i]  = formatTime(t.createdTime);
        finishedText_[i] = t.completed ? formatTime(t.completedTime) : std::string("(not finished)");
        delayedText_[i]  = t.delayed ? formatTime(t.delayedTime) : std::string();
        if (finishedText_[i].empty()) finishedText_[i] = "(not finished)";
        if (t.delayed && delayedText_[i].empty()) delayedText_[i] = "(postponed)";
    }
}

void TaskTable::refresh() {
    const int keepRow = selectedDisplayRow();
    int keepTaskId = -1;
    if (keepRow >= 0 && keepRow < displayedCount() && tasks_) {
        const int idx = displayed_[keepRow];
        if (idx >= 0 && idx < static_cast<int>(tasks_->size()))
            keepTaskId = (*tasks_)[idx].id;
    }

    displayed_.clear();
    if (tasks_) {
        displayed_.reserve(tasks_->size());
        for (std::size_t i = 0; i < tasks_->size(); ++i) {
            if (visible((*tasks_)[i])) displayed_.push_back(static_cast<int>(i));
        }
    }
    sortDisplayed();
    rebuildTextCache();

    rows(static_cast<int>(displayed_.size()));
    layoutColumns();

    // rows() only grows/shrinks Fl_Table_Row's selection vector, it does not
    // clear it; stale flags would make the next key press act on a task the
    // user never selected.
    select_all_rows(0);
    if (keepTaskId >= 0) selectTaskId(keepTaskId);
    else if (displayedCount() > 0) selectDisplayRow(0);
    redraw();
}

// ---------------------------------------------------------------------------
// selection helpers
// ---------------------------------------------------------------------------

int TaskTable::displayedTaskIndex(int row) const {
    if (row < 0 || row >= displayedCount()) return -1;
    return displayed_[static_cast<std::size_t>(row)];
}

int TaskTable::selectedDisplayRow() const {
    for (int i = 0; i < displayedCount(); ++i) {
        if (const_cast<TaskTable*>(this)->row_selected(i)) return i;
    }
    return -1;
}

int TaskTable::selectedTaskIndex() const {
    const int row = selectedDisplayRow();
    if (row < 0) return -1;
    return displayed_[row];
}

bool TaskTable::selectTaskId(int id) {
    if (!tasks_) return false;
    for (int row = 0; row < displayedCount(); ++row) {
        const int idx = displayed_[row];
        if (idx >= 0 && idx < static_cast<int>(tasks_->size()) && (*tasks_)[idx].id == id) {
            selectDisplayRow(row);
            return true;
        }
    }
    return false;
}

void TaskTable::selectDisplayRow(int row) {
    if (row < 0 || row >= displayedCount()) return;
    select_all_rows(0);
    select_row(row, 1);
    // Fl_Table_Row::select_row(int,int) hides Fl_Table's `select_row` member, so
    // the arrow-key anchor of the base class is updated with a qualified name.
    this->Fl_Table::select_row = row;
    current_row = row;
    current_col = 0;
    row_position(row);
    redraw();
}

void TaskTable::moveSelection(int delta) {
    if (displayedCount() == 0) return;
    int row = selectedDisplayRow();
    if (row < 0) row = 0;
    else row = std::min(std::max(row + delta, 0), displayedCount() - 1);
    selectDisplayRow(row);
}

void TaskTable::focusFirstRow() {
    if (displayedCount() == 0) return;
    if (selectedDisplayRow() < 0) selectDisplayRow(0);
    take_focus();
}

// ---------------------------------------------------------------------------
// layout
// ---------------------------------------------------------------------------

void TaskTable::layoutColumns() {
    cols(static_cast<int>(activeCols_.size()));
    // Leave room for the vertical scrollbar so that the last column is not
    // pushed out of view when the list is longer than the window.
    const int scrollbar = 16;
    const int tableWidth = std::max(w() - scrollbar - 6, 240);

    // Everything except the description column has a fixed width; the
    // description takes whatever is left so the table always fills the window.
    int fixed = 0;
    for (ColId id : activeCols_) {
        switch (id) {
            case C_STATUS:   fixed += 44;  break;
            case C_ID:       fixed += 44;  break;
            case C_CREATED:
            case C_FINISHED:
            case C_DELAYED:  fixed += 152; break;
            default: break;
        }
    }
    const int descWidth = std::max(tableWidth - fixed, 160);

    for (std::size_t i = 0; i < activeCols_.size(); ++i) {
        int width = 152;
        switch (activeCols_[i]) {
            case C_STATUS: width = 44; break;
            case C_ID:     width = 44; break;
            case C_DESC:   width = descWidth; break;
            default:       width = 152; break;
        }
        col_width(static_cast<int>(i), std::max(width, 1));
    }
}

void TaskTable::resize(int X, int Y, int W, int H) {
    Fl_Table_Row::resize(X, Y, W, H);
    layoutColumns();
}

// ---------------------------------------------------------------------------
// drawing
// ---------------------------------------------------------------------------

std::string TaskTable::headerText(int col) const {
    if (col < 0 || col >= static_cast<int>(activeCols_.size())) return std::string();

    const ColId id = activeCols_[col];
    std::string text;
    SortKey key = SortKey::None;

    switch (id) {
        case C_STATUS:   text = "S";    break;
        case C_ID:       text = "ID";   break;
        case C_DESC:     text = "Description"; break;
        case C_CREATED:  text = "Created Time";  key = SortKey::Created;  break;
        case C_FINISHED: text = "Finished Time"; key = SortKey::Completed; break;
        case C_DELAYED:  text = "Delayed At";    break;
        default: break;
    }

    if (key != SortKey::None && sort_.key == key) {
        text += sort_.ascending ? "  ^" : "  v";   // sort direction marker
    }
    return text;
}

int TaskTable::rowBackground(int ROW) const {
    if (!tasks_ || ROW < 0 || ROW >= displayedCount()) return FL_WHITE;
    const Task& t = (*tasks_)[displayed_[ROW]];
    if (t.state() == TaskState::Delayed) return fl_rgb_color(255, 247, 224);
    if (t.state() == TaskState::Done)    return fl_rgb_color(243, 243, 243);
    return FL_WHITE;
}

void TaskTable::draw_cell(TableContext context, int ROW, int COL, int X, int Y, int W, int H) {
    switch (context) {
        case CONTEXT_STARTPAGE:
            fl_font(FL_HELVETICA, 12);
            return;

        case CONTEXT_COL_HEADER: {
            fl_push_clip(X, Y, W, H);
            fl_draw_box(FL_THIN_UP_BOX, X, Y, W, H, FL_BACKGROUND_COLOR);
            fl_color(FL_BLACK);
            fl_font(FL_HELVETICA_BOLD, 12);
            const std::string text = headerText(COL);
            fl_draw(text.c_str(), X, Y, W, H, FL_ALIGN_CENTER);
            fl_pop_clip();
            return;
        }

        case CONTEXT_CELL: {
            if (ROW < 0 || ROW >= displayedCount() || !tasks_) return;
            const int idx = displayed_[ROW];
            if (idx < 0 || idx >= static_cast<int>(tasks_->size())) return;
            const Task& task = (*tasks_)[idx];

            fl_push_clip(X, Y, W, H);
            if (row_selected(ROW)) {
                fl_color(selection_color());
                fl_rectf(X, Y, W, H);
            } else {
                fl_color(rowBackground(ROW));
                fl_rectf(X, Y, W, H);
            }
            fl_color(FL_BLACK);
            fl_font(FL_HELVETICA, 12);

            std::string text;
            if (COL >= 0 && COL < static_cast<int>(activeCols_.size())) {
                switch (activeCols_[COL]) {
                    case C_STATUS:   text = stateLabel(task); break;
                    case C_ID:       text = std::to_string(task.id); break;
                    case C_DESC:     text = task.description; break;
                    case C_CREATED:  text = createdText_[idx]; break;
                    case C_FINISHED: text = finishedText_[idx]; break;
                    case C_DELAYED:  text = delayedText_[idx]; break;
                    case C_COL_COUNT: break;
                }
            }

            const Fl_Align align =
                (COL >= 0 && COL < static_cast<int>(activeCols_.size()) &&
                 (activeCols_[COL] == C_STATUS || activeCols_[COL] == C_ID))
                    ? (FL_ALIGN_CENTER | FL_ALIGN_CLIP)
                    : (FL_ALIGN_LEFT | FL_ALIGN_CLIP);
            fl_draw(text.c_str(), X + 4, Y, W - 8, H, align);

            fl_color(fl_rgb_color(224, 224, 224));
            fl_rect(X, Y, W, H);
            fl_pop_clip();
            return;
        }

        default:
            return;
    }
}

// ---------------------------------------------------------------------------
// events: header click sorting + keyboard shortcuts
// ---------------------------------------------------------------------------

int TaskTable::handle(int event) {
    if (event == FL_PUSH) {
        // Clicking anywhere in the list must move the keyboard focus here, so
        // that the shortcuts (d/c/p/q/...) act on the list and not on the task
        // entry field that holds the focus by default.
        take_focus();
    }

    // Note: FL_BUTTON1 is a *state* bit mask (0x01000000); the button number
    // returned by Fl::event_button() is compared against FL_LEFT_MOUSE.
    if (event == FL_PUSH && Fl::event_button() == FL_LEFT_MOUSE && !Fl::event_clicks()) {
        // Click on a column header?  Decide before Fl_Table_Row::handle() runs,
        // so that a header click does not also change the row selection.
        int X = 0, Y = 0, W = 0, H = 0;
        get_bounds(CONTEXT_COL_HEADER, X, Y, W, H);
        if (Fl::event_inside(X, Y, W, H)) {
            int col = -1;
            for (int c = leftcol; c <= rightcol; ++c) {
                int cx = 0, cy = 0, cw = 0, ch = 0;
                find_cell(CONTEXT_COL_HEADER, 0, c, cx, cy, cw, ch);
                if (Fl::event_x() >= cx && Fl::event_x() < cx + cw) { col = c; break; }
            }
            // Within 3 px of a column border the click belongs to the column
            // resize handle, so it is passed on to the base class.
            const bool onBorder = (col >= 0) &&
                                  (Fl::event_x() <= X + 3 || Fl::event_x() >= X + W - 3);
            if (col >= 0 && col < static_cast<int>(activeCols_.size()) && !onBorder) {
                const ColId id = activeCols_[static_cast<std::size_t>(col)];
                SortKey key = SortKey::None;
                if (id == C_CREATED)  key = SortKey::Created;
                if (id == C_FINISHED) key = SortKey::Completed;
                if (key != SortKey::None && sortHandler_) {
                    take_focus();   // so the shortcuts keep working right after
                    sortHandler_(sortHandlerUser_, key);
                    return 1;   // consumed: no column selection, no rearrange
                }
            }
        }
    }

    if (event == FL_KEYDOWN) {
        const int key = Fl::event_key();
        if (key == FL_Up)   { moveSelection(-1); return 1; }
        if (key == FL_Down) { moveSelection(+1); return 1; }
    }

    return Fl_Table_Row::handle(event);
}
