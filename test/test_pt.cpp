// 纯逻辑层测试：PieceTable、Buffer、Row。
//
// 这一组文件不依赖 termios / unistd 等 POSIX 终端接口，所以在 Linux、macOS、
// Windows 上都能直接构建运行：走 cmake 的 kalo_tests 目标，或者直接
//   g++ -std=c++17 -I src test/test_pt.cpp src/PieceTable.cpp src/Buffer.cpp src/Row.cpp
//
// 最关键的是「随机差分测试」：把 PieceTable 每一步操作的结果和 std::string 参照
// 模型逐字符比对。Treap 的坑（切片段时的堆序、size 维护、撤销还原）只有靠大量
// 随机操作才能兜住。

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <new>
#include <random>
#include <string>
#include <vector>

#include "Buffer.h"
#include "PieceTable.h"
#include "Row.h"
#include "Syntax.h"
#include "Utf8.h"

// PieceTable 里的节点全是手工 new / delete，一旦哪条路径漏了释放就会长期泄漏。
// 这里接管全局 new / delete 做一次分配与释放的收支核对。
static long long g_allocs = 0;
static long long g_frees = 0;

// 这几个函数刻意标成 noinline：否则 GCC 会把它内联进标准库的 allocator 里，
// 然后看到「指针来自 malloc，却交给 operator delete 释放」，报一条
// -Wmismatched-new-delete 的误报 —— 这里 malloc / free 本来就是成对使用的。
#if defined(__GNUC__) || defined(__clang__)
#define KALO_TEST_NOINLINE __attribute__((noinline))
#else
#define KALO_TEST_NOINLINE // MSVC 不需要，它本来就不会做这个内联诊断
#endif

KALO_TEST_NOINLINE void *operator new(std::size_t n)
{
    ++g_allocs;
    void *p = std::malloc(n ? n : 1);
    if (!p)
        throw std::bad_alloc();
    return p;
}

KALO_TEST_NOINLINE void operator delete(void *p) noexcept
{
    if (p)
        ++g_frees;
    std::free(p);
}

KALO_TEST_NOINLINE void operator delete(void *p, std::size_t) noexcept
{
    if (p)
        ++g_frees;
    std::free(p);
}

KALO_TEST_NOINLINE void *operator new[](std::size_t n)
{
    ++g_allocs;
    void *p = std::malloc(n ? n : 1);
    if (!p)
        throw std::bad_alloc();
    return p;
}

KALO_TEST_NOINLINE void operator delete[](void *p) noexcept
{
    if (p)
        ++g_frees;
    std::free(p);
}

KALO_TEST_NOINLINE void operator delete[](void *p, std::size_t) noexcept
{
    if (p)
        ++g_frees;
    std::free(p);
}

namespace
{

int g_checks = 0;
int g_failed = 0;

void reportFailure(const char *file, int line, const char *what)
{
    ++g_failed;
    std::printf("  [FAIL] %s:%d  %s\n", file, line, what);
}

#define CHECK(cond)                                   \
    do {                                              \
        ++g_checks;                                   \
        if (!(cond))                                  \
            reportFailure(__FILE__, __LINE__, #cond); \
    } while (false)

#define CHECK_EQ(actual, expected)                                \
    do {                                                          \
        ++g_checks;                                               \
        const auto _actual = (actual);                            \
        const auto _expected = (expected);                        \
        if (!(_actual == _expected))                              \
        {                                                         \
            reportFailure(__FILE__, __LINE__, "CHECK_EQ 不相等"); \
            std::cout << "         期望: " << _expected << "\n";  \
            std::cout << "         实际: " << _actual << "\n";    \
        }                                                         \
    } while (false)

// ==================== PieceTable ====================

void testPieceTableBasics()
{
    std::printf("PieceTable 构造与查询\n");

    PieceTable pt("hello world");
    CHECK_EQ(pt.totalChars(), 11);
    CHECK_EQ(pt.pieceCount(), 1);
    CHECK_EQ(pt.toString(), std::string("hello world"));
    CHECK_EQ(pt.query(0, 5), std::string("hello"));
    CHECK_EQ(pt.query(6, 5), std::string("world"));
    CHECK_EQ(pt.query(2, 3), std::string("llo"));
    CHECK_EQ(pt.query(3, 0), std::string(""));              // 长度为 0
    CHECK_EQ(pt.query(0, 100), std::string("hello world")); // 超出长度自动截断
    CHECK_EQ(pt.query(50, 5), std::string(""));             // 起点越界
    CHECK(!pt.canUndo());
    CHECK(!pt.empty());

    PieceTable empty;
    CHECK_EQ(empty.totalChars(), 0);
    CHECK(empty.empty());
    CHECK_EQ(empty.toString(), std::string(""));
    CHECK_EQ(empty.pieceCount(), 0);
    CHECK_EQ(empty.undo(), false);
}

void testPieceTableInsert()
{
    std::printf("PieceTable 插入\n");

    PieceTable pt("world");
    pt.insert(0, "hello ");
    CHECK_EQ(pt.toString(), std::string("hello world"));

    pt.insert(pt.totalChars(), "!");
    CHECK_EQ(pt.toString(), std::string("hello world!"));

    pt.insert(5, ","); // 切点落在片段内部
    CHECK_EQ(pt.toString(), std::string("hello, world!"));

    pt.insert(999, "?"); // 越界位置夹到末尾
    CHECK_EQ(pt.toString(), std::string("hello, world!?"));

    pt.insert(-5, "<<"); // 负位置夹到开头
    CHECK_EQ(pt.toString(), std::string("<<hello, world!?"));

    PieceTable fromEmpty;
    fromEmpty.insert(0, "abc");
    CHECK_EQ(fromEmpty.toString(), std::string("abc"));

    PieceTable pt2("abcdef");
    pt2.insert(6, "");
    CHECK_EQ(pt2.toString(), std::string("abcdef")); // 空串插入不应有副作用
    CHECK(!pt2.canUndo());
}

void testPieceTableRemove()
{
    std::printf("PieceTable 删除\n");

    PieceTable pt("hello, world!");
    pt.remove(5, 2); // 去掉 ", "
    CHECK_EQ(pt.toString(), std::string("helloworld!"));

    pt.remove(0, 5);
    CHECK_EQ(pt.toString(), std::string("world!"));

    pt.remove(5, 1);
    CHECK_EQ(pt.toString(), std::string("world"));

    pt.remove(3, 0); // 长度 0 不应有副作用
    CHECK_EQ(pt.toString(), std::string("world"));

    pt.remove(0, 100); // 越界长度夹到末尾，等价于清空
    CHECK_EQ(pt.toString(), std::string(""));
    CHECK_EQ(pt.totalChars(), 0);

    pt.remove(0, 1); // 空表上删除不应崩溃
    CHECK_EQ(pt.toString(), std::string(""));
}

void testPieceTableCoalesce()
{
    std::printf("PieceTable 相邻插入合并\n");

    // 在文末连续敲字：追加缓冲区最终只该合成一个片段
    PieceTable pt("ab");
    for (char c : std::string("cdefg"))
        pt.insert(pt.totalChars(), std::string(1, c));

    CHECK_EQ(pt.toString(), std::string("abcdefg"));
    CHECK_EQ(pt.pieceCount(), 2); // 初始片段 + 追加片段

    // 在文档中间连续敲字：每次插入都紧接上一次，同样应当并进同一片段
    PieceTable pt2("hello");
    pt2.insert(2, "X");
    pt2.insert(3, "Y");
    pt2.insert(4, "Z");
    CHECK_EQ(pt2.toString(), std::string("heXYZllo"));
    CHECK_EQ(pt2.pieceCount(), 3); // "he" + "XYZ" + "llo"

    // 中间穿插了其它编辑就不该再合并，此处只校验内容正确
    PieceTable pt3("ac");
    pt3.insert(1, "b"); // abc
    pt3.remove(0, 1);   // bc
    pt3.insert(0, "a"); // abc
    CHECK_EQ(pt3.toString(), std::string("abc"));
}

void testPieceTableUndo()
{
    std::printf("PieceTable 撤销\n");

    PieceTable pt("abcd");
    CHECK(!pt.canUndo());

    pt.insert(4, "EF");
    CHECK_EQ(pt.toString(), std::string("abcdEF"));
    CHECK(pt.canUndo());

    CHECK(pt.undo());
    CHECK_EQ(pt.toString(), std::string("abcd"));
    CHECK(!pt.canUndo());

    // 撤销删除：被删片段应当原样还原
    pt.remove(1, 2);
    CHECK_EQ(pt.toString(), std::string("ad"));
    CHECK(pt.undo());
    CHECK_EQ(pt.toString(), std::string("abcd"));

    // 多步撤销按后进先出逐条回滚
    pt.remove(0, 1);
    pt.insert(0, "ZZ");
    CHECK_EQ(pt.toString(), std::string("ZZbcd"));
    CHECK(pt.undo());
    CHECK_EQ(pt.toString(), std::string("bcd"));
    CHECK(pt.undo());
    CHECK_EQ(pt.toString(), std::string("abcd"));
    CHECK(!pt.canUndo());
    CHECK_EQ(pt.undo(), false);

    // 撤销之后再编辑，历史应当干净地接着走
    PieceTable pt2("12345");
    pt2.insert(0, "A");
    pt2.undo();
    pt2.insert(5, "B");
    CHECK_EQ(pt2.toString(), std::string("12345B"));
    pt2.undo();
    CHECK_EQ(pt2.toString(), std::string("12345"));

    // 已合并进同一片段的多次插入，也要能逐个撤销
    PieceTable pt3("ab");
    pt3.insert(2, "c");
    pt3.insert(3, "d");
    pt3.insert(4, "e");
    CHECK_EQ(pt3.toString(), std::string("abcde"));
    CHECK(pt3.undo());
    CHECK_EQ(pt3.toString(), std::string("abcd"));
    CHECK(pt3.undo());
    CHECK_EQ(pt3.toString(), std::string("abc"));
    CHECK(pt3.undo());
    CHECK_EQ(pt3.toString(), std::string("ab"));
}

// 与 std::string 参照模型逐字符比对，覆盖插入 / 删除 / 查询 / 撤销的随机组合
void testPieceTableFuzz()
{
    std::printf("PieceTable 随机差分测试\n");

    std::mt19937 gen(20261005);
    int editCount = 0;
    int undoCount = 0;
    bool broken = false;

    for (int round = 0; round < 120 && !broken; ++round)
    {
        PieceTable pt("seed");
        std::string model = "seed";
        std::vector<std::string> snapshots; // 每次编辑前的模型快照

        for (int step = 0; step < 150; ++step)
        {
            const int op = static_cast<int>(gen() % 4);

            if (op == 0 || model.empty())
            {
                const int pos = static_cast<int>(gen() % (model.size() + 1));
                std::string text;
                const int n = 1 + static_cast<int>(gen() % 3);
                for (int i = 0; i < n; ++i)
                    text += static_cast<char>('a' + static_cast<int>(gen() % 26));

                snapshots.push_back(model);
                pt.insert(pos, text);
                model.insert(static_cast<size_t>(pos), text);
                ++editCount;
            }
            else if (op == 1)
            {
                const int pos = static_cast<int>(gen() % model.size());
                int len = 1 + static_cast<int>(gen() % 4);
                if (pos + len > static_cast<int>(model.size()))
                    len = static_cast<int>(model.size()) - pos;

                snapshots.push_back(model);
                pt.remove(pos, len);
                model.erase(static_cast<size_t>(pos), static_cast<size_t>(len));
                ++editCount;
            }
            else if (op == 2)
            {
                const int pos = static_cast<int>(gen() % model.size());
                int len = static_cast<int>(gen() % 8);
                if (pos + len > static_cast<int>(model.size()))
                    len = static_cast<int>(model.size()) - pos;

                CHECK_EQ(pt.query(pos, len),
                         model.substr(static_cast<size_t>(pos), static_cast<size_t>(len)));
                CHECK_EQ(pt.totalChars(), static_cast<int>(model.size()));
            }
            else
            {
                if (!snapshots.empty())
                {
                    const std::string expected = snapshots.back();
                    snapshots.pop_back();
                    CHECK(pt.undo());
                    model = expected;
                    ++undoCount;
                }
            }

            if (pt.toString() != model)
            {
                ++g_checks;
                std::printf("  [FAIL] 第 %d 轮第 %d 步：内容与参照模型不一致\n", round, step);
                std::printf("         期望: %s\n", model.c_str());
                std::printf("         实际: %s\n", pt.toString().c_str());
                broken = true;
                break;
            }
        }
    }

    std::printf("  共执行 %d 次编辑、%d 次撤销\n", editCount, undoCount);
}

// 规模测试：大量随机位置插入会把片段数堆到几千，Treap 若退化成链就会在这里暴露
void testPieceTableScale()
{
    std::printf("PieceTable 规模测试\n");

    std::mt19937 gen(777);
    PieceTable pt;
    std::string model;

    for (int i = 0; i < 5000; ++i)
    {
        const int pos = model.empty() ? 0 : static_cast<int>(gen() % (model.size() + 1));
        const std::string text(
            1 + static_cast<size_t>(gen() % 4),
            static_cast<char>('a' + static_cast<int>(gen() % 26)));
        pt.insert(pos, text);
        model.insert(static_cast<size_t>(pos), text);
    }

    CHECK_EQ(pt.totalChars(), static_cast<int>(model.size()));
    CHECK_EQ(pt.toString(), model);
    CHECK_EQ(pt.query(100, 200), model.substr(100, 200));

    const int peakPieces = pt.pieceCount();
    CHECK(peakPieces > 100); // 随机位置插入确实产生了大量片段

    // 逐条撤销，应当完全回到空文档
    int undone = 0;
    while (pt.undo())
        ++undone;
    CHECK_EQ(undone, 5000);
    CHECK_EQ(pt.toString(), std::string(""));
    CHECK(pt.empty());

    std::printf("  峰值片段数 %d，撤销 %d 次后回到空文档\n", peakPieces, undone);
}

// 分配与释放不能出现净增长：析构、切片段、撤销还原这几条路径都不该漏释放。
//
// 这里只判方向（释放数不能少于分配数）而不要求严格相等：在 MinGW 上如果
// libstdc++ 是动态链接的，DLL 内部用自己那份 operator new 分配、却由本模块的
// operator delete 释放，计数天然会对不上一个零头。但真正的泄漏是成百上千个树节点
// 的量级，方向判断足以抓住。
void testPieceTableNoLeak()
{
    std::printf("PieceTable 内存收支\n");

    const long long baseline = g_allocs - g_frees;
    long long after = baseline;

    {
        {
            PieceTable pt("hello world");
            for (int i = 0; i < 400; ++i)
                pt.insert(i % 12, "xy");
            for (int i = 0; i < 120; ++i)
                pt.remove(0, 1);
            while (pt.undo())
            {
            }
            pt.reset("fresh");
            pt.insert(0, "abc");

            PieceTable empty;
            empty.insert(0, "z");
            empty.remove(0, 1);
        } // 析构在这里发生

        after = g_allocs - g_frees;
    }

    std::printf("  本次作用域净增分配 %lld 次（应为 0）\n", after - baseline);
    CHECK(after <= baseline);
}

// ==================== Buffer ====================

void testBufferLoad()
{
    std::printf("Buffer 装载与往返\n");

    Buffer b;
    b.fromText("a\nb\nc\n");
    CHECK_EQ(static_cast<int>(b.size()), 3);
    CHECK_EQ(b[0].chars, std::string("a"));
    CHECK_EQ(b[2].chars, std::string("c"));
    CHECK_EQ(b.toString(), std::string("a\nb\nc\n")); // 原样往返

    Buffer noTrailing;
    noTrailing.fromText("a\nb\nc"); // 结尾没有换行
    CHECK_EQ(static_cast<int>(noTrailing.size()), 3);
    CHECK_EQ(noTrailing.toString(), std::string("a\nb\nc"));

    Buffer blank;
    blank.fromText("");
    CHECK(blank.empty());
    CHECK_EQ(blank.toString(), std::string(""));

    Buffer onlyNewline;
    onlyNewline.fromText("\n");
    CHECK_EQ(static_cast<int>(onlyNewline.size()), 1);
    CHECK_EQ(onlyNewline[0].chars, std::string(""));

    Buffer innerBlank;
    innerBlank.fromText("a\n\nb\n");
    CHECK_EQ(static_cast<int>(innerBlank.size()), 3);
    CHECK_EQ(innerBlank[1].chars, std::string(""));

    // 旧入口：每行补一个换行，与改造前的行为一致
    Buffer fromLines;
    fromLines.fromLines({"x", "y"});
    CHECK_EQ(static_cast<int>(fromLines.size()), 2);
    CHECK_EQ(fromLines.toString(), std::string("x\ny\n"));

    // 制表符仍然由 Row 展开成渲染列
    Buffer tabs;
    tabs.fromText("\tx\n");
    CHECK_EQ(tabs[0].chars, std::string("\tx"));
    CHECK_EQ(tabs[0].render, std::string("    x")); // KILO_TAB_STOP = 4
}

void testBufferEdit()
{
    std::printf("Buffer 插入 / 删除 / 换行 / 撤销\n");

    Buffer b;
    b.fromText("hello\nworld\n");
    CHECK_EQ(static_cast<int>(b.size()), 2);

    b.insert(0, 5, '!');
    CHECK_EQ(b[0].chars, std::string("hello!"));
    CHECK_EQ(b.toString(), std::string("hello!\nworld\n"));

    // 行尾回车：光标换到下一行行首
    int cx = 6;
    int cy = 0;
    b.insertNewline(0, 6, cx, cy);
    CHECK_EQ(static_cast<int>(b.size()), 3);
    CHECK_EQ(cy, 1);
    CHECK_EQ(cx, 0);
    CHECK_EQ(b[1].chars, std::string(""));
    CHECK_EQ(b.toString(), std::string("hello!\n\nworld\n"));

    // 行首退格：把两行接起来，光标落到接缝处
    int x = 0;
    int y = 1;
    b.del(1, x, y);
    CHECK_EQ(static_cast<int>(b.size()), 2);
    CHECK_EQ(y, 0);
    CHECK_EQ(x, 6);
    CHECK_EQ(b[0].chars, std::string("hello!"));
    CHECK_EQ(b[1].chars, std::string("world"));
    CHECK_EQ(b.toString(), std::string("hello!\nworld\n"));

    // 行内退格
    x = 6;
    y = 0;
    b.del(0, x, y);
    CHECK_EQ(b[0].chars, std::string("hello"));
    CHECK_EQ(x, 5);
    CHECK_EQ(b.toString(), std::string("hello\nworld\n"));

    // 撤销回滚最近一次删除
    CHECK(b.undo());
    CHECK_EQ(b[0].chars, std::string("hello!"));
    CHECK_EQ(b.toString(), std::string("hello!\nworld\n"));

    // 文档最开头退格：什么都不该发生
    x = 0;
    y = 0;
    b.del(0, x, y);
    CHECK_EQ(b.toString(), std::string("hello!\nworld\n"));

    // 光标停在最后一行之后（幽灵行）时输入 —— 旧实现会抛 std::out_of_range
    Buffer phantom;
    phantom.fromText("ab\n");
    phantom.insert(1, 3, 'c');
    CHECK_EQ(static_cast<int>(phantom.size()), 2);
    CHECK_EQ(phantom.toString(), std::string("ab\nc"));
    CHECK_EQ(phantom[1].chars, std::string("c"));

    // 虚拟末行按回车应新增空行，不能夹回上一行再拆分它。
    Buffer phantom2;
    phantom2.fromText("ab\n");
    int pcx = 0;
    int pcy = 1;
    phantom2.insertNewline(1, 0, pcx, pcy);
    CHECK_EQ(phantom2.toString(), std::string("ab\n\n"));
    CHECK_EQ(pcy, 2);
    CHECK_EQ(pcx, 0);
}

// 行视图不能和 PieceTable 里的权威文本跑偏
bool checkViewConsistent(const Buffer &b, const char *where)
{
    std::string joined;
    for (int i = 0; i < static_cast<int>(b.size()); ++i)
    {
        if (i > 0)
            joined += '\n';
        joined += b[i].chars;
    }

    const std::string text = b.toString();
    const bool ok = (joined == text) || (joined + "\n" == text);
    if (!ok)
    {
        ++g_checks;
        std::printf("  [FAIL] %s：行视图与 PieceTable 不一致\n", where);
        std::printf("         视图连接: %s\n", joined.c_str());
        std::printf("         权威文本: %s\n", text.c_str());
    }
    return ok;
}

void testBufferViewSync()
{
    std::printf("Buffer 行视图与 PieceTable 的一致性（随机）\n");

    std::mt19937 gen(4242);

    for (int round = 0; round < 40; ++round)
    {
        Buffer b;
        b.fromText("alpha\nbeta\ngamma\n");

        for (int step = 0; step < 120; ++step)
        {
            if (b.empty())
            {
                b.insert(0, 0, 'x');
                continue;
            }

            const int row = static_cast<int>(gen() % b.size());
            const int rowLen = static_cast<int>(b[row].chars.size());
            const int op = static_cast<int>(gen() % 3);

            if (op == 0)
            {
                const int x = rowLen ? static_cast<int>(gen() % (rowLen + 1)) : 0;
                b.insert(row, x, static_cast<char>('a' + static_cast<int>(gen() % 26)));
            }
            else if (op == 1)
            {
                int x = rowLen ? 1 + static_cast<int>(gen() % rowLen) : 0;
                int cursorRow = row;
                b.del(row, x, cursorRow);
            }
            else
            {
                const int x = rowLen ? static_cast<int>(gen() % (rowLen + 1)) : 0;
                int cx = x;
                int cy = row;
                b.insertNewline(row, x, cx, cy);
            }

            if (!checkViewConsistent(b, "随机编辑"))
            {
                std::printf("         第 %d 轮第 %d 步\n", round, step);
                return;
            }
        }
    }
}

void testBufferUndo()
{
    std::printf("Buffer 撤销与行视图重建\n");

    Buffer b;
    b.fromText("one\ntwo\nthree\n");

    b.insert(1, 0, 'X');
    CHECK_EQ(b.toString(), std::string("one\nXtwo\nthree\n"));
    CHECK(b.canUndo());

    CHECK(b.undo());
    CHECK_EQ(b.toString(), std::string("one\ntwo\nthree\n"));
    CHECK_EQ(static_cast<int>(b.size()), 3);
    CHECK(checkViewConsistent(b, "撤销后"));

    // 撤销插入的换行后，行数必须跟着减少
    int cx = 0;
    int cy = 1;
    b.insertNewline(1, 0, cx, cy);
    CHECK_EQ(static_cast<int>(b.size()), 4);
    CHECK_EQ(b.toString(), std::string("one\n\ntwo\nthree\n"));

    CHECK(b.undo());
    CHECK_EQ(static_cast<int>(b.size()), 3);
    CHECK_EQ(b.toString(), std::string("one\ntwo\nthree\n"));
    CHECK(checkViewConsistent(b, "撤销换行后"));

    // 撤销行合并
    int dx = 0;
    int dy = 1;
    b.del(1, dx, dy);
    CHECK_EQ(b.toString(), std::string("onetwo\nthree\n"));
    CHECK(b.undo());
    CHECK_EQ(b.toString(), std::string("one\ntwo\nthree\n"));
    CHECK(checkViewConsistent(b, "撤销合并后"));

    CHECK(!b.canUndo());
}

// ==================== 搜索（必须走 PieceTable 权威文本） ====================

void testBufferFindForward()
{
    std::printf("Buffer 向前查找\n");

    Buffer b;
    const std::string ref = "alpha beta\nbeta gamma\nbeta\n";
    b.fromText(ref);

    const std::string needle = "beta";

    // 全量比对「从 from 起向前」的结果与 std::string::find 的语义是否一致
    for (int from = 0; from <= static_cast<int>(ref.size()) + 2; ++from)
    {
        const size_t expected = ref.find(needle, static_cast<size_t>(from < 0 ? 0 : from));
        const int got = b.findText(needle, from, true);
        CHECK_EQ(got, expected == std::string::npos ? -1 : static_cast<int>(expected));
    }

    CHECK_EQ(b.findText(needle, 0, true), 6);    // 首次命中在偏移 6（前面是 "alpha "）
    CHECK_EQ(b.findText(needle, 11, true), 11);  // 第二处命中
    CHECK_EQ(b.findText(needle, 12, true), 22);  // 跳过第二处后落到第三处
    CHECK_EQ(b.findText("nope", 0, true), -1);  // 未命中
    CHECK_EQ(b.findText(needle, 999, true), -1);  // 起点越界
}

void testBufferFindBackward()
{
    std::printf("Buffer 反向查找\n");

    Buffer b;
    const std::string ref = "alpha beta\nbeta gamma\nbeta\n";
    b.fromText(ref);

    const std::string needle = "beta";

    // findText(backward) 的 fromOffset 是「匹配起点上界」，与 std::string::rfind 对齐
    for (int from = 0; from <= static_cast<int>(ref.size()) + 2; ++from)
    {
        const size_t expected = ref.rfind(needle, static_cast<size_t>(from < 0 ? 0 : from));
        const int got = b.findText(needle, from, false);
        CHECK_EQ(got, expected == std::string::npos ? -1 : static_cast<int>(expected));
    }

    CHECK_EQ(b.findText(needle, static_cast<int>(ref.size()), false),
             static_cast<int>(ref.rfind(needle))); // 从末尾能找回最后一处
    CHECK_EQ(b.findText(needle, 11, false), 11);   // 上界 11 只覆盖到第二处
    CHECK_EQ(b.findText("nope", 999, false), -1);  // 未命中
}

void testBufferFindEdgeCases()
{
    std::printf("Buffer 查找边界\n");

    Buffer b;
    const std::string ref = "one\ntwo\nthree\n";
    b.fromText(ref);

    // 空关键词一律返回 -1，避免「零长度命中」的歧义
    CHECK_EQ(b.findText("", 0, true), -1);
    CHECK_EQ(b.findText("", 5, false), -1);

    // 正向起点为负按 0 处理
    CHECK_EQ(b.findText("one", -10, true), 0);
    // 反向起点为负按末尾处理（等价于整串 rfind）
    CHECK_EQ(b.findText("three", -10, false), static_cast<int>(ref.rfind("three")));

    // 起点越过末尾：正向无匹配，反向仍等价于从末尾找
    const int total = b.charCount();
    CHECK_EQ(b.findText("one", total + 10, true), -1);
    CHECK_EQ(b.findText("three", total + 10, false), static_cast<int>(ref.rfind("three")));

    // 跨行关键词：needle 可以含 '\n'
    CHECK_EQ(b.findText("two\nthree", 0, true), static_cast<int>(ref.find("two\nthree")));
    CHECK_EQ(b.findText("one\ntwo", 0, true), 0);

    // 命中末尾那个换行符
    CHECK_EQ(b.findText("\n", total - 1, true), total - 1);

    // 空文档不能崩
    Buffer empty;
    empty.fromText("");
    CHECK_EQ(empty.charCount(), 0);
    CHECK_EQ(empty.findText("x", 0, true), -1);
    CHECK_EQ(empty.findText("x", 0, false), -1);
}

void testBufferLocateRoundTrip()
{
    std::printf("Buffer 偏移与坐标互逆\n");

    Buffer b;
    b.fromText("alpha\nbeta\ngamma\n");

    // locate 与 offsetOf 共用「每行占 chars.size() + 1」的进位规则，必须互逆
    for (int y = 0; y < static_cast<int>(b.size()); ++y)
    {
        const int rowLen = static_cast<int>(b[y].chars.size());
        for (int x = 0; x <= rowLen; ++x)
        {
            int off = 0;
            for (int i = 0; i < y; ++i)
                off += static_cast<int>(b[i].chars.size()) + 1;
            off += x;

            int ny = -1;
            int nx = -1;
            CHECK(b.locate(off, ny, nx));
            CHECK_EQ(ny, y);
            CHECK_EQ(nx, x);
        }
    }

    // 文档末尾之后（offset == charCount）：合法，落在末行行尾
    int ny = -1;
    int nx = -1;
    CHECK(b.locate(b.charCount(), ny, nx));
    CHECK_EQ(ny, static_cast<int>(b.size()) - 1);
    CHECK_EQ(nx, static_cast<int>(b[static_cast<int>(b.size()) - 1].chars.size()));

    // 越界返回 false，且不得改动出参
    int oy = 123;
    int ox = 456;
    CHECK(!b.locate(-1, oy, ox));
    CHECK_EQ(oy, 123);
    CHECK_EQ(ox, 456);
    CHECK(!b.locate(b.charCount() + 1, oy, ox));
    CHECK_EQ(oy, 123);
    CHECK_EQ(ox, 456);

    // 空文档：locate(0) 得 (0, 0)
    Buffer empty;
    empty.fromText("");
    CHECK_EQ(empty.charCount(), 0);
    ny = -1;
    nx = -1;
    CHECK(empty.locate(0, ny, nx));
    CHECK_EQ(ny, 0);
    CHECK_EQ(nx, 0);
}

// 随机差分：搜索读的必须是权威文本，而不是可能滞后的行视图缓存。
// 每一步都把 findText 的结果与「对权威文本做 std::string::find/rfind」比对。
void testBufferFindAfterEdit()
{
    std::printf("Buffer 编辑后查找与权威文本一致（随机）\n");

    std::mt19937 gen(20261006);

    for (int round = 0; round < 30; ++round)
    {
        Buffer b;
        b.fromText("alpha beta\ngamma beta\ndelta\n");

        for (int step = 0; step < 100; ++step)
        {
            if (b.empty())
            {
                b.insert(0, 0, 'x');
                continue;
            }

            const int row = static_cast<int>(gen() % b.size());
            const int rowLen = static_cast<int>(b[row].chars.size());
            const int op = static_cast<int>(gen() % 4);

            if (op == 0)
            {
                const int x = rowLen ? static_cast<int>(gen() % (rowLen + 1)) : 0;
                b.insert(row, x, static_cast<char>('a' + static_cast<int>(gen() % 26)));
            }
            else if (op == 1)
            {
                int x = rowLen ? static_cast<int>(gen() % (rowLen + 1)) : 0;
                int cursorRow = row;
                b.del(row, x, cursorRow);
            }
            else if (op == 2)
            {
                const int x = rowLen ? static_cast<int>(gen() % (rowLen + 1)) : 0;
                int cx = x;
                int cy = row;
                b.insertNewline(row, x, cx, cy);
            }
            else
            {
                b.undo();
            }

            // 参照模型就是权威文本本身（toString 直接返回 PieceTable 的内容）
            const std::string ref = b.toString();

            // 随机挑 needle：一半从权威文本里截，一半用固定串（含未命中情形）
            std::string needle;
            if (!ref.empty() && (gen() % 2) == 0)
            {
                const int start = static_cast<int>(gen() % ref.size());
                const int len = 1 + static_cast<int>(gen() % 4);
                needle = ref.substr(static_cast<size_t>(start), static_cast<size_t>(len));
            }
            else
            {
                needle = (gen() % 2) ? "beta" : "zzz";
            }

            const int from = static_cast<int>(gen() % (ref.size() + 1));

            const size_t fwd = ref.find(needle, static_cast<size_t>(from));
            CHECK_EQ(b.findText(needle, from, true),
                     fwd == std::string::npos ? -1 : static_cast<int>(fwd));

            const size_t bwd = ref.rfind(needle, static_cast<size_t>(from));
            CHECK_EQ(b.findText(needle, from, false),
                     bwd == std::string::npos ? -1 : static_cast<int>(bwd));
        }
    }
}

// ==================== UTF-8 与显示列宽 ====================

void testUtf8Basics()
{
    std::printf("UTF-8 序列切分与东亚宽度\n");

    CHECK_EQ(Utf8::seqLen("a", 0), 1);
    CHECK_EQ(Utf8::seqLen("\xE4\xB8\xAD", 0), 3);          // 中
    CHECK_EQ(Utf8::seqLen("\xF0\x9F\x98\x80", 0), 4);      // 一个 emoji
    CHECK_EQ(Utf8::seqLen("\xE4\xB8", 0), 1);              // 截断的序列按 1 字节处理
    CHECK_EQ(Utf8::seqLen("\x80\x41", 0), 1);              // 孤立续字节
    CHECK_EQ(Utf8::seqLen("", 0), 1);                      // 越界也不许返回 0，否则调用方会死循环

    CHECK_EQ(Utf8::decode("\xE4\xB8\xAD", 0), 0x4E2Du);
    CHECK(Utf8::isWide(0x4E2D));   // 中文
    CHECK(Utf8::isWide(0x3042));   // 平假名
    CHECK(Utf8::isWide(0xFF21));   // 全角 A
    CHECK(Utf8::isWide(0x1F600));  // emoji
    CHECK(!Utf8::isWide('A'));
    CHECK(!Utf8::isWide(0x00E9));  // é（拉丁字母，窄）

    CHECK_EQ(Utf8::charWidth("\xE4\xB8\xAD", 0), 2);
    CHECK_EQ(Utf8::charWidth("a", 0), 1);
}

void testUtf8Boundaries()
{
    std::printf("UTF-8 字符边界与大小写折叠\n");

    // "a中b"：a 占 [0,1)，中 占 [1,4)，b 占 [4,5)
    const std::string s = "a\xE4\xB8\xAD"
                          "b";
    CHECK_EQ(Utf8::nextBoundary(s, 0), 1);
    CHECK_EQ(Utf8::nextBoundary(s, 1), 4);
    CHECK_EQ(Utf8::nextBoundary(s, 4), 5);
    CHECK_EQ(Utf8::prevBoundary(s, 5), 4);
    CHECK_EQ(Utf8::prevBoundary(s, 4), 1);
    CHECK_EQ(Utf8::prevBoundary(s, 1), 0);
    CHECK_EQ(Utf8::prevBoundary(s, 0), 0);

    // 折叠只动 A-Z，字节长度必须严格不变 —— 大小写不敏感搜索靠这一点保住偏移
    CHECK_EQ(Utf8::foldCase("AbC-Zz"), std::string("abc-zz"));
    const std::string mixed = "A\xE4\xB8\xAD"
                              "Z";
    const std::string folded = Utf8::foldCase(mixed);
    CHECK_EQ(folded.size(), mixed.size());
    CHECK_EQ(folded, std::string("a\xE4\xB8\xAD"
                                 "z"));
}

void testRowWidthAndCells()
{
    std::printf("Row 的列宽与三套下标换算\n");

    Row row;
    row.chars = "ab\xE4\xB8\xAD";
    row.update();

    CHECK_EQ(row.cellCount(), 3);
    CHECK_EQ(row.width(), 4); // a、b 各 1 列，中文 2 列
    CHECK_EQ(row.xToRx(0), 0);
    CHECK_EQ(row.xToRx(1), 1);
    CHECK_EQ(row.xToRx(2), 2); // 中文首字节处
    CHECK_EQ(row.xToRx(5), 4); // 行尾
    // 落在中文中间（第 3、4 字节）时，统一按该字符的起点算
    CHECK_EQ(row.xToRx(3), 2);
    CHECK_EQ(row.xToRx(4), 2);

    CHECK_EQ(row.rxToByte(0), 0);
    CHECK_EQ(row.rxToByte(2), 2);
    CHECK_EQ(row.rxToByte(3), 2); // 宽字符的第二列仍指向该字符起点
    CHECK_EQ(row.cellText(2), std::string("\xE4\xB8\xAD"));

    // 制表符仍然按 KILO_TAB_STOP=4 对齐展开
    Row tabbed;
    tabbed.chars = "\ta";
    tabbed.update();
    CHECK_EQ(tabbed.width(), 5);
    CHECK_EQ(tabbed.cellRx(1), 4);
    CHECK_EQ(tabbed.cellText(0), std::string("    "));

    // 控制字符按 kilo 的做法渲染成 '?'，不直接写进终端
    Row ctrl;
    ctrl.chars = "a\x01"
                 "b";
    ctrl.update();
    CHECK_EQ(ctrl.width(), 3);
    CHECK_EQ(ctrl.render, std::string("a?b"));
}

void testUtf8Backspace()
{
    std::printf("退格按字符删除\n");

    Buffer b;
    b.fromText("a\xE4\xB8\xAD"
               "b");

    int x = 4; // 光标停在 b 之前
    int y = 0;
    b.del(y, x, y);
    CHECK_EQ(b.toString(), std::string("ab")); // 整个中文被删掉，不留半个序列
    CHECK_EQ(x, 1);

    x = 1;
    b.del(y, x, y);
    CHECK_EQ(b.toString(), std::string("b"));
}

// ==================== 语法高亮 ====================

void testSyntaxHighlight()
{
    std::printf("语法高亮标记\n");

    const SyntaxRule *cpp = Syntax::forFile("main.cpp");
    CHECK(cpp != nullptr);
    CHECK_EQ(cpp->name, std::string("C/C++"));
    CHECK(Syntax::forFile("MAIN.CPP") != nullptr); // 后缀大小写无关
    CHECK(Syntax::forFile("a.txt") == nullptr);    // 认不出来就不高亮
    CHECK(Syntax::forFile("") == nullptr);

    Row row;
    row.chars = "int x = 42; // note";
    row.update();
    bool open = true;
    Syntax::highlightRow(row, cpp, false, open);

    // int 在 kilo 的分组里属于「类型名」，走第二组关键字
    CHECK(row.hl[0] == Hl::Keyword2);
    CHECK(row.hl[1] == Hl::Keyword2);
    CHECK(row.hl[2] == Hl::Keyword2);
    CHECK(row.hl[3] == Hl::Normal); // 空格
    CHECK(row.hl[8] == Hl::Number); // '4'
    CHECK(row.hl[9] == Hl::Number); // '2'
    CHECK(row.hl[10] == Hl::Normal);
    CHECK(row.hl[12] == Hl::Comment); // '/'
    CHECK(row.hl[18] == Hl::Comment); // 行尾仍在注释里
    CHECK(!open);

    // "myif" 不该被当成关键字：关键字只在分隔符之后才识别
    Row ident;
    ident.chars = "myif int";
    ident.update();
    Syntax::highlightRow(ident, cpp, false, open);
    CHECK(ident.hl[0] == Hl::Normal);
    CHECK(ident.hl[5] == Hl::Keyword2);

    // 字符串：引号内全部染成字符串色，转义序列不会提前截断
    Row str;
    str.chars = "const char *s = \"a\\\"b\";";
    str.update();
    Syntax::highlightRow(str, cpp, false, open);
    const int quote = static_cast<int>(str.chars.find('"'));
    CHECK(str.hl[quote] == Hl::String);
    CHECK(str.hl[quote + 1] == Hl::String);
    CHECK(str.hl[quote + 4] == Hl::String); // 被转义的那个引号仍在字符串里

    // 第一组关键字（控制流等）与类型名分开
    Row ctrlKw;
    ctrlKw.chars = "return 0";
    ctrlKw.update();
    Syntax::highlightRow(ctrlKw, cpp, false, open);
    CHECK(ctrlKw.hl[0] == Hl::Keyword1);

    // 类型名走第二组关键字
    Row typed;
    typed.chars = "unsigned x";
    typed.update();
    Syntax::highlightRow(typed, cpp, false, open);
    CHECK(typed.hl[0] == Hl::Keyword2);

    // 中文不应该影响后面的注释识别
    Row cjk;
    cjk.chars = "\xE4\xB8\xAD"
                " // x";
    cjk.update();
    Syntax::highlightRow(cjk, cpp, false, open);
    const int slash = static_cast<int>(cjk.chars.find("//"));
    CHECK(cjk.hl[cjk.cellOfCharByte(slash)] == Hl::Comment);

    // 未知语言：整行 Normal
    Row plain;
    plain.chars = "int x";
    plain.update();
    Syntax::highlightRow(plain, nullptr, false, open);
    CHECK(plain.hl[0] == Hl::Normal);
    CHECK(!open);
}

void testSyntaxMultilineComment()
{
    std::printf("多行注释的跨行传播\n");

    const SyntaxRule *cpp = Syntax::forFile("a.c");
    CHECK(cpp != nullptr);

    bool open = false;

    Row first;
    first.chars = "int a; /* 开始";
    first.update();
    Syntax::highlightRow(first, cpp, false, open);
    CHECK(open); // 这一行结束时仍在块注释里
    CHECK(first.hl[first.cellOfCharByte(static_cast<int>(first.chars.find("/*")))] == Hl::MlComment);

    Row mid;
    mid.chars = "仍然是注释";
    mid.update();
    Syntax::highlightRow(mid, cpp, open, open);
    CHECK(mid.hl[0] == Hl::MlComment);
    CHECK(open);

    Row last;
    last.chars = "结束 */ int b;";
    last.update();
    Syntax::highlightRow(last, cpp, open, open);
    CHECK(!open); // 遇到 */ 就退出注释状态
    const int endPos = static_cast<int>(last.chars.find("*/"));
    CHECK(last.hl[last.cellOfCharByte(endPos)] == Hl::MlComment);
    const int after = static_cast<int>(last.chars.find("int"));
    CHECK(last.hl[last.cellOfCharByte(after)] == Hl::Keyword2); // 注释后面的代码正常着色

    // Python：# 是单行注释，且没有块注释
    const SyntaxRule *py = Syntax::forFile("x.py");
    CHECK(py != nullptr);
    Row pyRow;
    pyRow.chars = "def f(): # hi";
    pyRow.update();
    bool pyOpen = false;
    Syntax::highlightRow(pyRow, py, false, pyOpen);
    CHECK(pyRow.hl[0] == Hl::Keyword1);
    CHECK(pyRow.hl[pyRow.cellOfCharByte(static_cast<int>(pyRow.chars.find('#')))] == Hl::Comment);
    CHECK(!pyOpen);
}

/*
 * 大文档上的随机删除压力测试。
 *
 * 这条用例是专门为一个真实崩溃写的围栏：split 把片段劈成两半时若沿用原节点的
 * 优先级，整棵树的优先级会趋于全等，Treap 退化成一条链，而 split / merge /
 * destroy 都是递归的，链长到一万多就会栈溢出 —— 表现为「在长文件里连续删除
 * 删到一半，编辑器直接崩掉」，且 Windows 上只留一个退出码，没有任何提示。
 *
 * 判据不是断言某棵树有多深（那是实现细节），而是：跑完不崩，且文本始终与
 * std::string 参照模型一致。退化会先崩，所以这两条足以把 bug 钉住。
 */
void testPieceTableDeleteStress()
{
    constexpr int kDocChars = 200000;
    constexpr int kOps      = 20000;
    constexpr int kCheckEvery = 1000;

    std::string doc;
    doc.reserve(static_cast<size_t>(kDocChars) + kDocChars / 60 + 2);
    int col = 0;
    for (int i = 0; i < kDocChars; i++)
    {
        doc += static_cast<char>('a' + (i % 26));
        if (++col >= 60)
        {
            doc += '\n';
            col = 0;
        }
    }

    PieceTable pt(doc);
    std::string model = doc;
    std::mt19937 rng(20261008u);

    for (int i = 0; i < kOps; i++)
    {
        const int len = static_cast<int>(model.size());
        if (len == 0)
            break; // 规模固定，理论上到不了这里；留着是为了不出现除零取模
        const int p = static_cast<int>(rng() % static_cast<unsigned>(len));

        pt.remove(p, 1);
        model.erase(static_cast<size_t>(p), 1);

        // 全量比对是 O(n)，每次都做会把用例拖到几十秒；抽样够了。
        if (i % kCheckEvery == 0 || i + 1 == kOps)
        {
            CHECK(pt.totalChars() == static_cast<int>(model.size()));
            CHECK(pt.toString() == model);
        }
    }

    CHECK(pt.totalChars() == kDocChars + (kDocChars / 60) - kOps);
}

void testUnicodeAndVirtualLineRegression()
{
    const std::vector<std::string> units = {u8"中", u8"😀", u8"🚀", u8"🫠", u8"👍🏽",
                                          u8"👨‍👩‍👧‍👦", u8"🇨🇳", u8"❤️", u8"1️⃣"};
    for (const auto &unit : units)
    {
        Row row;
        row.chars = "a" + unit + "b";
        row.update();
        const int end = 1 + static_cast<int>(unit.size());
        CHECK_EQ(row.cellCount(), 3);
        CHECK_EQ(row.width(), 4);
        CHECK_EQ(row.nextBoundary(1), end);
        CHECK_EQ(row.prevBoundary(end), 1);
        CHECK_EQ(row.cellText(1), unit == u8"1️⃣" ? std::string("1 ") : unit);
        CHECK_EQ(Utf8::truncate(row.chars, 2), std::string("a"));
        CHECK_EQ(Utf8::truncate(row.chars, 3), "a" + unit);
        Buffer buffer;
        buffer.insertText(0, 0, row.chars);
        int x = end, y = 0;
        buffer.del(y, x, y);
        CHECK_EQ(buffer.toString(), std::string("ab"));
        CHECK(buffer.undo());
        CHECK_EQ(buffer.toString(), row.chars);
    }
    Buffer blank;
    int x = 0, y = 0;
    blank.insertNewline(y, x, x, y);
    CHECK_EQ(blank.toString(), std::string("\n"));
    CHECK_EQ(y, 1);
    blank.insertNewline(y, x, x, y);
    blank.insertText(y, x, u8"中文😀");
    CHECK_EQ(blank.toString(), std::string(u8"\n\n中文😀"));
    CHECK(checkViewConsistent(blank, "empty Enter"));
    for (const std::string doc : {"abc", "abc\n"})
    {
        Buffer b;
        b.fromText(doc);
        x = 0;
        y = 1;
        b.insertNewline(y, x, x, y);
        b.insertText(y, x, "X");
        CHECK_EQ(b.toString(), std::string("abc\n\nX"));
        CHECK(checkViewConsistent(b, "virtual Enter"));
        CHECK(b.undo());
        CHECK(b.undo());
        CHECK_EQ(b.toString(), doc);
    }
    CHECK_EQ(Utf8::seqLen(std::string("\xED\xA0\x80"), 0), 1);
    CHECK_EQ(Utf8::seqLen(std::string("\xF4\x90\x80\x80"), 0), 1);
}

} // namespace

int main()
{
    std::printf("=== kalo 逻辑层测试 ===\n\n");

    testPieceTableBasics();
    testPieceTableInsert();
    testPieceTableRemove();
    testPieceTableCoalesce();
    testPieceTableUndo();
    testPieceTableFuzz();
    testPieceTableScale();
    testPieceTableNoLeak();
    testBufferLoad();
    testBufferEdit();
    testBufferViewSync();
    testBufferUndo();
    testBufferFindForward();
    testBufferFindBackward();
    testBufferFindEdgeCases();
    testBufferLocateRoundTrip();
    testBufferFindAfterEdit();
    testUtf8Basics();
    testUtf8Boundaries();
    testRowWidthAndCells();
    testUtf8Backspace();
    testSyntaxHighlight();
    testSyntaxMultilineComment();
    testPieceTableDeleteStress();
    testUnicodeAndVirtualLineRegression();

    std::printf("\n=== 断言 %d 项，失败 %d 项 ===\n", g_checks, g_failed);
    if (g_failed == 0)
        std::printf("全部通过\n");

    return g_failed == 0 ? 0 : 1;
}
