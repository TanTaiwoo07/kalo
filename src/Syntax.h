#pragma once

#include <string>
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
};

/// 一种语言的着色规则。
struct SyntaxRule
{
    std::string name;
    std::vector<std::string> filematch;   // 文件名后缀，如 ".c"
    std::vector<std::string> keywords;    // 第一组关键字（黄）
    std::vector<std::string> types;       // 第二组关键字（绿）
    std::string singlelineComment;        // 单行注释起始串，如 "//"；为空表示不支持
    std::string multilineStart;           // 多行注释起始串，如 "/*"
    std::string multilineEnd;             // 多行注释结束串，如 "*/"
    bool highlightStrings = true;
    bool highlightNumbers = true;
};

// Row.h 会 include 本文件（为了拿 Hl 枚举），所以这里只能前向声明 ——
// 真正需要 Row 完整定义的只有 Syntax.cpp。
class Row;

namespace Syntax
{
/// 按文件名挑规则。没有匹配的语言返回 nullptr，此时整篇不做高亮。
const SyntaxRule *forFile(const std::string &filename);

/// 给一行打高亮标记。
///
/// @param row             目标行；结果写进 row.hl（按 cell 对齐）
/// @param rule            语言规则；为 nullptr 时全部标记为 Normal
/// @param prevOpenComment 上一行结束时是否仍处于多行注释中
/// @param outOpenComment  输出：本行结束时是否仍处于多行注释中
void highlightRow(Row &row, const SyntaxRule *rule, bool prevOpenComment, bool &outOpenComment);

/// 高亮类型 → ANSI 前景色号（30 系列）。
int colorOf(Hl h);
}
