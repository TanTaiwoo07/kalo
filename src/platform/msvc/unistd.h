#pragma once

/*
 * MSVC 专用的 <unistd.h> 替身。
 *
 * MinGW 自带完整的 <unistd.h>，用不着这份文件，所以它单独放在 platform/msvc 下，
 * 而不是和 termios.h 一起放在 platform/win32 —— 后者是 MinGW 与 MSVC 共用的目录，
 * 若把本文件塞进去，MinGW 构建（-I src/platform/win32 在前）就会误用到它。
 * 只有 MSVC 构建才把本目录加进 include 路径。
 *
 * 这里只补编辑器真正缺的几样。read / write / open / close 由 MSVC 的 <io.h>
 * 以 POSIX 别名形式提供，无需重复声明。
 */

#ifndef _MSC_VER
#error "src/platform/msvc 只应在 MSVC 构建时加入 include 路径；MinGW 请用系统自带的 <unistd.h>。"
#endif

#include <io.h>
#include <sys/types.h>

#include <errno.h>
#include <stddef.h>

#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#endif

/* MSVC 的 <errno.h> 里没有 EWOULDBLOCK */
#ifndef EWOULDBLOCK
#define EWOULDBLOCK EAGAIN
#endif

/*
 * MSVC 只有 _chsize / _chsize_s，没有 ftruncate。用它补上，语义一致：
 * 把文件截断到指定长度。用 _chsize_s 是为了支持超过 2GB 的长度参数
 * （_chsize 的长度参数是 32 位的 long）。
 */
static inline int ftruncate(int fd, long long length)
{
    return static_cast<int>(_chsize_s(fd, length));
}
