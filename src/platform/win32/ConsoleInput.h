#pragma once

#include <windows.h>
#include <string>

// 控制台输入使用 UTF-16 API，避免 CRT 单字节读取丢失中文和代理对。
// 管道仍由调用方按原始 UTF-8 字节读取。
inline int readConsoleUtf8Byte(char *out)
{
    static std::string pending;
    static size_t offset = 0;
    static wchar_t carried = 0;
    if (offset == pending.size())
    {
        wchar_t chars[2] = {};
        DWORD got = 0;
        const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
        if (carried)
        {
            chars[0] = carried;
            carried = 0;
        }
        else if (!ReadConsoleW(input, chars, 1, &got, nullptr))
            return -1;
        else if (!got)
            return 0;
        int count = 1;
        if (chars[0] >= 0xD800 && chars[0] <= 0xDBFF)
        {
            if (!ReadConsoleW(input, chars + 1, 1, &got, nullptr) || !got)
                return -1;
            if (chars[1] >= 0xDC00 && chars[1] <= 0xDFFF)
                count = 2;
            else
            {
                carried = chars[1];
                chars[0] = 0xFFFD;
            }
        }
        else if (chars[0] >= 0xDC00 && chars[0] <= 0xDFFF)
            chars[0] = 0xFFFD;
        char bytes[8] = {};
        const int size = WideCharToMultiByte(CP_UTF8, 0, chars, count, bytes,
                                            sizeof(bytes), nullptr, nullptr);
        if (size <= 0)
            return -1;
        pending.assign(bytes, size);
        offset = 0;
    }
    *out = pending[offset++];
    return 1;
}
