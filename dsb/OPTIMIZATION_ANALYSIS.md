# task_manager 优化分析（baseline = `task/task_manager_gui.cpp`, 921 行）

分析命令：`task/dsb/build.sh` 中的编译命令（MSYS2 UCRT64 + FLTK 1.4.5）。
基线可编译通过（`baseline_original.exe`），说明工具链与依赖可用。

## A. 正确性 bug（会造成数据错误 / 崩溃）

### A1 【严重】删除任务的撤销语义与"筛选删除"不一致
`deleteTask()` 直接 `tasks.erase(begin() + actualIndex)`，`actualIndex` 是
`displayedTaskIndices` 里的**全量数组下标**。筛选状态下删除后：
- 没有确认（`fl_ask.h` 被 include 但从未使用）；
- 选择行（`Fl_Table_Row` 的 `_rowselect`）在 `updateDisplay()` 里**从不清理**，
  `rows(n)` 只做 push/pop，删行后旧的选中位会"漂移"到别的任务上：
  下一次按 `d`/`del` 会作用在错误的任务上。
- 复现：筛选=未完成 → 选中第 3 行 → 删除 → 按 `d`，被完成的是第 4 个任务。

### A2 【严重】`updateDisplay()` 的列布局与 `draw_cell()` 列号硬编码耦合
`draw_cell()` 用 `COL == 0/1/2/3/4` 直接对应，而列数在 2/3/5 之间切换。
只要以后插入一列（比如新增"延期"列）就会错位。应改为"列描述表 + 列索引查找"。

### A3 【中】`TaskTable` 的全局公开 API 破坏封装
`handleDeleteKey()` 等 8 个自由函数 + `extern` 前置声明是"反向依赖"，
新增一个按键要同时改 3 个地方。应改为类内静态回调。

### A4 【中】`exit(0)` / 析构时不保存
`window_cb` 里 `if (Fl::event() == FL_CLOSE) exit(0);` —— 绕过 `~TaskManagerGUI`，
`saveTasks()` 不会执行。虽然多数操作已即时落盘，但"详情视图里 Esc 才保存备注"
的语义配合 `exit(0)` 会**丢最后一次编辑**。

### A5 【中】二进制格式无校验、无原子写
`loadTasks()` 全程不检查 `file.read` 结果：
- `size_t notesLen` 若损坏（比如读到垃圾），`string notes(notesLen, '\0')` 直接抛/爆内存；
- `count` 损坏则循环读垃圾并无限 push_back；
- `saveTasks()` 直接 `ofstream(filename)` 截断原文件，写一半崩溃 = 数据全丢。

### A6 【中】`localtime()` 非线程安全 + `formatTime` 每次 `strftime`
`draw_cell()` 每个单元格都调用 `localtime()` 和 `strftime()`，滚动时是纯浪费。
应在载入/修改时格式化并缓存，或者用 `localtime_s`。

### A7 【轻】筛选/排序状态不持久化
`currentFilter`、窗口大小位置、滚动位置都不保存，每次启动回到 `FILTER_UNFINISHED`。

### A8 【轻】`select_all_rows(0)` 的位置漂移
`addTask()`/`focusTable()` 用 `select_row(i, 1)` 前先 `select_all_rows(0)`，
但没有同步 `current_row`，`Fl_Table::move_cursor` 的锚点仍在旧行，方向键第一次按下会跳。

### A9 【轻】内存/资源
`saveTasks()` 一遍遍写全量文件，每次按键都调用；任务上千后有明显 IO 抖动。
`detailBuffer` 用裸 `new` 且只在析构删（异常路径泄漏）。

## B. 可维护性 / 结构

- B1 单文件 921 行，`TaskTable`/`TaskManagerGUI` 相互依赖 + 自由函数桥接，无测试。
- B2 无 Makefile / 构建脚本，编译命令不可复现（`task_manager_gui.o` 是手工产物）。
- B3 `statusBox` 的提示串 `"i/l/h/f/c/enter/del/esc"` 硬编码 3 处，新增按键易漏改。
- B4 无错误提示路径：文件读失败静默返回空列表，用户以为"任务都没了"。

## C. 功能缺失（本次要实现）

- C1 **点击"创建时间"/"完成时间"表头排序**（升/降切换，表头显示 ▲/▼）。
- C2 **delay 列表**：`b` 查看、`p` 推迟（进入 delay 列表）、`q` 恢复。

## D. 已确认的接口细节（FLTK 1.4.5）

- 列头点击可在子类 `handle()` 中于 `Fl_Table::handle()` **之前**拦截：
  `Fl_Table::cursor2rowcol()` 在列头内只填 `R=0, C=列号`，不会动
  `current_row/select_row`，所以提前 return 不会破坏行选择。用
  `get_bounds(CONTEXT_COL_HEADER,...)` 判区，`event_x()` 在边界 ±3px 内仍交给基类做列宽拖拽。
- `Fl_Table::rows(int)` 只 push/pop `_rowselect`，不清理选中状态 → 每次重建后必须
  自己 `select_all_rows(0)` + 重新 `select_row()`。
- `Fl_Table_Row::select_row()` 在 `SELECT_SINGLE` 下会把其它行全部清 0，可安全用于"只选一行"。

## E. 数据格式（必须向前兼容，`task/` 下的文件不许被改动）

现有文件：`size_t count; int nextId; bool hasNotes;` 然后每条
`int id; bool completed; time_t createdTime; time_t completedTime;
 size_t descLen; desc; size_t notesLen; notes`。

`task/tasks.dat` 已按此格式导出验证：30 条 / nextId=40 / 消耗字节数 == 文件长度，
最后一条 id=39 描述正好是 "task manager should add sorting feature"。

**最终采用的策略：延期状态放到独立的 `delay.dat` 旁路文件，不改 `tasks.dat` 一个字节。**
考虑过"在 notes 之后追加 DEL1 块"，但那样有两个缺点：
1. 老程序虽然能忽略尾部数据，但只要新程序一保存，老程序再保存就会**把延期信息抹掉**；
2. 延期信息的读写会污染主数据文件，一旦损坏损失的是整个任务列表。

旁路文件格式：`uint32 magic 'DEL1'; uint32 count; { int32 id; int64 delayedAt; }...`
（全部小端、定长）。没有任何延期任务时该文件会被**删除**，所以"不用 delay 功能"
的用户目录里不会多出文件。启动时若 `delay.dat` 损坏或引用了已删除的 id，
只丢弃延期信息并给出警告，任务列表照常加载。

自测里有一条硬性检查：把真实的 `task/tasks.dat` 读进来再原样写回，
**必须逐字节一致**（`tests/store_selftest.cpp::testRealFileRoundTrip`）。

## F. 本次实际做的修改

| 问题 | 处理 |
| --- | --- |
| A1 删除后选中漂移 / 无确认 | 每次重建列表后按 **任务 id** 恢复选中；`Del` 前用 `fl_choice()` 确认 |
| A2 列号硬编码 | 改成 `ColId` 枚举 + 每行过滤器的"列集合"表，`draw_cell` 只按 ColId 取值 |
| A3 自由函数反向依赖 | 全部改成类内回调（`SortRequestHandler`、lambda + `user_data`） |
| A4 `exit(0)` 丢数据 | 窗口回调改为 `hide()` + 保存，析构里也会保存 |
| A5 无校验 / 非原子写 | 每次 `read` 都做边界检查；写 `<file>.tmp` 再 rename；内容没变就跳过写 |
| A6 `localtime()` + 每格 `strftime` | 改用 `localtime_s`，并且把时间文本**缓存**在 `std::vector<std::string>` 里 |
| A7 状态不持久化 | 按用户要求：只做排序、不持久化（重启回到默认视图） |
| A8 方向键锚点漂移 | `selectDisplayRow()` 同步 `Fl_Table::select_row/current_row` |
| A9 每次按键全量写盘 | 内容未变化的保存直接跳过；备注缓冲区改由 `Fl_Text_Buffer` 管理 |
| B1 单文件 921 行、无测试 | 拆成 model / store / task_table / main_window / main，共 137 个断言的自测 |
| B2 编译命令不可复现 | `build.sh` / `build.cmd`，`-Wall -Wextra` 无警告 |
| B3 提示串硬编码 3 处 | 统一成 `kLegend` 一处，并且会显示 pending/delay/done 计数 |
| B4 读失败静默 | 状态栏明确报错；整个文件被拒绝时打印原因，绝不半加载 |
| C1 点击表头排序 | 列头点击实现（`^`/`v` 标记），另加 `v` 键循环切换 |
| C2 delay 列表 | `p` 推迟 / `b` 查看（带 *Delayed At* 列）/ `q` 恢复 |

### 额外发现并在新版本里规避的坑

1. **`FL_BUTTON1` 是状态位掩码（0x01000000），不是按键号。**
   判断"鼠标左键按下"必须用 `Fl::event_button() == FL_LEFT_MOUSE`。
   用 `FL_BUTTON1` 比较会永远为假，表头点击排序会静默失效。
2. **FLTK 是先给"焦点控件"发键盘事件，再往上冒泡。**
   原程序启动后焦点在输入框上，于是给表格写的 `d`/`c`/`p`/`q` 快捷键
   在输入框有焦点时会被输入框吃掉（输入框只在 Enter 上装回调，其他键不返回）。
   `MainWindow::handle()` 现在**先于** FLTK 的分发处理键盘事件：
   焦点在列表里（默认）时按字母就是命令，按 `i` 或鼠标点输入框后字母就是文本。
3. **`Fl_Table_Row::select_row(int,int)` 会遮蔽 `Fl_Table` 的 `int select_row` 成员**，
   设置它必须写 `this->Fl_Table::select_row = row;`。
4. **`Fl::screen_work_area()` 返回的是物理像素，窗口尺寸是 FLTK 逻辑单位。**
   这台机器是 200% 缩放，`900x600` 的窗口实际占 1800x1200 物理像素，
   1920x1080 的屏幕上状态栏会跑到屏幕外。`show()` 现在按缩放比例把窗口
   限制在工作区内并拉回可见区域。

