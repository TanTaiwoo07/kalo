#include "Cursor.h"
#include <algorithm>

// ─── 归属：本文件约 95% 原始手搓。AI 只改了 ArrowLeft / ArrowRight 两处 ────
// （把逐字节挪动换成按 UTF-8 字符步进，见下方 [AI] 标记）

void Cursor::init(Screen &screen)
{
    x = 0;
    y = 0;
    rx = 0;
    coloff = 0;
    rowoff = 0;

    screen.getWindowSize();
    // 减 2 是状态栏与消息栏各占一行。
    // 问题：这里的 2 是硬编码的，且没做下限保护 —— 终端高度不足 2 行时
    // screenrows 会变成 0 或负数，scroll() 里的 rowoff 计算随即错乱。
    screenrows = screen.row - 2;
}

void Cursor::move(Key key, Buffer &buf)
{
    switch (key)
    {
    // 左右移动按「字符」走而不是按「字节」走：中文一个字占 3 个字节，
    // 逐字节挪会让光标停在半个 UTF-8 序列中间，之后插入/删除都会切坏字符。
    case Key::ArrowLeft:
        if (x != 0)
            x = buf[y].prevBoundary(x); // [AI] 原为 x--：按字节退会停在半个 UTF-8 序列上
        else if (y > 0)
            x = static_cast<int>(buf[--y].chars.size());
        break;
    case Key::ArrowRight:
        if (y < (int)buf.size() && x < (int)buf[y].chars.size())
            x = buf[y].nextBoundary(x); // [AI] 原为 x++
        else if (y < (int)buf.size() && x == (int)buf[y].chars.size())
            // 原代码问题：`y++, x = 0;` 把两条语句塞进一个逗号表达式，
            // 读起来像一条；而且这里会让 y 变成 buf.size()（越界一行），
            // 只是后面靠调用方 clampCursor() 兜着。
            y++, x = 0;
        break;
    case Key::ArrowUp:
        if (y != 0)
            y--;
        break;
    case Key::ArrowDown:
        if (y < (int)buf.size())
            y++;
        break;
    default:
        break;
    }
    // 问题：只夹 x 不夹 y。当 y 已经越界（停在末行之后的「幽灵行」）时，
    // x 保留着上一行的旧值，之后 buf[y] 就是越界访问 —— 这个坑后来由
    // Editor::clampCursor() 补上，但根源在这里处理不一致。
    //
    // 另一处风格问题：(int)buf.size() 是 C 风格强转，同文件其他位置用的
    // 是 static_cast，两种写法并存。
    if (y < (int)buf.size())
    {
        x = std::min(x, (int)buf[y].chars.size());
        if (x < static_cast<int>(buf[y].chars.size()))
            x = buf[y].cellCharByte(buf[y].cellOfCharByte(x));
    }
    else
        x = 0;
}

void Cursor::scroll(Screen &screen, Buffer &buf)
{
    rx = (y < (int)buf.size()) ? buf[y].xToRx(x) : 0;

    // 垂直滚动：让光标行始终落在 [rowoff, rowoff + screenrows) 区间里
    if (y < rowoff)
        rowoff = y;
    // 问题：screenrows 若为非正数（见 init 里的说明），rowoff 会算成 y+1，
    // 反而把光标自己挤出可视区，表现为窗口极小时光标乱跳。
    if (y >= rowoff + screenrows)
        rowoff = y - screenrows + 1;

    if (rx < coloff)
        coloff = rx;
    if (rx >= coloff + screen.col)
        coloff = rx - screen.col + 1;
}
