#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Table_Row.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/fl_draw.H>
#include <FL/fl_ask.H>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <ctime>
#include <iomanip>

using namespace std;

struct Task {
    int id;
    string description;
    bool completed;
    time_t createdTime;
    time_t completedTime;
    string notes;

    Task(int id, const string& desc)
        : id(id), description(desc), completed(false),
          createdTime(time(nullptr)), completedTime(0), notes("") {}
};

enum DisplayFilter {
    FILTER_ALL,
    FILTER_UNFINISHED,
    FILTER_FINISHED
};

class TaskManagerGUI;

// Custom main window that forwards events to GUI
class MainWindow : public Fl_Window {
private:
    TaskManagerGUI* gui;

public:
    MainWindow(int W, int H, const char* L = 0) : Fl_Window(W, H, L), gui(nullptr) {}

    void setGUI(TaskManagerGUI* g) { gui = g; }

    void resize(int X, int Y, int W, int H) override {
        Fl_Window::resize(X, Y, W, H);
        if (gui) {
            extern void handleWindowResize(TaskManagerGUI* g);
            handleWindowResize(gui);
        }
    }

    int handle(int event) override {
        if (gui) {
            extern int handleMainWindowEvent(TaskManagerGUI* g, int event);
            int result = handleMainWindowEvent(gui, event);
            if (result) return result;
        }
        return Fl_Window::handle(event);
    }
};

// Custom text editor that passes Esc to parent
class DetailTextEditor : public Fl_Text_Editor {
private:
    TaskManagerGUI* gui;

public:
    DetailTextEditor(int X, int Y, int W, int H, const char* l=0)
        : Fl_Text_Editor(X, Y, W, H, l), gui(nullptr) {}

    void setGUI(TaskManagerGUI* g) { gui = g; }

    int handle(int event) override {
        if (event == FL_KEYDOWN && Fl::event_key() == FL_Escape) {
            // Pass Esc to the GUI handler
            if (gui) {
                extern void handleDetailEscape(TaskManagerGUI* g);
                handleDetailEscape(gui);
            }
            return 1;
        }
        return Fl_Text_Editor::handle(event);
    }
};

// Custom input that passes arrow keys to table
class TaskInput : public Fl_Input {
private:
    TaskManagerGUI* gui;

public:
    TaskInput(int X, int Y, int W, int H, const char* l=0)
        : Fl_Input(X, Y, W, H, l), gui(nullptr) {}

    void setGUI(TaskManagerGUI* g) { gui = g; }

    int handle(int event) override {
        if (event == FL_KEYDOWN) {
            int key = Fl::event_key();
            if (key == FL_Up || key == FL_Down) {
                // Pass arrow keys to focus table
                if (gui) {
                    extern void handleFocusTable(TaskManagerGUI* g);
                    handleFocusTable(gui);
                }
                return 1;
            }
        }
        return Fl_Input::handle(event);
    }
};

class TaskTable : public Fl_Table_Row {
private:
    vector<Task>* allTasks;
    vector<int> displayedTaskIndices;
    DisplayFilter* filterPtr;
    TaskManagerGUI* gui;

    string formatTime(time_t t) {
        if (t == 0) return "";
        struct tm* timeinfo = localtime(&t);
        char buffer[80];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
        return string(buffer);
    }

protected:
    void draw_cell(TableContext context, int ROW=0, int COL=0, int X=0, int Y=0, int W=0, int H=0) {
        static char s[512];

        switch(context) {
            case CONTEXT_STARTPAGE:
                fl_font(FL_HELVETICA, 12);
                return;

            case CONTEXT_COL_HEADER:
                fl_push_clip(X, Y, W, H);
                fl_draw_box(FL_THIN_UP_BOX, X, Y, W, H, FL_BACKGROUND_COLOR);
                fl_color(FL_BLACK);
                fl_font(FL_HELVETICA_BOLD, 12);

                if (*filterPtr == FILTER_UNFINISHED) {
                    if (COL == 0) strcpy(s, "Description");
                    else if (COL == 1) strcpy(s, "Created Time");
                } else if (*filterPtr == FILTER_FINISHED) {
                    if (COL == 0) strcpy(s, "Description");
                    else if (COL == 1) strcpy(s, "Created Time");
                    else if (COL == 2) strcpy(s, "Finished Time");
                } else {
                    if (COL == 0) strcpy(s, "Status");
                    else if (COL == 1) strcpy(s, "ID");
                    else if (COL == 2) strcpy(s, "Description");
                    else if (COL == 3) strcpy(s, "Created Time");
                    else if (COL == 4) strcpy(s, "Finished Time");
                }

                fl_draw(s, X, Y, W, H, FL_ALIGN_CENTER);
                fl_pop_clip();
                return;

            case CONTEXT_CELL: {
                if (ROW >= (int)displayedTaskIndices.size()) return;

                int taskIndex = displayedTaskIndices[ROW];
                const Task& task = (*allTasks)[taskIndex];

                fl_push_clip(X, Y, W, H);
                if (row_selected(ROW)) {
                    fl_color(fl_rgb_color(200, 200, 200));  // Light gray for selection
                    fl_rectf(X, Y, W, H);
                    fl_color(FL_BLACK);
                } else {
                    fl_color(FL_WHITE);
                    fl_rectf(X, Y, W, H);
                    fl_color(FL_BLACK);  // All text black
                }

                fl_font(FL_HELVETICA, 12);

                if (*filterPtr == FILTER_UNFINISHED) {
                    if (COL == 0) {
                        strncpy(s, task.description.c_str(), sizeof(s)-1);
                        s[sizeof(s)-1] = '\0';
                    } else if (COL == 1) {
                        string timeStr = formatTime(task.createdTime);
                        strncpy(s, timeStr.c_str(), sizeof(s)-1);
                        s[sizeof(s)-1] = '\0';
                    }
                } else if (*filterPtr == FILTER_FINISHED) {
                    if (COL == 0) {
                        strncpy(s, task.description.c_str(), sizeof(s)-1);
                        s[sizeof(s)-1] = '\0';
                    } else if (COL == 1) {
                        string timeStr = formatTime(task.createdTime);
                        strncpy(s, timeStr.c_str(), sizeof(s)-1);
                        s[sizeof(s)-1] = '\0';
                    } else if (COL == 2) {
                        string timeStr = formatTime(task.completedTime);
                        strncpy(s, timeStr.c_str(), sizeof(s)-1);
                        s[sizeof(s)-1] = '\0';
                    }
                } else {
                    if (COL == 0) {
                        strcpy(s, task.completed ? "[✓]" : "[ ]");
                    } else if (COL == 1) {
                        snprintf(s, sizeof(s), "%d", task.id);
                    } else if (COL == 2) {
                        strncpy(s, task.description.c_str(), sizeof(s)-1);
                        s[sizeof(s)-1] = '\0';
                    } else if (COL == 3) {
                        string timeStr = formatTime(task.createdTime);
                        strncpy(s, timeStr.c_str(), sizeof(s)-1);
                        s[sizeof(s)-1] = '\0';
                    } else if (COL == 4) {
                        string timeStr = formatTime(task.completedTime);
                        strncpy(s, timeStr.c_str(), sizeof(s)-1);
                        s[sizeof(s)-1] = '\0';
                    }
                }

                fl_draw(s, X, Y, W, H, FL_ALIGN_LEFT);
                fl_color(FL_LIGHT2);
                fl_rect(X, Y, W, H);

                fl_pop_clip();
                return;
            }

            default:
                return;
        }
    }

    int handle(int event) {
        if (event == FL_PUSH) {
            take_focus();
        }

        if (event == FL_KEYDOWN) {
            int key = Fl::event_key();

            if (key == FL_Delete) {
                if (gui) {
                    extern void handleDeleteKey(TaskManagerGUI* g);
                    handleDeleteKey(gui);
                }
                return 1;
            } else if (key == FL_Enter || key == FL_KP_Enter) {
                if (gui) {
                    extern void handleTableEnter(TaskManagerGUI* g);
                    handleTableEnter(gui);
                }
                return 1;
            } else if (key == 'd' || key == 'D') {
                // Only handle D key if Ctrl is NOT pressed (Ctrl+D is for filter)
                if (!(Fl::event_state() & FL_CTRL)) {
                    if (gui) {
                        extern void handleCompleteKey(TaskManagerGUI* g);
                        handleCompleteKey(gui);
                    }
                    return 1;
                }
            } else if (key == 'c' || key == 'C') {
                // C key - uncomplete task
                if (gui) {
                    extern void handleUncompleteKey(TaskManagerGUI* g);
                    handleUncompleteKey(gui);
                }
                return 1;
            } else if (key == 'i' || key == 'I') {
                // I key - focus on input bar
                if (gui) {
                    extern void handleFocusInput(TaskManagerGUI* g);
                    handleFocusInput(gui);
                }
                return 1;
            } else if (key == FL_Up) {
                int currentRow = -1;
                for (int i = 0; i < (int)displayedTaskIndices.size(); i++) {
                    if (row_selected(i)) {
                        currentRow = i;
                        break;
                    }
                }
                if (currentRow > 0) {
                    select_all_rows(0);
                    select_row(currentRow - 1, 1);
                    redraw();
                } else if (currentRow == -1 && displayedTaskIndices.size() > 0) {
                    select_row(0, 1);
                    redraw();
                }
                return 1;
            } else if (key == FL_Down) {
                int currentRow = -1;
                for (int i = 0; i < (int)displayedTaskIndices.size(); i++) {
                    if (row_selected(i)) {
                        currentRow = i;
                        break;
                    }
                }
                if (currentRow >= 0 && currentRow < (int)displayedTaskIndices.size() - 1) {
                    select_all_rows(0);
                    select_row(currentRow + 1, 1);
                    redraw();
                } else if (currentRow == -1 && displayedTaskIndices.size() > 0) {
                    select_row(0, 1);
                    redraw();
                }
                return 1;
            }
        }

        return Fl_Table_Row::handle(event);
    }

public:
    TaskTable(int X, int Y, int W, int H, const char* l=0)
        : Fl_Table_Row(X, Y, W, H, l), allTasks(nullptr), filterPtr(nullptr), gui(nullptr) {

        cols(5);
        col_header(1);
        col_resize(1);
        row_header(0);
        row_resize(0);
        type(SELECT_SINGLE);

        col_width(0, 60);
        col_width(1, 50);
        col_width(2, 300);
        col_width(3, 180);
        col_width(4, 180);

        end();
    }

    void setTasksPointer(vector<Task>* tasks) {
        allTasks = tasks;
    }

    void setFilterPointer(DisplayFilter* filter) {
        filterPtr = filter;
    }

    void setGUIPointer(TaskManagerGUI* g) {
        gui = g;
    }

    void updateDisplay() {
        if (!allTasks || !filterPtr) return;

        displayedTaskIndices.clear();

        for (size_t i = 0; i < allTasks->size(); i++) {
            const Task& task = (*allTasks)[i];
            bool shouldShow = false;

            switch(*filterPtr) {
                case FILTER_UNFINISHED:
                    shouldShow = !task.completed;
                    break;
                case FILTER_FINISHED:
                    shouldShow = task.completed;
                    break;
                case FILTER_ALL:
                default:
                    shouldShow = true;
                    break;
            }

            if (shouldShow) {
                displayedTaskIndices.push_back(i);
            }
        }

        int tableWidth = w();

        if (*filterPtr == FILTER_UNFINISHED) {
            cols(2);
            col_width(0, tableWidth * 0.60);
            col_width(1, tableWidth * 0.38);
        } else if (*filterPtr == FILTER_FINISHED) {
            cols(3);
            col_width(0, tableWidth * 0.45);
            col_width(1, tableWidth * 0.27);
            col_width(2, tableWidth * 0.27);
        } else {
            cols(5);
            col_width(0, tableWidth * 0.07);
            col_width(1, tableWidth * 0.06);
            col_width(2, tableWidth * 0.40);
            col_width(3, tableWidth * 0.23);
            col_width(4, tableWidth * 0.23);
        }

        rows(displayedTaskIndices.size());
        redraw();
    }

    int getSelectedTaskIndex() {
        if (displayedTaskIndices.empty()) return -1;

        for (int i = 0; i < (int)displayedTaskIndices.size(); i++) {
            if (row_selected(i)) {
                return displayedTaskIndices[i];
            }
        }
        return -1;
    }

    int getDisplayedCount() const {
        return displayedTaskIndices.size();
    }

    int getDisplayedTaskIndex(int displayRow) const {
        if (displayRow < 0 || displayRow >= (int)displayedTaskIndices.size()) return -1;
        return displayedTaskIndices[displayRow];
    }
};

class TaskManagerGUI {
private:
    vector<Task> tasks;
    int nextId;
    string filename;
    DisplayFilter currentFilter;
    int currentDetailTaskIndex;

    MainWindow* window;

    // List view components
    Fl_Group* listViewGroup;
    TaskInput* taskInput;
    TaskTable* taskTable;
    Fl_Box* statusBox;

    // Detail view components
    Fl_Group* detailViewGroup;
    Fl_Box* detailTitle;
    DetailTextEditor* detailEditor;
    Fl_Text_Buffer* detailBuffer;
    Fl_Box* detailHint;

public:
    TaskManagerGUI() : nextId(1), filename("tasks.dat"), currentFilter(FILTER_UNFINISHED), currentDetailTaskIndex(-1) {
        loadTasks();
        createWindow();
    }

    ~TaskManagerGUI() {
        saveTasks();
        delete detailBuffer;
        delete window;
    }

    void createWindow() {
        window = new MainWindow(900, 600, "Task Manager");
        window->color(FL_WHITE);
        window->size_range(700, 400);
        window->setGUI(this);
        window->callback(window_cb, this);

        // Create list view group
        listViewGroup = new Fl_Group(0, 0, 900, 600);

        // Title removed

        taskInput = new TaskInput(20, 10, 860, 35, "");
        taskInput->textsize(14);
        taskInput->tooltip("Enter task description and press Enter to add");
        taskInput->when(FL_WHEN_ENTER_KEY);
        taskInput->callback(inputEnter_cb, this);
        taskInput->setGUI(this);

        taskTable = new TaskTable(20, 55, 860, 515);
        taskTable->setTasksPointer(&tasks);
        taskTable->setFilterPointer(&currentFilter);
        taskTable->setGUIPointer(this);

        statusBox = new Fl_Box(20, 575, 860, 20, "i/l/h/f/c/enter/del/esc");
        statusBox->box(FL_FLAT_BOX);
        statusBox->color(FL_WHITE);
        statusBox->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        statusBox->labelsize(11);

        listViewGroup->end();

        // Create detail view group (initially hidden)
        detailViewGroup = new Fl_Group(0, 0, 900, 600);
        detailViewGroup->hide();

        detailTitle = new Fl_Box(20, 10, 860, 30, "");
        detailTitle->labelsize(14);
        detailTitle->labelfont(FL_BOLD);
        detailTitle->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

        detailBuffer = new Fl_Text_Buffer();
        detailEditor = new DetailTextEditor(20, 50, 860, 520);
        detailEditor->buffer(detailBuffer);
        detailEditor->textsize(13);
        detailEditor->wrap_mode(Fl_Text_Editor::WRAP_AT_BOUNDS, 0);
        detailEditor->setGUI(this);

        detailHint = new Fl_Box(20, 575, 860, 20, "Esc: back (auto-saved)");
        detailHint->labelsize(11);
        detailHint->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

        detailViewGroup->end();

        window->end();
        window->resizable(window);

        setFilter(FILTER_UNFINISHED);
        refreshDisplay();
    }

    int handle(int event) {
        // Handle Esc in detail view
        if (event == FL_KEYDOWN && Fl::event_key() == FL_Escape) {
            if (detailViewGroup->visible()) {
                closeDetail();
                return 1;
            }
        }

        // Handle Ctrl+F/U/D shortcuts
        if (event == FL_KEYDOWN) {
            if (Fl::event_state() & FL_CTRL) {
                int key = Fl::event_key();
                if (key == 'f' || key == 'F') {
                    setFilter(FILTER_ALL);
                    return 1;
                } else if (key == 'u' || key == 'U') {
                    setFilter(FILTER_UNFINISHED);
                    return 1;
                } else if (key == 'd' || key == 'D') {
                    setFilter(FILTER_FINISHED);
                    return 1;
                }
            }
        }

        return 0;
    }

    void show() {
        window->show();
    }

    void handleResize() {
        int ww = window->w();
        int wh = window->h();

        // Input - resize width, keep original position Y=10
        taskInput->resize(20, 10, ww - 40, 35);

        // Table - resize width and height, keep original gap (Y=55, 45px below input)
        taskTable->resize(20, 55, ww - 40, wh - 80);

        // Status box - resize width, reposition at bottom
        statusBox->resize(20, wh - 25, ww - 40, 20);

        // Update table display to adjust column widths
        refreshDisplay();
    }

    void addTask(const string& description) {
        if (description.empty()) {
            setStatus("Error: Task description cannot be empty");
            return;
        }

        int newTaskId = nextId;
        tasks.push_back(Task(nextId++, description));
        saveTasks();

        // Switch to undone filter
        setFilter(FILTER_UNFINISHED);
        refreshDisplay();

        // Find and select the newly added task in the displayed list
        for (int i = 0; i < taskTable->getDisplayedCount(); i++) {
            int taskIndex = taskTable->getDisplayedTaskIndex(i);
            if (taskIndex >= 0 && taskIndex < (int)tasks.size() && tasks[taskIndex].id == newTaskId) {
                taskTable->select_all_rows(0);
                taskTable->select_row(i, 1);
                taskTable->take_focus();
                break;
            }
        }

        taskInput->value("");
        setStatus("Task added successfully!");
    }

    void completeTask() {
        int actualIndex = taskTable->getSelectedTaskIndex();
        if (actualIndex < 0) {
            setStatus("Error: Please select a task to complete");
            return;
        }

        if (actualIndex < (int)tasks.size()) {
            if (tasks[actualIndex].completed) {
                setStatus("Task is already completed!");
                return;
            }
            tasks[actualIndex].completed = true;
            tasks[actualIndex].completedTime = time(nullptr);
            saveTasks();
            refreshDisplay();
            setStatus("Task marked as completed!");
        }
    }

    void uncompleteTask() {
        int actualIndex = taskTable->getSelectedTaskIndex();
        if (actualIndex < 0) {
            setStatus("Error: Please select a task to uncomplete");
            return;
        }

        if (actualIndex < (int)tasks.size()) {
            if (!tasks[actualIndex].completed) {
                setStatus("Task is already uncompleted!");
                return;
            }
            tasks[actualIndex].completed = false;
            tasks[actualIndex].completedTime = 0;
            saveTasks();
            refreshDisplay();
            setStatus("Task marked as uncompleted!");
        }
    }

    void focusInput() {
        taskInput->take_focus();
        setStatus("Focus on input bar");
    }

    void focusTable() {
        taskTable->take_focus();
        // If no row is selected, select the first one
        if (taskTable->getSelectedTaskIndex() < 0 && taskTable->getDisplayedCount() > 0) {
            taskTable->select_row(0, 1);
            taskTable->redraw();
        }
    }

    void deleteTask() {
        int actualIndex = taskTable->getSelectedTaskIndex();
        if (actualIndex < 0) {
            setStatus("Error: Please select a task to delete");
            return;
        }

        if (actualIndex < (int)tasks.size()) {
            tasks.erase(tasks.begin() + actualIndex);
            saveTasks();
            refreshDisplay();
            setStatus("Task deleted!");
        }
    }

    void openDetail() {
        int actualIndex = taskTable->getSelectedTaskIndex();
        if (actualIndex < 0) {
            setStatus("Error: Please select a task");
            return;
        }

        if (actualIndex < (int)tasks.size()) {
            currentDetailTaskIndex = actualIndex;
            const Task& task = tasks[actualIndex];

            detailTitle->copy_label(task.description.c_str());
            detailBuffer->text(task.notes.c_str());

            listViewGroup->hide();
            detailViewGroup->show();
            detailEditor->take_focus();
            window->redraw();
        }
    }

    void closeDetail() {
        if (currentDetailTaskIndex >= 0 && currentDetailTaskIndex < (int)tasks.size()) {
            tasks[currentDetailTaskIndex].notes = detailBuffer->text();
            saveTasks();
        }

        detailViewGroup->hide();
        listViewGroup->show();
        taskTable->take_focus();
        window->redraw();
        currentDetailTaskIndex = -1;
    }

    void setFilter(DisplayFilter filter) {
        currentFilter = filter;
        refreshDisplay();
    }

    void refreshDisplay() {
        taskTable->updateDisplay();
        setStatus("i/l/h/f/c/enter/del/esc");
    }

    void setStatus(const string& message) {
        statusBox->copy_label(message.c_str());
        window->redraw();
    }

    void saveTasks() {
        ofstream file(filename, ios::binary);
        if (!file) return;

        size_t count = tasks.size();
        file.write(reinterpret_cast<const char*>(&count), sizeof(count));
        file.write(reinterpret_cast<const char*>(&nextId), sizeof(nextId));

        bool hasNotes = true;
        file.write(reinterpret_cast<const char*>(&hasNotes), sizeof(hasNotes));

        for (const auto& task : tasks) {
            file.write(reinterpret_cast<const char*>(&task.id), sizeof(task.id));
            file.write(reinterpret_cast<const char*>(&task.completed), sizeof(task.completed));
            file.write(reinterpret_cast<const char*>(&task.createdTime), sizeof(task.createdTime));
            file.write(reinterpret_cast<const char*>(&task.completedTime), sizeof(task.completedTime));

            size_t len = task.description.length();
            file.write(reinterpret_cast<const char*>(&len), sizeof(len));
            file.write(task.description.c_str(), len);

            size_t notesLen = task.notes.length();
            file.write(reinterpret_cast<const char*>(&notesLen), sizeof(notesLen));
            if (notesLen > 0) {
                file.write(task.notes.c_str(), notesLen);
            }
        }

        file.close();
    }

    void loadTasks() {
        ifstream file(filename, ios::binary);
        if (!file) return;

        size_t count;
        file.read(reinterpret_cast<char*>(&count), sizeof(count));
        file.read(reinterpret_cast<char*>(&nextId), sizeof(nextId));

        bool hasNotes = false;
        if (file.peek() != EOF) {
            file.read(reinterpret_cast<char*>(&hasNotes), sizeof(hasNotes));
        }

        for (size_t i = 0; i < count; i++) {
            int id;
            bool completed;
            time_t createdTime;
            time_t completedTime;

            file.read(reinterpret_cast<char*>(&id), sizeof(id));
            file.read(reinterpret_cast<char*>(&completed), sizeof(completed));
            file.read(reinterpret_cast<char*>(&createdTime), sizeof(createdTime));
            file.read(reinterpret_cast<char*>(&completedTime), sizeof(completedTime));

            size_t len;
            file.read(reinterpret_cast<char*>(&len), sizeof(len));

            string description(len, '\0');
            file.read(&description[0], len);

            Task task(id, description);
            task.completed = completed;
            task.createdTime = createdTime;
            task.completedTime = completedTime;

            if (hasNotes) {
                size_t notesLen = 0;
                file.read(reinterpret_cast<char*>(&notesLen), sizeof(notesLen));
                if (notesLen > 0) {
                    string notes(notesLen, '\0');
                    file.read(&notes[0], notesLen);
                    task.notes = notes;
                }
            }

            tasks.push_back(task);
        }

        file.close();
    }

    static void inputEnter_cb(Fl_Widget* w, void* data) {
        TaskManagerGUI* gui = (TaskManagerGUI*)data;
        gui->addTask(gui->taskInput->value());
    }

    static void window_cb(Fl_Widget* w, void* data) {
        if (Fl::event() == FL_CLOSE) {
            exit(0);
        }
    }
};

void handleDeleteKey(TaskManagerGUI* gui) {
    gui->deleteTask();
}

void handleTableEnter(TaskManagerGUI* gui) {
    gui->openDetail();
}

void handleCompleteKey(TaskManagerGUI* gui) {
    gui->completeTask();
}

void handleUncompleteKey(TaskManagerGUI* gui) {
    gui->uncompleteTask();
}

void handleFocusInput(TaskManagerGUI* gui) {
    gui->focusInput();
}

void handleFocusTable(TaskManagerGUI* gui) {
    gui->focusTable();
}

void handleDetailEscape(TaskManagerGUI* gui) {
    gui->closeDetail();
}

int handleMainWindowEvent(TaskManagerGUI* gui, int event) {
    if (event == FL_KEYDOWN) {
        int key = Fl::event_key();

        // Handle Ctrl key combinations
        if (Fl::event_state() & FL_CTRL) {
            if (key == 'c' || key == 'C') {
                extern void handleUncompleteKey(TaskManagerGUI* g);
                handleUncompleteKey(gui);
                return 1;
            }
        }

        // Handle single key shortcuts (non-Ctrl)
        if (!(Fl::event_state() & FL_CTRL)) {
            if (key == 'f' || key == 'F') {
                extern void setFilterAll(TaskManagerGUI* g);
                setFilterAll(gui);
                return 1;
            } else if (key == 'l' || key == 'L') {
                extern void setFilterUnfinished(TaskManagerGUI* g);
                setFilterUnfinished(gui);
                return 1;
            } else if (key == 'h' || key == 'H') {
                extern void setFilterFinished(TaskManagerGUI* g);
                setFilterFinished(gui);
                return 1;
            } else if (key == FL_Up || key == FL_Down) {
                // Arrow keys should focus on table
                extern void handleFocusTable(TaskManagerGUI* g);
                handleFocusTable(gui);
                // Let the event continue to table for navigation
                return 0;
            }
        }
    }
    return 0;
}

void setFilterAll(TaskManagerGUI* gui) {
    gui->setFilter(FILTER_ALL);
}

void setFilterUnfinished(TaskManagerGUI* gui) {
    gui->setFilter(FILTER_UNFINISHED);
}

void setFilterFinished(TaskManagerGUI* gui) {
    gui->setFilter(FILTER_FINISHED);
}

void handleWindowResize(TaskManagerGUI* gui) {
    gui->handleResize();
}

// Global pointer for event handler
TaskManagerGUI* g_gui = nullptr;

int global_event_handler(int event) {
    if (event == FL_KEYDOWN && g_gui) {
        return g_gui->handle(event);
    }
    return 0;
}

int main(int argc, char** argv) {
    Fl::option(Fl::OPTION_SHOW_TOOLTIPS, true);

    TaskManagerGUI gui;
    g_gui = &gui;

    Fl::add_handler(global_event_handler);

    gui.show();

    return Fl::run();
}