#pragma once

#include <string>
#include <vector>

#include "PieceTable.h"
#include "Row.h"

// 缓冲区。
//
// text_（PieceTable）是权威文本存储，撤销历史也归它管；rows 是由 text_ 派生、
// 并在每次编辑时增量同步的行视图，用来让 Editor / Cursor 继续沿用原来的
// (行, 列) 接口，Row 则继续负责制表符展开这类渲染相关的加工。
//
// 不变式：把 rows 用 '\n' 连接起来得到的字符串，要么正好等于 text_，要么等于
// text_ 去掉末尾那一个 '\n'。换句话说 rows 恰好是 text_ 按 '\n' 切分、再丢弃
// 末尾那一个空段的结果 —— 这一条是「行视图没有和存储跑偏」的判据。
// ─── 归属：类骨架与公开接口是手搓的，AI 补了下面的文档与后加的接口 ────
// 手搓：rows / dirty / size() / empty() / operator[] / insert / del /
//       insertNewline / clear —— 这套 (行, 列) 接口从头到尾没变过。
// AI 加：类头那段长注释、fromText / toString / charCount / findText /
//        locate / undo 这些后补的接口与说明。
class Buffer
{
public:
    std::vector<Row> rows;
    int dirty = 0;

    size_t size() const { return rows.size(); }
    bool empty() const { return rows.empty(); }
    Row &operator[](int i) { return rows[i]; }
    const Row &operator[](int i) const { return rows[i]; }

    void insert(int y, int x, char c);
    void del(int y, int &x, int &cy);
    void insertNewline(int y, int x, int &cx, int &cy);
    void clear();

    std::string toString() const;                          // 规范化文本，保存时用

    /// 权威文本的字符总数（含内部换行符）。
    ///
    /// 用于 Editor 判断搜索回绕边界，以及校验 locate 的入参范围。
    /// 直接转发给 PieceTable，不经过行视图。
    int charCount() const { return text_.totalChars(); }

    /// 在权威文本中查找子串。
    ///
    /// 匹配源是 PieceTable::query(0, totalChars()) 的返回结果，
    /// 与 rows 行视图缓存无关 —— 这是 AGENTS.md「PieceTable 是唯一权威文本存储」
    /// 的直接体现，也保证搜索不受未同步缓存影响。
    ///
    /// @param needle     要查找的子串；为空时直接返回 -1（视为无匹配）。
    /// @param fromOffset 查找起点（绝对偏移）。
    ///                   forward 时表示「起点不小于该值」，超出范围按 0 处理；
    ///                   backward 时表示「匹配起点不大于该值」，超出范围按末尾处理。
    /// @param forward    true 向后查找（用 std::string::find）；
    ///                   false 向前查找（用 std::string::rfind）。
    /// @return 命中子串首字符的绝对偏移；未命中返回 -1。
    int findText(const std::string &needle, int fromOffset, bool forward) const;

    /// 把绝对偏移反查为编辑器坐标 (行, 列)。
    ///
    /// 与 offsetOf 使用完全相同的进位规则（每行宽度 = chars.size() + 1，
    /// 末尾 +1 为行尾换行符），保证 locate(offsetOf(y, x)) == (y, x)。
    ///
    /// @param offset 绝对偏移，取值范围 [0, charCount()]。
    ///               等于 charCount() 时表示「文档末尾之后」，落在末行行尾。
    /// @param y      输出：行号（0 起）。
    /// @param x      输出：列号（0 起）。
    /// @return offset 合法返回 true；小于 0 或大于 charCount() 返回 false，
    ///         此时不修改 y / x。
    bool locate(int offset, int &y, int &x) const;

    void fromText(const std::string &text);                // 原样装载文本
    void fromLines(const std::vector<std::string> &lines); // 旧入口：每行补一个 '\n'

    bool undo();
    bool canUndo() const { return text_.canUndo(); }
    int textLength() const { return text_.totalChars(); }

private:
    PieceTable text_;

    void rebuildView();
    int offsetOf(int y, int x) const;
};
