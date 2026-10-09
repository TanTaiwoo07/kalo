#include "Screen.h"

#include <termios.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cstdio>

// ─── 归属：本文件约 70% 原始手搓 ────────────────────────────────────
// 手搓：getCursorPosition / 构造函数 / appendRow / clear / print。
// AI 改的只有 getWindowSize 里那三处兜底分支（下方有 [AI] 标记），
// 目的是让它不再去读 stdin —— 详见函数内的说明。

// 非交互式环境下的兜底尺寸：够放下状态栏，也够绝大多数段落显示。
constexpr int kFallbackRows = 24; // [AI]
constexpr int kFallbackCols = 80; // [AI]

// 手搓：向终端问「光标现在在哪」，从而反推出窗口尺寸。
//
// 原代码问题：这个 read 没有任何超时或上限保护，非交互式下会一直阻塞 ——
// 调用方已经加了「不是终端就不走这条路」的判断来绕开，但函数本身仍然危险，
// 谁要是哪天直接在别处调它，就会重新踩进来。
static int getCursorPosition(int *rows, int *cols)
{
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
        return -1;

    while (i < sizeof(buf) - 1)
    {
        if (read(STDIN_FILENO, &buf[i], 1) != 1)
            break;
        if (buf[i] == 'R')
            break;
        i++;
    }

    buf[i] = '\0';
    if (buf[0] != '\x1b' || buf[1] != '[')
        return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
        return -1;
    return 0;
}

// 手搓。构造函数里做 I/O（问终端尺寸），失败时静默用兜底值。
// 问题：构造函数没法返回错误，调用方拿不到「尺寸没问到」这个信息。
Screen::Screen()
{
    getWindowSize();
}

// 手搓主体（问 ioctl、失败则移光标 + 问光标位置），AI 补了三处兜底（见 [AI] 标记）。
void Screen::getWindowSize()
{
    /*
     * [AI] 下面这个「只有交互才问」的判断是后加的。
     *
     * 只有在标准输入输出确实是交互式终端时，才用「移光标 + 问光标位置」的办法
     * 去问终端尺寸。
     *
     * 这个兜底路径是要从 **stdin 读** 终端回送的坐标信息的。如果 stdin 不是终端
     * （CI / 管道驱动的冒烟测试 / VS 里开着重定向调试），后果有两层：
     *   1. 它把真正的用户输入当成终端响应吃掉，编辑行为变得飘忽不定；
     *   2. 永远等不到 'R'，read 会一直阻塞 —— 进程表现为「卡死」，不报错也不退出。
     *      这一点在 MSVC 的 Debug（/MDd）CRT 上稳定复现，Release 下则时好时坏，
     *      因为能不能读到那 4 个字节取决于管道里数据到达的时机，典型竞态。
     *
     * 所以非终端场景直接用一个固定的默认尺寸，不碰 stdin，行为确定。
     */
    const bool interactive = isatty(STDOUT_FILENO) && isatty(STDIN_FILENO);

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
    {
        // [AI] 非交互时直接给默认尺寸，绝不碰 stdin
        if (!interactive)
        {
            row = kFallbackRows;
            col = kFallbackCols;
            return;
        }

        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
        {
            row = kFallbackRows;
            col = kFallbackCols;
            return;
        }

        if (getCursorPosition(&row, &col) != 0)
        {
            row = kFallbackRows;
            col = kFallbackCols;
        }
    }
    else
    {
        col = ws.ws_col;
        row = ws.ws_row;
    }
}

// 手搓。把一行内容追加到帧缓冲。
//
// 原代码问题（性能）：`buffer = buffer + s` 每次都会构造一个新字符串并整体拷贝。
// 一帧要调几十次，总代价是 O(n^2)。改成 `buffer += s` 即可，
// 但这是原代码的写法，按要求保留不动。
void Screen::appendRow(std::string s)
{
    buffer = buffer + s;
}

// 手搓。
void Screen::clear()
{
    buffer.clear();
}

// 手搓。整帧一次写出。
// 问题：write 可能只写一部分（短写），返回值也没检查，丢字符不会有任何提示。
void Screen::print()
{
    const char *p = buffer.c_str();
    int len = static_cast<int>(buffer.size());
    write(STDOUT_FILENO, p, len);
}
