#include "PieceTable.h"

#include <algorithm>
#include <random>

namespace
{
// 优先级来源。旧实现里在头文件定义了 std::mt19937 rnd(233) 却没人调用，节点构造
// 反而用的是从未播种的 rand()，于是每次运行的优先级序列完全相同，Treap 会退化成
// 一条链。这里换成真正生效的 mt19937，固定种子以便失败用例可复现。
int nextPriority()
{
    static std::mt19937 gen(233);
    static std::uniform_int_distribution<int> dist(0, 0x7fffffff);
    return dist(gen);
}
} // namespace

// 手搓（原名 getSize，是自由函数；后来改成静态成员并统一命名）。
// 空节点按 0 处理，于是所有遍历都不必先判空。
int PieceTable::nodeSize(const PieceNode *t)
{
    return t ? t->size : 0;
}

// 手搓（原名 pull，一字未改）。重算子树字符总数。
// 这是 Treap 里唯一需要维护的聚合信息：size = 本片段长度 + 左右子树。
void PieceTable::pull(PieceNode *t)
{
    if (t)
        t->size = t->len + nodeSize(t->lc) + nodeSize(t->rc);
}

PieceTable::PieceTable(const std::string &initText) : root_(nullptr)
{
    reset(initText);
}

PieceTable::~PieceTable()
{
    destroy(root_);
}

void PieceTable::reset(const std::string &initText)
{
    destroy(root_);
    root_ = nullptr;

    original_ = initText;
    add_.clear();
    history_.clear();

    if (!initText.empty())
        root_ = new PieceNode(SOURCE_ORIGINAL, 0, static_cast<int>(initText.size()), nextPriority());
}

// ==================== FHQ-Treap 核心 ====================

// 按字符数切分：左树含前 k 个字符，右树含其余字符
std::pair<PieceNode *, PieceNode *> PieceTable::split(PieceNode *t, int k)
{
    if (!t)
        return std::make_pair(nullptr, nullptr);

    const int leftSize = nodeSize(t->lc);

    if (k <= leftSize)
    {
        std::pair<PieceNode *, PieceNode *> parts = split(t->lc, k);
        t->lc = parts.second;
        pull(t);
        return std::make_pair(parts.first, t);
    }

    if (k < leftSize + t->len)
    {
        // 切点落在本片段内部，需要把它劈成两半。
        //
        // 两半都必须取【新的】随机优先级，绝不能沿用 t->prio。原因很实在：
        // 装载大文件时整篇文本只有一个片段，若每次劈开都继承同一个优先级，
        // 那么删得越多，共享同一优先级的节点就越多 —— 极端情况下整棵树所有节点
        // 优先级全等，merge 时永远走同一个分支，Treap 会退化成一条链。
        // 而 split / merge / destroy 都是递归的，链长到一万多就会栈溢出直接崩
        // （实测：20 万字符的文档随机删到约 1.6 万次时崩溃）。
        //
        // 代价是堆序可能被轻微破坏 —— 新优先级未必不小于原子树根的优先级。
        // 但堆序只影响平衡性，不影响正确性：split / merge 始终维护中序序列，
        // 文本内容不会因为优先级大小而错。两害相权，随机性远比堆序重要。
        const int off = k - leftSize;
        PieceNode *leftPart = new PieceNode(t->source, t->start, off, nextPriority());
        PieceNode *rightPart = new PieceNode(t->source, t->start + off, t->len - off, nextPriority());

        leftPart->lc = t->lc;
        rightPart->rc = t->rc;
        pull(leftPart);
        pull(rightPart);

        delete t;
        return std::make_pair(leftPart, rightPart);
    }

    std::pair<PieceNode *, PieceNode *> parts = split(t->rc, k - leftSize - t->len);
    t->rc = parts.first;
    pull(t);
    return std::make_pair(t, parts.second);
}

// 合并两棵树，要求左树内容整体位于右树之前
PieceNode *PieceTable::merge(PieceNode *L, PieceNode *R)
{
    if (!L)
        return R;
    if (!R)
        return L;

    if (L->prio > R->prio)
    {
        L->rc = merge(L->rc, R);
        pull(L);
        return L;
    }

    R->lc = merge(L, R->lc);
    pull(R);
    return R;
}

void PieceTable::destroy(PieceNode *t)
{
    if (!t)
        return;
    destroy(t->lc);
    destroy(t->rc);
    delete t;
}

bool PieceTable::extendRightmost(PieceNode *t, int addStart, int extra)
{
    if (!t)
        return false;

    if (t->rc)
    {
        if (extendRightmost(t->rc, addStart, extra))
        {
            pull(t);
            return true;
        }
        return false;
    }

    // t 已是子树最右片段，也就是紧挨着插入位置的前一片段
    if (t->source == SOURCE_ADD && t->start + t->len == addStart)
    {
        t->len += extra;
        pull(t);
        return true;
    }

    return false;
}

// ==================== 编辑操作 ====================

void PieceTable::insert(int pos, const std::string &text)
{
    if (text.empty())
        return;

    pos = std::clamp(pos, 0, totalChars());

    const int addStart = static_cast<int>(add_.size());
    const int len = static_cast<int>(text.size());
    add_ += text;

    std::pair<PieceNode *, PieceNode *> parts = split(root_, pos);
    PieceNode *L = parts.first;
    PieceNode *R = parts.second;

    if (extendRightmost(L, addStart, len))
    {
        // 新文本正好接在左树最右片段之后，直接延长它。连续输入时正是靠这一步
        // 把「每按一次键新增一个节点」压下去，让片段数保持稳定。
        root_ = merge(L, R);
    }
    else
    {
        PieceNode *mid = new PieceNode(SOURCE_ADD, addStart, len, nextPriority());
        root_ = merge(merge(L, mid), R);
    }

    history_.push_back(UndoEntry{kUndoInsert, pos, len, std::vector<Piece>()});
}

void PieceTable::remove(int pos, int len)
{
    std::vector<Piece> saved;
    cut(pos, len, &saved);

    if (saved.empty())
        return; // 没有任何字符被删除，不记入历史

    history_.push_back(UndoEntry{kUndoRemove, std::max(pos, 0), 0, std::move(saved)});
}

void PieceTable::cut(int pos, int len, std::vector<Piece> *saved)
{
    if (len <= 0)
        return;

    const int total = totalChars();

    if (pos < 0)
    {
        len += pos;
        pos = 0;
    }
    if (len <= 0 || pos >= total)
        return;
    if (pos + len > total)
        len = total - pos;

    std::pair<PieceNode *, PieceNode *> first = split(root_, pos);
    PieceNode *L = first.first;
    PieceNode *mid = first.second;

    std::pair<PieceNode *, PieceNode *> second = split(mid, len);
    PieceNode *target = second.first;
    PieceNode *R = second.second;

    if (saved)
        collectPieces(target, *saved);

    destroy(target);
    root_ = merge(L, R);
}

bool PieceTable::undo()
{
    if (history_.empty())
        return false;

    UndoEntry entry = std::move(history_.back());
    history_.pop_back();

    if (entry.type == kUndoInsert)
    {
        // 撤销插入 = 把当初插进来的那些字符再删掉
        cut(entry.pos, entry.len, nullptr);
        return true;
    }

    // 撤销删除 = 把当初删掉的片段原样接回去；缓冲区只增不减，
    // 所以 saved 里的区间依然有效。
    PieceNode *mid = nullptr;
    for (const Piece &p : entry.saved)
        mid = merge(mid, new PieceNode(p.source, p.start, p.len, nextPriority()));

    if (mid)
    {
        const int pos = std::clamp(entry.pos, 0, totalChars());
        std::pair<PieceNode *, PieceNode *> parts = split(root_, pos);
        root_ = merge(merge(parts.first, mid), parts.second);
    }

    return true;
}

// ==================== 读取 ====================

std::string PieceTable::query(int pos, int len) const
{
    std::string out;

    if (pos < 0)
    {
        len += pos;
        pos = 0;
    }

    const int total = totalChars();
    if (len <= 0 || pos >= total)
        return out;
    if (pos + len > total)
        len = total - pos;

    out.reserve(static_cast<size_t>(len));
    collectRange(root_, pos, pos + len, out);
    return out;
}

std::string PieceTable::toString() const
{
    std::string out;
    out.reserve(static_cast<size_t>(totalChars()));
    collect(root_, out);
    return out;
}

int PieceTable::pieceCount() const
{
    return countPieces(root_);
}

void PieceTable::collect(const PieceNode *t, std::string &out) const
{
    if (!t)
        return;

    collect(t->lc, out);
    out.append(bufferOf(t->source), static_cast<size_t>(t->start), static_cast<size_t>(t->len));
    collect(t->rc, out);
}

// 只取子树中相对下标 [l, r) 的区间，沿途剪掉不相交的子树
void PieceTable::collectRange(const PieceNode *t, int l, int r, std::string &out) const
{
    if (!t || l >= r)
        return;

    const int nodeBegin = nodeSize(t->lc);
    const int nodeEnd = nodeBegin + t->len;

    if (l < nodeBegin)
        collectRange(t->lc, l, std::min(r, nodeBegin), out);

    const int from = std::max(l, nodeBegin);
    const int to = std::min(r, nodeEnd);
    if (from < to)
        out.append(bufferOf(t->source), static_cast<size_t>(t->start + (from - nodeBegin)),
                   static_cast<size_t>(to - from));

    if (r > nodeEnd)
        collectRange(t->rc, std::max(l, nodeEnd) - nodeEnd, r - nodeEnd, out);
}

void PieceTable::collectPieces(const PieceNode *t, std::vector<Piece> &out) const
{
    if (!t)
        return;

    collectPieces(t->lc, out);
    out.push_back(Piece{t->source, t->start, t->len});
    collectPieces(t->rc, out);
}

int PieceTable::countPieces(const PieceNode *t) const
{
    if (!t)
        return 0;
    return 1 + countPieces(t->lc) + countPieces(t->rc);
}
