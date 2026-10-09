# kalo

一个用 C++17 写的终端文本编辑器，蓝本是经典教学项目 [kilo](https://github.com/antirez/kilo)。
那份 C 语言原版 `kilo.c` 不在仓库里（属第三方代码，见下文说明），本地保留供对照。

作者：TaiWoo_Chen

GitHub：[TanTaiwoo07/kalo](https://github.com/TanTaiwoo07/kalo) ·
[CI 构建与测试](https://github.com/TanTaiwoo07/kalo/actions)

公开仓库从当前 1.0 功能快照开始；早期本地 Git 历史包含已移除的第三方参考文件，
因此未上传。`HANDOVER.md` 保留的是发布前的历史交接记录。

## 现在能做什么

- 打开 / 编辑 / 保存文本文件，带状态栏与消息栏
- 光标移动、滚动、制表符展开
- **撤销**（`Ctrl-Z`），由分块表（Piece Table）支撑
- **语法高亮**：按文件名后缀识别 C/C++ 与 Python，着色关键字、类型名、数字、
  字符串、单行与多行注释；认不出的类型就不着色
- **正确的中文 / 全角排版**：按 Unicode 东亚宽度算列宽（中文 2 列），
  左右方向键与退格都按「字符」而不是按「字节」走
- **搜索**（`Ctrl-F`）：匹配走 `PieceTable` 权威文本，支持增量查找、上/下一个命中与
  回绕、`Tab` 切换是否忽略大小写，状态栏显示「第几个 / 共几个」
- 在 Linux / macOS / WSL 和 Windows（MinGW-w64）上都能构建运行

## 项目的由来与 AI 参与情况

这个仓库是**人主导设计、AI 协助实现**的产物。如实交代每一行代码的来历，分三类。

### 一、本人的原始设计（未改动的部分）

- **整体架构与分层**：`Terminal` / `Screen` / `Editor` / `Cursor` / `Buffer` / `Row` /
  `PieceTable` 这套划分，以及「`PieceTable` 存权威文本，`rows` 只是派生的行视图缓存」
  这条核心不变式。至今未变。
- **数据结构选型**：从一开始就是 **FHQ-Treap 版的 Piece Table** —— `original_` / `add_`
  双缓冲区 + 按字符数切分的 `split` / `merge`。不是朴素的 `vector<Piece>` 线性表。
- **编辑器的主循环、按键分发、绘制与滚动逻辑**：`Editor.cpp` 原始 329 行中有 304 行
  （**92%**）一字未改地保留到现在；`Cursor.h`、`Key.h`、`Screen.h` 是 **100%**。
- 量化地看：原始 977 行里有 **815 行（83%）原封不动地留在当前代码里**。

### 二、AI 协助完成的部分

与 WorkBuddy 结对完成，性质上是「把已经定好的设计补成能跑、能测的东西」：

- **`PieceTable` 的实现**：原始 `split()` 是空函数、`UndoEntry` 里只有 `/* data */` 占位。
  现在的 `split` / `merge` / `insert` / `remove` / `query` / `undo` / `toString` 由 AI 写出。
- **UTF-8 与东亚宽度**（`src/Utf8.*`，217 行）、**语法高亮**（`src/Syntax.*`，341 行）。
- **全部测试**：单元测试 1214 行（原始是 0 字节）、端到端冒烟 162 行、性能基准 226 行。
- **Windows 兼容层**（`src/platform/`，336 行）与 CI 配置。
- **修掉的一批缺陷**，其中几个只有跑起来才会暴露：Treap 在长文件连续删除时退化成链
  导致栈溢出、`prompt()` 遇输入结束时空转吃满 CPU、整篇重算时只有第 0 行着色等。

### 三、第三方代码

`kilo.c`（snaptoken 的 kilo 教程源码）与 `example/` 下两份网上的 FHQ-Treap 参考实现
共 1211 行，**不是本项目的作品**，已停止跟踪、不进仓库（文件仍保留在本地供参考）。
仓库里 42 个受版本控制的文件，其余均为本人与 AI 协作产出。

> 之所以把这件事写清楚：这个项目是用来练思维和 C++ 手感的，而「哪些是自己想清楚的、
> 哪些是别人手里拿来的」恰恰是最需要分清的部分。

## 环境要求

| 平台 | 需要 |
| --- | --- |
| Windows（Visual Studio） | Visual Studio 2022/2026 的「使用 C++ 的桌面开发」工作负载。用自带的 CMake + Ninja 构建，开箱即用 |
| Windows（MinGW-w64） | MSYS2 的 MinGW-w64 工具链（`g++`、`gdb`）。本文档按装在 `D:/msys64/mingw64` 来写 |
| Linux / macOS / WSL | `g++`（或 `clang++`）、`make` |
| 可选 | CMake ≥ 3.16。用 Visual Studio 时不需要单独装，VS 自带 |

## 构建与运行

### Windows（Visual Studio，推荐）

直接用 Visual Studio 打开项目文件夹即可 —— 它有 CMake 的原生支持：菜单
`文件 → 打开 → 文件夹`，选中本目录，等右下角 CMake 配置跑完，然后选 `kalo.exe`
作为启动项按 `F5`。

命令行等价做法（在「Developer PowerShell for VS」或 `x64 Native Tools Command Prompt` 里）：

```powershell
cmake -S . -B out/build/x64-Debug -G Ninja
cmake --build out/build/x64-Debug
.\out\build\x64-Debug\kalo.exe 某文件.txt
```

> 若在 `Developer` 终端之外用 CMake，需要先让 `cl.exe` 可见；直接开 VS 的
> Developer 命令行最省事。本仓库在 MSVC 19.51（VS 18 / 2026 Community）+
> CMake 4.3 + Ninja 下验证过，`/W4` 零警告。

### Windows（MinGW-w64）

```powershell
New-Item -ItemType Directory -Force -Path build | Out-Null
& 'D:/msys64/mingw64/bin/g++.exe' -std=c++17 -Wall -Wextra -Wpedantic -g `
    -I src/platform/win32 -I src `
    main.cpp src/Editor.cpp src/Cursor.cpp src/Terminal.cpp src/Screen.cpp `
    src/Buffer.cpp src/PieceTable.cpp src/Row.cpp src/Utf8.cpp src/Syntax.cpp -o build/kalo.exe

.\build\kalo.exe            # 新建
.\build\kalo.exe 某文件.txt  # 打开
```

### Linux / macOS / WSL

```bash
mkdir -p build
g++ -std=c++17 -Wall -Wextra -Wpedantic -g -Isrc \
    main.cpp src/Editor.cpp src/Cursor.cpp src/Terminal.cpp src/Screen.cpp \
    src/Buffer.cpp src/PieceTable.cpp src/Row.cpp src/Utf8.cpp src/Syntax.cpp -o build/kalo
./build/kalo 某文件.txt
```

### 用 CMake

```bash
cmake -S . -B out/build/x64-Debug -G Ninja   # Windows（MSVC 环境里）
cmake -S . -B build -G "MinGW Makefiles"     # Windows（MinGW，需 mingw32-make 在 PATH 里）
cmake -S . -B build                          # Linux / macOS / WSL
cmake --build <构建目录>
ctest --test-dir <构建目录> --output-on-failure
```

CTest 会跑两个用例：`kalo_core`（逻辑层单元测试）与 `kalo_smoke`（端到端冒烟）。
后者需要 `bash`（Windows 上走 MSYS2 / Git Bash），`timeout` 是可选依赖；没有 bash 时会跳过冒烟测试，
但 `kalo_core` 仍然有效。

## 在 VS Code 里用

`.vscode/` 已经配好，打开目录即可：

- **构建**：`Ctrl+Shift+B`（默认构建任务「构建 kalo」）
- **运行任务**：`Ctrl+Shift+P` → `Tasks: Run Task`，可选：
  - `构建 kalo` / `构建逻辑层测试`
  - `运行逻辑层测试`
  - `端到端冒烟测试`
- **调试**：`F5`，配置里已经有三套 —— Windows 的 `kalo` 与测试，以及 Linux 的 `kalo`

> 调试编辑器时 `externalConsole` 设成了 `true`：这是个 TUI 程序，需要在真正的控制台
> 窗口里才能进入裸模式并解释 ANSI 转义序列，用集成终端会花屏。
>
> CMake Tools 扩展只有在装了 CMake 之后才有用；没装的话上面的任务不依赖它。

## 快捷键

| 按键 | 作用 |
| --- | --- |
| `Ctrl-S` | 保存（没有文件名时会弹出「Save as」提示） |
| `Ctrl-Z` | 撤销 |
| `Ctrl-F` | 搜索（`Enter` 确认、`Esc` 取消、方向键切换命中、`Tab` 切换是否忽略大小写） |
| `Ctrl-Q` | 退出；有未保存修改时需要连按 3 次 |
| 方向键 | 移动光标（左右可跨行，按字符而非字节移动） |
| `Home` / `End` | 行首 / 行尾 |
| `PageUp` / `PageDown` | 翻页 |
| `Delete` | 删除光标处字符 |
| `Enter` / `Backspace` | 换行 / 退格（行首退格会与上一行合并） |

## 测试

分两层，都不依赖 CMake，可以单独编译运行。

### 1. 逻辑层单元测试 —— `test/test_pt.cpp`

```bash
# Windows
& 'D:/msys64/mingw64/bin/g++.exe' -std=c++17 -Wall -Wextra -Wpedantic -I src `
    test/test_pt.cpp src/PieceTable.cpp src/Buffer.cpp src/Row.cpp src/Utf8.cpp src/Syntax.cpp -o build/kalo_tests.exe
.\build\kalo_tests.exe

# Linux / WSL
g++ -std=c++17 -Wall -Wextra -Wpedantic -Isrc \
    test/test_pt.cpp src/PieceTable.cpp src/Buffer.cpp src/Row.cpp src/Utf8.cpp src/Syntax.cpp -o build/kalo_tests
./build/kalo_tests
```

覆盖：PieceTable 的插入 / 删除 / 查询 / 撤销 / 相邻插入合并、**对照 `std::string`
参照模型的随机差分测试**（120 轮 × 150 步）、规模测试（5000 次随机插入 → 逐条撤销回空）、
`new`/`delete` 收支核对，以及 Buffer 的装载往返、行视图一致性、撤销后行视图重建、
搜索的向前 / 反向查找与偏移 ⇄ 坐标互逆；此外还有 UTF-8 序列切分与东亚宽度、
`Row` 三套下标（字节 / 显示列 / 字符单元）的换算、按字符退格，
以及语法高亮标记与多行注释的跨行传播。

因为这一组只用标准库，所以在任何平台上都能跑。

### 2. 端到端冒烟测试 —— `test/smoke_editor.sh`

```bash
bash test/smoke_editor.sh build/kalo.exe    # Windows（需要 bash，用 MSYS2 / Git Bash）
bash test/smoke_editor.sh build/kalo        # Linux / WSL
```

把按键序列直接喂给编辑器本体，让它自己走「按键 → 编辑 → 保存」，再核对落盘内容。
覆盖多行、退格、方向键、Home/End/Delete、撤销、搜索（`Ctrl-F`，含忽略大小写）、
中文输入与按字符退格 / 移动、功能键序列不残留字符，以及「保存提示行遇到输入结束
不会卡死」这条回归用例。

### 3. Esc 的手工验证 —— `test/verify_esc.cmd`

Windows 上「单独按 Esc 立即生效」这条修复**只能靠人在真实控制台前确认**：它只在
`stdin` 是真实终端时才走到（`stdin_is_tty_`），而所有自动化测试都是用管道喂按键，
永远走不到那条分支。双击 `test/verify_esc.cmd` 会开一个真实控制台并按脚本提示操作，
约 30 秒。详见「已知限制」。

## 架构

```
main.cpp ──┬─> Terminal   裸模式、按字节读键、把转义序列翻译成 Key
           ├─> Screen     输出缓冲、窗口尺寸
           └─> Editor     主循环、绘制、存盘、命令分发
                 ├─> Cursor   光标位置与滚动
                 ├─> Syntax    语法高亮标记（按行、可跨行传播注释状态）
                 ├─> Utf8      序列切分、东亚宽度、ASCII 大小写折叠
                 └─> Buffer ──┬─> PieceTable   权威文本存储 + 撤销历史
                              └─> Row[]        行视图缓存（制表符展开、列宽、高亮标记）
```

`Buffer` 是这里唯一需要理解的设计：

- **`PieceTable` 是权威文本存储**，撤销历史也在它手里。文本由「片段」挂在
  FHQ-Treap 上，插入 / 删除 / 查询是 O(log K)，K 是片段数而不是文档长度。
- **`rows`（`std::vector<Row>`）只是派生的行视图缓存**，每次编辑时增量同步。
  留着它是为了 `Editor` / `Cursor` 能继续用 `(行, 列)` 这套接口，`Row` 则继续
  负责制表符展开这类渲染加工。
- **三套下标要分清**（中文不出错的关键）：`chars` 的字节下标（编辑器坐标 `cur.x`）、
  `render` 的字节下标（真正写进终端的字节）、显示列（一个字符占几列）。三者靠
  「字符单元」对齐 —— 一个单元 = 一个 UTF-8 字符或一个制表符，`Row` 里的
  `cellByte_` / `cellRx_` / `cellCharByte_` 就是这三者的换算表，高亮数组也按单元下标。
- **不变式**：把 `rows` 用 `\n` 连接起来，要么正好等于权威文本，要么等于权威文本
  去掉末尾那一个 `\n`。换句话说 `rows` 恰好是文本按 `\n` 切分、再丢掉末尾那个空段
  的结果。这条不变式在随机编辑测试里被逐步校验。

## Windows 兼容层

`src/platform/` 下放的是编辑器用到的那些 POSIX 头的替身，分两个目录：

- **`win32/`** —— MinGW 与 MSVC 共用。`termios.h` 与 `sys/ioctl.h` 用控制台 API 模拟出
  编辑器真正需要的两件事：关闭回显与行缓冲、以及让方向键等按键编码成 `\x1b[` 开头的
  转义序列（打开 `ENABLE_VIRTUAL_TERMINAL_INPUT`），好让 `Key.h` 里那张映射表原封不动
  继续用。
- **`msvc/`** —— 只给 MSVC 补 `<unistd.h>`（MSVC 完全没有这个头文件，MinGW 自带）。
  它必须单独放一个目录：`win32/` 是两边共用的，把 `unistd.h` 放进去会让 MinGW 构建
  优先用错版本。

两个目录都**只在对应平台构建时被加进 include 路径**，所以 **`src/` 下的 POSIX 源码
一行都不用改，WSL / Linux 的构建路径完全没有被动过**。

另外 `Terminal` 与 `Editor` 里有少量 `#ifdef _WIN32`，处理的是 Windows CRT 的文本模式
默认行为：标准流和新建文件默认会把 `\n` 翻成 `\r\n`，还会把 `0x1A`（`Ctrl-Z`）当成
文件结束符，所以都要显式切到二进制模式。

## 各平台验证状态

| 工具链 | 状态 |
| --- | --- |
| MinGW-w64 g++ 16.2.0 | 编译零警告；单元测试 18964 项断言全过；端到端冒烟 22 项全过 |
| MSVC 19.51（VS 18 Community） | 编译零警告（`/W4`）；18964 项断言 + 22 项冒烟全过；`ctest` 两条用例全过 |
| Linux（GitHub Actions, ubuntu-latest） | 见下节说明 |
| macOS / WSL | **尚未实测**。POSIX 代码路径未改，理论上可用，但没有真机验证过 |

### 持续集成

`.github/workflows/ci.yml` 在四个环境上跑同一套测试：Linux(GCC)、macOS(Clang)、
Windows(MSVC)、Windows(MinGW via MSYS2)，并且**把警告当错误**（`-Werror` / `/WX`）——
换到 CI 上编译器版本往往比开发机新，正好照出潜伏的写法。

获取公开仓库：

```bash
git clone https://github.com/TanTaiwoo07/kalo.git
cd kalo
```

每次向 main 推送或创建拉取请求都会触发 CI；各平台的最新结果以上方 Actions 链接为准。

## 性能基准

`test/bench.cpp` 拿 PieceTable 和 `std::string` 跑同一组操作、同一串随机位置。
它不是测试、不注册进 ctest（耗时会随机器漂移，当门禁只会制造噪声），手动跑：

```bash
cmake --build build && ./build/kalo_bench
```

MinGW-w64 g++ 16.2.0、`-O2`、文档 203333 字符、随机操作 20000 次的实测：

| 操作 | PieceTable | std::string | 比值 |
| --- | --- | --- | --- |
| 顺序追加 20 万字符 | 6.0 ms | 0.2 ms | 0.03x |
| 随机位置插入 2 万次 | 8.2 ms | 25.0 ms | **3.06x** |
| 随机位置删除 2 万次 | 10.2 ms | 22.5 ms | **2.20x** |
| 全量序列化（单次） | 0.004 ms | — | — |
| 搜索 1000 次 | 3.2 ms | — | — |
| 撤销 2 万次插入 | 6.7 ms | — | — |
| `Buffer` 层逐字符输入 1 万次 | 190.6 ms | — | — |

比值 > 1 表示 PieceTable 更快。几点如实说明：

- **顺序追加这一项 PieceTable 输了，而且输得不少**（0.03x）。每敲一个字符都要走一次
  树的插入与节点分配，`std::string::push_back` 是摊还 O(1)。分块表换来的优势在
  **随机位置的插入与删除**，不在顺序写。
- 序列化 20 万字符只要 4 微秒，所以「每次搜索都重新拼一遍全文」并没有想象中贵。
  数字要注意的是：**单次计时会被首次的大块堆分配污染**（第一次约 100 微秒，稳定态
  4 微秒，差 30 倍），基准里这一项因此跑 100 次取平均。
- 最后一行才是真实瓶颈所在：同样的 1 万次输入，走 `Buffer`（也就是带着行视图一起
  更新）比直接操作 PieceTable **慢两个数量级**。原因是每敲一个字符都要把当前行的
  渲染信息重算一遍，行越长越吃亏。真要优化编辑器手感，该动的是这里，不是存储层。

## 与 kilo 相比还缺什么

- 跨文件的复制粘贴、多缓冲区
- 选择区（按住 Shift 选一段文本）
- 更细的着色规则：字符串内的转义、预处理指令、函数名等

## 已知限制

- 画面上不做行号、不做软换行。
- 垂直方向不做 Unicode 组合字符（如带声调的字母）与从右到左文字的处理。
- 需要 CMake 才能用 CMake Tools 扩展；不装也能用 `.vscode/tasks.json` 里的任务。
- 冒烟测试依赖 `bash`。Windows 上若 PATH 里的 `bash.exe` 是 WSL 的启动器
  （`System32` 或 `WindowsApps` 下的那个），CMake 会自动跳过 `kalo_smoke`，
  只注册 `kalo_core`。
- **Windows 的 Esc 需要手工确认一次**（`test/verify_esc.cmd`）。修复做法是在读到
  `ESC` 后先探 60ms 有没有后续按键事件，没有就判定为单独按了 Esc。这条路径只在
  真实控制台上生效，管道驱动的测试覆盖不到。若发现方向键被吞成 Esc，把
  `src/Terminal.cpp` 里的 `kEscTimeoutMs` 调大（如 100）后重编即可。
