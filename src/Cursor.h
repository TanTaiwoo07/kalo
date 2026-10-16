#pragma once

#include "Key.h"
#include "Screen.h"
#include "Buffer.h"

// ─── 归属：本文件 100% 原始手搓，AI 未改动 ──────────────────────────
//
// 光标位置与滚动偏移。
//
// 原代码问题（保留原样未改，仅在此标注）：
// 1. screenrows 是「屏幕」的属性，却挂在 Cursor 上 —— 职责错位。根源是照搬
//    kilo 的全局 struct E：kilo 里所有状态都在一个结构体里，拆成 C++ 类之后
//    没有重新划分归属，只是把字段分了家。
// 2. 数据成员全部 public，外部可以不经 scroll() 直接改 x / y，
//    一致性全靠调用方自觉（Editor::clampCursor 就是在补这个窟窿）。
// 3. rx 的语义（渲染列，即制表符与全角展开后的列号）从类型上看不出来，
//    与 x（字节下标）混在一起容易用错。
struct Cursor
{
    int x = 0, y = 0, rx = 0;   // x=字节列, y=行, rx=渲染列
    int coloff = 0, rowoff = 0; // 水平 / 垂直滚动偏移
    int screenrows = 0;         // 可视文本区高度

    void init(Screen &screen);
    void move(Key key, Buffer &buf);
    void scroll(Screen &screen, Buffer &buf);
    void click(int column, int row, int columns, const Buffer &buf);
};
