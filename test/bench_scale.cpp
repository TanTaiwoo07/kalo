// 规模基准：测「打开一个 10 MB 文件」的完整链路，以及插入/删除随文档规模的变化。
//
// 与 bench.cpp 的分工：那边测 PieceTable 各操作与 std::string 的逐项对比，
// 这边只回答简历里要写的三个问题 ——
//   1. 打开 10 MB 文件要多久（拆成读盘 / 存储 / 行视图三段）
//   2. 插入删除是不是真的 O(log n)（用 1 / 5 / 10 MB 三档看增长曲线）
//   3. 打开后占多少内存
//
// 不注册进 ctest：耗时随机器漂移，当门禁只会制造噪声。

#include "Buffer.h"
#include "PieceTable.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

using Clock = std::chrono::steady_clock;

static double msSince(const Clock::time_point &t0)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// 中位数：首次大块堆分配会把单次计时抬高一个数量级，取中位数避开它。
static double median(std::vector<double> v)
{
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

static size_t workingSetKB()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return static_cast<size_t>(pmc.WorkingSetSize) / 1024;
#endif
    return 0;
}

// 生成接近真实源码的样本：整行、带缩进、含中文注释，避免全是单字符行
static std::string makeText(size_t targetBytes)
{
    static const char *kLines[] = {
        "    const int len = static_cast<int>(buffer.size());\n",
        "    for (int i = 0; i < count; ++i) { total += values[i]; }\n",
        "    // 分块表把文本拆成片段，每段记录自己在缓冲区里的位置\n",
        "    if (!node) return 0;\n",
        "    std::string result; result.reserve(expected);\n",
        "    switch (kind) { case 1: return \"one\"; default: break; }\n",
    };
    const size_t n = sizeof(kLines) / sizeof(kLines[0]);

    std::string s;
    s.reserve(targetBytes + 256);
    for (size_t i = 0; s.size() < targetBytes; ++i)
        s += kLines[i % n];
    s.resize(targetBytes);
    if (!s.empty() && s.back() != '\n')
        s.back() = '\n';
    return s;
}

static size_t countLines(const std::string &s)
{
    size_t n = 0;
    for (char c : s)
        if (c == '\n')
            ++n;
    return n;
}

// 在指定规模的文本上做 n 次随机插入，返回耗时
static double timeInserts(const std::string &text, int n, unsigned seed)
{
    PieceTable pt(text);
    const int len0 = pt.totalChars();
    unsigned st = seed;
    std::vector<int> pos(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
    {
        st = st * 1103515245u + 12345u;
        pos[static_cast<size_t>(i)] = static_cast<int>((st >> 8) % static_cast<unsigned>(len0));
    }

    const Clock::time_point t0 = Clock::now();
    for (int i = 0; i < n; ++i)
        pt.insert(pos[static_cast<size_t>(i)] + i, "x");
    return msSince(t0);
}

static double timeRemoves(const std::string &text, int n, unsigned seed)
{
    PieceTable pt(text);
    unsigned st = seed;
    const Clock::time_point t0 = Clock::now();
    for (int i = 0; i < n; ++i)
    {
        const int cur = pt.totalChars();
        if (cur <= 0)
            break;
        st = st * 1103515245u + 12345u;
        pt.remove(static_cast<int>((st >> 8) % static_cast<unsigned>(cur)), 1);
    }
    return msSince(t0);
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    const size_t MB = 1024 * 1024;
    const size_t kSize = 10 * MB;

    std::printf("=== kalo 规模基准 ===\n");
    std::printf("编译器: ");
#if defined(__clang__)
    std::printf("Clang %d.%d\n", __clang_major__, __clang_minor__);
#elif defined(_MSC_VER)
    std::printf("MSVC %d\n", _MSC_VER);
#elif defined(__GNUC__)
    std::printf("GCC %d.%d\n", __GNUC__, __GNUC_MINOR__);
#else
    std::printf("unknown\n");
#endif

    const std::string text = makeText(kSize);
    const size_t lines = countLines(text);
    std::printf("样本: %.2f MB, %zu 行, 平均行长 %.0f 字节\n\n",
                static_cast<double>(text.size()) / MB, lines,
                static_cast<double>(text.size()) / static_cast<double>(lines));

    // ---------- 1. 打开 10 MB 文件，拆成三段 ----------
    const char *path = "bench_scale_sample.txt";
    {
        std::FILE *f = std::fopen(path, "wb");
        if (!f)
        {
            std::printf("无法写样本文件\n");
            return 1;
        }
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
    }

    std::vector<double> readV, ptV, viewV, fullV;
    for (int r = 0; r < 5; ++r)
    {
        // (a) 读盘
        Clock::time_point t0 = Clock::now();
        std::string fromDisk;
        {
            std::FILE *f = std::fopen(path, "rb");
            std::fseek(f, 0, SEEK_END);
            const long sz = std::ftell(f);
            std::fseek(f, 0, SEEK_SET);
            fromDisk.resize(static_cast<size_t>(sz));
            std::fread(&fromDisk[0], 1, static_cast<size_t>(sz), f);
            std::fclose(f);
        }
        readV.push_back(msSince(t0));

        // (b) 只建存储层
        t0 = Clock::now();
        PieceTable pt(fromDisk);
        ptV.push_back(msSince(t0));
        if (pt.totalChars() != static_cast<int>(fromDisk.size()))
            std::printf("!! 字符数不符\n");

        // (c) 完整装载：存储 + 行视图
        t0 = Clock::now();
        Buffer buf;
        buf.fromText(fromDisk);
        fullV.push_back(msSince(t0));

        // 行视图单独用时 = 完整 - 存储
        viewV.push_back(fullV.back() - ptV.back());

        if (buf.size() != static_cast<int>(lines))
            std::printf("!! 行数不符: %d vs %zu\n", buf.size(), lines);
    }

    const double readMs = median(readV);
    const double ptMs = median(ptV);
    const double viewMs = median(viewV);
    const double fullMs = median(fullV);

    std::printf("---------- 打开 %.0f MB 文件 ----------\n", static_cast<double>(kSize) / MB);
    std::printf("  %-28s %8.1f ms\n", "读盘（fread）", readMs);
    std::printf("  %-28s %8.1f ms\n", "构建 PieceTable", ptMs);
    std::printf("  %-28s %8.1f ms\n", "构建行视图 rows", viewMs);
    std::printf("  %-28s %8.1f ms   <-- 完整打开\n", "合计", fullMs);
    std::printf("  %-28s %8.1f MB/s\n", "等效吞吐",
                static_cast<double>(kSize) / MB / (fullMs / 1000.0));
    std::printf("\n");

    // ---------- 2. 内存占用 ----------
    const size_t baseKB = workingSetKB();
    Buffer big;
    big.fromText(text);
    const size_t afterKB = workingSetKB();
    std::printf("---------- 内存 ----------\n");
    std::printf("  %-28s %8.1f MB\n", "装载前工作集", static_cast<double>(baseKB) / 1024);
    std::printf("  %-28s %8.1f MB\n", "装载后工作集", static_cast<double>(afterKB) / 1024);
    std::printf("  %-28s %8.1f MB  (%.2fx 原文)\n", "增量",
                static_cast<double>(afterKB - baseKB) / 1024,
                static_cast<double>(afterKB - baseKB) / 1024.0 /
                    (static_cast<double>(kSize) / MB));
    std::printf("\n");

    // ---------- 3. 插入/删除随规模的变化 ----------
    std::printf("---------- 插入 20000 次（随机位置）----------\n");
    const size_t sizes[] = {1 * MB, 5 * MB, 10 * MB};
    double prevIns = 0.0;
    for (size_t i = 0; i < 3; ++i)
    {
        const std::string t = (i == 2) ? text : makeText(sizes[i]);
        double best = 1e18;
        for (int r = 0; r < 3; ++r)
            best = std::min(best, timeInserts(t, 20000, 12345u + static_cast<unsigned>(r)));
        std::printf("  %-24s %8.1f ms", (std::to_string(sizes[i] / MB) + " MB 文档").c_str(), best);
        if (i > 0 && prevIns > 0.0)
            std::printf("   (vs 上一档 x%.2f)", best / prevIns);
        std::printf("\n");
        prevIns = best;
    }

    std::printf("\n---------- 删除 20000 次（随机位置）----------\n");
    double prevDel = 0.0;
    for (size_t i = 0; i < 3; ++i)
    {
        const std::string t = (i == 2) ? text : makeText(sizes[i]);
        double best = 1e18;
        for (int r = 0; r < 3; ++r)
            best = std::min(best, timeRemoves(t, 20000, 12345u + static_cast<unsigned>(r)));
        std::printf("  %-24s %8.1f ms", (std::to_string(sizes[i] / MB) + " MB 文档").c_str(), best);
        if (i > 0 && prevDel > 0.0)
            std::printf("   (vs 上一档 x%.2f)", best / prevDel);
        std::printf("\n");
        prevDel = best;
    }

    // ---------- 4. 与 std::string 的对照（10 MB 规模）----------
    std::printf("\n---------- 10 MB 文档上 20000 次随机操作：对照 std::string ----------\n");
    {
        std::string s = text;
        const int len0 = static_cast<int>(s.size());
        unsigned st = 999u;
        std::vector<int> pos(20000);
        for (int i = 0; i < 20000; ++i)
        {
            st = st * 1103515245u + 12345u;
            pos[static_cast<size_t>(i)] = static_cast<int>((st >> 8) % static_cast<unsigned>(len0));
        }

        const Clock::time_point t0 = Clock::now();
        for (int i = 0; i < 20000; ++i)
            s.insert(static_cast<size_t>(pos[static_cast<size_t>(i)] + i), 1, 'x');
        const double strMs = msSince(t0);

        PieceTable pt(text);
        const Clock::time_point t1 = Clock::now();
        for (int i = 0; i < 20000; ++i)
            pt.insert(pos[static_cast<size_t>(i)] + i, "x");
        const double ptI = msSince(t1);

        std::printf("  %-28s %8.1f ms\n", "std::string 插入", strMs);
        std::printf("  %-28s %8.1f ms\n", "PieceTable 插入", ptI);
        std::printf("  %-28s %8.1fx\n", "加速比", strMs / std::max(ptI, 1e-9));
    }

    std::remove(path);
    std::printf("\n完成。\n");
    return 0;
}