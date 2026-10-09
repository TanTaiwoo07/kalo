#pragma once
#include <unordered_map>
#include <string>

// ─── 归属：本文件除 Eof 一项外，全部原始手搓 ────────────────────────
// AI 只加了 Eof = 2000（见文件末尾标记）。

// 手搓。把字母变成对应的控制码（Ctrl-A → 1，Ctrl-Z → 26）。
// 命名问题：CTRL_KEY 全大写、带下划线，看起来像宏，实际是 constexpr 函数。
// 叫 ctrlKey() 会更不容易误解。
constexpr int CTRL_KEY(char k) { return (k) & 0x1f; }

// 手搓。
//
// 设计上值得肯定的一点：功能键从 1000 起步，与 0~127 的 ASCII 完全分开，
// 于是「是不是普通字符」可以用 `raw < 1000` 一刀切 —— 这个哨兵区间划得很干净。
//
// 原代码问题：
// 1. Enter 只认 '\r'(13)，不认 '\n'(10)。多数终端在裸模式下发 CR，
//    但少部分环境会发 LF，那种情况下回车键会没反应。
// 2. Backspace = 127 是 DEL 键，而有些终端退格发的是 8(Ctrl-H)。
//    Editor 里另外单独判了 CTRL_KEY('h') 来兜住，说明这个映射本身不完整。
enum class Key : int
{
    None = 0,
    Enter = '\r',    // 13
    Backspace = 127, // DEL
    Escape = '\x1b', // 27
    ArrowLeft = 1000,
    ArrowRight,
    ArrowUp,
    ArrowDown,
    Home,
    End,
    PageUp,
    PageDown,
    Delete,

    // [AI] 以下这一项是后加的：
    // 标准输入已结束（被重定向成管道或文件）。取值刻意远离 1000 那段哨兵区间，
    // 免得掉进 processKeyPress 里 `raw < 1000` 那个「当作普通字符插入」的分支。
    Eof = 2000,
    Text = 2001, // 完整的非 ASCII UTF-8 输入，见 Terminal::textInput()
};

// 手搓。转义序列 → 按键的查表。
//
// 原代码问题：17 条固定不变的记录却用了 unordered_map，每次查询要算哈希、
// 还要承担静态初始化期构造全局对象的开销。这里用线性表（或 switch）更合适，
// 但这是原代码的写法，按要求保留不动。
inline const std::unordered_map<std::string, Key>
    escMap = {
        // ESC [ X   (取 "[A" 这类两字符)
        {"[A", Key::ArrowUp},
        {"[B", Key::ArrowDown},
        {"[C", Key::ArrowRight},
        {"[D", Key::ArrowLeft},
        {"[H", Key::Home},
        {"[F", Key::End},

        // ESC [ n ~ (取 "[3~" 这类三字符)
        {"[1~", Key::Home},
        {"[3~", Key::Delete},
        {"[4~", Key::End},
        {"[5~", Key::PageUp},
        {"[6~", Key::PageDown},
        {"[7~", Key::Home},
        {"[8~", Key::End},

        // ESC O X   (取 "OH" 这类两字符)
        {"OH", Key::Home},
        {"OF", Key::End},
};
