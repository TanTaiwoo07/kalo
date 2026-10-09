// kalo 性能基准。
//
// 存在的理由：说「分块表快」是空的，得有数字。这里拿 PieceTable 和
// std::string 跑同一组操作、同一串随机位置，把耗时摆在一起。
//
// 它不是测试，不判断对错，所以不注册进 ctest —— 基准耗时会随机器漂移，
// 拿它当门禁只会制造噪声。要跑就手动跑：
//
//   cmake --build build && ./build/kalo_bench
//
// 标签一律用 ASCII：printf 的宽度按字节算，中文按 UTF-8 占 3 字节会让列对不齐，
// 而这张表是要直接贴进 README 的。

#include "Buffer.h"
#include "PieceTable.h"

#include <chrono>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;

double elapsedMs(Clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// 规模：定在「能看出量级差异，又不至于让基准跑上一分钟」的位置。
constexpr int kDocChars  = 200000; // 文档基线字符数
constexpr int kOps       = 20000;  // 随机操作次数
constexpr int kTypeChars = 10000;  // 模拟打字的字符数
constexpr int kSearches  = 1000;   // 搜索次数
constexpr int kLineChars = 60;     // 生成文档时的行宽

std::string makeDocument()
{
    std::string s;
    s.reserve(static_cast<size_t>(kDocChars) + kDocChars / kLineChars + 2);
    int col = 0;
    for (int i = 0; i < kDocChars; i++)
    {
        s += static_cast<char>('a' + (i % 26));
        if (++col >= kLineChars)
        {
            s += '\n';
            col = 0;
        }
    }
    return s;
}

// 两个结构跑同一串位置才谈得上公平：种子固定、取模逻辑一致。
std::vector<int> makePositions(int count, unsigned seed)
{
    std::mt19937 rng(seed);
    std::vector<int> v;
    v.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; i++)
        v.push_back(static_cast<int>(rng() % (1u << 30)));
    return v;
}

void row(const char *label, double ptMs, double strMs)
{
    if (strMs > 0.0)
        std::printf("%-44s %10.1f %12.1f %9.2fx\n", label, ptMs, strMs, strMs / ptMs);
    else
        std::printf("%-44s %10.1f %12s %9s\n", label, ptMs, "-", "-");
}

} // namespace

int main()
{
    // stdout 设成无缓冲。输出被重定向到文件时默认是块缓冲，一旦后面某一项
    // 崩了，已经算好的结果会连着缓冲区一起丢掉，现场只剩一个退出码 ——
    // 这么踩过一次，排查代价太高。基准不差这点冲刷开销。
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    const std::string doc = makeDocument();
    const std::vector<int> pos = makePositions(kOps, 20261007u);

    std::printf("=== kalo benchmark ===\n");
    std::printf("doc: %d chars, line width %d; ops: %d\n\n",
                static_cast<int>(doc.size()), kLineChars, kOps);
    std::printf("%-44s %10s %12s %9s\n", "operation", "piecetable", "std::string", "ratio");
    std::printf("%-44s %10s %12s %9s\n", "---------", "ms", "ms", "string/pt");

    // 1. 顺序追加：模拟在文件末尾一路敲下去。分块表有「相邻插入合并」优化，
    //    std::string 是摊还 O(1)，两者都快，这一项主要看合并有没有真的生效。
    {
        PieceTable pt;
        const Clock::time_point t0 = Clock::now();
        for (int i = 0; i < kDocChars; i++)
            pt.insert(i, "x");
        const double ptMs = elapsedMs(t0);

        std::string s;
        s.reserve(static_cast<size_t>(kDocChars));
        const Clock::time_point t1 = Clock::now();
        for (int i = 0; i < kDocChars; i++)
            s += "x";
        const double strMs = elapsedMs(t1);

        row("append 200k chars sequentially", ptMs, strMs);
        std::printf("%-44s %10d\n", "  -> pieces after append", pt.pieceCount());
    }

    // 2. 随机插入：这是分块表的主场。std::string 每次插入都要把后半段整体搬走，
    //    平均搬半个文档；分块表只动 O(log n) 个片段。
    {
        PieceTable pt(doc);
        const Clock::time_point t0 = Clock::now();
        for (int i = 0; i < kOps; i++)
        {
            const int len = pt.totalChars();
            pt.insert(pos[i] % (len + 1), "Z");
        }
        const double ptMs = elapsedMs(t0);

        std::string s = doc;
        const Clock::time_point t1 = Clock::now();
        for (int i = 0; i < kOps; i++)
            s.insert(static_cast<size_t>(pos[i]) % (s.size() + 1), 1, 'Z');
        const double strMs = elapsedMs(t1);

        row("insert 20k chars at random offsets", ptMs, strMs);
    }

    // 3. 随机删除：std::string::erase 同样要搬后半段。
    {
        PieceTable pt(doc);
        const Clock::time_point t0 = Clock::now();
        for (int i = 0; i < kOps; i++)
        {
            const int len = pt.totalChars();
            if (len == 0)
                break;
            pt.remove(pos[i] % len, 1);
        }
        const double ptMs = elapsedMs(t0);

        std::string s = doc;
        const Clock::time_point t1 = Clock::now();
        for (int i = 0; i < kOps; i++)
        {
            if (s.empty())
                break;
            s.erase(static_cast<size_t>(pos[i]) % s.size(), 1);
        }
        const double strMs = elapsedMs(t1);

        row("delete 20k chars at random offsets", ptMs, strMs);
    }

    // 4. 全量序列化：分块表的代价之一。保存文件、搜索都要先拼出整串，
    //    std::string 本来就是连续内存，这一步几乎免费。
    //
    //    跑 100 次而不是 1 次：单次计时会被首次的大块堆分配污染
    //    （实测第一次约 100us，稳定态只有 3us 左右，差 30 倍），
    //    拿单次冷启动当结论会严重高估这一项。
    {
        constexpr int kReps = 100;
        PieceTable pt(doc);
        size_t chars = 0;
        const Clock::time_point t0 = Clock::now();
        for (int i = 0; i < kReps; i++)
            chars = pt.toString().size();
        const double ptMs = elapsedMs(t0);
        std::printf("%-44s %10.2f %12s %9s\n", "toString 100x (200k chars each)", ptMs, "-", "-");
        std::printf("%-44s %10.3f\n", "  -> ms per call", ptMs / kReps);
        std::printf("%-44s %10d\n", "  -> chars out", static_cast<int>(chars));
    }

    // 5. 搜索：走的是权威文本，每次都要先序列化再 find —— 这一项暴露的正是
    //    「每帧全表查询」的代价（Editor 里已经改成进入搜索时取一次快照）。
    {
        Buffer buf;
        buf.fromText(doc);
        const Clock::time_point t0 = Clock::now();
        for (int i = 0; i < kSearches; i++)
            buf.findText("abcdef", (i * 37) % kDocChars, true);
        const double ptMs = elapsedMs(t0);
        row("search 1000x (each re-serializes doc)", ptMs, 0.0);
    }

    // 6. 撤销：分块表撤销只是把片段挂回去，不搬字符。
    {
        PieceTable pt(doc);
        for (int i = 0; i < kOps; i++)
        {
            const int len = pt.totalChars();
            pt.insert(pos[i] % (len + 1), "Z");
        }
        const Clock::time_point t0 = Clock::now();
        int undone = 0;
        while (pt.undo())
            undone++;
        const double ptMs = elapsedMs(t0);
        row("undo 20k inserts back to empty", ptMs, 0.0);
        std::printf("%-44s %10d\n", "  -> undo steps", undone);
    }

    // 7. Buffer 层逐字符输入（真实打字路径）：这一项通常最慢，因为它带着
    //    行视图一起走 —— 每敲一个字符都要把当前行的渲染信息重算一遍，
    //    单行越长越吃亏。这是把「存储层」和「编辑器层」分开量的意义所在。
    {
        // 初始文本不能是空串：fromText("") 之后 rows 是空的，而 Buffer::insert
        // 直接按下标取 rows[y]，越界即未定义行为（基准第一版就栽在这里）。
        // 真实编辑器打开空文件也会给出一行，这里同样先给一行。
        Buffer buf;
        buf.fromText("a");
        const Clock::time_point t0 = Clock::now();
        for (int i = 0; i < kTypeChars; i++)
            buf.insert(0, i + 1, static_cast<char>('a' + (i % 26)));
        const double ptMs = elapsedMs(t0);
        row("Buffer: type 10k chars into one line", ptMs, 0.0);
    }

    std::printf("\nnote: ratio > 1 means PieceTable is faster by that factor.\n");
    return 0;
}
