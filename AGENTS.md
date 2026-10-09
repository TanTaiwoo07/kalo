# AGENTS.md

给在此仓库里工作的 AI 助手（以及未来的自己）看的工程约定。动手前请先读完。

## 这个项目是什么

`kalo` —— 用 C++17 重写的终端文本编辑器，蓝本是 `kilo.c`（根目录里那份 C 语言原版，
**只作参考，不要修改、也不要编进构建**）。

工作目录内有**两份副本**：`WorkBuddy/2026-10-05-15-28-16/kalo/` 与
`Desktop/kalo/`。改之前先确认改的是哪一份，改完记得同步另一份，否则会分叉。

## 改完必须跑的验证

**改了任何 `.cpp` / `.h` 之后都要重新构建并跑测试。** 三条工具链的现状：

| 工具链 | 构建方式 | 状态 |
| --- | --- | --- |
| MSVC 19.51（VS 18 Community） | VS 打开文件夹直接 F5，或 `cmake -S . -B out/build/x64-Debug -G Ninja` | 零警告，测试全过 |
| MinGW-w64 g++ 16.2.0 | 见 README 的单命令，或 `cmake -G "MinGW Makefiles"` | 零警告，测试全过 |
| Linux / WSL / macOS | `cmake -S . -B build` | 代码路径未改但**未实测** |

当前基线（**不许退回去**）：

- 单元测试 **18839 项断言全过**
- 端到端冒烟测试 **16 项全过**
- MinGW 的 `-Wall -Wextra -Wpedantic` 与 MSVC 的 `/W4` 均**零警告**

```bash
# 单元测试
g++ -std=c++17 -Wall -Wextra -Wpedantic -I src \
    test/test_pt.cpp src/PieceTable.cpp src/Buffer.cpp src/Row.cpp -o build/kalo_tests && ./build/kalo_tests

# 端到端冒烟（需要先构建出编辑器本体）
bash test/smoke_editor.sh build/kalo
```

## 架构约定（重要）

```
Buffer ──┬─> PieceTable   权威文本存储 + 撤销历史
         └─> Row[]        派生出来的行视图缓存（制表符展开、列宽、高亮标记）
Syntax ──> Row            按「字符单元」打高亮标记，可跨行传播块注释状态
Utf8   ──> Row / Editor   序列切分、东亚宽度、ASCII 大小写折叠
```

1. **`PieceTable` 是唯一的权威文本存储，`Buffer::rows` 只是派生缓存。**
   写代码时不要反过来「以 rows 为准再回填 PieceTable」，否则撤销和保存会不一致。
2. **不变式**：`rows` 用 `\n` 连接后，要么等于权威文本，要么等于权威文本去掉末尾那一个
   `\n`。任何改动 Buffer 的编辑操作都必须维持它；`test_pt.cpp` 里
   `testBufferViewSync` 会随机校验这一点。
3. **`Buffer::toString()` 原样返回权威文本**（字节级往返，不补末尾换行）；
   `Buffer::fromLines()` 保留旧语义（每行为一行并各补 `\n`）。两个入口的差别是有意的，
   `testBufferLoad` 覆盖了。
4. **撤销依赖两个缓冲区「只增不减」。** `PieceTable` 的撤销之所以能靠记录
   `(source, start, len)` 还原，就是因为 `original_` / `add_` 从不被改写或回收。
   `reset()` 里清空它们是安全的（历史同时被清掉），但**别在编辑过程中对它们做
   `shrink` / `erase`**。
5. **编辑操作走绝对偏移。** `Buffer` 负责 `(行, 列) ↔ 绝对偏移` 的换算，然后把操作
   交给 `PieceTable`。不要再往 Buffer 里塞基于行的文本修改逻辑。
6. **`Cursor` 和 `Editor` 仍然按 `(行, 列)` 工作**，为此 `Buffer` 才保留了行视图缓存。
   如果哪天行视图真的成了性能瓶颈，再考虑换掉它，但那是独立的一次重构。
7. **搜索（`Ctrl-F`）的匹配源必须是权威文本**：`Buffer::findText` 内部走
   `PieceTable::query`，`rows` 只用于把命中偏移反查成 `(行, 列)`。别拿 `rows` 当文本来源。
   `Ctrl-F` 那个模态循环里改用「进入时取一次快照」：搜索期间文档不会被改动，
   所以每次按键都在快照上查，不必重复向 PieceTable 取全文，大小写折叠也只做一次。
8. **三套下标必须分清**：`chars` 字节下标（编辑器坐标 `cur.x`）、`render` 字节下标
   （写进终端的字节）、显示列（中文占 2 列）。三者靠 `Row` 的「字符单元」（cell）对齐，
   `cellByte_` / `cellRx_` / `cellCharByte_` 就是换算表，`Row::hl` 也按 cell 下标。
   **新增绘制 / 光标 / 高亮代码一律走 cell，不要直接拿字节下标当列号用** ——
   这正是中文排版曾经错位的原因。
9. **涉及字符步进的操作要按字符而不是按字节**：左右方向键走
   `Row::prevBoundary/nextBoundary`，退格走 `Utf8::prevBoundary` 算整个字符长度。
   逐字节挪会停在半个 UTF-8 序列上，之后插入删除都会把字符切坏。

### 不要踩的坑（都是真踩过的）

- **切片段时必须给两半各抽一个新的随机优先级**（这条反直觉，但代价极大，别改回去）。
  `PieceTable::split` 在切点落在片段内部时会把它劈成两半，两半都要调 `nextPriority()`，
  **绝不能沿用 `t->prio`**。
  原因：装载大文件时整篇文本只有一个片段，于是所有节点都是那个节点的后代；若每次劈开都
  继承同一个优先级，删得越多、共享同一优先级的节点就越多，最终整棵树优先级全等 ——
  `merge` 永远走同一分支，Treap 退化成一条链，而 `split` / `merge` / `destroy` 全是递归的，
  链长到一万多就**栈溢出直接崩**。实测：20 万字符文档随机删到约 1.6 万次时崩溃，
  在长文件里连续删除就能触发。
  （早期这里写的结论是反的 —— 以为沿用原优先级「满足堆序」更好，结果正是它埋的雷。）
  代价是堆序可能被轻微破坏：新优先级未必不小于原子树根的优先级。但**堆序只影响平衡性，
  不影响正确性** —— split / merge 始终维护中序序列，文本内容不会因为优先级大小而错。
  **两害相权，随机性远比堆序重要。** 回归用例见 `testPieceTableDeleteStress()`。
- **别信「顺序追加更快」之类的直觉**：基准实测顺序追加 PieceTable 比 `std::string` 慢
  约 30 倍（每个字符一次树操作 + 节点分配）。分块表换来的是**随机位置的插入与删除**。
  同理，真实手感瓶颈在 `Buffer` 那层（行视图每敲一个字符重算一遍，越长越亏），
  不在 PieceTable —— 要优化先量，别猜。
- **相邻插入合并**：`insert` 会尝试把新文本并进左树最右片段。连续键入时正是靠它把片段数
  从「按键次数」压回常数。它与 `history_` **不冲突** —— 每次插入仍然各记一条 `(pos, len)`
  记录，撤销时按位置逐个切掉即可。别为了「简化」把合并和撤销记录绑在一起。
- **Windows 的文本模式**：标准流与新建文件默认会把 `\n` 翻成 `\r\n`，还会把 `0x1A`
  （`Ctrl-Z`）当文件结束符。所以 `Terminal::enableRawMode` 里要把 stdin/stdout 切二进制，
  `Editor::save` 里 `open` 要带 `O_BINARY`。新增任何文件读写时都要注意这一点。
- **Windows 控制台是阻塞读**，没有 POSIX 那个 `VMIN=0 / VTIME=1` 的 100ms 超时。
  因此 `Terminal::getkey` 里解析转义序列必须**只读该序列真正需要的字节**；按 CSI 规范
  读到终止字节（0x40~0x7E）为止，既不能多读（会吞掉下一个按键），也不能少读
  （F5 这种长序列会残留 `~` 被当普通字符插入）。
- **`read` 返回 0 的含义取决于标准输入是不是终端**：交互式终端上是「这 100ms 没按键」
  （正常轮询），非终端上才是「流结束」。`Terminal::stdin_is_tty_` 就是干这个的。
- **`size_t` → `int` 的窄化在 MSVC 上是 `C4267` 警告**（`/W4` 会报）。GCC 宽松得多，
  很容易只在一边发现。别用宏屏蔽，老老实实加 `static_cast<int>(...)`。
- **别用局部变量遮蔽成员**：`Editor::prompt` 原来有个局部 `std::string buf`，把
  `Editor::buf` 这个成员遮住了，MSVC 报 `C4458`。已改名为 `input`。
- **`<cctype>` 系列函数（`iscntrl` / `isprint` …）在本仓库一律禁止直接调用**。
  按键值来自 `Key` 枚举，功能键是 1000 起步（`ArrowDown=1003`，`Eof=2000`），丢给
  `iscntrl` 是未定义行为。MSVC 的调试 CRT（`/MDd`，正是 VS 默认的 Debug 配置）会把它当
  运行时错误挂起 —— 表现为「搜索模式下一按方向键就卡死」，而 Release 和 MinGW 下完全没事。
  这个坑前后炸出过**两次**（第二次是跑了重建之前的旧 exe，弹
  `Debug Assertion Failed: c >= -1 && c <= 255`）。现在的 `Editor.cpp::isPrintableAscii()`
  用纯比较运算（`kc >= 0x20 && kc <= 0x7e`）实现，**不经过任何 `<cctype>` 函数**，
  这一类缺陷在构造上已不可能发生。新代码也请保持这个写法：判断字符类别就写显式区间比较，
  不要 `#include <cctype>`。
- **输入流结束（EOF）在每个模态循环里都要处理**。`processKeyPress` 和 `find` 都处理了，
  但 `prompt` 曾经漏掉 —— 结果是管道驱动时 `getkey` 一直返回 `Eof`，循环空转到永远，
  表现为「进程卡死且吃满一个核」（冒烟脚本里专门钉了一条用例）。
  **新增任何按键循环，第一件事就是写 Eof 分支。**
- **确认窗口尺寸时不要读 stdin**。`Screen::getWindowSize` 在 `ioctl` 失败时的兜底做法
  是向终端发 `\x1b[6n` 并从 stdin 读回来的坐标 —— 非交互场景（管道测试、重定向调试）下
  它会把**真正的用户输入**当成终端响应吃掉，而且读不到 `'R'` 就永远阻塞。现在只在
  `stdin`/`stdout` 都确实是终端时才走这条路，否则用 24×80 默认值。

## 验证时的一个硬要求：两套工具链都要跑

调试 CRT（`/MDd`）与发布 CRT、GCC 与 MSVC 的行为差异是真的会咬人的 —— 上面第 8、9 两条
坑都只有**一边**的构建能暴露出来。MSVC 侧的实测命令在本仓库根目录的 `.build_check/msvc_build.bat`
（走 VS 自带的 CMake + Ninja，`CMAKE_BUILD_TYPE=Debug`），它会把产物放到 `out/build/msvc-check/`。
改完跨平台相关的代码，两边都绿灯才算完。

## 平台分层

- `src/platform/win32/`（`termios.h`、`sys/ioctl.h`）是 MinGW 与 MSVC **共用**的替身。
- `src/platform/msvc/`（`unistd.h`）**只给 MSVC**。MSVC 没有 `<unistd.h>`，MinGW 自带，
  所以必须分开放 —— 塞进 `win32/` 会让 MinGW 构建优先选错版本。
- 两者都**只在对应平台构建时被加进 include 路径**（见 `CMakeLists.txt`）。
- **不要**为了兼容 Windows 就往 `src/` 的 POSIX 源码里堆 `#ifdef`。优先扩展兼容层。
  只有 CRT 文本模式这类无法在头文件里解决的东西（`_setmode`、`O_BINARY`）才写
  `#ifdef _WIN32`，而且要配注释说明原因。
- **改动必须保持所有工具链可用。** 在一个平台上验证过不等于没破坏另一个：
  平台相关代码请在 MSVC 与 MinGW 下都编一遍。两个编译器会各自发现对方漏掉的问题
  （例如 MSVC 没有 `ssize_t`、GCC 对窄化转换更宽松），所以别只验证一个。
- MSVC 需要 `/utf-8`（源文件是 UTF-8 且含中文，否则按系统代码页读会乱码），
  已在 `CMakeLists.txt` 里设好；`/wd4996` 是为了屏蔽 MSVC 对 `read`/`write`/`open`
  这些 POSIX 名字的弃用提醒。
- **写入长度尽量用 `int` 而不是 `size_t`**：`write` 的返回类型和参数类型随平台而变，
  统一转成 `int` 可以避开 `ssize_t` 在 MSVC 上不存在这个坑（`Editor::save` 就是这么写的）。

## 代码风格

- 大括号另起一行（Allman），4 空格缩进，不用制表符，行宽约 100。
- 指针/引用的 `*` `&` 贴着变量名：`PieceNode *t`、`const std::string &text`。
- 仓库根有 `.clang-format`，格式化风格以它为准。
- 注释用中文，解释**为什么**而不是复述代码在做什么。
- 命名沿用现有风格：类名 `PascalCase`，成员变量 `snake_case_`（带尾下划线），
  函数 `camelCase`。

## 测试怎么写

- `test/test_pt.cpp` 是自包含的断言测试（自带 `CHECK` / `CHECK_EQ` 宏），**不引入任何第三方
  测试框架**，也**不依赖终端接口**，这是它能在 Windows 上跑的前提。新增用例请沿用这个约束。
- 涉及数据结构正确性的改动，优先加进随机差分测试（对照 `std::string` 参照模型），
  比手写几个固定用例更能兜住 Treap 这类结构的坑。
- 涉及按键行为的改动，把用例加进 `test/smoke_editor.sh`。加用例时记得用独立文件名，
  不要依赖删除旧文件。新加的纯逻辑层（Utf8 / Row / Syntax）也都写进了 `test_pt.cpp`。
- **`test/bench.cpp` 是基准不是测试，刻意不注册进 ctest**。耗时会随机器漂移，
  拿它当门禁只会制造噪声。要跑就手动跑：`cmake --build build && ./build/kalo_bench`。
- **写基准时两个必踩的坑**：① 单次计时会被首次的大块堆分配污染（本项目里第一次序列化
  20 万字符约 100 微秒，稳定态只有 4 微秒，**差 30 倍**），所以要么跑多次取平均，
  要么在报告里说清是冷启动；② 输出被重定向时 stdout 是块缓冲，某一项一崩，前面算好的
  结果会连着缓冲区一起丢掉，现场只剩一个退出码 —— 基准开头就
  `std::setvbuf(stdout, nullptr, _IONBF, 0)`，别省。
- **`kalo_smoke` 在 CMake 里靠 `find_program` 找 bash**。Windows 上 PATH 里的 `bash.exe`
  很可能是 WSL 启动器（`System32` / `WindowsApps` 下那个，实为指向 `wsl.exe` 的符号链接），
  它会把脚本丢进 Linux 子系统，路径全错。所以 CMake 里先在 Git Bash / MSYS2 的惯用位置找，
  再用 `REALPATH` 排掉 wsl。换机器时如果冒烟测试莫名其妙失败，先看
  `CMakeCache.txt` 里的 `KALO_BASH` 指向哪里。
- 冒烟脚本里按键序列是当作 `printf` 格式串用的，**别写 `%`**。

## 还没做的功能

- **多缓冲区 / 多标签页**、**复制粘贴**：目前只有单个 `Buffer`，做大改动前先想清楚
  `PieceTable` 与 `rows` 的一一对应关系要不要变成「每缓冲区一份」。
- **选择区**（Shift 选一段文本）与基于选择区的复制粘贴。
- **更细的着色规则**：预处理指令、函数名、字符串内的格式串等；
  要加就往 `src/Syntax.cpp` 的 `SyntaxRule` 里补，不要散到 `Editor` 里。
- **搜索目前的已知取舍**（不是 bug，但要做的话心里有数）：跨行匹配在单行绘制模型下
  只反显起点行可见的部分；命中计数要全表扫一遍（已合并成一趟扫描，且只在关键词或
  大小写开关变化时才需要，但文档极大时仍可考虑增量维护）。

> 注：本仓库这份 `kilo.c` 是教程早期快照，实测并不含 `HL_*` / `editorFind`；
> 语法高亮与搜索都是按那份设计的已知行为自行重建的，不要指望照抄。

## 持续集成

`.github/workflows/ci.yml` 在 Linux(GCC) / macOS(Clang) / Windows(MSVC) / Windows(MinGW)
四个环境上跑同一套 `ctest`，并且**把警告当错误**（`-Werror` / `/WX`）—— CI 上的编译器
版本往往比开发机新，正好照出潜伏的写法。

仓库目前**没有远端**，workflow 处于「写好待上线」状态。加个 remote 并 push 之后，
Linux / macOS 这两条从未真机验证过的路径就第一次有答案了。

## 已知的待收拾项

- **Windows 上单独按 `Esc` 的判定依赖一个假设**：`consoleInputPending()` 假定 conhost
  把 `ESC [ A` 这样的序列拆成多条 `KEY_EVENT` 记录（所以读完 ESC 后还能探到后续按键）。
  这条路径只在真实控制台上生效，管道驱动的测试覆盖不到 —— 若哪天发现方向键被吞成 Esc，
  第一个要怀疑的就是它。
- Linux / WSL 那条构建与运行路径仍然没有真机验证过。
