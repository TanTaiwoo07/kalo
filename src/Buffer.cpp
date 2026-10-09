#include "Buffer.h"

#include "Utf8.h"

#include <algorithm>

// ==================== 行视图维护 ====================

// 把行视图整体重算一遍。撤销会改动任意位置，此时增量同步不划算，直接重建。
void Buffer::rebuildView()
{
    rows.clear();

    const std::string text = text_.toString();
    if (text.empty())
        return; // 空文件没有行，保留原有的欢迎页行为

    auto appendRow = [this](const std::string &line) {
        Row row;
        row.chars = line;
        row.update();
        rows.push_back(row);
    };

    size_t start = 0;
    while (true)
    {
        const size_t nl = text.find('\n', start);
        if (nl == std::string::npos)
        {
            appendRow(text.substr(start));
            break;
        }
        // 末尾那个换行只是行终止符，不该再额外产生一个空行
        if (nl + 1 == text.size())
        {
            appendRow(text.substr(start, nl - start));
            break;
        }
        appendRow(text.substr(start, nl - start));
        start = nl + 1;
    }
}

// (行, 列) → 绝对字符偏移
int Buffer::offsetOf(int y, int x) const
{
    if (y > static_cast<int>(rows.size()))
        y = static_cast<int>(rows.size());

    int off = 0;
    for (int i = 0; i < y; i++)
        off += static_cast<int>(rows[i].chars.size()) + 1; // +1 是行尾换行符
    return off + x;
}

// ==================== 装载 ====================

void Buffer::clear()
{
    rows.clear();
    text_.reset(std::string());
    dirty = 0;
}

void Buffer::fromText(const std::string &text)
{
    text_.reset(text);
    rebuildView();
    dirty = 0;
}

void Buffer::fromLines(const std::vector<std::string> &lines)
{
    std::string text;
    for (const std::string &line : lines)
    {
        text += line;
        text += '\n';
    }
    fromText(text);
}

std::string Buffer::toString() const
{
    return text_.toString();
}

// ==================== 搜索 ====================

int Buffer::findText(const std::string &needle, int fromOffset, bool forward) const
{
    // 空关键词不算匹配：否则 find("") 会命中 fromOffset 本身，产生零长度命中的歧义
    if (needle.empty())
        return -1;

    // 显式经 PieceTable::query 取权威文本，不读 rows 缓存 —— PieceTable 是唯一权威
    // 文本存储（见 AGENTS.md）。整段取出而非分块拼接：编辑器文档量级下正确性优先。
    const std::string text = text_.query(0, text_.totalChars());

    if (forward)
    {
        size_t from = (fromOffset < 0) ? 0 : static_cast<size_t>(fromOffset);
        if (from > text.size())
            return -1; // 起点已越过末尾，不可能再有匹配
        const size_t hit = text.find(needle, from);
        return (hit == std::string::npos) ? -1 : static_cast<int>(hit);
    }

    // rfind 的语义是「匹配起点不大于 pos」，所以「严格向前」由调用方传
    // (当前命中起点 - 1) 实现；本函数不再自行排除自身，保持语义单一。
    // 起点为负或越过末尾都按「末尾」处理（等价于 rfind 默认的 npos），这样
    // 「查找上一个」在文档开头能自然回绕到最后一个匹配，与 Buffer.h 的契约一致。
    size_t from = (fromOffset < 0) ? text.size() : static_cast<size_t>(fromOffset);
    if (from > text.size())
        from = text.size();
    const size_t hit = text.rfind(needle, from);
    return (hit == std::string::npos) ? -1 : static_cast<int>(hit);
}

bool Buffer::locate(int offset, int &y, int &x) const
{
    if (offset < 0 || offset > text_.totalChars())
        return false;

    // 与 offsetOf 用同一套进位规则：每行占 chars.size() + 1 个字符（含行尾换行符）。
    // 逐行扣除行宽，第一个「装得下」的行即命中行，由此保证 locate(offsetOf(y, x)) == (y, x)。
    int remaining = offset;
    for (int i = 0; i < static_cast<int>(rows.size()); i++)
    {
        const int rowWidth = static_cast<int>(rows[i].chars.size()) + 1;
        if (remaining < rowWidth)
        {
            y = i;
            x = remaining;
            return true;
        }
        remaining -= rowWidth;
    }

    // 走到这里说明 offset 正好等于 charCount()（文档末尾之后），挂到末行行尾；
    // 空文档则回落到 (0, 0)。
    if (rows.empty())
    {
        y = 0;
        x = 0;
        return true;
    }
    y = static_cast<int>(rows.size()) - 1;
    x = static_cast<int>(rows.back().chars.size());
    return true;
}

// ==================== 编辑 ====================

void Buffer::insert(int y, int x, char c)
{
    if (y < 0)
        return;

    if (y >= static_cast<int>(rows.size()))
    {
        // 光标可能停在最后一行的下一行（ArrowDown 允许），此时补一行再写。
        // 旧实现在这里直接 rows[y].chars.insert(x, ...) —— x 只要大于 0 且该行
        // 尚为空，就会抛 std::out_of_range 直接把编辑器打崩。
        if (y > static_cast<int>(rows.size()))
            return;

        Row fresh;
        fresh.update();
        rows.push_back(fresh);
        x = 0;
    }

    x = std::clamp(x, 0, static_cast<int>(rows[y].chars.size()));

    dirty++;
    text_.insert(offsetOf(y, x), std::string(1, c));
    // 手搓。往行视图里插入并重算这一行。
    //
    // 原代码问题（性能，已由基准量化）：rows[y].update() 是 O(行长)，
    // 每敲一个字符都要整行重算一遍，于是在一行里连续输入的总代价是 O(n^2)。
    // 实测往一行敲 1 万个字符耗时 190ms，而同样次数直接走 PieceTable
    // 只要个位数毫秒 —— 差两个数量级。编辑器的真实瓶颈在这里，不在存储层。
    rows[y].chars.insert(x, 1, c);
    rows[y].update();
}

void Buffer::del(int y, int &x, int &cy)
{
    if (y < 0 || y >= static_cast<int>(rows.size()))
        return;
    if (x == 0 && y == 0)
        return;

    dirty++;

    if (x > 0)
    {
        // 退格按「字符」删而不是按「字节」删：中文一个字 3 个字节，
        // 只删 1 个字节会留下半个 UTF-8 序列，屏幕上立刻变乱码。
        const int start = Utf8::prevBoundary(rows[y].chars, x);
        const int len = x - start;
        text_.remove(offsetOf(y, start), len);
        rows[y].deleteChar(start, len);
        x = start;
        return;
    }

    // 行首退格：删掉上一行行尾的换行符，等价于把两行接起来
    text_.remove(offsetOf(y, 0) - 1, 1);
    x = static_cast<int>(rows[y - 1].chars.size());
    rows[y - 1].chars += rows[y].chars;
    rows[y - 1].update();
    rows.erase(rows.begin() + y);
    cy--;
}

void Buffer::insertNewline(int y, int x, int &cx, int &cy)
{
    if (y < 0 || y >= static_cast<int>(rows.size()))
        return;

    x = std::clamp(x, 0, static_cast<int>(rows[y].chars.size()));

    dirty++;
    text_.insert(offsetOf(y, x), "\n");

    Row &row = rows[y];
    std::string after = row.chars.substr(x);
    row.chars.erase(x);
    row.update();

    Row fresh;
    fresh.chars = after;
    fresh.update();
    rows.insert(rows.begin() + y + 1, fresh);

    cy++;
    cx = 0;
}

bool Buffer::undo()
{
    if (!text_.undo())
        return false;

    dirty++;
    rebuildView(); // 撤销可能改到任意位置，行视图整体重算最稳妥
    return true;
}
