#pragma once

#include <string>
#include <vector>

#include "Syntax.h"

/*
 * 一行文本，以及它派生出来的「显示形态」。
 *
 * 三个下标体系必须分清，这是中文/全角字符不跑偏的关键：
 *   - chars 的字节下标：编辑器坐标 cur.x 用的就是这个（与 PieceTable 的偏移规则一致）；
 *   - render 的字节下标：真正要写进终端的字节流；
 *   - 显示列（rx）：一个字符在屏幕上占几列 —— 制表符展开、中文占 2 列都体现在这里。
 *
 * 三者通过「字符单元」（cell）对齐：一个 cell = 一个 UTF-8 字符（或一个制表符），
 * cellByte_ / cellRx_ / cellCharByte_ 三个数组给出它在这三个体系里的起点，
 * 高亮数组 hl 也按 cell 下标，于是高亮、列宽、搜索反显共用同一套下标。
 */
class Row
{
public:
    std::string chars;   // 原始字节（权威文本在这一行上的切片）
    std::string render;  // 显示字节：制表符展开成空格，不可打印字符换成 '?'

    std::vector<Hl> hl;        // 每个 cell 的高亮类型；长度不足时按 Normal 处理
    bool hlOpenComment = false; // 本行结束时是否仍处于多行注释中

    int update();

    void insertChar(int at, char c);
    void deleteChar(int at, int len = 1);

    /// 编辑器列（chars 字节下标）→ 显示列。x 落在多字节字符中间时按该字符起点算。
    int xToRx(int x) const;

    /// 显示列 → render 字节下标（用于按列切出要显示的那一段）。
    int rxToByte(int rx) const;

    /// 显示列 → cell 下标。
    int cellOfRx(int rx) const;

    /// chars 字节下标 → cell 下标（搜索命中区间换算用）。
    int cellOfCharByte(int b) const;

    int cellCount() const { return cellCount_; }
    int width() const { return width_; }

    /// 第 c 个 cell 在 render 中的字节起点。传 cellCount() 得到 render.size()。
    int cellByte(int c) const;

    /// 第 c 个 cell 的显示列起点。
    int cellRx(int c) const;

    /// 第 c 个 cell 在 chars 中的字节起点。
    int cellCharByte(int c) const;

    /// 第 c 个 cell 要写进终端的字节（制表符已展开）。
    std::string cellText(int c) const;

    /// 按字符回退 / 前进一个边界（左右方向键、退格都要按字符而不是按字节走）。
    int prevBoundary(int x) const;
    int nextBoundary(int x) const;

private:
    int cellCount_ = 0;
    int width_ = 0;
    std::vector<int> cellByte_;     // size = cellCount_ + 1
    std::vector<int> cellRx_;       // size = cellCount_ + 1
    std::vector<int> cellCharByte_; // size = cellCount_ + 1
};
