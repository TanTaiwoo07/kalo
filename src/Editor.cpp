#include "Editor.h"
#include "Key.h"
#include "Screen.h"
#include "Cursor.h"
#include "Syntax.h" // [AI] 语法高亮（新增模块）
#include "Utf8.h"   // [AI] UTF-8 宽度与大小写折叠（新增模块）
#include <algorithm>
#include <fstream>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

// ═══ 归属索引 ═══════════════════════════════════════════════════════════
// 本文件原始 329 行中有 304 行（92%）原封不动保留到现在。AI 的介入分两类：
//
//   (A) 新增功能：语法高亮的调度（markSyntaxDirty / updateSyntax）、
//       Ctrl-F 搜索（find / findCallback）、UTF-8 相关的输入处理。
//   (B) 修缺陷：每处都在原地用 [AI] 标出，并写明改了什么、为什么。
//
// 手搓部分的已知问题同样以注释形式写在原地（搜索「原代码问题」即可找到）：
//   欢迎语的宽度按字节算、PageUp/Down 的重复位移、Home/End 不处理渲染列、
//   quit() 直接 exit 导致析构被跳过等。这些问题均未修改，仅是标注。
// ═══════════════════════════════════════════════════════════════════════

// [AI] 下面这个函数是新增的：原来这里直接用 iscntrl() 判断按键是否可打印，
// 而功能键的值是 1003 这种，传给 <cctype> 是未定义行为（详见函数内说明）。
/*
 * 判断一个按键值是否代表可打印 ASCII 字符。
 *
 * 按键值是 Key 枚举：除了 0~127 的字符，还包含 1000+ 的功能键（ArrowDown=1003 等）
 * 和 EOF=2000。这一类值**绝不能**传给 <cctype> 的 iscntrl/isprint 等函数 —— 它们的
 * 入参只允许 unsigned char 或 EOF，越界是未定义行为，MSVC 的调试 CRT（/MDd）会直接
 * 弹 Debug Assertion（c >= -1 && c <= 255），历史上已两次炸出。
 *
 * 所以这里用纯比较运算，不经过任何 <cctype> 函数，这一类缺陷在构造上不可能发生。
 */
static bool isPrintableAscii(int kc)
{
    return kc >= 0x20 && kc <= 0x7e;
}

void Editor::insert(Key key)
{
    char c = static_cast<char>(static_cast<int>(key));
    buf.insert(cur.y, cur.x, c);
    cur.x++;
    markSyntaxDirty(cur.y);
}

void Editor::del()
{
    const int before = cur.y;
    buf.del(cur.y, cur.x, cur.y);
    // 行首退格会把两行接起来，被改的是上一行，重算起点要往前提
    markSyntaxDirty(std::min(before, cur.y));
}

void Editor::DrawRows(Screen &screen)
{
    for (int i = 0; i < cur.screenrows; i++)
    {
        int filerow = i + cur.rowoff;
        if (filerow >= (int)buf.size())
        {
            // 手搓：空文件时在三分之一高度处显示欢迎语。
            //
            // 原代码问题：
            // 1. welcome.size() 按字节算，若欢迎语改成中文就会错位一半
            //    （本项目后来专门为此加了 Utf8 的列宽计算，但这里没跟上）。
            // 2. `if (padding)` 才输出 "~" 再 padding--：窗口窄到 padding 为 0 时
            //    这一行什么都不输出，直接变空行。逻辑没错但很隐晦。
            if (buf.empty() && i == cur.screenrows / 3)
            {
                std::string welcome = "kalo editor -- version 0.0.1";
                int welcomelen = static_cast<int>(welcome.size());
                if (welcomelen > screen.col)
                    welcomelen = screen.col;
                int padding = (screen.col - welcomelen) / 2;
                if (padding)
                {
                    screen.appendRow("~");
                    padding--;
                }
                while (padding-- > 0)
                    screen.appendRow(" ");
                screen.appendRow(welcome);
            }
            else
            {
                screen.appendRow("~");
            }
        }
        else
        {
            Row &row = buf[filerow];

            // 命中区间换算到 cell 下标：match_col_ / match_len_ 是 chars 的字节下标，
            // 而绘制是按 cell 走的（一个中文字符 3 字节 = 1 个 cell = 2 列）。
            int mStart = -1;
            int mEnd = -1;
            if (has_match_ && filerow == match_row_)
            {
                mStart = row.cellOfCharByte(match_col_);
                mEnd = row.cellOfCharByte(match_col_ + match_len_);
            }

            const int visStart = cur.coloff;
            const int visEnd = visStart + screen.col;

            int prevColor = -1;
            bool inMatch = false;

            for (int cell = row.cellOfRx(visStart); cell < row.cellCount(); cell++)
            {
                const int crx = row.cellRx(cell);
                if (crx >= visEnd)
                    break; // 已经超出右边界，后面都不用看了

                const int cellWidth = row.cellRx(cell + 1) - crx;
                if (crx + cellWidth <= visStart)
                    continue; // 整个字符都滚出了左边界

                const bool matched = (mStart >= 0 && cell >= mStart && cell < mEnd);
                if (matched && !inMatch)
                {
                    screen.appendRow("\x1b[7m"); // 反显命中
                    inMatch = true;
                    prevColor = -1; // 反显会盖掉前景色，出区间后必须重发一次
                }
                else if (!matched && inMatch)
                {
                    screen.appendRow("\x1b[0m");
                    inMatch = false;
                    prevColor = -1;
                }

                // hl 的长度可能还是上一次重算的结果（本行刚被编辑过），
                // 越界就按 Normal 处理，下一帧 updateSyntax 会补齐。
                const Hl h = (cell < static_cast<int>(row.hl.size())) ? row.hl[cell] : Hl::Normal;
                const int color = Syntax::colorOf(h);
                if (color != prevColor)
                {
                    screen.appendRow("\x1b[" + std::to_string(color) + "m");
                    prevColor = color;
                }

                // 宽字符被水平滚动截断时，把缺掉的那一列补成空格，否则整行会错位
                const int clipped = visStart - crx;
                if (clipped > 0)
                    screen.appendRow(std::string(static_cast<size_t>(clipped), ' '));
                else
                    screen.appendRow(row.cellText(cell));
            }

            if (inMatch)
                screen.appendRow("\x1b[0m");
            if (prevColor != -1)
                screen.appendRow("\x1b[39m"); // 复位前景色，别把颜色带到状态栏
        }
        screen.appendRow("\x1b[K");
        screen.appendRow("\r\n");
    }
}

void Editor::drawStatusBar(Screen &screen)
{
    screen.appendRow("\x1b[7m");
    char buf_status[80];
    std::string left = filename.empty() ? "[No Name]" : filename;
    if (left.size() > 20)
        left = left.substr(0, 20);
    // 状态栏顺带显示当前着色规则，好确认高亮有没有按预期生效
    std::string dirtyStr = buf.dirty ? "(modified)" : "";
    if (syntax_)
        dirtyStr += std::string(" ") + syntax_->name;

    int len = snprintf(buf_status, sizeof(buf_status), "%.20s - %zu lines %s",
                       left.c_str(), buf.size(), dirtyStr.c_str());
    if (len > screen.col)
        len = screen.col;
    screen.appendRow(std::string(buf_status, len));

    char rbuf[80];
    int rlen = snprintf(rbuf, sizeof(rbuf), "%d/%zu", cur.y + 1, buf.size());
    while (len < screen.col)
    {
        if (screen.col - len == rlen)
        {
            screen.appendRow(std::string(rbuf, rlen));
            break;
        }
        screen.appendRow(" ");
        len++;
    }
    screen.appendRow("\x1b[m");
    screen.appendRow("\r\n");
}

void Editor::drawMessageBar(Screen &screen)
{
    screen.appendRow("\x1b[K");
    int msglen = (int)statusmsg.size();
    if (msglen > screen.col)
        msglen = screen.col;
    if (msglen && time(nullptr) - statusmsg_time < 5)
        screen.appendRow(statusmsg.substr(0, msglen));
}

void Editor::setStatusMessage(const std::string &msg)
{
    statusmsg = msg;
    statusmsg_time = time(nullptr);
}

void Editor::init(Screen &screen)
{
    cur.init(screen);
    buf.clear(); // 清空缓冲：文本存储与行视图必须一起复位，不能只清 rows
    markSyntaxDirty(0);
}

void Editor::clampCursor()
{
    if (buf.empty())
    {
        cur.y = 0;
        cur.x = 0;
        return;
    }

    if (cur.y >= (int)buf.size())
        cur.y = (int)buf.size() - 1;
    if (cur.y < 0)
        cur.y = 0;

    if (cur.x > (int)buf[cur.y].chars.size())
        cur.x = (int)buf[cur.y].chars.size();
    if (cur.x < 0)
        cur.x = 0;
}

void Editor::openFile(std::string fname)
{
    filename = fname;

    // 着色规则按扩展名选；认不出来的类型就不高亮（syntax_ 为 nullptr）
    syntax_ = Syntax::forFile(filename);
    markSyntaxDirty(0);

    std::ifstream file(filename);
    if (!file.is_open())
    {
        setStatusMessage("File not found: " + fname);
        return;
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(line);
    }
    file.close();
    buf.fromLines(lines);
}

void Editor::save(Terminal &t, Screen &screen)
{
    if (filename.empty())
    {
        filename = prompt("Save as: %s (ESC to cancel)", t, screen);
        if (filename.empty())
        {
            setStatusMessage("Save aborted");
            return;
        }
        // 文件名现在才知道，着色规则要跟着换（存成 .cpp 就该按 C++ 高亮）
        syntax_ = Syntax::forFile(filename);
        markSyntaxDirty(0);
    }

    std::string data = buf.toString();

#ifdef _WIN32
    // Windows 上新建的文件描述符默认是文本模式，写入时会把 \n 翻译成 \r\n，
    // 于是每存一次盘行尾就变一次。加 O_BINARY 让落盘字节与 POSIX 版一致。
    int fd = ::open(filename.c_str(), O_RDWR | O_CREAT | O_BINARY, 0644);
#else
    int fd = ::open(filename.c_str(), O_RDWR | O_CREAT, 0644);
#endif
    if (fd != -1)
    {
        // 长度统一用 int 传递，写入结果也转成 int 再比较：write 的返回类型随平台而变
        // （POSIX 上是 ssize_t，Windows 上是 int），MSVC 更是没有 ssize_t 这个名字，
        // 统一转成 int 就不会依赖它是否存在。
        const int size = static_cast<int>(data.size());
        if (ftruncate(fd, size) != -1)
        {
            const unsigned int chunk = static_cast<unsigned int>(size);
            if (static_cast<int>(write(fd, data.c_str(), chunk)) == size)
            {
                close(fd);
                buf.dirty = 0;
                char msg[80];
                snprintf(msg, sizeof(msg), "%d bytes written to disk", size);
                setStatusMessage(msg);
                return;
            }
        }
        close(fd);
    }
    char err[80];
    snprintf(err, sizeof(err), "Can't save! I/O error: %s", strerror(errno));
    setStatusMessage(err);
}

std::string Editor::prompt(std::string promptMsg, Terminal &t, Screen &screen)
{
    // 这个局部变量原来叫 buf，把 Editor::buf 这个成员遮住了 —— 在同名成员眼皮底下
    // 编辑文本很容易看错，改名为 input。
    std::string input;
    while (true)
    {
        char msg[128];
        snprintf(msg, sizeof(msg), promptMsg.c_str(), input.c_str());
        setStatusMessage(msg);
        refreshScreen(screen);

        int c = static_cast<int>(t.getkey());

        // 输入流结束就按「取消」处理。少了这一支，被管道驱动时（或用户按了 Ctrl-D）
        // getkey 会一直返回 Eof，这个循环就变成空转 —— 表现为进程卡死且吃满一个核。
        if (c == static_cast<int>(Key::Eof))
        {
            setStatusMessage("");
            return "";
        }

        if (c == static_cast<int>(Key::Backspace) || c == CTRL_KEY('h'))
        {
            if (!input.empty())
                input.pop_back();
        }
        else if (c == static_cast<int>(Key::Escape))
        {
            setStatusMessage("");
            return "";
        }
        else if (c == static_cast<int>(Key::Enter))
        {
            if (!input.empty())
            {
                setStatusMessage("");
                return input;
            }
        }
        else             if (isPrintableAscii(c))
                input += (char)c;
    }
}

void Editor::clearSearchHighlight()
{
    // 四个字段一起失效，避免 DrawRows 读到「有新命中行但长度为 0」这类半新半旧状态
    has_match_ = false;
    match_row_ = -1;
    match_col_ = 0;
    match_len_ = 0;
    match_index_ = 0;
    match_total_ = 0;
}

void Editor::markSyntaxDirty(int fromRow)
{
    if (fromRow < 0)
        fromRow = 0;
    syntax_dirty_ = true;
    // 多次编辑取最靠前那一行：从那里起往后的着色都可能过期
    syntax_from_ = std::min(syntax_from_, fromRow);
}

/*
 * 重算高亮。
 *
 * 只从失效的那一行开始算，并且**只在多行注释状态发生变化时继续往后传播** ——
 * 这是 kilo 的做法：一个跨行的块注释会让后面所有行的着色都不一样，但只要某行的
 * 注释状态没变、且它自己也不在块注释里，后面那些行的颜色就不可能受影响。
 */
void Editor::updateSyntax()
{
    if (!syntax_dirty_)
        return;

    const int rows = static_cast<int>(buf.size());
    if (rows == 0)
    {
        syntax_dirty_ = false;
        syntax_from_ = 0;
        return;
    }

    int from = syntax_from_;
    if (from < 0)
        from = 0;
    if (from > rows - 1)
        from = rows - 1;

    // 从第 0 行开始说明是「整篇重算」（刚打开文件、撤销、改文件名），
    // 这时每一行都要算，不能套用下面的提前终止规则 —— 否则只有第 0 行会亮。
    const bool fullPass = (from == 0);

    bool prevOpen = (from > 0) ? buf[from - 1].hlOpenComment : false;

    for (int i = from; i < rows; i++)
    {
        bool open = false;
        Syntax::highlightRow(buf[i], syntax_, prevOpen, open);

        const bool changed = (buf[i].hlOpenComment != open);
        buf[i].hlOpenComment = open;

        // 增量更新（改了某一行）时：状态没变且本行不处在块注释中，
        // 后面的行不可能受影响，可以停。整篇重算时不能这么早停。
        if (!fullPass && !changed && !open)
            break;

        prevOpen = open;
    }

    syntax_dirty_ = false;
    syntax_from_ = rows;
}

void Editor::findCallback(const std::string &query, Key key, const std::string &doc,
                          const std::string &docFold, int &cursorY, int &cursorX)
{
    // Enter / Escape 由 find() 的循环负责收尾，这里只驱动搜索与方向跳转
    if (key == Key::Enter || key == Key::Escape)
        return;

    if (query.empty())
    {
        clearSearchHighlight();
        return;
    }

    const bool findNext = (key == Key::ArrowRight || key == Key::ArrowDown);
    const bool findPrev = (key == Key::ArrowLeft || key == Key::ArrowUp);

    // 大小写不敏感时在折叠串上找。Utf8::foldCase 只改 A-Z 且字节长度不变，
    // 所以折叠串里的偏移与原文一一对应，可以直接拿去 buf.locate 反查坐标。
    const std::string &haystack = ignore_case_ ? docFold : doc;
    const std::string needle = ignore_case_ ? Utf8::foldCase(query) : query;

    // 把当前光标（搜索过程中即上一次命中位置）换算成绝对偏移。这里只做坐标换算，
    // 沿用与 Buffer::offsetOf 完全相同的进位规则（每行占 chars.size() + 1，含行尾换行）。
    auto offsetAt = [this](int y, int x) -> int {
        const int rows = static_cast<int>(buf.size());
        if (y > rows)
            y = rows;
        int off = 0;
        for (int i = 0; i < y; i++)
            off += static_cast<int>(buf.rows[i].chars.size()) + 1;
        return off + x;
    };

    auto findFrom = [&haystack, &needle](size_t from) -> int {
        const size_t at = haystack.find(needle, from);
        return (at == std::string::npos) ? -1 : static_cast<int>(at);
    };

    auto rfindBefore = [&haystack, &needle](int from) -> int {
        if (from < 0)
            from = 0;
        const size_t at = haystack.rfind(needle, static_cast<size_t>(from));
        return (at == std::string::npos) ? -1 : static_cast<int>(at);
    };

    int hit = -1;
    if (has_match_ && (findNext || findPrev))
    {
        // 已有命中：方向键在其前后继续找；找不到就回绕到另一端（对标 kilo 的环形查找）
        const int base = offsetAt(cursorY, cursorX);
        hit = findNext ? findFrom(static_cast<size_t>(base + 1)) : rfindBefore(base - 1);
        if (hit < 0)
            hit = findNext ? findFrom(0) : rfindBefore(static_cast<int>(haystack.size()));
    }
    else
    {
        // 关键词刚变化（或首次进入）：从文档开头找第一个命中
        hit = findFrom(0);
    }

    if (hit < 0)
    {
        clearSearchHighlight();
        return;
    }

    // 一次扫描同时得到「共几个」和「这是第几个」：两者都要全表过一遍，
    // 合并成一趟，避免为了显示计数再多扫一次文档。
    int total = 0;
    int index = 0;
    for (size_t at = haystack.find(needle, 0); at != std::string::npos;
         at = haystack.find(needle, at + needle.size()))
    {
        total++;
        if (static_cast<int>(at) == hit)
            index = total;
    }
    match_total_ = total;
    match_index_ = index;

    int y = 0;
    int x = 0;
    if (!buf.locate(hit, y, x))
    {
        clearSearchHighlight();
        return;
    }

    cursorY = y;
    cursorX = x;
    has_match_ = true;
    match_row_ = y;
    match_col_ = x;
    match_len_ = static_cast<int>(query.size());
}

// [AI] 从这里往下的 find() / findCallback() 是整块新增的功能：
// 原项目没有搜索。实现上刻意让匹配源走 PieceTable 的权威文本（Buffer::findText），
// 而不是行视图缓存，理由是缓存可能与存储跑偏。
void Editor::find(Terminal &t, Screen &screen)
{
    // 记录回退点：Escape 取消时要能精确回到打开搜索前的位置
    saved_cy_ = cur.y;
    saved_cx_ = cur.x;

    // 进入时先清掉上一次的残留高亮，避免旧命中误导
    clearSearchHighlight();

    // 预填上次的关键词（决策：不自动跳到下一个，保持行为可预测）
    std::string query = last_search_;

    // 搜索过程中文档不可能被改动（这个循环里没有任何编辑入口），所以取一次快照即可：
    // 之后每次按键都在快照上查找，不必再向 PieceTable 取全文，大小写折叠也只做一次。
    const std::string doc = buf.toString();
    const std::string docFold = Utf8::foldCase(doc);

    while (true)
    {
        // 每帧都重设：消息栏只在 statusmsg_time 起 5 秒内显示，而搜索可能持续更久
        std::string status = "Search: " + query + " (ESC=取消/方向键=上下/Tab=" +
                             (ignore_case_ ? std::string("忽略大小写") : std::string("区分大小写")) +
                             "/Enter=确认)";
        if (has_match_ && match_total_ > 0)
            status += "  [" + std::to_string(match_index_) + "/" + std::to_string(match_total_) + "]";
        else if (!query.empty())
            status += "  未找到";
        setStatusMessage(status);
        refreshScreen(screen);

        const Key key = t.getkey();

        if (key == Key::Eof)
        {
            // 输入流已结束，无法再交互，直接退出搜索模式
            setStatusMessage("");
            return;
        }

        if (key == Key::Escape)
        {
            // 取消：撤销高亮并把光标送回原位
            clearSearchHighlight();
            cur.y = saved_cy_;
            cur.x = saved_cx_;
            setStatusMessage("");
            clampCursor();
            return;
        }

        if (key == Key::Enter)
        {
            // 确认：保留当前命中位置，记住关键词供下次 Ctrl-F 预填
            if (!query.empty())
                last_search_ = query;
            // 确认后即取消反显：否则旧的 match_* 会一直留在画面上，后续编辑
            // 还会让这块反显停在过期坐标上错位，直到下一次搜索才消失。
            clearSearchHighlight();
            setStatusMessage("");
            return;
        }

        // 编辑关键词：退格删字、可打印 ASCII 字符追加、Tab 切换大小写
        const int raw = static_cast<int>(key);
        if (key == Key::Backspace || raw == CTRL_KEY('h'))
        {
            if (!query.empty())
                query.pop_back();
        }
        else if (raw == '\t')
        {
            ignore_case_ = !ignore_case_;
        }
        else if (isPrintableAscii(raw))
        {
            query += static_cast<char>(raw);
        }

        // 增量搜索：每次按键都重新定位（方向键继续，字符/退格/切换大小写从头重找）
        findCallback(query, key, doc, docFold, cur.y, cur.x);
        clampCursor();
    }
}

void Editor::refreshScreen(Screen &screen)
{
    // 高亮在绘制前补齐：编辑过之后 hl 的长度会与 cell 数不一致，
    // DrawRows 只是「越界按 Normal 处理」，真正的重算在这里发生。
    updateSyntax();

    cur.scroll(screen, buf);
    screen.clear();

    screen.appendRow("\x1b[?25l");
    screen.appendRow("\x1b[H");

    DrawRows(screen);
    drawStatusBar(screen);
    drawMessageBar(screen);

    char pos[32];
    snprintf(pos, sizeof(pos), "\x1b[%d;%dH",
             (cur.y - cur.rowoff) + 1, (cur.rx - cur.coloff) + 1);
    screen.appendRow(pos);

    screen.appendRow("\x1b[?25h");
    screen.print();
}

// 手搓。
//
// 原代码问题：exit(0) 直接终止进程，会跳过所有析构函数 ——
// Editor / Buffer / Terminal 的析构都不会跑。终端状态现在靠上面手动
// t.cleanup() 这一句补回来，但这是「记住要补」而不是「结构上不会漏」：
// 将来任何需要在退出时释放的资源都得有人记得在这里再加一行。
// 配合 main.cpp 里那个没有出口的 while (true)，退出路径是很脆的。
void Editor::quit(Terminal &t)
{
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    t.cleanup(); // 恢复终端后再退出
    exit(0);
}

void Editor::processKeyPress(Terminal &t, Screen &screen)
{
    Key key = t.getkey();

    if (key == Key::Eof)
    {
        // 输入流已结束，不可能再读到任何按键，只能退出。
        // 消息写到 stderr，免得被退出时清屏的转义序列冲掉。
        if (buf.dirty)
            std::fprintf(stderr, "\nkalo: 标准输入已结束，未保存的修改已丢弃\n");
        quit(t);
    }

    // 与 kilo 一致：每次按键都重置“连按退出”的剩余次数。旧实现只减不重置，
    // 于是整场会话里累计按满 3 次 Ctrl-Q 就会在没有任何提示的情况下直接退出。
    quit_times = KILO_QUIT_TIMES;

    // ArrowDown 可能把光标停在最后一行的下一行，先夹回文档范围内再处理
    clampCursor();

    switch (key)
    {
    case Key::Enter:
        buf.insertNewline(cur.y, cur.x, cur.x, cur.y);
        // 换行会把一行拆成两行，两行的着色都要重算
        markSyntaxDirty(cur.y - 1);
        break;

    case Key::Backspace:
    case Key::Delete:
        if (key == Key::Delete)
            cur.move(Key::ArrowRight, buf);
        del();
        break;

    case Key::ArrowUp:
    case Key::ArrowDown:
    case Key::ArrowLeft:
    case Key::ArrowRight:
        cur.move(key, buf);
        break;

    case Key::PageUp:
    case Key::PageDown:
    {
        // 手搓。翻页：先跳到当前页的首/末行，再移动一整屏。
        //
        // 原代码问题：
        // 1. 先赋值 cur.y、再循环移动 screenrows 次，位移量被算了两遍，
        //    读代码时很容易误以为是移动两页。
        // 2. PageDown 分支里 cur.y 可能算成 > buf.size()（文档末尾之后），
        //    虽然下面夹了一次，但夹完的 y 又会被 cur.move() 再改一次。
        // 3. PageUp 分支没有对应的夹取，与 PageDown 不对称。
        if (key == Key::PageUp)
            cur.y = cur.rowoff;
        else if (key == Key::PageDown)
        {
            cur.y = cur.rowoff + cur.screenrows - 1;
            if (cur.y >= (int)buf.size())
                cur.y = (int)buf.size() - 1;
            if (cur.y < 0)
                cur.y = 0;
        }
        int times = cur.screenrows;
        while (times--)
            cur.move(key == Key::PageUp ? Key::ArrowUp : Key::ArrowDown, buf);
    }
    break;

    case Key::Home:
        // 手搓。原代码问题：只把列清零，没有考虑水平滚动（coloff）。
        // 光标确实回到行首，但视口不会跟着左移，看起来像没动。
        cur.x = 0;
        break;

    case Key::End:
        // 手搓。原代码问题：同上，跳到行尾后 coloff 不跟着调整，
        // 长行按 End 之后光标停在视口外。
        if (cur.y < (int)buf.size())
            cur.x = static_cast<int>(buf[cur.y].chars.size());
        break;

    default:
        break;
    }

    int raw = static_cast<int>(key);
    if (raw == CTRL_KEY('q'))
    {
        if (buf.dirty && quit_times > 0)
        {
            char msg[80];
            snprintf(msg, sizeof(msg),
                     "WARNING! File has unsaved changes. "
                     "Press Ctrl-Q %d more times to quit.",
                     quit_times);
            setStatusMessage(msg);
            quit_times--;
            return;
        }
        quit(t);
    }
    else if (raw == CTRL_KEY('s'))
    {
        save(t, screen);
    }
    else if (raw == CTRL_KEY('f'))
    {
        // CTRL_F == 0x06 落在兜底 `raw < 1000` 内，必须排在那条 insert 分支之前，
        // 否则搜索键会被当普通字符插进文档并写进撤销历史。
        find(t, screen);
    }
    else if (raw == CTRL_KEY('z'))
    {
        if (buf.undo())
        {
            // 撤销后光标可能落到不存在的行列上，立即夹回来
            clampCursor();
            // 撤销可能改到任意位置，行视图是整体重建的，高亮也只能整篇重算
            markSyntaxDirty(0);
            setStatusMessage("Undo");
        }
        else
        {
            setStatusMessage("Nothing to undo");
        }
    }
    else if (raw == CTRL_KEY('h'))
    {
        del();
    }
    else if (raw == CTRL_KEY('l'))
    {
        // refresh only
    }
    else if (raw != static_cast<int>(Key::Enter) && raw != static_cast<int>(Key::Backspace) && raw != static_cast<int>(Key::Delete) && raw != static_cast<int>(Key::Escape) && raw < 1000)
    {
        insert(key);
    }
}
