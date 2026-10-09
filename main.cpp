// ─── 归属：本文件 95% 原始手搓，AI 只加了中间那个警告提示 ────────────
//
// 原代码问题：
// 1. include 写的是 "src/Editor.h"，依赖「从项目根目录编译」这个前提。
//    CMake 已经把 src/ 加进了 include 路径，写成 "Editor.h" 才对。
//    现在这样能编过，但换个构建方式就会找不到头文件。
// 2. 只认 argv[1]，多余的参数被静默忽略。

#include "src/Editor.h"
#include "src/Terminal.h"
#include "src/Screen.h"

int main(int argc, char *argv[])
{
    Terminal terminal;
    Screen screen;
    Editor editor;

    editor.setStatusMessage("HELP: Ctrl-S = save | Ctrl-Z = undo | Ctrl-Q = quit");
    editor.init(screen);

    if (argc >= 2)
        editor.openFile(argv[1]);

    // 放在 openFile 之后，免得开文件失败的提示把它盖掉
    if (!terminal.rawModeActive())
        editor.setStatusMessage("WARNING: stdin 不是交互式终端，裸模式未生效");

    // 主循环：刷新画面 → 读一个键 → 处理。
    //
    // 原代码问题：这是个没有出口的 while (true)，退出靠 Editor::quit() 里
    // 直接 exit(0) 强行终止。后果是 ——
    //   1. 下面的 return 0 永远不会执行（死代码）；
    //   2. 退出路径绕过了所有析构函数，Terminal 的裸模式恢复只能靠
    //      quit() 里手动调 Terminal::cleanup() 来补；
    //   3. 进程无法返回有意义的退出码。
    // 更稳的做法是让 processKeyPress 返回一个「是否继续」的标志。
    while (true)
    {
        editor.refreshScreen(screen);
        editor.processKeyPress(terminal, screen);
    }
    return 0;
}
