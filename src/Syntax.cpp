#include "Syntax.h"

#include "Row.h"

#include <algorithm>

namespace
{

// ---------------------------------------------------------------- 语言规则表
//
// 每条规则都是纯数据：加一门语言 = 加一条 SyntaxRule，状态机一行都不用改。
// 规则用「多套块规则 + 多套行注释」来描述语法，而不是照着某门语言的具体写法写 if ——
// 于是 Python 的三引号、Go 的原始字符串、Rust 的文档注释都只是多一条数据。

SyntaxRule makeCpp()
{
    SyntaxRule r;
    r.name = "C/C++";
    r.filematch = {".c", ".h", ".cc", ".cpp", ".cxx", ".hh", ".hpp", ".hxx"};
    r.keywords = {"switch", "if", "while", "for", "break", "continue", "return", "else",
                  "struct", "union", "typedef", "static", "enum", "class", "case", "do",
                  "default", "goto", "sizeof", "const", "constexpr", "extern", "register",
                  "volatile", "auto", "inline", "namespace", "using", "template", "typename",
                  "public", "private", "protected", "virtual", "override", "new", "delete",
                  "this", "try", "catch", "throw", "operator", "noexcept", "final",
                  "static_cast", "const_cast", "reinterpret_cast", "dynamic_cast"};
    r.types = {"int", "long", "double", "float", "char", "unsigned", "signed", "void", "short",
               "bool", "size_t", "ssize_t", "wchar_t", "char16_t", "char32_t", "std"};
    r.constants = {"true", "false", "nullptr", "NULL"};
    r.lineComments = {"//"};
    r.blocks = {{"/*", "*/", Hl::MlComment}};
    r.stringDelims = {"\"", "'"};
    return r;
}

SyntaxRule makeJava()
{
    SyntaxRule r;
    r.name = "Java";
    r.filematch = {".java"};
    r.keywords = {"abstract", "assert", "break", "case", "catch", "class", "continue",
                  "default", "do", "else", "enum", "extends", "final", "finally", "for",
                  "goto", "if", "implements", "import", "instanceof", "interface", "native",
                  "new", "package", "private", "protected", "public", "return", "static",
                  "strictfp", "super", "switch", "synchronized", "this", "throw", "throws",
                  "transient", "try", "volatile", "while", "var", "record", "sealed",
                  "yield", "permits"};
    r.types = {"int", "long", "double", "float", "char", "byte", "short", "boolean", "void",
               "String", "Integer", "Double", "Boolean", "Object", "List", "Map", "Set"};
    r.constants = {"true", "false", "null"};
    r.lineComments = {"//"};
    r.blocks = {{"/*", "*/", Hl::MlComment}};
    r.stringDelims = {"\"", "'"};
    return r;
}

SyntaxRule makeJavaScript()
{
    SyntaxRule r;
    r.name = "JavaScript";
    r.filematch = {".js", ".jsx", ".mjs", ".cjs"};
    r.keywords = {"await", "break", "case", "catch", "class", "const", "continue", "debugger",
                  "default", "delete", "do", "else", "export", "extends", "finally", "for",
                  "function", "if", "import", "in", "instanceof", "let", "new", "of",
                  "return", "static", "super", "switch", "this", "throw", "try", "typeof",
                  "var", "void", "while", "with", "yield", "async"};
    r.types = {"Array", "Object", "String", "Number", "Boolean", "Symbol", "BigInt", "Map",
               "Set", "Promise", "JSON", "Math"};
    r.constants = {"true", "false", "null", "undefined", "NaN", "Infinity"};
    r.lineComments = {"//"};
    r.blocks = {{"/*", "*/", Hl::MlComment}, {"`", "`", Hl::String}}; // 模板串可跨行
    r.stringDelims = {"\"", "'"};
    r.escapeInBlock = true;
    return r;
}

SyntaxRule makeTypeScript()
{
    SyntaxRule r = makeJavaScript();
    r.name = "TypeScript";
    r.filematch = {".ts", ".tsx", ".mts", ".cts"};
    r.keywords.insert("interface");
    r.keywords.insert("type");
    r.keywords.insert("enum");
    r.keywords.insert("declare");
    r.keywords.insert("namespace");
    r.keywords.insert("readonly");
    r.keywords.insert("keyof");
    r.keywords.insert("satisfies");
    r.keywords.insert("infer");
    r.types.insert("string");
    r.types.insert("number");
    r.types.insert("boolean");
    r.types.insert("unknown");
    r.types.insert("never");
    r.types.insert("any");
    return r;
}

SyntaxRule makeGo()
{
    SyntaxRule r;
    r.name = "Go";
    r.filematch = {".go"};
    r.keywords = {"break", "case", "chan", "const", "continue", "default", "defer", "else",
                  "fallthrough", "for", "func", "go", "goto", "if", "import", "interface",
                  "map", "package", "range", "return", "select", "struct", "switch", "type",
                  "var"};
    r.types = {"bool", "byte", "complex64", "complex128", "error", "float32", "float64",
               "int", "int8", "int16", "int32", "int64", "rune", "string", "uint", "uint8",
               "uint16", "uint32", "uint64", "uintptr", "any"};
    r.constants = {"true", "false", "nil", "iota"};
    r.lineComments = {"//"};
    r.blocks = {{"/*", "*/", Hl::MlComment}, {"`", "`", Hl::String}}; // 原始字符串
    r.stringDelims = {"\""};
    r.capitalizedIsType = true; // Go 社区约定：首字母大写即导出
    return r;
}

SyntaxRule makeRust()
{
    SyntaxRule r;
    r.name = "Rust";
    r.filematch = {".rs"};
    r.keywords = {"as", "async", "await", "break", "const", "continue", "crate", "dyn",
                  "else", "enum", "extern", "fn", "for", "if", "impl", "in", "let", "loop",
                  "match", "mod", "move", "mut", "pub", "ref", "return", "self", "Self",
                  "static", "struct", "super", "trait", "type", "unsafe", "use", "where",
                  "while", "yield", "macro_rules"};
    r.types = {"i8", "i16", "i32", "i64", "i128", "isize", "u8", "u16", "u32", "u64", "u128",
               "usize", "f32", "f64", "bool", "char", "str", "String", "Vec", "Option",
               "Result", "Box", "Rc", "Arc", "HashMap", "HashSet"};
    r.constants = {"true", "false", "None", "Some", "Ok", "Err"};
    r.lineComments = {"//", "///", "//!"}; // 文档注释按注释色即可，不必再开一类
    r.blocks = {{"/**", "*/", Hl::MlComment}, {"/*", "*/", Hl::MlComment}};
    r.stringDelims = {"\""};
    r.capitalizedIsType = true;
    return r;
}

SyntaxRule makePython()
{
    SyntaxRule r;
    r.name = "Python";
    r.filematch = {".py", ".pyw", ".pyi"};
    r.keywords = {"and", "as", "assert", "async", "await", "break", "class", "continue",
                  "def", "del", "elif", "else", "except", "finally", "for", "from", "global",
                  "if", "import", "in", "is", "lambda", "match", "nonlocal", "not", "or",
                  "pass", "raise", "return", "try", "while", "with", "yield"};
    r.types = {"int", "float", "str", "bytes", "bool", "list", "dict", "set", "tuple",
               "object", "range", "enumerate", "zip", "map", "filter"};
    r.constants = {"True", "False", "None", "self", "cls", "__init__", "__name__", "print",
                   "len", "str", "int", "float", "repr", "isinstance", "super"};
    r.lineComments = {"#"};
    // 三引号是能跨行的字符串：1.0 按普通字符串处理，所以一遇到换行就串色。
    // 把它当块规则之后，词法上与块注释完全同构，状态机不用改。
    r.blocks = {{"\"\"\"", "\"\"\"", Hl::String}, {"'''", "'''", Hl::String}};
    r.stringDelims = {"\"", "'"};
    r.capitalizedIsType = true;
    return r;
}

SyntaxRule makeSql()
{
    SyntaxRule r;
    r.name = "SQL";
    r.filematch = {".sql"};
    r.keywords = {"ADD", "ALL", "ALTER", "AND", "AS", "ASC", "BETWEEN", "BY", "CASE", "CHECK",
                  "COLUMN", "CREATE", "CROSS", "DELETE", "DESC", "DISTINCT", "DROP", "ELSE",
                  "EXISTS", "FOREIGN", "FROM", "FULL", "GROUP", "HAVING", "IN", "INDEX",
                  "INNER", "INSERT", "INTO", "IS", "JOIN", "LEFT", "LIKE", "LIMIT", "NOT",
                  "NULL", "ON", "OR", "ORDER", "OUTER", "PRIMARY", "REFERENCES", "RIGHT",
                  "SELECT", "SET", "TABLE", "THEN", "UNION", "UNIQUE", "UPDATE", "VALUES",
                  "VIEW", "WHEN", "WHERE", "WITH"};
    r.types = {"INT", "INTEGER", "BIGINT", "SMALLINT", "DECIMAL", "NUMERIC", "FLOAT", "REAL",
               "DOUBLE", "CHAR", "VARCHAR", "TEXT", "DATE", "TIME", "TIMESTAMP", "BOOLEAN",
               "BLOB", "SERIAL"};
    r.lineComments = {"--"};
    r.blocks = {{"/*", "*/", Hl::MlComment}};
    r.stringDelims = {"'", "\""};
    r.caseSensitive = false; // SQL 关键字大小写不敏感，规则表里统一存大写
    return r;
}

// 大小写不敏感的语言（SQL）用这一组：规则表可以按语言习惯写成大写，
// 初始化时统一折叠成小写，运行时查询前也转小写 —— 两边必须对上，否则永远命中不了。
char lowerAscii(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

void foldSet(std::unordered_set<std::string> &set)
{
    std::unordered_set<std::string> folded;
    folded.reserve(set.size());
    for (const std::string &w : set)
    {
        std::string low;
        low.reserve(w.size());
        for (char c : w)
            low += lowerAscii(c);
        folded.insert(low);
    }
    set.swap(folded);
}

const std::vector<SyntaxRule> &ruleTable()
{
    // 函数内静态：只初始化一次，之后所有语言共用。
    static const std::vector<SyntaxRule> table = [] {
        std::vector<SyntaxRule> t = {
            makeCpp(), makeJava(),     makeJavaScript(), makeTypeScript(),
            makeGo(),  makeRust(),     makePython(),     makeSql(),
        };
        for (SyntaxRule &r : t)
        {
            if (r.caseSensitive)
                continue;
            foldSet(r.keywords);
            foldSet(r.types);
            foldSet(r.constants);
        }
        return t;
    }();
    return table;
}

// 文件后缀匹配：逐字节做结尾比较并忽略大小写（Windows 上 .C 与 .c 都要认）。
bool matchesExtension(const std::string &filename, const std::string &ext)
{
    if (filename.size() < ext.size())
        return false;
    for (size_t i = 0; i < ext.size(); i++)
    {
        char a = filename[filename.size() - ext.size() + i];
        const char b = ext[i];
        if (a >= 'A' && a <= 'Z')
            a = static_cast<char>(a - 'A' + 'a');
        if (a != b)
            return false;
    }
    return true;
}

bool upperAscii(char c)
{
    return c >= 'A' && c <= 'Z';
}

// 分隔符判定。全部用区间比较，不经过 <cctype>（入参域问题见 AGENTS.md）。
bool isSeparatorChar(char c)
{
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

bool isIdentStart(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool isIdentChar(char c)
{
    return isIdentStart(c) || (c >= '0' && c <= '9');
}

bool isHexDigit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}

} // namespace

const SyntaxRule *Syntax::forFile(const std::string &filename)
{
    if (filename.empty())
        return nullptr;

    for (const SyntaxRule &rule : ruleTable())
    {
        for (const std::string &ext : rule.filematch)
        {
            if (matchesExtension(filename, ext))
                return &rule;
        }
    }
    return nullptr;
}

void Syntax::highlightRow(Row &row, const SyntaxRule *rule, const SyntaxState &prev,
                          SyntaxState &out)
{
    const int cells = row.cellCount();
    row.hl.assign(static_cast<size_t>(cells), Hl::Normal);

    out = SyntaxState();

    if (!rule)
        return;

    const std::string &s = row.chars;

    // 上一行记的是「第几条块规则」。文件换了规则表之后那个下标可能失效，
    // 越界时按「不在块内」处理，至少不会去访问越界的 vector 元素。
    int block = prev.block;
    if (block >= static_cast<int>(rule->blocks.size()))
        block = -1;

    bool inString = false;
    char stringEnd = 0;
    bool prevSep = true;

    auto cellChar = [&](int c) -> char {
        const int b = row.cellCharByte(c);
        return (b >= 0 && b < static_cast<int>(s.size())) ? s[b] : '\0';
    };

    auto matchAt = [&](int ci, const std::string &pat) -> bool {
        if (pat.empty() || ci < 0 || ci >= cells)
            return false;
        const int b = row.cellCharByte(ci);
        if (b < 0 || b + static_cast<int>(pat.size()) > static_cast<int>(s.size()))
            return false;
        return s.compare(static_cast<size_t>(b), pat.size(), pat) == 0;
    };

    auto paint = [&](int from, int n, Hl h) {
        for (int k = 0; k < n && from + k < cells; k++)
            row.hl[static_cast<size_t>(from + k)] = h;
    };

    // 多套规则同时命中时取最长的一条（更具体的胜出）：
    // /// 要盖过 //，""" 要盖过 "，/** 要盖过 /*。
    auto matchLongestString = [&](int ci, const std::vector<std::string> &pats, int &len) -> bool {
        bool any = false;
        len = 0;
        for (const std::string &p : pats)
        {
            if (static_cast<int>(p.size()) > len && matchAt(ci, p))
            {
                len = static_cast<int>(p.size());
                any = true;
            }
        }
        return any;
    };

    auto matchLongestBlock = [&](int ci, int &idx, int &len) -> bool {
        bool any = false;
        idx = -1;
        len = 0;
        for (size_t k = 0; k < rule->blocks.size(); k++)
        {
            const std::string &p = rule->blocks[k].start;
            if (static_cast<int>(p.size()) > len && matchAt(ci, p))
            {
                len = static_cast<int>(p.size());
                idx = static_cast<int>(k);
                any = true;
            }
        }
        return any;
    };

    auto lookupWord = [&](const std::string &word) -> Hl {
        // 大小写不敏感的语言（SQL）：规则表里统一存大写，查之前先折叠
        std::string low;
        const std::string *key = &word;
        if (!rule->caseSensitive)
        {
            low.reserve(word.size());
            for (char c : word)
                low += lowerAscii(c);
            key = &low;
        }
        if (rule->keywords.count(*key))
            return Hl::Keyword1;
        if (rule->types.count(*key))
            return Hl::Keyword2;
        if (rule->constants.count(*key))
            return Hl::Constant;
        if (rule->capitalizedIsType && !word.empty() && upperAscii(word[0]))
            return Hl::Type;
        return Hl::Normal;
    };

    int i = 0;
    while (i < cells)
    {
        // ---- 处于跨行块内：块注释 / 三引号 / 模板串 / 原始字符串 ----
        if (block >= 0)
        {
            const BlockRule &br = rule->blocks[static_cast<size_t>(block)];

            if (rule->escapeInBlock && cellChar(i) == '\\' && i + 1 < cells)
            {
                paint(i, 2, br.type);
                i += 2;
                continue;
            }
            if (!br.end.empty() && matchAt(i, br.end))
            {
                const int step = static_cast<int>(br.end.size());
                paint(i, step, br.type);
                i += step;
                block = -1;
                prevSep = true;
                continue;
            }
            row.hl[static_cast<size_t>(i)] = br.type;
            i++;
            continue;
        }

        const char c = cellChar(i);
        const unsigned char uc = static_cast<unsigned char>(c);

        // 多字节字符不可能是关键字、数字或注释符，直接当分隔符放行
        if (uc >= 0x80)
        {
            prevSep = true;
            i++;
            continue;
        }

        // ---- 单行字符串（处理反斜杠转义）----
        if (inString)
        {
            row.hl[static_cast<size_t>(i)] = Hl::String;
            if (c == '\\' && i + 1 < cells)
            {
                row.hl[static_cast<size_t>(i + 1)] = Hl::String;
                i += 2;
                continue;
            }
            if (c == stringEnd)
            {
                inString = false;
                prevSep = true;
            }
            else
            {
                prevSep = false;
            }
            i++;
            continue;
        }

        // ---- 单行注释（多套，最长匹配优先）----
        int len = 0;
        if (matchLongestString(i, rule->lineComments, len))
        {
            for (int k = i; k < cells; k++)
                row.hl[static_cast<size_t>(k)] = Hl::Comment;
            break; // 到行尾为止
        }

        // ---- 块规则起始（多套，最长匹配优先）----
        int bidx = -1;
        int blen = 0;
        if (matchLongestBlock(i, bidx, blen))
        {
            paint(i, blen, rule->blocks[static_cast<size_t>(bidx)].type);
            i += blen;
            block = bidx;
            prevSep = false;
            continue;
        }

        // ---- 单行字符串起始 ----
        {
            int slen = 0;
            if (matchLongestString(i, rule->stringDelims, slen))
            {
                paint(i, slen, Hl::String);
                stringEnd = c;
                inString = true;
                i += slen;
                continue;
            }
        }

        // ---- 数字：0x / 0b / 0o 前缀、小数、科学计数、下划线分隔 ----
        if (rule->highlightNumbers && isDigit(c) && prevSep)
        {
            int k = i;
            if (c == '0' && k + 1 < cells)
            {
                const char p = cellChar(k + 1);
                const bool hex = (p == 'x' || p == 'X');
                const bool bin = (p == 'b' || p == 'B');
                const bool oct = (p == 'o' || p == 'O');
                if (hex || bin || oct)
                {
                    k += 2;
                    while (k < cells && (hex ? isHexDigit(cellChar(k)) : isDigit(cellChar(k))))
                        k++;
                    paint(i, k - i, Hl::Number);
                    i = k;
                    prevSep = false;
                    continue;
                }
            }
            while (k < cells && (isDigit(cellChar(k)) || cellChar(k) == '_'))
                k++;
            if (k + 1 < cells && cellChar(k) == '.' && isDigit(cellChar(k + 1)))
            {
                k++;
                while (k < cells && (isDigit(cellChar(k)) || cellChar(k) == '_'))
                    k++;
            }
            if (k < cells && (cellChar(k) == 'e' || cellChar(k) == 'E'))
            {
                int e = k + 1;
                if (e < cells && (cellChar(e) == '+' || cellChar(e) == '-'))
                    e++;
                if (e < cells && isDigit(cellChar(e)))
                {
                    k = e;
                    while (k < cells && isDigit(cellChar(k)))
                        k++;
                }
            }
            paint(i, k - i, Hl::Number);
            i = k;
            prevSep = false;
            continue;
        }

        // ---- 标识符：关键字 / 类型 / 常量；后紧跟 ( 视为函数调用 ----
        if (isIdentStart(c))
        {
            int j = i;
            std::string word;
            while (j < cells && isIdentChar(cellChar(j)))
            {
                word += cellChar(j);
                j++;
            }

            Hl hit = lookupWord(word);
            if (hit == Hl::Normal && j < cells && cellChar(j) == '(')
                hit = Hl::Func;

            if (hit != Hl::Normal)
                paint(i, j - i, hit);

            i = j;
            prevSep = false;
            continue;
        }

        prevSep = isSeparatorChar(c);
        i++;
    }

    out.block = block;
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
    case Hl::Type:
        return 94; // 亮蓝
    case Hl::Constant:
        return 96; // 亮青
    case Hl::Func:
        return 34; // 蓝
    case Hl::Normal:
    default:
        return 39; // 终端默认前景色
    }
}