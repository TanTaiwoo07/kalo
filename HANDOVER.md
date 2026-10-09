# kalo 交付报告

给更高阶的分析者：这份文档说明**项目是什么**、**AI 改了什么**、**还留着哪些原代码问题没改以及该怎么改**。
所有数字都来自对原始快照 `kalo_extract/`（未解压的 `kalo.7z` 即此版本）的实测比对，不是估计。

---

## 一、项目简介

**kalo** 是一个用 C++17 写的终端文本编辑器，蓝本是经典教学项目 [kilo](https://github.com/antirez/kilo)（用 C 写的、约 1000 行的教学编辑器）。

作者原始意图：写一个**朴素 Piece Table 的文本编辑器** + 基本架构设计。
（实测纠正：原始代码的数据结构选型其实是 **FHQ-Treap 版 Piece Table**，不是线性 `vector<Piece>`。详见第三节。）

### 架构

```
main.cpp ──┬─> Terminal   裸模式、按字节读键、转义序列 → Key
           ├─> Screen     输出缓冲（整帧拼接后一次 write）、窗口尺寸
           └─> Editor     主循环、绘制、存盘、命令分发
                 ├─> Cursor   光标位置与滚动
                 ├─> Syntax   语法高亮标记（按行，可跨行传播注释状态）
                 ├─> Utf8     序列切分、东亚宽度、ASCII 大小写折叠
                 └─> Buffer ──┬─> PieceTable   权威文本存储 + 撤销历史
                              └─> Row[]        行视图缓存（制表符展开、列宽、高亮）
```

### 核心不变式

- `PieceTable` 是**唯一权威文本存储**，`rows` 只是派生的行视图缓存，每次编辑增量同步。
  判据：把 `rows` 用 `\n` 连接得到的串，要么等于权威文本，要么等于它去掉末尾那个 `\n`。
- 三套下标必须分清（中文不错位的关键）：`chars` 字节下标（编辑器坐标 `cur.x`）、
  `render` 字节下标（写进终端的字节）、**显示列**（中文占 2 列）。
  三者靠「字符单元 cell」对齐，`Row` 里的 `cellByte_` / `cellRx_` / `cellCharByte_` 是换算表。

### 规模

| | 原始 | 现在 |
|---|---|---|
| 源码（17 个原始文件） | 977 行 | 2545 行 |
| 单元测试 `test_pt.cpp` | **0 字节** | 1214 行 |
| 新增模块（Utf8 / Syntax / platform） | 0 | 894 行 |
| 测试脚本 + 基准 + CI | 0 | 548 行 |
| 断言数 | 0 | 18964 |
| 端到端冒烟 | 0 | 22 项 |

---

## 二、统计口径

用 `difflib.SequenceMatcher` 对 `kalo_extract/kalo/*` 与当前代码逐文件做 opcode 级比对，
区分 `equal`（原封不动）/ `insert`（AI 新增）/ `replace`（AI 改写）/ `delete`（AI 删除）。

> 注意：`git` 历史**不能**用来做这个统计 —— 仓库的第一个提交 `fcdd37d` 就已经包含改过的代码
> （该提交里 `PieceTable.cpp` 已 340 行，而原始快照是 23 行）。

---

## 三、归属分析

| 文件 | 原始 | 现在 | 原样保留 | 保留率 |
|---|---|---|---|---|
| `src/Cursor.h` | 16 | 16 | 16 | **100%** |
| `src/Screen.h` | 23 | 23 | 23 | **100%** |
| `src/Editor.h` | 41 | 110 | 40 | 97% |
| `src/Screen.cpp` | 68 | 103 | 66 | 97% |
| `main.cpp` | 23 | 27 | 22 | 95% |
| `src/Cursor.cpp` | 60 | 62 | 57 | 95% |
| `src/Editor.cpp` | 329 | 754 | 304 | **92%** |
| `src/Terminal.h` | 22 | 34 | 20 | 90% |
| `src/Buffer.h` | 24 | 82 | 21 | 87% |
| `CMakeLists.txt` | 22 | 111 | 18 | 81% |
| `src/PieceTable.cpp` | 23 | 348 | 17 | 73% |
| `src/Row.h` | 15 | 71 | 11 | 73% |
| `src/Terminal.cpp` | 82 | 216 | 56 | 68% |
| `src/Row.cpp` | 51 | 187 | 30 | 58% |
| `src/PieceTable.h` | 52 | 106 | 31 | 59% |
| `src/Buffer.cpp` | 80 | 245 | 37 | 46% |
| **合计** | **977** | — | **815** | **83%** |

### 逐项的定性结论

- **作者本人的**：架构分层、`PieceTable` 为权威存储这条不变式、FHQ-Treap 选型（节点结构 +
  `split`/`merge` 接口 + 双缓冲区都是原始代码里就有的）、编辑器主循环 / 按键分发 / 绘制 / 滚动。
- **AI 写的**：`split`/`merge`/`insert`/`remove`/`query`/`undo` 的实现；UTF-8 与语法高亮；
  全部测试；Windows 兼容层；CI；文档。
- **第三方（已移出仓库）**：`kilo.c`(812 行) 与 `example/`(399 行)，共 1211 行。
  用 `git rm --cached` 停止跟踪，文件仍在本地。恢复：`git add -f kilo.c example/`。

### 原始代码是手写的（非 AI 生成）的判据

1. 注释密度 **0.5%**（939 行非空代码仅 5 行注释），而当前代码是 12.7%。
2. 存在 AI 不会犯的疏忽：`tatalChars()`（total 拼错）、头文件里定义全局
   `std::mt19937 rnd(233)` 却没人用、`#include <string>` 写了两遍。
3. 未完成态：`split()` 返回空 `std::pair()`、`UndoEntry { /* data */ }` 占位。

---

## 四、AI 的全部改动

### A. 功能新增

| 改动 | 说明 |
|---|---|
| `PieceTable` 完整实现 | 原始 `split()` 是空函数体、`UndoEntry` 只有占位。补上 split / merge / insert / remove / query / undo / toString，以及相邻插入片段合并（连续键入时把片段数压回常数） |
| `Buffer` 以 PieceTable 为权威 | `insert`/`del`/`insertNewline` 双写权威存储与行视图；新增 `findText` / `locate` / `charCount` / `undo` |
| UTF-8 与东亚宽度 `src/Utf8.*` | 序列切分、显示宽度（中文 2 列）、ASCII 大小写折叠；方向键与退格改为**按字符**步进 |
| 语法高亮 `src/Syntax.*` | 按扩展名识别 C/C++ 与 Python；关键字 / 类型 / 数字 / 字符串 / 单行与多行注释；块注释状态跨行传播 |
| 搜索 `Ctrl-F` | 增量查找、上/下一个命中并回绕、`Tab` 切大小写、状态栏显示第 n/m 个命中；匹配源走权威文本 |
| 撤销 `Ctrl-Z` | 由 PieceTable 的 undo 历史支撑 |
| Windows 兼容层 | `src/platform/win32/{termios.h, sys/ioctl.h}`、`src/platform/msvc/unistd.h` |

### B. 缺陷修复（按严重性排序）

| # | 缺陷 | 根因 | 修法 |
|---|---|---|---|
| 1 | **长文件连续删除时崩溃** | `split` 劈开片段时两半沿用了原节点优先级。装载大文件时整篇只有一个片段，删得越多共享同一优先级的节点越多，最终全树优先级全等 → `merge` 永远走同一分支 → Treap 退化成链 → `split`/`merge`/`destroy` 递归栈溢出。20 万字符文档删到约 1.6 万次时崩 | 两半各取 `nextPriority()`。堆序可能被轻微破坏，但堆序只影响平衡性不影响正确性（split/merge 始终维护中序序列），两害相权取随机性 |
| 2 | **`prompt()` 遇输入结束时空转** | 未处理 EOF，管道驱动时 `getkey` 一直返回 `Eof`，循环空转吃满一个核 | 加 `Eof` 分支按「取消」处理 |
| 3 | **整篇重算只有第 0 行着色** | 把「注释状态没变就停止传播」这条**增量**规则用在了全量重算上 | 整篇重算时不提前终止 |
| 4 | **搜索模式按方向键挂死**（两次） | `iscntrl(1003)` 入参越界属未定义行为，MSVC 调试 CRT 弹 `Debug Assertion: c >= -1 && c <= 255` | 改纯区间比较 `kc >= 0x20 && kc <= 0x7e`，全仓库不再 include `<cctype>` |
| 5 | **`getWindowSize` 偷读 stdin** | 非交互下 `read` 永远等不到响应 → 阻塞；且会吃掉真正的用户输入 | 仅当 stdin/stdout 都是真实终端时才发询问序列，否则用 24×80 兜底 |
| 6 | **幽灵行越界崩溃** | 光标停在末行之后时 `rows[y].chars.insert(x, ...)` 越界抛 `out_of_range` | `Buffer::insert` 补行；`Editor::clampCursor()` 夹回 |
| 7 | **Treap 退化**（另一处） | 构造函数用未播种的 `rand()`，每次运行优先级序列相同 | 改用真正生效的 `mt19937` |
| 8 | **退出要按多次 Ctrl-Q 却仍静默退出** | `quit_times` 只减不重置 | 每次按键重置 |
| 9 | **局部变量遮蔽成员** | `Editor::prompt` 里局部 `std::string buf` 遮蔽 `Editor::buf` | 改名 `input` |
| 10 | **转义序列吞掉后续按键** | 固定读若干字节，多读的那个把下一次按键吃掉；F5 这类长序列只吃掉前几个字节，剩下 `~` 被当普通字符插入 | 按序列实际需要的长度读 |
| 11 | **Windows 上 Ctrl-Z 传不进来** | CRT 默认文本模式把 `0x1A` 当文件结束符 | 标准流切 `_O_BINARY` |
| 12 | **存盘行尾变 CRLF** | Windows 新建文件描述符默认文本模式 | `open` 加 `O_BINARY`；长度统一用 `int` 传递（MSVC 没有 `ssize_t`） |
| 13 | **Windows 单独按 Esc 要多按一个键** | 控制台 `read` 阻塞，读完 ESC 后一直等后续字节 | 读到 ESC 后先探 60ms 有无后续按键事件（只认 `KEY_EVENT`），无则立即判定为单独按 Esc |

### C. 工程设施

- **测试**：`test/test_pt.cpp` 1214 行（18964 断言，含对照 `std::string` 的随机差分测试、
  `new`/`delete` 收支核对、20 万字符删除压力）；`test/smoke_editor.sh` 162 行（22 项端到端，
  管道喂按键 → 核对落盘内容）；`test/bench.cpp` 226 行（性能基准，**刻意不进 ctest**）。
- **CI**：`.github/workflows/ci.yml`，Linux(GCC) / macOS(Clang) / Windows(MSVC) / Windows(MinGW) 四环境，
  警告当错误。**仓库暂无远端**，workflow 待上线。
- **文档**：`README.md`（含署名与 AI 参与说明）、`AGENTS.md`（给 AI 看的施工约束与踩坑记录）。
- **杂项**：`.gitattributes`（钉死 LF）、`.clang-format`、`LICENSE`(MIT, TaiWoo_Chen)。

---

## 五、标出但**未修改**的原代码问题（含具体修法）

> 这些是作者手写代码里真实存在的问题。按要求**只标注、不改**，保留原写法与文风。
> 下面给出每一项的可执行修法，供后续自己动手时参考。

### 5.1 `src/Cursor.cpp`

**问题 1 · `screenrows = screen.row - 2` 硬编码且无下限保护**
终端高度不足 2 行时变成 0 或负数，`scroll()` 里 `rowoff = y - screenrows + 1` 随即错乱（光标被挤出可视区）。
```cpp
// 修法：常量具名 + 下限保护
static constexpr int kReservedLines = 2;  // 状态栏 + 消息栏
screenrows = std::max(0, screen.row - kReservedLines);
```
同时在 `scroll()` 里对 `screenrows <= 0` 提前返回。

**问题 2 · `move()` 末尾只夹 `x` 不夹 `y`**
`y` 越界（幽灵行）时 `x` 保留上一行旧值，之后 `buf[y]` 越界。目前靠 `Editor::clampCursor()` 兜着，根源在这里。
```cpp
// 修法：先夹 y 再夹 x
if (y > static_cast<int>(buf.size())) y = static_cast<int>(buf.size());
if (y < static_cast<int>(buf.size())) x = std::min(x, static_cast<int>(buf[y].chars.size()));
else                                  x = 0;
```

**问题 3 · `ArrowRight` 用逗号表达式且会让 `y` 越界一行**
`y++, x = 0;` 两条语句挤在一起，读起来像一条；且 `y` 会变成 `buf.size()`（越界一行）。
```cpp
// 修法：展开，并加边界检查
if (y + 1 <= static_cast<int>(buf.size()) && x == static_cast<int>(buf[y].chars.size()))
{
    y += 1;
    x = 0;
}
```

**问题 4 · `(int)buf.size()` 是 C 风格强转**，同文件其他位置用 `static_cast`，两种写法并存。
```cpp
// 修法：统一改成 static_cast<int>(buf.size())
```

### 5.2 `src/Screen.h` / `src/Screen.cpp`

**问题 5 · `appendRow(std::string s)` 按值传参**，每次调用多一次拷贝。
```cpp
// 修法
void appendRow(const std::string &s);
```

**问题 6 · `buffer = buffer + s` 每帧 O(n²)**
每次追加都构造新字符串并整体拷贝；一帧要调几十次。
```cpp
// 修法（最简单，收益最大）
void Screen::appendRow(const std::string &s) { buffer += s; }
// 进一步：构造时 buffer.reserve(估算的帧大小)，避免反复扩容
```

**问题 7 · `print()` 不检查 `write` 短写**，丢字符没有任何提示。
```cpp
// 修法：循环补写
int off = 0;
while (off < static_cast<int>(buffer.size()))
{
    const int n = static_cast<int>(write(STDOUT_FILENO, buffer.data() + off, buffer.size() - off));
    if (n <= 0) break;
    off += n;
}
```

**问题 8 · `getCursorPosition()` 的 `read` 无超时保护**
非交互下会一直阻塞（目前由调用方的 `isatty` 判断绕开，但函数本身仍危险）。
```cpp
// 修法：用 select 带超时读，超时即返回 -1
fd_set rd; FD_ZERO(&rd); FD_SET(STDIN_FILENO, &rd);
struct timeval tv{0, 200000};  // 200ms
if (select(STDIN_FILENO + 1, &rd, nullptr, nullptr, &tv) <= 0) return -1;
```

**问题 9 · 构造函数做 I/O 且无法返回错误**（`Screen::Screen()` 里调 `getWindowSize()`）
```cpp
// 修法：把尺寸获取挪到显式的 Screen::init() / refresh() 里，由调用方处理失败
```

**问题 10 · 封装不一致**：`buffer`/`ws` 私有，`row`/`col` 却是公有，而后者同样不该被外部随意改。
```cpp
// 修法：row / col 改私有，提供 const 访问器 getRows() / getCols()
```

### 5.3 `src/Key.h`

**问题 11 · `Enter` 只认 `\r`(13)，不认 `\n`(10)**
多数终端裸模式下发 CR，少数发 LF，那种情况下回车键没反应。
```cpp
// 修法：在 processKeyPress 的按键映射处同时接受两者
if (key == Key::Enter || static_cast<int>(key) == '\n') { /* 回车 */ }
// 或在枚举里加 EnterLf = '\n' 并同时处理
```

**问题 12 · `Backspace` 只认 127(DEL)，不认 8(Ctrl-H)**
目前靠 `Editor` 里单独判 `CTRL_KEY('h')` 兜住，说明映射本身不完整。
```cpp
// 修法：把 8 也映射进 Backspace，或在 Editor 的 case 里并列
case Key::Backspace:            // 127
case static_cast<Key>(8):       // Ctrl-H
```

**问题 13 · 17 条固定记录用 `unordered_map`**，每次查要算哈希，还承担静态初始化期构造全局对象的开销。
```cpp
// 修法：改成静态数组 + 线性查找（17 条的量级线性更快），或直接用 switch
struct EscEntry { const char *seq; Key key; };
static const EscEntry kEscTable[] = { {"[A", Key::ArrowUp}, ... };
```

**问题 14 · `CTRL_KEY` 全大写像宏，实际是 `constexpr` 函数**
```cpp
// 修法：改名为 ctrlKey()
```

### 5.4 `main.cpp`

**问题 15 · `#include "src/Editor.h"` 依赖「从项目根目录编译」这个前提**
CMake 已把 `src/` 加进 include 路径，换构建方式就会找不到头文件。
```cpp
// 修法
#include "Editor.h"
#include "Terminal.h"
#include "Screen.h"
```

**问题 16 · `while (true)` 无出口，退出靠 `quit()` 里 `exit(0)`**
后果：`return 0` 永不可达（死代码）；所有析构被跳过（Terminal 恢复靠 `quit()` 里手动补一句）；
进程无法返回有意义的退出码。
```cpp
// 修法：让 processKeyPress 返回「是否继续」
while (editor.processKeyPress(terminal, screen))
    editor.refreshScreen(screen);
editor.shutdown();   // 显式清理
return 0;
// Editor::quit() 里不再 exit(0)，只置退出标志
```

### 5.5 `src/Editor.cpp`

**问题 17 · 欢迎语宽度按字节算**
`welcome.size()` 若换成中文会错位一半（项目后来已有 `Utf8` 的列宽计算，但这里没跟上）。
```cpp
// 修法
int welcomelen = Utf8::displayWidth(welcome);   // 而不是 welcome.size()
```

**问题 18 · `PageUp` / `PageDown` 位移算两遍，且两分支不对称**
先赋值 `cur.y`、再循环移动 `screenrows` 次；PageDown 分支夹了一次而 PageUp 没有。
```cpp
// 修法：只保留一种位移方式 —— 直接算目标行并夹取，删掉后面的 while 循环
if (key == Key::PageUp)
    cur.y = std::max(0, cur.rowoff - cur.screenrows);
else
    cur.y = std::min(static_cast<int>(buf.size()) - 1, cur.rowoff + 2 * cur.screenrows - 1);
```

**问题 19 · `Home` / `End` 不调整 `coloff`**
光标确实回到行首/跳到行尾，但视口不跟着移动，长行按完看起来像没动。
```cpp
// 修法：设置完 x 后让滚动逻辑重算，或把光标移动统一交给 cur.scroll() 处理
case Key::Home: cur.x = 0; break;
// 然后在 refreshScreen 前调用 cur.scroll(screen, buf) —— 它本来就负责调 coloff
```

**问题 20 · `quit()` 用 `exit(0)` 跳过所有析构**
`Editor` / `Buffer` / `Terminal` 的析构都不会跑，将来任何需要释放的资源都得有人记得手动补一行。
```cpp
// 修法：同问题 16 —— 改成返回退出标志，让 main 正常返回
```

### 5.6 `src/Buffer.cpp`

**问题 21 · 每敲一个字符整行重算 → O(n²)（性能瓶颈，已量化）**
`rows[y].update()` 是 O(行长)，长行连续输入的总代价是平方级。
**实测：往一行敲 1 万个字符耗时 190ms，而同样次数直接走 `PieceTable` 只要个位数毫秒 —— 差两个数量级。**
这是编辑器真实的卡顿来源，不在存储层。
```cpp
// 修法（三选一，按改动量递增）：
// (a) 增量更新：只重算插入位置之后的 cell，前面的 render/换算表保持不变
//     —— 需要 Row::update 支持「从某个 x 之后重算」
// (b) 延迟更新：给 Row 加 dirty 标记，只在真正绘制该行时才 update()，
//     一帧内多次编辑同一行只算一次
// (c) 架构层：行视图改成按需切片（渲染时才从 PieceTable 取当前屏的几行），
//     彻底去掉常驻的 rows —— 这是 VS Code 那类编辑器的做法，改动最大
```

### 5.7 `src/Terminal.cpp`

**问题 22 · 设 `CS8` 前没先清 `CSIZE`**
标准写法是先清再设，否则原值若是 `CS7`，两个位叠加是未定义组合（Linux 默认 CS8，所以一直没出事）。
```cpp
// 修法
raw.c_cflag &= ~CSIZE;
raw.c_cflag |= CS8;
```

**问题 23 · `tcsetattr` 返回值没检查**
设置失败会静默继续，之后「读不到数据就当超时」的判断都建立在它成功的前提上。
```cpp
// 修法
if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    die("tcsetattr");
```

### 5.8 `src/Row.cpp`

**问题 24 · `KILO_TAB_STOP` 用宏，且带着蓝本项目的前缀**
```cpp
// 修法
static constexpr int kTabStop = 4;
```

---

## 六、验证状态

| 工具链 | 编译 | 单元测试 | 端到端 |
|---|---|---|---|
| MinGW-w64 g++ 16.2.0 | 零警告（`-Wall -Wextra -Wpedantic`） | 18964 断言 ✅ | 22 项 ✅ |
| MSVC 19.51 Debug（`/MDd` + Ninja） | `/W4` 零警告 | 18964 断言 ✅ | 22 项 ✅ |
| Linux / macOS | **未实测** | — | — |

- `ctest` 两条用例在 MSVC 构建上通过。
- 3000 字符 × 连删 3000 次的压力轰炸，两端均通过（原崩溃场景）。
- **唯一未验证**：Windows 真实控制台下的 Esc（`test/verify_esc.cmd`，管道测不到那条分支）。

### 性能基准（`test/bench.cpp`，203333 字符，MinGW 16.2.0 `-O2`）

| 操作 | PieceTable | std::string | 比值 |
|---|---|---|---|
| 顺序追加 20 万字符 | 6.0 ms | 0.2 ms | **0.03x（输）** |
| 随机插入 2 万次 | 8.2 ms | 25.0 ms | **3.06x** |
| 随机删除 2 万次 | 10.2 ms | 22.5 ms | **2.20x** |
| 全量序列化（单次） | 0.004 ms | — | — |
| `Buffer` 层逐字符输入 1 万次 | 190.6 ms | — | — |

诚实结论：分块表换来的是**随机位置的插入与删除**，不是顺序写；真实瓶颈在 `Row::update`（见问题 21）。

---

## 七、待办

- **2.0 功能**：选择区（Shift 选段）→ 基于选择区的复制粘贴 → 多缓冲区 → 行号 → 更细的着色规则。
- **挂 GitHub**：`git remote add origin <地址> && git push -u origin master`
  （push 后 CI 才会跑，Linux/macOS 两条从未验证的路径才有答案）。
- **手工确认**：Windows 真实控制台下的 Esc。

---

## 八、想请高阶分析者重点看的几处

1. **问题 1（Treap 退化）的修法是否严谨**：我选择「两半各取新随机优先级、接受堆序可能被轻微破坏」，
   理由是堆序只影响平衡性不影响正确性（split/merge 始终维护中序序列）。
   这个取舍在有严格堆序要求的场景下是否会有隐患？
2. **架构选择**：`rows` 作为常驻行视图缓存，在多大文件规模上会成为不可接受的瓶颈？
   是否应该改成按需切片（问题 21 的修法 c）？
3. **问题 16/20 的退出路径改造**：把 `exit(0)` 换成「返回退出标志」，
   与 `Terminal` 的 RAII 恢复如何配合最干净？
4. **`Editor.cpp` 92% 是作者原始代码** —— 这部分在架构与实现质量上，
   有哪些是「新手常见但可以指出」的系统性问题？
