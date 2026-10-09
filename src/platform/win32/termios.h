#pragma once

/*
 * Windows（MinGW）专用的 <termios.h> 兼容层。
 *
 * Windows 上并没有 termios。编辑器真正需要的能力只有两条：
 *   1. 关掉回显和行缓冲，做到「按一个键就读到一个键」；
 *   2. 让方向键、Home、PageUp 这类按键以 \x1b[ 开头的转义序列返回，
 *      好让 Key.h 里那张 escMap 原封不动地继续用。
 * 这两件事在 Windows 控制台上分别对应 SetConsoleMode 的
 * ENABLE_VIRTUAL_TERMINAL_INPUT 与清掉 ENABLE_ECHO_INPUT / ENABLE_LINE_INPUT，
 * 本文件就把它们伪装成 tcgetattr / tcsetattr。
 *
 * 之所以做成「假头文件」而不是在 Terminal.cpp 里写 #ifdef：这样 src/ 下的
 * POSIX 源码一个字都不用改，Linux/WSL 的构建路径也完全没有被动过。
 * 本目录只在 Windows 构建时加入 include 路径。
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
#include <string.h>

/* 较老的 MinGW 头文件里可能还没有这两个常量 */
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#ifndef ENABLE_VIRTUAL_TERMINAL_INPUT
#define ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200
#endif

typedef unsigned char cc_t;
typedef unsigned int tcflag_t;
typedef int speed_t;

/* c_iflag */
#define BRKINT 0000002
#define ICRNL 0000400
#define INPCK 0000020
#define ISTRIP 0000040
#define IXON 0002000

/* c_oflag */
#define OPOST 0000001

/* c_cflag */
#define CS8 0000060

/* c_lflag */
#define ECHO 0000010
#define ICANON 0000002
#define IEXTEN 0100000
#define ISIG 0000001

/* c_cc 下标，取值与 Linux 一致，纯粹为了和 POSIX 代码看起来一样 */
#define VMIN 6
#define VTIME 5

#define TCSANOW 0
#define TCSADRAIN 1
#define TCSAFLUSH 2

struct termios
{
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t c_cc[32];

    /* 以下四个字段不是 POSIX 的一部分，用来把 Windows 控制台的原始模式
       暂存下来，好在退出时还原。tcgetattr 失败时 win_ok 为 0。 */
    unsigned long win_in_mode;
    unsigned long win_out_mode;
    int win_ok;
    int win_out_ok;
};

/*
 * 读出当前控制台模式，并把其中与 ECHO / ICANON / ISIG 对应的那几位反映到
 * c_lflag 上 —— 上层那句 `raw.c_lflag &= ~(...)` 才能照常生效。
 *
 * 如果标准输入不是控制台（比如被重定向成管道），返回 -1，且 win_ok 保持 0，
 * 之后的 tcsetattr 会直接跳过，不至于拿未初始化的结构体去乱设模式。
 */
static inline int tcgetattr(int fd, struct termios *t)
{
    (void)fd;

    if (!t)
    {
        errno = EINVAL;
        return -1;
    }

    memset(t, 0, sizeof(*t));

    HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD inMode = 0;
    if (hin == INVALID_HANDLE_VALUE || !GetConsoleMode(hin, &inMode))
        return -1;

    t->win_in_mode = inMode;
    t->win_ok = 1;

    if (inMode & ENABLE_ECHO_INPUT)
        t->c_lflag |= ECHO;
    if (inMode & ENABLE_LINE_INPUT)
        t->c_lflag |= ICANON;
    if (inMode & ENABLE_PROCESSED_INPUT)
        t->c_lflag |= ISIG;

    HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD outMode = 0;
    if (hout != INVALID_HANDLE_VALUE && GetConsoleMode(hout, &outMode))
    {
        t->win_out_mode = outMode;
        t->win_out_ok = 1;
    }

    /* Windows 没有沿用的那两个缓冲水位，填上 POSIX 的常用值只是为了让结构体
       看起来正常；真正的阻塞行为由控制台模式决定。 */
    t->c_cc[VMIN] = 1;
    t->c_cc[VTIME] = 0;

    return 0;
}

/*
 * 按 c_lflag 上的 ECHO / ICANON / ISIG 反推出目标控制台模式。
 *
 * 输入侧一律打开 ENABLE_VIRTUAL_TERMINAL_INPUT 并关掉 ENABLE_QUICK_EDIT_MODE：
 * 前者让按键编码成 \x1b[ 序列（与 POSIX 裸模式一致），后者不关的话鼠标点一下
 * 窗口就会把输入冻住。
 */
static inline int tcsetattr(int fd, int optional_actions, const struct termios *t)
{
    (void)fd;
    (void)optional_actions;

    if (!t)
    {
        errno = EINVAL;
        return -1;
    }

    if (!t->win_ok)
    {
        /* 输入不是控制台（管道 / 重定向），没有模式可设，安静地跳过 */
        errno = ENOTTY;
        return -1;
    }

    HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD inMode = (DWORD)t->win_in_mode;
    inMode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
    inMode |= ENABLE_EXTENDED_FLAGS; /* 否则下面的 QUICK_EDIT 改动不生效 */
    inMode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
    inMode &= ~(ENABLE_QUICK_EDIT_MODE | ENABLE_MOUSE_INPUT);

    if (t->c_lflag & ECHO)
        inMode |= ENABLE_ECHO_INPUT;
    if (t->c_lflag & ICANON)
        inMode |= ENABLE_LINE_INPUT;
    if (t->c_lflag & ISIG)
        inMode |= ENABLE_PROCESSED_INPUT;

    if (!SetConsoleMode(hin, inMode))
        return -1;

    if (t->win_out_ok)
    {
        HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD outMode = (DWORD)t->win_out_mode;
        /* 打开 VT 序列解释，\x1b[7m、\x1b[K 这些才能在控制台上生效 */
        outMode |= ENABLE_PROCESSED_OUTPUT;
        outMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        if (!SetConsoleMode(hout, outMode))
            return -1;
    }

    return 0;
}

/* 让 Terminal 知道裸模式到底有没有生效（管道输入时不会生效） */
static inline int tcIsConsole(void)
{
    HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD inMode = 0;
    return hin != INVALID_HANDLE_VALUE && GetConsoleMode(hin, &inMode);
}

#else
#error "src/platform/win32/termios.h 只应在 Windows 构建时使用；Linux/macOS 请用系统自带的 <termios.h>。"
#endif /* _WIN32 */
