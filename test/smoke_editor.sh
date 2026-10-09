#!/usr/bin/env bash
#
# kalo 编辑器的端到端冒烟测试。
#
# 做法是把按键序列直接喂给编辑器的标准输入，让它自己走完
# 「按键 → 编辑文本 → 保存到磁盘」，最后核对文件内容。
#
# 之所以能用管道驱动，是因为 kalo 在标准输入不是终端时会跳过裸模式，并且把
# Ctrl-Z 这类控制字符原样交给按键解析层（见 src/Terminal.cpp 里的说明）。
#
# 用法：
#   test/smoke_editor.sh [kalo 可执行文件路径]
# 不给路径时会在常见位置里找。
#
# 依赖 bash 的 printf 解释 \xHH 转义。timeout 命令则是可选的：Linux 自带，
# macOS 默认没有（要装 coreutils）。没有它也能跑 —— 每个用例都以明确的退出按键收尾，
# timeout 只是防止万一卡住时把 CI 挂死，缺了它最多是失去这层保险。

set -u

if command -v timeout >/dev/null 2>&1; then
    TIMEOUT="timeout 10"
else
    TIMEOUT=""
fi

BIN="${1:-}"
if [ -z "$BIN" ]; then
    for cand in ./build/kalo ./build/kalo.exe ./kalo ./kalo.exe ../kalo.exe ./kalo/build/kalo.exe; do
        if [ -x "$cand" ]; then BIN="$cand"; break; fi
    done
fi

if [ -z "$BIN" ] || [ ! -x "$BIN" ]; then
    echo "找不到 kalo 可执行文件；请把它的路径作为第一个参数传进来。" >&2
    exit 2
fi

# 用相对路径做临时目录，避免个别环境下绝对路径触发的清理限制；每个用例用独立
# 文件名，这样即使删不掉旧文件也不会互相污染。
TMP="./.kalo_smoke_$$"
mkdir -p "$TMP" || { echo "无法创建临时目录 $TMP" >&2; exit 2; }

pass=0
fail=0
total=0

# check <名称> <按键序列> <期望落盘内容>
#
# 按键序列会当作 printf 的格式串使用，所以里面不要出现 % 字符。
check() {
    local name="$1" keys="$2" expected="$3"
    total=$((total + 1))

    local out="$TMP/case$total.txt" # 每例一个文件名，无需先删
    printf "$keys" | ${TIMEOUT} "$BIN" "$out" >/dev/null 2>/dev/null
    local code=$?

    if [ "$code" -eq 124 ]; then
        echo "  [FAIL] $name —— 编辑器超时未退出（疑似读输入时卡住）"
        fail=$((fail + 1))
        return
    fi
    if [ "$code" -ne 0 ]; then
        echo "  [FAIL] $name —— 退出码 $code"
        fail=$((fail + 1))
        return
    fi

    local actual=''
    [ -f "$out" ] && actual="$(cat "$out")"

    if [ "$actual" != "$expected" ]; then
        echo "  [FAIL] $name"
        printf '         期望: %q\n' "$expected"
        printf '         实际: %q\n' "$actual"
        fail=$((fail + 1))
        return
    fi

    # 期望为空时，进一步确认编辑器确实没有生成文件
    if [ -z "$expected" ] && [ -e "$out" ]; then
        echo "  [FAIL] $name —— 期望不产生文件，但 $out 存在"
        fail=$((fail + 1))
        return
    fi

    echo "  [ ok ] $name"
    pass=$((pass + 1))
}

echo "kalo 端到端冒烟测试（${BIN}）"
echo

# 按键速记：\x13 = Ctrl-S 保存，\x11 = Ctrl-Q 退出，\x1a = Ctrl-Z 撤销
#           \r = Enter，\x7f = 退格

check "输入 abc 后保存"           'abc\x13\x11'             'abc'
check "多行：ab 回车 cd"          'ab\rcd\x13\x11'          "$(printf 'ab\ncd')"
check "退格删掉最后一个字符"       'abc\x7f\x13\x11'         'ab'
check "方向键上移后插入"           'ab\rcd\x1b[AX\x13\x11'   "$(printf 'abX\ncd')"
check "方向键下移到虚拟末行后插入" 'ab\rcd\x1b[BY\x13\x11'   "$(printf 'ab\ncd\nY')"
check "Home 回到行首后插入"        'ab\x1b[HZ\x13\x11'       'Zab'
check "End 跳到行尾后插入"         'ab\x1b[H\x1b[FZ\x13\x11' 'abZ'
check "Delete 删除光标处字符"      'ab\x1b[H\x1b[3~\x13\x11' 'b'
check "撤销 Ctrl-Z"                'abZ\x1a\x13\x11'         'ab'
check "连续撤销两次"               'abXY\x1a\x1a\x13\x11'    'ab'
check "撤销之后继续编辑"           'abZ\x1aC\x13\x11'        'abC'
check "未知功能键不残留字符（F5）"  'ab\x1b[15~Z\x13\x11'     'abZ'
check "PageDown 后插入"            'ab\x1b[6~Z\x13\x11'      'abZ'
check "空缓冲直接退出"             '\x11'                    ''

# Ctrl-F 搜索：\x06 = Ctrl-F，\r 确认，\x1b[B 下一个（ArrowDown）。
# 用例做成「搜索命中 → 在命中处插入字符 → 保存」，用落盘内容证明光标确实跳到了命中处。
check "搜索命中后原地插入"         'one two one two\x06two\rX\x13\x11'       'one Xtwo one two'
check "搜索后按下一个命中"         'one two one two\x06two\x1b[B\rX\x13\x11' 'one two one Xtwo'
# Tab（\x09）切换大小写：忽略大小写后能命中大小写不同的那一次
check "搜索忽略大小写命中"         'One ONE two\x06one\x09\rX\x13\x11'       'XOne ONE two'

# UTF-8：中文按字符处理 —— 方向键跳过整个字符，退格一次删掉整个字。
# 用 utf8 字节序列直接喂进去，落盘内容必须是完整汉字而不是半个序列。
check "中文输入后保存"             'a\xe4\xb8\xadb\x13\x11'                  "$(printf 'a\xe4\xb8\xadb')"
check "退格删掉整个汉字"           'a\xe4\xb8\xad\x7f\x13\x11'               'a'
check "左方向键跨过整个汉字"        'a\xe4\xb8\xad\x1b[DX\x13\x11'            "$(printf 'aX\xe4\xb8\xad')"

check "空文件先回车" '\rX\x13\x11' "$(printf '\nX')"
check "虚拟末行回车不移动上一行" 'abc\x1b[B\rX\x13\x11' "$(printf 'abc\n\nX')"
check "连续空行后输入" 'abc\r\rX\x13\x11' "$(printf 'abc\n\nX')"
check "emoji 原样保存" '中文😀🚀🫠\x13\x11' '中文😀🚀🫠'
check "组合 emoji 整体退格" 'A👨‍👩‍👧‍👦\x7fB\x13\x11' 'AB'
check "肤色 emoji 整体移动" 'A👍🏽B\x1b[D\x1b[DX\x13\x11' 'AX👍🏽B'
check "中文整字撤销" 'A中\x1a\x13\x11' 'A'
check "中文搜索" '甲乙\x06乙\rX\x13\x11' '甲X乙'
check "emoji 搜索" 'A😀B\x06😀\rX\x13\x11' 'AX😀B'
check "中文搜索退格" '甲乙\x06甲乙\x7f\rX\x13\x11' 'X甲乙'
check "上下移动不切断中文" '中文\r1234\x1b[AX\x13\x11' "$(printf '中X文\n1234')"
check "LF 回车" 'a\nb\x13\x11' "$(printf 'a\nb')"
check "鼠标点击 ASCII" 'abcd\x1b[<0;3;1MX\x13\x11' 'abXcd'
check "鼠标点击中文右半格" '中文\x1b[<0;2;1MX\x13\x11' 'X中文'
check "鼠标点击 emoji" 'A😀B\x1b[<0;3;1MX\x13\x11' 'AX😀B'
check "鼠标点击键帽编号" '1️⃣中文\x1b[<0;3;1MX\x13\x11' '1️⃣X中文'
check "鼠标点击空白至行尾" 'abc\x1b[<0;20;1MX\x13\x11' 'abcX'
check "鼠标点击第二行" 'abc\rdef\x1b[<0;2;2MX\x13\x11' "$(printf 'abc\ndXef')"
check "鼠标释放和右键不插入协议文本" 'abc\x1b[<0;1;1m\x1b[<2;1;1MX\x13\x11' 'abcX'
check "鼠标点击状态栏不移动" 'abc\x1b[<0;1;23MX\x13\x11' 'abcX'
check "鼠标报告允许长坐标" 'abc\x1b[<0;12345;12345MX\x13\x11' 'abcX'
check "鼠标点击制表符内部" 'a\tb\x1b[<0;3;1MX\x13\x11' "$(printf 'aX\tb')"
check "鼠标点击虚拟末行" 'abc\x1b[<0;1;2MX\x13\x11' "$(printf 'abc\nX')"
check "鼠标非法坐标不混入正文" 'abc\x1b[<0;0;1M\x1b[<0;9999999999;1MX\x13\x11' 'abcX'

long_text=''
for ((i=0; i<90; i++)); do long_text="${long_text}a"; done
# 输入 90 个字符后水平偏移为 11；点屏幕第 2 列对应原文偏移 12。
check "鼠标水平滚动坐标" "${long_text}\x1b[<0;2;1MX\x13\x11" "${long_text:0:12}X${long_text:12}"
many_lines=''
for ((i=0; i<25; i++)); do many_lines="${many_lines}a\r"; done
expected_lines=''
for ((i=0; i<25; i++)); do
    if [ "$i" -eq 4 ]; then expected_lines="${expected_lines}Xa\n";
    else expected_lines="${expected_lines}a\n"; fi
done
# 24 行终端中的文本区为 22 行，光标第 26 行时 rowoff=4。
check "鼠标垂直滚动坐标" "${many_lines}\x1b[<0;1;1MX\x13\x11" "$(printf "$expected_lines")"

# 检查渲染字节，而不仅是存盘：编号回退显示，但原文保持不变。
total=$((total + 1))
printf '1️⃣ 2️⃣ 3️⃣\x13\x11' | ${TIMEOUT} "$BIN" "$TMP/keycaps.txt" > "$TMP/keycaps.out" 2>/dev/null
if [ $? -eq 0 ] && grep -aq '1  ' "$TMP/keycaps.out" &&
   ! grep -aq '1️⃣' "$TMP/keycaps.out" && grep -q '1️⃣ 2️⃣ 3️⃣' "$TMP/keycaps.txt"; then
    pass=$((pass + 1))
else
    echo '  [FAIL] 键帽显示回退且原文保留'
    fail=$((fail + 1))
fi

# 回归用例：不带文件名启动，按 Ctrl-S 会进入 "Save as" 提示行。
# 提示行如果不处理「输入流结束」，getkey 会一直返回 Eof，进程就空转到永远 ——
# 这个坑真的踩过一次，所以单独钉一条用例（判据是「能在超时前退出」）。
total=$((total + 1))
printf '\x13' | ${TIMEOUT} "$BIN" >/dev/null 2>&1
if [ $? -eq 124 ]; then
    echo "  [FAIL] 保存提示行遇到输入结束不应卡死"
    fail=$((fail + 1))
else
    echo "  [ ok ] 保存提示行遇到输入结束能正常结束"
    pass=$((pass + 1))
fi

# 语法高亮：打开一个 .cpp，渲染输出里必须出现 ANSI 颜色序列。
# 钉这条用例是因为曾经只有第 0 行被着色 —— 整篇重算被「注释状态没变就停止传播」
# 这条增量规则提前终止了，而这条规则只对「改了某一行」才成立。
total=$((total + 1))
hl_src="$TMP/hl.cpp"
hl_out="$TMP/hl.out"
printf 'int main(void) {\n    return 42;\n}\n' > "$hl_src"
printf '\x11' | ${TIMEOUT} "$BIN" "$hl_src" > "$hl_out" 2>/dev/null
if [ $? -eq 124 ]; then
    echo "  [FAIL] 打开 .cpp 文件时编辑器超时未退出"
    fail=$((fail + 1))
elif grep -aq "$(printf '\033')\[3[0-9]m" "$hl_out" &&
     grep -aq "$(printf '\033')\[3[1-6]m" "$hl_out"; then
    echo "  [ ok ] 打开 .cpp 会输出语法高亮颜色"
    pass=$((pass + 1))
else
    echo "  [FAIL] 打开 .cpp 却没有输出任何高亮颜色"
    fail=$((fail + 1))
fi

echo
echo "通过 $pass 项，失败 $fail 项（共 $total 项）"
rm -rf "$TMP" 2>/dev/null || true
[ "$fail" -eq 0 ]
