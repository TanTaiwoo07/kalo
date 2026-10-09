#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <termios.h>
#include <unistd.h>
#include <unordered_map>
#include <string>
#include <vector>
#include "Terminal.h"

#ifdef _WIN32
#include <fcntl.h> // _O_BINARY
#include <io.h>    // _setmode
#include <stdio.h> // _fileno
#endif

void Terminal::cleanup()
{
    if (stdin_is_tty_)
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

#ifdef _WIN32
/*
 * Windows 控制台上还有没有「待读的按键」。
 *
 * 这是为了修掉一个 Windows 独有的问题：控制台的 read 是阻塞的，用户单独按下
 * Esc 时，read 拿到 ESC 之后就一直等后续字节，于是「按一下 Esc 没反应，得再按
 * 一个键才生效」。POSIX 那侧没这毛病 —— 裸模式设了 VMIN=0 / VTIME=1，read 返回
 * 0 就代表这段时间没有按键（见 readStdinByte 的 ReadResult::Retry）。
 *
 * 所以 Windows 上在读 ESC 之后、读后续字节之前先探一下：短时间内没有按键事件，
 * 就认定用户只是单独按了 Esc。
 */
static bool consoleInputPending(int timeoutMs)
{
    const HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE)
        return true; // 判断不了就按「有后续输入」处理，退回原来的阻塞行为

    const DWORD slice = 10;
    for (int waited = 0; waited < timeoutMs; waited += static_cast<int>(slice))
    {
        DWORD count = 0;
        if (GetNumberOfConsoleInputEvents(h, &count) && count > 0)
        {
            // 只认按键事件：鼠标、窗口尺寸变化这类事件不会变成要读的字节，
            // 把它们当成「有后续输入」会让 Esc 又卡回原样。
            std::vector<INPUT_RECORD> recs(count);
            DWORD got = 0;
            if (PeekConsoleInputA(h, recs.data(), count, &got))
            {
                for (DWORD i = 0; i < got; i++)
                {
                    if (recs[i].EventType == KEY_EVENT)
                        return true;
                }
            }
        }
        Sleep(slice);
    }
    return false;
}
#endif

[[noreturn]] static void die(const char *s)
{
    perror(s);
    exit(1);
}

Terminal::Terminal()
{
    enableRawMode();
}

Terminal::~Terminal()
{
    disableRawMode();
}

// 读一个字节的结果。必须把「暂时没按键」和「输入流真的结束了」分开：
// POSIX 下裸模式设的是 VMIN=0 / VTIME=1，read 返回 0 只说明这 100ms 内没有按键，
// 属于正常轮询；只有在非交互式输入（管道 / 重定向）上，0 才代表流结束。
enum class ReadResult
{
    Byte,
    Retry,
    Eof,
};

// 判定「单独按了 Esc」之前要等多久。太短会误判方向键（ESC [ A 三个字节
// 到达有间隔），太长会让按 Esc 的手感发黏。60ms 在两者之间。
constexpr int kEscTimeoutMs = 60;

static ReadResult readStdinByte(char *out)
{
    const int n = static_cast<int>(::read(STDIN_FILENO, out, 1));

    if (n == 1)
        return ReadResult::Byte;

    if (n == 0)
        return Terminal::stdin_is_tty_ ? ReadResult::Retry : ReadResult::Eof;

    if (errno == EAGAIN || errno == EWOULDBLOCK)
        return ReadResult::Retry;

    die("read");
}

Key Terminal::getkey()
{
    char c = 0;

    for (;;)
    {
        const ReadResult r = readStdinByte(&c);
        if (r == ReadResult::Retry)
            continue; // 还没按键，接着等
        if (r == ReadResult::Eof)
            return Key::Eof; // 输入流已结束，否则外层会在这里空转到烧满一个核
        break;
    }

    if (c != '\x1b')
    {
        return static_cast<Key>(c);
    }

#ifdef _WIN32
    // Windows 控制台的 read 会阻塞，单独按 Esc 时必须自己判定「后面没有了」，
    // 否则要再按一个键才生效（详见 consoleInputPending 的说明）。
    // 只在实际终端上这么做：管道输入时没有控制台事件可探，行为保持原样。
    if (stdin_is_tty_ && !consoleInputPending(kEscTimeoutMs))
        return Key::Escape;
#endif

    /*
     * 转义序列。这里按「这个序列究竟需要几个字节」来读，而不是像早先那样固定读
     * 若干字节 —— 早先的实现每次都会多读一个，把紧跟在方向键后面的按键一起吞掉
     * （Linux 上靠 100ms 超时侥幸掩盖了，Windows 控制台的阻塞读则会直接卡在等第
     * 四个字节上）；而且它对 F5 这种更长的序列只吃掉前面几个字节，剩下的 "~"
     * 会被当成普通字符插进文本里。
     *
     * escMap 里的键长这样：两字节的 "[A" / "OH"，以及带参数的 "[3~"。
     */
    std::string seq;
    char b = 0;

    auto appendIfAvailable = [&seq](char *dst) -> bool {
        if (readStdinByte(dst) != ReadResult::Byte)
            return false;
        seq += *dst;
        return true;
    };

    if (!appendIfAvailable(&b))
        return Key::Escape; // ESC 后面什么都没有：用户就是单独按了 ESC

    if (b == 'O')
    {
        // SS3 形式（OH / OF 这类）：后面固定再跟一个字节
        if (!appendIfAvailable(&b))
            return Key::Escape;
    }
    else if (b == '[')
    {
        // CSI 形式：中间是参数字节，最后一个字节落在 0x40~0x7E 区间。
        // 一直读到终止字节，才不会给后面留下 "^[[15~" 里的那个 "~"。
        int guard = 0;
        while (++guard <= 8)
        {
            if (!appendIfAvailable(&b))
                return Key::Escape;
            if (b >= '\x40' && b <= '\x7e')
                break;
        }
    }

    const auto it = escMap.find(seq);
    return (it != escMap.end()) ? it->second : Key::Escape;
}

void Terminal::disableRawMode()
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
        die("tcsetattr");
}

void Terminal::enableRawMode()
{
#ifdef _WIN32
    // Windows 的 CRT 默认把标准流当文本处理：会把 CRLF 折成 LF，更麻烦的是把 0x1A
    // 认作文件结束符 —— 于是 Ctrl-Z（撤销）这个按键根本传不进来。切成二进制模式后
    // 输入输出的字节流才和 POSIX 一致。
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    // tcgetattr 失败说明标准输入不是交互式终端（管道、重定向）：
    // 这时不进入裸模式，后面照常逐字节读取即可。
    stdin_is_tty_ = (tcgetattr(STDIN_FILENO, &orig_termios) == 0);
    if (!stdin_is_tty_)
        return;

    // 手搓：切裸模式。关回显、关行缓冲、关信号，把读改成非规范模式。
    // VMIN=0 / VTIME=1 表示 read 最多等 100ms，没数据就返回 0。
    //
    // 原代码问题：
    // 1. `c_cflag |= CS8` 没有先 `&= ~CSIZE` 清掉原先的字符宽度位。
    //    标准写法是先清再设，否则原来若是 CS7，两个位叠加起来是未定义组合。
    //    （实践中 Linux 默认就是 CS8，所以一直没出事。）
    // 2. tcsetattr 的返回值没检查 —— 设置失败会静默继续，
    //    之后所有「读不到数据就当超时」的判断都建立在它成功的前提上。
    struct termios raw = orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}
