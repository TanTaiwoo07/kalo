#pragma once
#include <windows.h>
#include <string>

// 读取 Unicode 输入记录，避免 ReadConsoleW 丢弃鼠标事件。
struct ConsoleInputState
{
    std::string pending;
    size_t offset = 0;
    wchar_t high = 0;
};

inline ConsoleInputState &consoleInputState()
{
    static ConsoleInputState state;
    return state;
}

inline bool consoleBytesPending()
{
    const auto &s = consoleInputState();
    return s.offset < s.pending.size();
}

inline int readConsoleUtf8Byte(char *out)
{
    auto &s = consoleInputState();
    while (!consoleBytesPending())
    {
        s.pending.clear();
        s.offset = 0;
        INPUT_RECORD event = {};
        DWORD got = 0;
        if (!ReadConsoleInputW(GetStdHandle(STD_INPUT_HANDLE), &event, 1, &got))
            return -1;
        if (!got)
            continue;
        if (event.EventType == MOUSE_EVENT)
        {
            const auto &m = event.Event.MouseEvent;
            if ((m.dwEventFlags == 0 || m.dwEventFlags == DOUBLE_CLICK) &&
                (m.dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED))
            {
                CONSOLE_SCREEN_BUFFER_INFO info = {};
                if (!GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info))
                    continue;
                const int x = m.dwMousePosition.X - info.srWindow.Left + 1;
                const int y = m.dwMousePosition.Y - info.srWindow.Top + 1;
                if (x > 0 && y > 0)
                    s.pending = "\x1b[<0;" + std::to_string(x) + ";" + std::to_string(y) + "M";
            }
            continue;
        }
        if (event.EventType != KEY_EVENT || !event.Event.KeyEvent.bKeyDown)
            continue;
        const auto &key = event.Event.KeyEvent;
        std::string bytes;
        const wchar_t unit = key.uChar.UnicodeChar;
        if (unit != 0)
        {
            wchar_t chars[2] = {unit, 0};
            int count = 1;
            if (s.high)
            {
                if (unit >= 0xDC00 && unit <= 0xDFFF)
                {
                    chars[0] = s.high;
                    chars[1] = unit;
                    count = 2;
                }
                else
                    bytes = "\xEF\xBF\xBD";
                s.high = 0;
            }
            if (unit >= 0xD800 && unit <= 0xDBFF)
            {
                s.high = unit;
                s.pending = bytes;
                continue;
            }
            if (count == 1 && unit >= 0xDC00 && unit <= 0xDFFF)
                chars[0] = 0xFFFD;
            char encoded[8] = {};
            const int size = WideCharToMultiByte(CP_UTF8, 0, chars, count, encoded,
                                                sizeof(encoded), nullptr, nullptr);
            if (size <= 0)
                return -1;
            bytes.append(encoded, size);
        }
        else
        {
            switch (key.wVirtualKeyCode)
            {
            case VK_LEFT: bytes = "\x1b[D"; break;
            case VK_RIGHT: bytes = "\x1b[C"; break;
            case VK_UP: bytes = "\x1b[A"; break;
            case VK_DOWN: bytes = "\x1b[B"; break;
            case VK_HOME: bytes = "\x1b[H"; break;
            case VK_END: bytes = "\x1b[F"; break;
            case VK_PRIOR: bytes = "\x1b[5~"; break;
            case VK_NEXT: bytes = "\x1b[6~"; break;
            case VK_DELETE: bytes = "\x1b[3~"; break;
            default: break;
            }
        }
        for (unsigned repeat = 0; repeat < key.wRepeatCount; repeat++)
            s.pending += bytes;
    }
    *out = s.pending[s.offset++];
    return 1;
}
