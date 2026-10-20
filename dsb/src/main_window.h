// main_window.h -- application shell: views, keyboard handling, detail pane.
#pragma once

#include "model.h"
#include "store.h"
#include "task_table.h"

#include <FL/Fl_Box.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/Fl_Window.H>

#include <string>
#include <vector>

class TaskManagerApp;

// Application window.
//
// All keyboard input is funnelled through TaskManagerApp::handleKey() from
// here, before FLTK dispatches the event to the focused widget.  That is what
// makes the single letter shortcuts reliable: had the shortcuts been handled by
// the table widget, FLTK would first offer every key to the focused entry field
// (which FLTK makes focusable first), and the field would swallow the letters.
class MainWindow : public Fl_Window {
public:
    MainWindow(TaskManagerApp* app, int W, int H, const char* L = nullptr);
    void resize(int X, int Y, int W, int H) override;
    int  handle(int event) override;

private:
    TaskManagerApp* app_ = nullptr;
};

// Task entry field.  The application has to know whether the field owns the
// keyboard, because while it does, the single letter shortcuts must be typed as
// text instead of being interpreted as commands.
class TaskInput : public Fl_Input {
public:
    TaskInput(TaskManagerApp* app, int X, int Y, int W, int H, const char* l = nullptr);
    int handle(int event) override;

private:
    TaskManagerApp* app_ = nullptr;
};

// ---------------------------------------------------------------------------
// Application
// ---------------------------------------------------------------------------

class TaskManagerApp {
public:
    explicit TaskManagerApp(std::string dataDir);
    ~TaskManagerApp();

    void show();
    void layout();                     // repositions the widgets on resize

    // Keyboard entry point; returns 1 when the key was consumed.
    int  handleKey(int key, int state);

    // -- callbacks used by the child widgets -------------------------------
    void requestSort(SortKey key);     // a sortable column header was clicked
    void focusList();
    void focusInput();
    void moveListSelection(int delta);
    void leaveDetail();                // Esc inside the notes editor
    void closeWindow();                // window close button

private:
    // ---- model -----------------------------------------------------------
    std::vector<Task> tasks_;
    int               nextId_ = 1;
    TaskStore         store_;

    DisplayFilter     filter_ = DisplayFilter::Pending;
    SortSpec          sort_;
    int               detailTaskId_ = -1;

    // ---- widgets ---------------------------------------------------------
    MainWindow*     window_       = nullptr;
    Fl_Group*       listGroup_    = nullptr;
    Fl_Input*       input_        = nullptr;
    TaskTable*      table_        = nullptr;
    Fl_Box*         messageBox_   = nullptr;
    Fl_Box*         legendBox_    = nullptr;

    Fl_Group*       detailGroup_  = nullptr;
    Fl_Box*         detailTitle_  = nullptr;
    Fl_Text_Editor* detailEditor_ = nullptr;
    Fl_Text_Buffer* detailBuffer_ = nullptr;
    Fl_Box*         detailHint_   = nullptr;

    // ---- construction ----------------------------------------------------
    void buildWidgets(int W, int H);
    void applyView();

    // ---- status ----------------------------------------------------------
    void setMessage(const std::string& text);
    void updateLegend();

    // ---- operations ------------------------------------------------------
    void addTask(const std::string& description);
    void completeSelected();
    void uncompleteSelected();
    void postponeSelected();
    void restoreSelected();
    void deleteSelected();
    void openDetail();
    void closeDetail();
    void cycleSort();
    void setFilter(DisplayFilter f);
    void showHelp();

    Task*       selectedTask();
    int         countOf(TaskState state) const;
    const char* filterName(DisplayFilter f) const;
    const char* sortName() const;
    void        save();
};
