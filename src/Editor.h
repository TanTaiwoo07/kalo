#pragma once

#include <vector>
#include <string>
#include <ctime>
#include "Row.h"
#include "Syntax.h"
#include "Terminal.h"
#include "Screen.h"
#include "Buffer.h"
#include "Cursor.h"

#define KILO_QUIT_TIMES 3

class Editor
{
private:
    Buffer buf;
    Cursor cur;
    int quit_times = KILO_QUIT_TIMES;
    std::string filename;
    std::string statusmsg;
    time_t statusmsg_time = 0;

    /// 上一次使用的搜索关键词。再次按 Ctrl-F 时用于预填。
    std::string last_search_;

    /// 当前是否存在有效命中区间。为 false 时 DrawRows 不做任何反显。
    bool has_match_ = false;

    /// 当前命中区间，编辑器坐标：起始行 / 起始列 / 关键词长度。
    /// 注意 match_len_ 是「关键词长度」而非「本行内可见长度」，
    /// 绘制时按 cell 换算，并由 DrawRows 按行尾截断。
    int match_row_ = -1;
    int match_col_ = 0;
    int match_len_ = 0;

    /// 打开搜索前保存的光标位置，Escape 取消时用于回退。
    int saved_cy_ = 0;
    int saved_cx_ = 0;

    /// 大小写不敏感搜索开关。搜索模式里按 Tab 切换，会一直记住。
    bool ignore_case_ = false;

    /// 当前命中是第几个 / 一共几个，用于状态栏显示。
    int match_index_ = 0;
    int match_total_ = 0;

    /// 当前文件所用的着色规则；未知类型（或无文件名）时为 nullptr，整篇不高亮。
    const SyntaxRule *syntax_ = nullptr;

    /// 高亮重算的调度状态：syntax_dirty_ 为真时，从 syntax_from_ 这一行开始重算
    /// （并沿「多行注释状态是否变化」向后传播，见 updateSyntax）。
    bool syntax_dirty_ = true;
    int syntax_from_ = 0;

    std::string prompt(std::string promptMsg, Terminal &t, Screen &screen);

    /// 打开搜索提示行并进入模态输入循环（Ctrl-F 的入口）。
    ///
    /// 进入时取一次文档快照：搜索过程中文档不会被改动，因此不必每次按键都
    /// 重新向 PieceTable 取全文；大小写不敏感只折叠一次。这也是把「每次按键
    /// 全表扫描」降下来的关键 —— 命中计数与大小写折叠都在快照上做。
    ///
    /// @param t      终端，用于读键。
    /// @param screen 屏幕，用于每帧刷新。
    /// @note 本方法不改变 Buffer 内容，只改变光标与命中高亮状态。
    void find(Terminal &t, Screen &screen);

    /// 处理搜索提示行中的一次按键，驱动增量搜索与方向跳转。
    ///
    /// @param query     当前关键词（只读，由调用方维护）。
    /// @param key       本次读到的按键。
    /// @param doc       文档快照（原文）。
    /// @param docFold   文档快照的 ASCII 大小写折叠版，与 doc 等长，偏移一一对应。
    /// @param cursorY   输入/输出：光标行（会随命中移动到新位置）。
    /// @param cursorX   输入/输出：光标列。
    void findCallback(const std::string &query, Key key, const std::string &doc,
                      const std::string &docFold, int &cursorY, int &cursorX);

    /// 清除当前命中区间，使 DrawRows 不再反显。
    void clearSearchHighlight();

    /// 说明从第 fromRow 行起的高亮已失效，下一帧重算。传 0 表示整篇重算。
    void markSyntaxDirty(int fromRow);

    /// 重算失效区间的高亮。多行注释会跨行，所以某一行的注释状态发生变化时，
    /// 必须继续往后传播（kilo 的做法），否则后面的行会停在过期的着色上。
    void updateSyntax();

    // ArrowDown 允许光标落到最后一行的下一行，编辑前先夹回文档范围内，
    // 否则插入 / 换行会在越界的行号上操作
    void clampCursor();

public:
    void init(Screen &screen);
    void openFile(std::string fname);
    void save(Terminal &t, Screen &screen);
    void quit(Terminal &t);

    void DrawRows(Screen &screen);
    void drawStatusBar(Screen &screen);
    void drawMessageBar(Screen &screen);
    void setStatusMessage(const std::string &msg);
    void refreshScreen(Screen &screen);
    void processKeyPress(Terminal &t, Screen &screen);

    void insert(Key key);
    void del();
};
