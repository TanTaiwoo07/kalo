#pragma once

#include <termios.h>
#include <unistd.h>
#include "Key.h"

// ─── 归属：类骨架手搓，AI 补了 readStdinByte 那套状态区分 ────────────
// 手搓：构造 / 析构 / getkey / cleanup / orig_termios，以及原始代码里那个
//       与 orig_termios 重复的静态 saved_termios（后者已被 AI 合并掉）。
// AI 加：stdin_is_tty_ / rawModeActive()，用来区分「暂时没按键」和
//       「输入流真的结束了」—— 详见 Terminal.cpp 里的说明。
class Terminal
{
public:
    Terminal();
    ~Terminal();

    static Key getkey();
    static const std::string &textInput() { return text_input_; }

    /// 恢复终端原始模式。
    ///
    /// 做成成员函数而不是静态函数，是因为要恢复的那份 termios 只保存在实例里 ——
    /// 早先另外用了一个静态 saved_termios 存同一件事，两处状态一旦不同步，
    /// 退出时恢复的就是错的那个。
    void cleanup();

    // 标准输入是否为交互式终端。裸模式能否生效、以及 read 返回 0 该当作「还没按键」
    // 还是「输入流结束」，都取决于它。
    static inline bool stdin_is_tty_ = false;

    // 裸模式是否真的生效了（标准输入不是终端时为 false）
    bool rawModeActive() const { return stdin_is_tty_; }

private:
    static inline std::string text_input_;
    unsigned int saved_input_cp_ = 0;
    unsigned int saved_output_cp_ = 0;
    termios orig_termios;

    void disableRawMode();
    void enableRawMode();
};
