#pragma once

#include <string>
#include <unordered_set>
#include <vector>

/*
 * 语法高亮。
 *
 * 本仓库那份 kilo.c 是教程早期快照，实测并不含 HL_* 与 editorUpdateSyntax，
 * 所以这里按 kilo 的设计重建：按「字符单元」（cell，即一个 UTF-8 字符）
 * 打类型标记，绘制时再把类型映射成 ANSI 颜色。
 *
 * 之所以按 cell 而不是按字节：一个中文字符 3 字节但只占 2 列，按字节打标记
 * 会和列宽换算打架。高亮与 Row 的 cell 划分共用同一套下标。
 */
enum class Hl : unsigned char
{
    Normal,
    Comment,    // 单行注释
    MlComment,  // 多行注释
    Keyword1,   // 关键字
    Keyword2,   // 类型名等第二组关键字
    String,
    Number,
    Type,       // 类型/类名等第三组（Rust 的 self.Self、Go 的大写标识符约定等）
    Constant,   // 常量与内置值
    Func,       // 函数调用
};

/// 跨行的「块」规则：块注释或跨行字符串。
///
/// 单独抽出来是因为两者在词法上完全同构 —— 都是「起始符 / 结束符 / 内容染色」，
/// 只是颜色不同。把它们放进同一个列表后，Python 的三引号、JS 的模板串、
/// Rust 的文档注释都只是多加一条数据，不用改状态机。
struct BlockRule
{
    std::string start;
    std::string end;
    Hl type = Hl::MlComment;
};

/// 一种语言的着色规则。
///
/// 关键字表用 unordered_set 而不是 vector：规则表扩到几百个词之后，
/// 逐行逐标识符的线性查找会成为明显瓶颈（这是 1.0 实测中最先暴露的问题）。
struct SyntaxRule
{
    std::string name;
    std::vector<std::string> filematch;        // 文件名后缀，如 ".c"

    std::unordered_set<std::string> keywords;  // 第一组关键字（黄）
    std::unordered_set<std::string> types;     // 第二组（绿）
    std::unordered_set<std::string> constants; // 常量与内置值（青蓝）

    std::vector<std::string> lineComments;     // 多套单行注释符：// /// #
    std::vector<BlockRule> blocks;             // 多套块规则：/* /** """
    std::vector<std::string> stringDelims;     // 单行字符串定界符：" ' `

    bool caseSensitive = true;                 // SQL 之类需要置 false
    bool highlightNumbers = true;
    bool escapeInBlock = true;                 // 块内是否处理反斜杠转义
    bool capitalizedIsType = false;            // 首字母大写视为类型（Go / Rust 约定）
};

/// 跨行状态：本行开始时处于哪个块规则的内部（-1 表示不在）。
///
/// 1.0 只有一个 bool（是否在块注释里），能表达「/* */ 跨行」但表达不了
/// 「Python 三引号跨行」「JS 模板串跨行」。改成「是第几条块规则」之后，
/// 任何成对的起止符都能跨行，状态本身也只多了一个 int。
struct SyntaxState
{
    int block = -1;

    SyntaxState() = default;
    explicit SyntaxState(bool inBlock) : block(inBlock ? 0 : -1) {}

    bool active() const { return block >= 0; }
    void reset() { block = -1; }
};

// Row.h 会 include 本文件（为了拿 Hl 枚举与 SyntaxState），所以这里只能前向声明 ——
// 真正需要 Row 完整定义的只有 Syntax.cpp。
class Row;

namespace Syntax
{
/// 按文件名挑规则。没有匹配的语言返回 nullptr，此时整篇不做高亮。
const SyntaxRule *forFile(const std::string &filename);

/// 给一行打高亮标记。
///
/// @param row   目标行；结果写进 row.hl（按 cell 对齐）
/// @param rule  语言规则；为 nullptr 时整行标记为 Normal
/// @param prev  上一行结束时的跨行状态
/// @param out   输出：本行结束时的跨行状态
void highlightRow(Row &row, const SyntaxRule *rule, const SyntaxState &prev, SyntaxState &out);

/// 高亮类型 → ANSI 前景色号（30 系列）。
int colorOf(Hl h);
}