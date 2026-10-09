#pragma once

#include <string>
#include <utility>
#include <vector>

// 片段来源：装载时的初始文本，或只增不减的追加缓冲区
enum PieceSource : int
{
    SOURCE_ORIGINAL = 0,
    SOURCE_ADD = 1,
};

// 撤销记录类型
constexpr int kUndoInsert = 1; // 该操作是一次插入
constexpr int kUndoRemove = 2; // 该操作是一次删除

struct PieceNode
{
    int source; // SOURCE_ORIGINAL / SOURCE_ADD
    int start;  // 在对应缓冲区中的起始下标
    int len;    // 片段长度

    int size; // 子树总字符数 = len + 左子树 + 右子树
    int prio; // 随机优先级
    PieceNode *lc;
    PieceNode *rc;

    PieceNode(int src, int st, int ln, int p)
        : source(src), start(st), len(ln), size(ln), prio(p), lc(nullptr), rc(nullptr) {}
};

// 分块表（Piece Table）：文本以「片段」为单位挂在 FHQ-Treap 上，
// 插入 / 删除 / 查询都是 O(log K)，K 为片段数而非文档长度。
//
// 两个缓冲区都只增不减：original_ 是装载时的初始文本，add_ 收集所有新写入的
// 字符。正因为它们不会被改写或回收，撤销才能安全地继续引用旧区间 —— 这也是
// 分块表做撤销格外省事的原因，代价是内存占用随编辑量单调增长。
class PieceTable
{
public:
    explicit PieceTable(const std::string &initText = std::string());
    ~PieceTable();

    PieceTable(const PieceTable &) = delete;
    PieceTable &operator=(const PieceTable &) = delete;

    void reset(const std::string &initText);

    void insert(int pos, const std::string &text);
    void remove(int pos, int len);
    std::string query(int pos, int len) const;

    bool undo();
    bool canUndo() const { return !history_.empty(); }

    int totalChars() const { return nodeSize(root_); }
    bool empty() const { return root_ == nullptr; }
    std::string toString() const;

    int pieceCount() const; // 片段数，用于验证「相邻插入合并」是否生效

private:
    // 撤销时记录被删片段用的轻量描述（不带树指针）
    struct Piece
    {
        int source;
        int start;
        int len;
    };

    struct UndoEntry
    {
        int type;                 // kUndoInsert / kUndoRemove
        int pos;                  // 操作发生的绝对位置
        int len;                  // type == kUndoInsert 时有效：插入的字符数
        std::vector<Piece> saved; // type == kUndoRemove 时有效：被删掉的片段
    };

    static int nodeSize(const PieceNode *t);
    static void pull(PieceNode *t);
    static std::pair<PieceNode *, PieceNode *> split(PieceNode *t, int k);
    static PieceNode *merge(PieceNode *L, PieceNode *R);
    static void destroy(PieceNode *t);

    // 按位置切掉一段字符；saved 非空时把被删片段一并记下（供撤销）
    void cut(int pos, int len, std::vector<Piece> *saved);

    // 若子树最右侧片段恰好是 add_ 的末尾，就把新文本并进该片段
    static bool extendRightmost(PieceNode *t, int addStart, int extra);

    void collect(const PieceNode *t, std::string &out) const;
    void collectRange(const PieceNode *t, int l, int r, std::string &out) const;
    void collectPieces(const PieceNode *t, std::vector<Piece> &out) const;
    int countPieces(const PieceNode *t) const;

    const std::string &bufferOf(int source) const
    {
        return source == SOURCE_ORIGINAL ? original_ : add_;
    }

    std::string original_;
    std::string add_;
    PieceNode *root_;
    std::vector<UndoEntry> history_;
};
