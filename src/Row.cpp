#include "Row.h"

#include "Utf8.h"

#include <algorithm>
#include <string>

// 手搓。制表符展开成几个空格。
// 命名问题：KILO_TAB_STOP 带着蓝本项目的前缀，而且用宏而不是常量。
// 项目已经全是 C++17 了，写成 constexpr int kTabStop = 4 更合适。
#define KILO_TAB_STOP 4

// 手搓（骨架保留，AI 往里加了 UTF-8 与东亚宽度的处理）。
// 一次遍历同时算出四张表：render（写给终端的字节）、以及字节下标 /
// 渲染列 / 字符单元三者之间的换算表。
int Row::update()
{
    render.clear();
    cellByte_.clear();
    cellRx_.clear();
    cellCharByte_.clear();

    int rx = 0;
    int i = 0;
    const int n = static_cast<int>(chars.size());

    while (i < n)
    {
        cellCharByte_.push_back(i);
        cellRx_.push_back(rx);
        cellByte_.push_back(static_cast<int>(render.size()));

        const unsigned char c = static_cast<unsigned char>(chars[i]);

        if (c == '\t')
        {
            // 制表符按 KILO_TAB_STOP 对齐展开成空格
            do
            {
                render += ' ';
                rx++;
            } while (rx % KILO_TAB_STOP != 0);
            i++;
            continue;
        }

        const int len = Utf8::seqLen(chars, i);
        const unsigned cp = Utf8::decode(chars, i);

        if (cp < 32 || cp == 127)
        {
            // 控制字符不能直接写进终端，按 kilo 的做法画成 '?'
            render += '?';
            rx++;
        }
        else
        {
            render.append(chars, i, len);
            rx += Utf8::isWide(cp) ? 2 : 1;
        }
        i += len;
    }

    // 三个数组都多放一个「终点」哨兵，好让 cellOf* / cellText 不必特判末尾
    cellCharByte_.push_back(n);
    cellRx_.push_back(rx);
    cellByte_.push_back(static_cast<int>(render.size()));

    cellCount_ = static_cast<int>(cellRx_.size()) - 1;
    width_ = rx;
    return rx;
}

void Row::insertChar(int at, char c)
{
    chars.insert(at, 1, c);
    update();
}

void Row::deleteChar(int at, int len)
{
    if (at >= 0 && at < static_cast<int>(chars.size()) && len > 0)
    {
        len = std::min(len, static_cast<int>(chars.size()) - at);
        chars.erase(at, len);
        update();
    }
}

/*
 * 三个「查表」函数都用同一个套路：在起点数组里找最后一个 <= 目标值的下标。
 * 起点数组长度是 cellCount_ + 1，所以 upper_bound 后再减一就是所求 cell；
 * 传进来的值超过整行宽度时统一收敛到最后一个 cell，不多不少地避免出现 -1。
 */
int Row::xToRx(int x) const
{
    if (cellCount_ == 0)
        return 0;
    if (x < 0)
        x = 0;
    // 光标停在行尾（x == chars.size()）时没有对应的 cell，必须落在整行宽度上，
    // 否则末列是中文的话光标会往回缩两列。
    if (x >= static_cast<int>(chars.size()))
        return width_;

    const int cell = cellOfCharByte(x);
    return cellRx_[cell];
}

int Row::rxToByte(int rx) const
{
    if (cellCount_ == 0)
        return 0;
    return cellByte_[cellOfRx(rx)];
}

int Row::cellOfRx(int rx) const
{
    if (cellCount_ == 0)
        return 0;
    if (rx < 0)
        rx = 0;

    const auto it = std::upper_bound(cellRx_.begin(), cellRx_.end(), rx);
    int idx = static_cast<int>(it - cellRx_.begin()) - 1;
    if (idx < 0)
        idx = 0;
    if (idx > cellCount_ - 1)
        idx = cellCount_ - 1;
    return idx;
}

int Row::cellOfCharByte(int b) const
{
    if (cellCount_ == 0)
        return 0;
    if (b < 0)
        b = 0;

    const auto it = std::upper_bound(cellCharByte_.begin(), cellCharByte_.end(), b);
    int idx = static_cast<int>(it - cellCharByte_.begin()) - 1;
    if (idx < 0)
        idx = 0;
    if (idx > cellCount_ - 1)
        idx = cellCount_ - 1;
    return idx;
}

int Row::cellByte(int c) const
{
    if (c < 0)
        return 0;
    if (c > cellCount_)
        return static_cast<int>(render.size());
    return cellByte_[c];
}

int Row::cellRx(int c) const
{
    if (c < 0)
        return 0;
    if (c > cellCount_)
        return width_;
    return cellRx_[c];
}

int Row::cellCharByte(int c) const
{
    if (c < 0)
        return 0;
    if (c > cellCount_)
        return static_cast<int>(chars.size());
    return cellCharByte_[c];
}

std::string Row::cellText(int c) const
{
    if (c < 0 || c >= cellCount_)
        return std::string();
    const int from = cellByte_[c];
    const int to = cellByte_[c + 1];
    return render.substr(from, to - from);
}

int Row::prevBoundary(int x) const
{
    return Utf8::prevBoundary(chars, x);
}

int Row::nextBoundary(int x) const
{
    return Utf8::nextBoundary(chars, x);
}
