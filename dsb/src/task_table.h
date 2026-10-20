// task_table.h -- the task list widget: filtering, sorting, delay columns.
#pragma once

#include "model.h"

#include <FL/Fl_Table_Row.H>

#include <string>
#include <vector>

// A row based table that shows a filtered, optionally sorted view of the task
// vector.  It owns no task data; it only maps display rows to task indices.
//
// Compared to the original single-file version:
//   * columns are described by an id, so adding/removing a column can no
//     longer shift the meaning of every other column;
//   * the formatted timestamps are cached, so scrolling does not call
//     strftime() for every cell on every repaint;
//   * clicking the "Created Time" / "Finished Time" header sorts the list;
//   * the selection is re-applied by task id after a refresh, so filtering,
//     deleting or postponing can never move the selection to another task.
class TaskTable : public Fl_Table_Row {
public:
    enum ColId {
        C_STATUS,
        C_ID,
        C_DESC,
        C_CREATED,
        C_FINISHED,
        C_DELAYED,
        C_COL_COUNT
    };

    typedef void (*SortRequestHandler)(void* user, SortKey key);

    TaskTable(int X, int Y, int W, int H, const char* l = 0);

    void setTasks(const std::vector<Task>* tasks) { tasks_ = tasks; }

    void setSortRequestHandler(SortRequestHandler handler, void* user) {
        sortHandler_    = handler;
        sortHandlerUser_ = user;
    }

    // Applies a new filter/sort and rebuilds the display.
    void setView(DisplayFilter filter, const SortSpec& sort);

    // Re-applies the current filter/sort to the task vector (call after the
    // model changed).  Keeps the selected task selected when still visible.
    void refresh();

    // Layout is recomputed on resize.
    void resize(int X, int Y, int W, int H) override;

    int displayedCount() const { return static_cast<int>(displayed_.size()); }
    int displayedTaskIndex(int row) const;   // row -> index into the task vector
    int selectedTaskIndex() const;      // index into the task vector, -1 if none
    int selectedDisplayRow() const;
    bool selectTaskId(int id);          // returns true when the task is visible
    void selectDisplayRow(int row);
    void moveSelection(int delta);      // -1 up, +1 down
    void focusFirstRow();

protected:
    void draw_cell(TableContext context, int ROW = 0, int COL = 0,
                   int X = 0, int Y = 0, int W = 0, int H = 0) override;
    int  handle(int event) override;

private:
    std::vector<ColId> activeCols_;
    std::vector<int>   displayed_;      // display row -> index into *tasks_

    std::vector<std::string> createdText_;
    std::vector<std::string> finishedText_;
    std::vector<std::string> delayedText_;

    const std::vector<Task>* tasks_ = nullptr;
    DisplayFilter            filter_ = DisplayFilter::Pending;
    SortSpec                 sort_;
    SortRequestHandler       sortHandler_ = nullptr;
    void*                    sortHandlerUser_ = nullptr;

    bool  visible(const Task& t) const;
    void  buildColumnSet();
    void  sortDisplayed();
    void  rebuildTextCache();
    void  layoutColumns();
    std::string headerText(int col) const;
    int   rowBackground(int ROW) const;
};
