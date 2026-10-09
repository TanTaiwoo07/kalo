#include "Utf8.h"

namespace
{
// 是否为 UTF-8 续字节（10xxxxxx）
inline bool isContinuation(unsigned char c)
{
    return (c & 0xC0) == 0x80;
}

// 校验从 s[i] 起、期望长度 expect 的序列是否完整且续字节都合法。
// 不完整就按 1 字节处理，避免一个坏字节把整行的切分带偏。
bool sequenceOk(const std::string &s, int i, int expect)
{
    if (i + expect > static_cast<int>(s.size()))
        return false;
    for (int k = 1; k < expect; k++)
    {
        if (!isContinuation(static_cast<unsigned char>(s[i + k])))
            return false;
    }
    const unsigned char first = static_cast<unsigned char>(s[i]);
    const unsigned char second = static_cast<unsigned char>(s[i + 1]);
    if (first < 0xC2 || first > 0xF4 ||
        (first == 0xE0 && second < 0xA0) || (first == 0xED && second >= 0xA0) ||
        (first == 0xF0 && second < 0x90) || (first == 0xF4 && second >= 0x90))
        return false;
    return true;
}

bool extendsCluster(unsigned cp)
{
    return (cp >= 0x300 && cp <= 0x36F) || (cp >= 0x1AB0 && cp <= 0x1AFF) ||
           (cp >= 0x1DC0 && cp <= 0x1DFF) || (cp >= 0x20D0 && cp <= 0x20FF) ||
           (cp >= 0xFE00 && cp <= 0xFE0F) || (cp >= 0xFE20 && cp <= 0xFE2F) ||
           (cp >= 0x1F3FB && cp <= 0x1F3FF) || (cp >= 0xE0020 && cp <= 0xE007F) ||
           (cp >= 0xE0100 && cp <= 0xE01EF);
}

bool regionalIndicator(unsigned cp)
{
    return cp >= 0x1F1E6 && cp <= 0x1F1FF;
}
} // namespace

int Utf8::seqLen(const std::string &s, int i)
{
    const int n = static_cast<int>(s.size());
    if (i < 0 || i >= n)
        return 1;

    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80)
        return 1;
    if ((c & 0xE0) == 0xC0)
        return sequenceOk(s, i, 2) ? 2 : 1;
    if ((c & 0xF0) == 0xE0)
        return sequenceOk(s, i, 3) ? 3 : 1;
    if ((c & 0xF8) == 0xF0)
        return sequenceOk(s, i, 4) ? 4 : 1;
    return 1; // 续字节或 0xF8 以上的非法起始字节
}

unsigned Utf8::decode(const std::string &s, int i, int *len)
{
    const int n = static_cast<int>(s.size());
    if (i < 0 || i >= n)
    {
        if (len)
            *len = 1;
        return 0;
    }

    const int l = seqLen(s, i);
    if (len)
        *len = l;

    const unsigned char c0 = static_cast<unsigned char>(s[i]);
    if (l == 1)
        return c0;

    unsigned cp = 0;
    switch (l)
    {
    case 2:
        cp = c0 & 0x1Fu;
        break;
    case 3:
        cp = c0 & 0x0Fu;
        break;
    default:
        cp = c0 & 0x07u;
        break;
    }

    for (int k = 1; k < l; k++)
        cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3Fu);

    return cp;
}

/*
 * 东亚宽度判定的简化版。
 *
 * 完整的 wcwidth 要带一整张 Unicode 表，这里只挑中文编辑器里真正会遇到的区段：
 * 中日韩统一表意文字及其扩展、假名、全角符号、谚文，外加常用的 emoji 区段。
 * 表外的字符统统按 1 列 —— 判窄最多是光标偏一点，判宽会让排版整体错位，
 * 所以宁可漏判宽。
 */
bool Utf8::isWide(unsigned cp)
{
    if (cp < 0x1100)
        return false;

    if (cp <= 0x115F) // 谚文字母
        return true;
    if (cp >= 0x2E80 && cp <= 0x303E) // CJK 部首、康熙部首、CJK 符号
        return true;
    if (cp >= 0x3041 && cp <= 0x33FF) // 假名、注音、CJK 兼容
        return true;
    if (cp >= 0x3400 && cp <= 0x4DBF) // CJK 扩展 A
        return true;
    if (cp >= 0x4E00 && cp <= 0x9FFF) // CJK 基本区
        return true;
    if (cp >= 0xA000 && cp <= 0xA4CF) // 彝文
        return true;
    if (cp >= 0xAC00 && cp <= 0xD7A3) // 谚文音节
        return true;
    if (cp >= 0xF900 && cp <= 0xFAFF) // CJK 兼容表意文字
        return true;
    if (cp >= 0xFE10 && cp <= 0xFE19) // 竖排标点
        return true;
    if (cp >= 0xFE30 && cp <= 0xFE6F) // CJK 兼容形式
        return true;
    if (cp >= 0xFF00 && cp <= 0xFF60) // 全角 ASCII 与全角标点
        return true;
    if (cp >= 0xFFE0 && cp <= 0xFFE6) // 全角符号
        return true;
    if (cp >= 0x1F300 && cp <= 0x1FAFF) // emoji（含交通、补充符号）
        return true;
    if (regionalIndicator(cp) || cp == 0x231A || cp == 0x231B || cp == 0x23F0 ||
        cp == 0x23F3 || (cp >= 0x2648 && cp <= 0x2653) || cp == 0x267F ||
        cp == 0x2614 || cp == 0x2615 || cp == 0x26A1 || cp == 0x26BD ||
        cp == 0x26BE || cp == 0x2705 || cp == 0x2728 || cp == 0x274C ||
        cp == 0x274E || cp == 0x2757 || (cp >= 0x2753 && cp <= 0x2755) ||
        cp == 0x2B50 || cp == 0x2B55)
        return true;
    if (cp >= 0x20000 && cp <= 0x3FFFD) // CJK 扩展 B 及以后
        return true;

    return false;
}

int Utf8::charWidth(const std::string &s, int i)
{
    int width = isWide(decode(s, i)) ? 2 : 1;
    const int end = nextBoundary(s, i);
    for (int at = i; at < end; at += seqLen(s, at))
    {
        const unsigned cp = decode(s, at);
        if (cp == 0xFE0F || cp == 0x20E3 || isWide(cp))
            width = 2;
    }
    return width;
}

int Utf8::prevBoundary(const std::string &s, int x)
{
    const int n = static_cast<int>(s.size());
    if (x <= 0)
        return 0;
    if (x > n)
        x = n;

    int previous = 0;
    for (int at = 0; at < x; at = nextBoundary(s, at))
        previous = at;
    return previous;
}

int Utf8::nextBoundary(const std::string &s, int x)
{
    const int n = static_cast<int>(s.size());
    if (x < 0)
        return 0;
    if (x >= n)
        return n;
    int end = x + seqLen(s, x);
    if (regionalIndicator(decode(s, x)) && end < n && regionalIndicator(decode(s, end)))
        end += seqLen(s, end);
    while (end < n)
    {
        const unsigned cp = decode(s, end);
        if (extendsCluster(cp))
            end += seqLen(s, end);
        else if (cp == 0x200D && end + seqLen(s, end) < n)
        {
            end += seqLen(s, end);
            end += seqLen(s, end);
        }
        else
            break;
    }
    return end;
}

std::string Utf8::truncate(const std::string &s, int columns)
{
    int at = 0;
    while (at < static_cast<int>(s.size()))
    {
        const int width = charWidth(s, at);
        if (width > columns)
            break;
        columns -= width;
        at = nextBoundary(s, at);
    }
    return s.substr(0, at);
}

std::string Utf8::foldCase(const std::string &s)
{
    std::string out = s;
    for (char &c : out)
    {
        // 只折叠 ASCII：非 ASCII 字节一律不动，这样字节长度与原文严格相等。
        // 也不用 std::tolower —— 那是 <cctype> 的函数，入参域问题见 AGENTS.md。
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}
