#pragma once

#include <sys/ioctl.h>
#include <termios.h>
#include <string>
#include <vector>

// ─── 归属：本文件 100% 原始手搓，AI 未改动 ──────────────────────────
//
// 输出缓冲与终端尺寸。做法是把一整帧拼进 buffer，最后一次 write 出去，
// 避免逐行 write 造成的闪烁。
//
// 原代码问题（保留原样未改，仅在此标注）：
// 1. 封装不一致：buffer 与 ws 私有，row / col 却是公有。而 row / col 同样
//    是不该被外部随意改的状态（改了不会触发任何重算）。
// 2. appendRow 按值传参（std::string s），每次调用都多一次拷贝；
//    调用方每帧要为每一行调一次。应该传 const std::string &。
// 3. print() 不修改对象却不是 const；write 的返回值也没检查
//    （短写是允许的，不循环补写会丢字符）。
// 4. 析构函数缺省：Screen 自己不持有终端状态，问题不大，但与 Terminal
//    的职责边界不清晰（谁负责恢复终端这件事散落在两处）。
class Screen
{
private:
    std::string buffer;
    winsize ws;

public:
    int row, col;

    Screen();
    void getWindowSize();

    void appendRow(std::string s);
    void clear();
    void print();
};