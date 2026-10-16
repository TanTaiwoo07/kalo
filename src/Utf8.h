#pragma once

#include <string>

/*
 * UTF-8 与显示列宽的最小工具集。
 *
 * 编辑器按字节存文本，但屏幕按「列」排版，两者不是一个东西：
 * 一个中文字符占 3 个字节，却在终端里占 2 列。早先 Row 只处理制表符，
 * 其余一律按 1 字节 = 1 列处理，于是带中文的行上光标会越走越偏。
 *
 * 这里只做三件事：切分 UTF-8 序列、判断东亚宽字符（2 列）、以及在不改变
 * 字节长度的前提下做 ASCII 大小写折叠（大小写不敏感搜索要靠它保住偏移语义）。
 * 不依赖任何 locale，也不碰 <cctype> —— 那类函数对本文件的入参域有要求，
 * 历史上已经炸过两次 Debug Assertion（详见 AGENTS.md）。
 */
namespace Utf8
{
/// s[i] 处一个 UTF-8 序列占几个字节。
///
/// 非法起始字节 / 截断序列一律按 1 字节处理：宁可显示错一个字符，
/// 也不能返回 0 让调用方的循环卡死，或者跳出去把后面的字节全判错。
int seqLen(const std::string &s, int i);

/// 解码 s[i] 处的码点；len 不为空时回填实际消耗字节数。
unsigned decode(const std::string &s, int i, int *len = nullptr);

/// 是否东亚宽字符（East Asian Width 为 W / F 的那批，终端里占 2 列）。
bool isWide(unsigned cp);

/// s[i] 处字符占的显示列数（宽字符 2，其余 1；控制字符编译期按 1 处理，
/// 因为 Row::update 会把它们渲染成 '?'）。
int charWidth(const std::string &s, int i);
// 按显示单元裁剪，避免状态栏截断中文或 emoji 的 UTF-8 字节。
std::string truncate(const std::string &s, int columns);

/// 回退一个显示单元；支持常见组合音标、emoji 修饰、国旗和 ZWJ 序列。
int prevBoundary(const std::string &s, int x);

/// 前进一个显示单元（非完整的 Unicode 字素分割实现）。
int nextBoundary(const std::string &s, int x);

/// ASCII 大小写折叠：只处理 A-Z，其余字节原样返回。
///
/// 关键是**字节长度不变**，所以原文偏移与折叠后的偏移一一对应 ——
/// 大小写不敏感搜索拿折叠串算出偏移，可以直接用回原文。
std::string foldCase(const std::string &s);
}
