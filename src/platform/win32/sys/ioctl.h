#pragma once

/*
 * Windows（MinGW）专用的 <sys/ioctl.h> 兼容层，只实现 TIOCGWINSZ 这一个查询。
 * 说明见上一级目录的 termios.h。
 */

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <errno.h>
#include <stdarg.h>

struct winsize
{
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

#define TIOCGWINSZ 0x5413
#define TIOCSWINSZ 0x5414

/* 拿不到真实窗口尺寸时的兜底值 */
#define WIN32_TERM_DEFAULT_ROWS 24
#define WIN32_TERM_DEFAULT_COLS 80

/*
 * 只认 TIOCGWINSZ。
 *
 * 注意：当标准输出不是控制台（例如重定向到文件或管道）时，这里填一个 80x24 的
 * 默认值并返回成功，而不是返回 -1。原因是 Screen::getWindowSize() 在 ioctl 失败
 * 后会退回到「往终端写 \x1b[6n 问光标位置」那条 POSIX 老路，而那个查询在 Windows
 * 上拿不到回答，反而会把标准输入里的按键字节吃掉。直接给个默认尺寸，既避开了这个
 * 陷阱，也顺手绕开了 row / col 保持未初始化时必然发生的那次崩溃。
 */
static inline int ioctl(int fd, int request, ...)
{
    (void)fd;

    va_list ap;
    va_start(ap, request);
    void *arg = va_arg(ap, void *);
    va_end(ap);

    if (request != TIOCGWINSZ || !arg)
    {
        errno = ENOTTY;
        return -1;
    }

    struct winsize *ws = (struct winsize *)arg;
    HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info;

    if (hout != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(hout, &info))
    {
        int cols = info.srWindow.Right - info.srWindow.Left + 1;
        int rows = info.srWindow.Bottom - info.srWindow.Top + 1;
        if (cols > 0 && rows > 0)
        {
            ws->ws_col = (unsigned short)cols;
            ws->ws_row = (unsigned short)rows;
            ws->ws_xpixel = 0;
            ws->ws_ypixel = 0;
            return 0;
        }
    }

    ws->ws_col = WIN32_TERM_DEFAULT_COLS;
    ws->ws_row = WIN32_TERM_DEFAULT_ROWS;
    ws->ws_xpixel = 0;
    ws->ws_ypixel = 0;
    return 0;
}

#else
#error "src/platform/win32/sys/ioctl.h 只应在 Windows 构建时使用；Linux/macOS 请用系统自带的 <sys/ioctl.h>。"
#endif /* _WIN32 */
