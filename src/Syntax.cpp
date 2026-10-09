#include "Syntax.h"

#include "Row.h"
#include "Utf8.h"

#include <algorithm>

namespace
{
// C / C++ 关键字。分两组是为了让「类型名」和「控制关键字」用不同颜色，
// 与 kilo 的 HL_KEYWORD1 / HL_KEYWORD2 对应。
const SyntaxRule kCpp = {
    "C/C++",
    {".c", ".h", ".cc", ".cpp", ".cxx", ".hpp", ".hh", ".hxx"},
    {"switch", "if", "while", "for", "break", "continue", "return", "else", "struct",
     "union", "typedef", "static", "enum", "class", "case", "do", "default", "goto",
     "sizeof", "const", "constexpr", "extern", "register", "volatile", "auto", "inline",
     "namespace", "using", "template", "typename", "public", "private", "protected",
     "virtual", "override", "new", "delete", "this", "try", "catch", "throw", "operator",
     "static_cast", "const_cast", "reinterpret_cast", "dynamic_cast"},
    {"int", "long", "double", "float", "char", "unsigned", "signed", "void", "short",
     "bool", "size_t", "ssize_t", "wchar_t", "char16_t", "char32_t", "true", "false",
     "nullptr", "std"},
    "//",
    "/*",
    "*/",
    true,
    true,
};

const SyntaxRule kPython = {
    "Python",
    {".py", ".pyw"},
    {"def", "class", "return", "if", "elif", "else", "while", "for", "in", "not", "and",
     "or", "import", "from", "as", "with", "try", "except", "finally", "raise", "lambda",
     "yield", "pass", "break", "continue", "global", "nonlocal", "assert", "del", "is",
     "async", "await", "None", "True", "False", "self"},
    {"int", "str", "float", "bool", "list", "dict", "set", "tuple", "bytes", "object",
     "type", "len", "range", "print"},
    "#",
    "", // Python 没有块注释；三引号字符串这里按普通字符串处理
    "",
    true,
    true,
};

// 文件后缀匹配。kilo 的做法是逐个后缀做「结尾匹配」，这里照抄，
// 只是把大小写差异也一并忽略了（Windows 上 .C 也很常见）。
bool matchesExtension(const std::string &filename, const std::string &ext)
{
    if (filename.size() < ext.size())
        return false;

    for (size_t i = 0; i < ext.size(); i++)
    {
        const char a = filename[filename.size() - ext.size() + i];
        const char b = ext[i];
        const char la = (a >= 'A' && a <= 'Z') ? static_cast<char>(a - 'A' + 'a') : a;
        if (la != b)
            return false;
    }
    return true;
}

inline bool isSeparatorChar(char c)
{
    // 与 kilo 的 is_separator 一致：空白或非字母数字的标点都算分隔符。
    // 注意不要去调 <cctype> 的 isspace / isalnum —— 入参域问题见 AGENTS.md，
    // 这里入参都是 ASCII，直接写区间比较。
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\0')
        return true;
    const unsigned char u = static_cast<unsigned char>(c);
    if (u >= 0x80)
        return true;
    if (c >= 'a' && c <= 'z')
        return false;
    if (c >= 'A' && c <= 'Z')
        return false;
    if (c >= '0' && c <= '9')
        return false;
    return c != '_';
}
} // namespace

const SyntaxRule *Syntax::forFile(const std::string &filename)
{
    if (filename.empty())
        return nullptr;

    for (const SyntaxRule *rule : {&kCpp, &kPython})
    {
        for (const std::string &ext : rule->filematch)
        {
            if (matchesExtension(filename, ext))
                return rule;
        }
    }
    return nullptr;
}

void Syntax::highlightRow(Row &row, const SyntaxRule *rule, bool prevOpenComment,
                          bool &outOpenComment)
{
    const int cells = row.cellCount();
    row.hl.assign(cells, Hl::Normal);

    if (!rule)
    {
        outOpenComment = false;
        return;
    }

    const std::string &s = row.chars;
    bool inComment = prevOpenComment && !rule->multilineStart.empty();
    char inString = 0;
    bool prevSep = true;

    // 取第 c 个 cell 的首字节。cell 一定是 UTF-8 序列的开头，所以首字节足以
    // 判断这个 cell 是不是 ASCII —— 关键字、数字、注释符全是 ASCII，
    // 非 ASCII 的 cell 一律当分隔符放行。
    auto cellChar = [&](int c) -> char {
        const int b = row.cellCharByte(c);
        return (b >= 0 && b < static_cast<int>(s.size())) ? s[b] : '\0';
    };

    // 从 cell i 起是否匹配一段 ASCII 串（注释起始/结束符）。
    auto matchesAt = [&](int i, const std::string &pat) -> bool {
        if (pat.empty())
            return false;
        const int b = row.cellCharByte(i);
        if (b < 0 || b + static_cast<int>(pat.size()) > static_cast<int>(s.size()))
            return false;
        return s.compare(b, pat.size(), pat) == 0;
    };

    int i = 0;
    while (i < cells)
    {
        const char c = cellChar(i);
        const unsigned char uc = static_cast<unsigned char>(c);

        // 多字节字符不可能属于关键字/数字，直接当分隔符放行，
        // 但字符串与注释内部的字符要继续被染色，所以只在非字符串/非注释时跳过。
        if (uc >= 0x80 && !inComment && !inString)
        {
            prevSep = true;
            i++;
            continue;
        }

        if (inComment)
        {
            row.hl[i] = Hl::MlComment;
            if (matchesAt(i, rule->multilineEnd))
            {
                for (int k = 1; k < static_cast<int>(rule->multilineEnd.size()) && i + k < cells; k++)
                    row.hl[i + k] = Hl::MlComment;
                i += static_cast<int>(rule->multilineEnd.size());
                inComment = false;
                prevSep = true;
                continue;
            }
            i++;
            continue;
        }

        if (inString)
        {
            row.hl[i] = Hl::String;
            // 转义序列：反斜杠连同下一个字符一起染色，否则 "\"" 会被误判成字符串结尾
            if (c == '\\' && i + 1 < cells)
            {
                row.hl[i + 1] = Hl::String;
                i += 2;
                continue;
            }
            if (c == inString)
                inString = 0;
            i++;
            // 字符串刚结束的位置视作分隔符，好让紧随其后的数字/关键字正常识别
            prevSep = !inString;
            continue;
        }

        // 单行注释：从起始符到行尾全染成注释色
        if (matchesAt(i, rule->singlelineComment))
        {
            for (int k = i; k < cells; k++)
                row.hl[k] = Hl::Comment;
            break;
        }

        // 多行注释起始
        if (matchesAt(i, rule->multilineStart))
        {
            for (int k = i; k < cells; k++)
                row.hl[k] = Hl::MlComment;
            inComment = true;
            // 起始符本身的每一个字符都要染色（"/*" 是两个 cell）
            i += static_cast<int>(rule->multilineStart.size());
            continue;
        }

        if (rule->highlightStrings && (c == '"' || c == '\''))
        {
            inString = c;
            row.hl[i] = Hl::String;
            i++;
            continue;
        }

        if (rule->highlightNumbers && (c >= '0' && c <= '9') &&
            (prevSep || (i > 0 && row.hl[i - 1] == Hl::Number)))
        {
            row.hl[i] = Hl::Number;
            i++;
            prevSep = false;
            continue;
        }

        // 标识符：只在前面是分隔符时才可能是关键字（"myif" 不该被当成 if）
        if (prevSep && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'))
        {
            int j = i;
            std::string word;
            while (j < cells)
            {
                const char w = cellChar(j);
                const unsigned char uw = static_cast<unsigned char>(w);
                if (uw >= 0x80)
                    break;
                if (!((w >= 'a' && w <= 'z') || (w >= 'A' && w <= 'Z') ||
                      (w >= '0' && w <= '9') || w == '_'))
                    break;
                word += w;
                j++;
            }

            Hl hit = Hl::Normal;
            if (std::find(rule->keywords.begin(), rule->keywords.end(), word) != rule->keywords.end())
                hit = Hl::Keyword1;
            else if (std::find(rule->types.begin(), rule->types.end(), word) != rule->types.end())
                hit = Hl::Keyword2;

            if (hit != Hl::Normal)
            {
                for (int k = i; k < j; k++)
                    row.hl[k] = hit;
            }
            i = j;
            prevSep = false;
            continue;
        }

        prevSep = isSeparatorChar(c);
        i++;
    }

    outOpenComment = inComment;
}

int Syntax::colorOf(Hl h)
{
    switch (h)
    {
    case Hl::Comment:
    case Hl::MlComment:
        return 36; // 青
    case Hl::Keyword1:
        return 33; // 黄
    case Hl::Keyword2:
        return 32; // 绿
    case Hl::String:
        return 35; // 洋红
    case Hl::Number:
        return 31; // 红
    case Hl::Normal:
    default:
        return 39; // 终端默认前景色
    }
}
