#include "Terminal.h"
#include <windows.h>
#include <string>

// 在独立、隐藏的真实控制台中喂 Unicode 键盘记录，不依赖用户的终端或管道。
int main(int argc, char **)
{
    if (argc == 1)
    {
        wchar_t executable[MAX_PATH] = {};
        if (!GetModuleFileNameW(nullptr, executable, MAX_PATH))
            return 1;
        std::wstring command = L"\"" + std::wstring(executable) + L"\" --child";
        STARTUPINFOW startup = {};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION process = {};
        if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                            CREATE_NEW_CONSOLE, nullptr, nullptr, &startup, &process))
            return 2;
        const DWORD waited = WaitForSingleObject(process.hProcess, 15000);
        DWORD code = 3;
        if (waited == WAIT_OBJECT_0)
            GetExitCodeProcess(process.hProcess, &code);
        else
            TerminateProcess(process.hProcess, 3);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return static_cast<int>(code);
    }

    SetConsoleCP(936);
    SetConsoleOutputCP(936);
    DWORD input_mode = 0, output_mode = 0;
    GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &input_mode);
    GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &output_mode);
    {
        Terminal terminal;
        if (!terminal.rawModeActive() || GetConsoleOutputCP() != CP_UTF8)
            return 4;
        const std::wstring sample = L"中\xD83D\xDE00\xD83D\xDE80\r";
        for (wchar_t unit : sample)
        {
            INPUT_RECORD event = {};
            event.EventType = KEY_EVENT;
            event.Event.KeyEvent.bKeyDown = TRUE;
            event.Event.KeyEvent.wRepeatCount = 1;
            event.Event.KeyEvent.uChar.UnicodeChar = unit;
            DWORD written = 0;
            if (!WriteConsoleInputW(GetStdHandle(STD_INPUT_HANDLE), &event, 1, &written) ||
                written != 1)
                return 5;
        }
        for (const std::string expected : {u8"中", u8"😀", u8"🚀"})
        {
            if (Terminal::getkey() != Key::Text || Terminal::textInput() != expected)
                return 6;
        }
        if (Terminal::getkey() != Key::Enter)
            return 7;
    }
    DWORD restored_input = 0, restored_output = 0;
    GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &restored_input);
    GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &restored_output);
    return GetConsoleCP() == 936 && GetConsoleOutputCP() == 936 &&
           restored_input == input_mode && restored_output == output_mode ? 0 : 8;
}
