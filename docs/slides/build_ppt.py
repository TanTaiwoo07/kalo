# -*- coding: utf-8 -*-
"""生成 kalo 项目介绍 PPT（14 页，含每页演讲备注）。

内容依据：github.com/TanTaiwoo07/kalo 的 README、src/ 目录结构，
以及 Syntax / PieceTable / Utf8 的实际实现。
"""

from pptx import Presentation
from pptx.util import Emu, Pt
from pptx.dml.color import RGBColor
from pptx.enum.text import PP_ALIGN
from pptx.enum.shapes import MSO_SHAPE

W, H = 1280, 720

def px(v):   return Emu(int(v * 914400 / 96))
def fs(v):   return Pt(v * 0.75)          # 设计稿用 px，PPT 用 pt

BG   = RGBColor(0x0F, 0x17, 0x2A)
CARD = RGBColor(0x1E, 0x29, 0x3B)
ACC  = RGBColor(0x38, 0xBD, 0xF8)
TXT  = RGBColor(0xE2, 0xE8, 0xF0)
MUT  = RGBColor(0x94, 0xA3, 0xB8)
BRD  = RGBColor(0x33, 0x41, 0x55)

CN, MONO = "微软雅黑", "Consolas"

prs = Presentation()
prs.slide_width, prs.slide_height = px(W), px(H)
TOTAL = 14


# ---------- 基础绘制 ----------

def shape(s, kind, x, y, w, h, fill=None, line=None, lw=1.0, radius=None):
    sh = s.shapes.add_shape(kind, px(x), px(y), px(w), px(h))
    sh.shadow.inherit = False
    if fill is None:
        sh.fill.background()
    else:
        sh.fill.solid(); sh.fill.fore_color.rgb = fill
    if line is None:
        sh.line.fill.background()
    else:
        sh.line.color.rgb = line; sh.line.width = Pt(lw)
    if radius is not None and kind == MSO_SHAPE.ROUNDED_RECTANGLE:
        try:
            sh.adjustments[0] = radius
        except Exception:
            pass
    sh.text_frame.text = ""
    return sh


def rrect(s, x, y, w, h, fill=CARD, line=BRD, radius=0.06):
    return shape(s, MSO_SHAPE.ROUNDED_RECTANGLE, x, y, w, h, fill, line, 1.0, radius)


def rect(s, x, y, w, h, fill=CARD, line=None):
    return shape(s, MSO_SHAPE.RECTANGLE, x, y, w, h, fill, line)


def text(s, x, y, w, h, runs, size=18, color=TXT, bold=False, font=CN,
         align=PP_ALIGN.LEFT, spacing=1.5, anchor=None, space_after=0):
    """runs: 字符串，或 [(片段, {覆盖样式}), ...] 列表；换行用 \\n。"""
    tb = s.shapes.add_textbox(px(x), px(y), px(w), px(h))
    tf = tb.text_frame
    tf.word_wrap = True
    tf.margin_left = tf.margin_right = tf.margin_top = tf.margin_bottom = 0
    if anchor:
        tf.vertical_anchor = anchor

    lines = runs if isinstance(runs, list) else [runs]
    for i, line in enumerate(lines):
        p = tf.paragraphs[0] if i == 0 else tf.add_paragraph()
        p.alignment = align
        p.line_spacing = spacing
        if space_after:
            p.space_after = Pt(space_after * 0.75)
        pieces = line if isinstance(line, list) else [(line, {})]
        for t, st in pieces:
            r = p.add_run(); r.text = t
            f = r.font
            f.size = fs(st.get("size", size))
            f.bold = st.get("bold", bold)
            f.name = st.get("font", font)
            f.color.rgb = st.get("color", color)
    return tb


def notes(s, txt):
    s.notes_slide.notes_text_frame.text = txt


def footer(s, n):
    rect(s, 72, 668, 1136, 1, BRD)
    text(s, 72, 678, 500, 20, "kalo · github.com/TanTaiwoo07/kalo",
         size=13, color=MUT)
    text(s, 1108, 678, 100, 20, f"{n:02d} / {TOTAL}", size=13,
         color=MUT, font=MONO, align=PP_ALIGN.RIGHT)


def head(s, title, kicker=None):
    """A 区标题块：36px 主标题 + 强调色短横线。"""
    y = 44
    if kicker:
        text(s, 72, y, 700, 22, kicker, size=14, color=ACC, font=MONO)
        y += 28
    text(s, 72, y, 1000, 52, title, size=36, color=TXT, bold=True, spacing=1.2)
    rect(s, 72, y + 62, 56, 4, ACC)


def page(title=None, kicker=None, n=None):
    s = prs.slides.add_slide(prs.slide_layouts[6])
    rect(s, 0, 0, W, H, BG)
    if title:
        head(s, title, kicker)
    if n:
        footer(s, n)
    return s


# ---------- 01 封面 ----------

def p01():
    s = prs.slides.add_slide(prs.slide_layouts[6])
    rect(s, 0, 0, W, H, BG)
    shape(s, MSO_SHAPE.RECTANGLE, 900, 0, 380, H, CARD)      # 右侧深色块
    rect(s, 0, 0, 6, H, ACC)

    text(s, 96, 150, 600, 24, "C++17 · PIECE TABLE · TERMINAL EDITOR",
         size=15, color=ACC, font=MONO)
    text(s, 92, 186, 620, 120, "kalo", size=96, color=TXT, bold=True, font=CN)
    rect(s, 96, 320, 72, 4, ACC)
    text(s, 96, 348, 620, 40, "从教学项目出发的终端文本编辑器",
         size=26, color=TXT, bold=True)
    text(s, 96, 400, 640, 90,
         "用分块表把随机编辑做成 O(log K)，用东亚宽度把中文排版做对，\n"
         "再用三层测试与四平台 CI 把「能跑」变成「可验证」。",
         size=17, color=MUT, spacing=1.8)

    # 终端线框
    rrect(s, 952, 150, 264, 300, CARD, BRD)
    rect(s, 952, 150, 264, 34, BRD)
    for i, c in enumerate([MUT, MUT, ACC]):
        shape(s, MSO_SHAPE.OVAL, 966 + i * 18, 161, 9, 9, c)
    text(s, 1040, 160, 160, 18, "demo.cpp", size=12, color=MUT, font=MONO)
    text(s, 972, 204, 226, 220,
         [[("int", {"color": ACC}), (" main(void) {", {})],
          [("  const char *s = \"中文\";", {})],
          [("  /* 分块表 + 撤销 */", {"color": MUT})],
          [("  return 0;", {})],
          [("}", {"color": ACC})],
          [(" ", {})],
          [("1864 行源码", {"color": ACC})],
          [("单二进制 · 零依赖", {"color": MUT})]],
         size=13, color=TXT, font=MONO, spacing=1.6)

    # 底部数据条
    for i, (num, lab, col) in enumerate([
            ("3,467", "行 C++ 源码", TXT), ("18,964", "项断言全通过", TXT),
            ("4", "个平台 CI 构建", TXT)]):
        x = 96 + i * 232
        text(s, x, 528, 220, 52, num, size=42, color=ACC if i == 0 else col,
             bold=True, font=MONO)
        text(s, x, 584, 220, 22, lab, size=14, color=MUT)
    text(s, 952, 560, 264, 24, "github.com/TanTaiwoo07/kalo",
         size=14, color=ACC, font=MONO)
    text(s, 952, 590, 264, 20, "作者 TaiWoo_Chen", size=13, color=MUT)

    notes(s, "大家好，今天介绍的项目叫 kalo，是一个用 C++17 重写的终端文本编辑器。\n"
             "它的蓝本是 antirez 的 kilo，也就是那个著名的 C 语言教学项目。\n"
             "我做的不是把它编译通就完事，而是把它做成了一个真正能用的编辑器："
             "存储层换成 Piece Table，中文排版是对的，测试和 CI 都齐了。\n"
             "右下角这三个数字是我全部的成果：三千多行源码、一万八千九百多项断言、四个平台的持续集成。")


# ---------- 02 目录 ----------

def p02():
    s = page("目录", "CONTENTS", 2)
    items = [("01", "定位与原理", "要解决什么问题，Piece Table 为什么值得", "P03 – P06"),
             ("02", "功能与工程", "能做什么，怎么保证它一直是对的", "P07 – P10"),
             ("03", "未来计划", "多语言语法高亮的分阶段路线", "P11 – P13")]
    for i, (num, title, desc, rng) in enumerate(items):
        y = 176 + i * 148
        rrect(s, 72, y, 1136, 120, CARD, BRD)
        text(s, 100, y + 30, 110, 60, num, size=46, color=ACC, bold=True, font=MONO)
        text(s, 230, y + 28, 420, 34, title, size=26, color=TXT, bold=True)
        text(s, 230, y + 68, 620, 26, desc, size=16, color=MUT)
        text(s, 1000, y + 46, 180, 26, rng, size=15, color=MUT, font=MONO,
             align=PP_ALIGN.RIGHT)
    notes(s, "整个介绍分三部分。\n"
             "第一部分讲定位和原理，我会说明这个项目要解决哪四个问题，"
             "以及 Piece Table 这个数据结构为什么值得用。\n"
             "第二部分讲功能和工程，包括它现在能做什么，以及我怎么保证它一直是对的。\n"
             "第三部分是重点，讲未来计划——多语言语法高亮的分阶段路线。")


# ---------- 章节扉页 ----------

def section(num, name, sub, n, note=""):
    s = prs.slides.add_slide(prs.slide_layouts[6])
    rect(s, 0, 0, W, H, BG)
    rect(s, 0, 0, W, 4, ACC)
    text(s, 120, 214, 400, 180, num, size=150, color=CARD, bold=True, font=MONO)
    text(s, 120, 300, 900, 70, name, size=52, color=TXT, bold=True)
    rect(s, 124, 392, 64, 4, ACC)
    text(s, 124, 414, 800, 30, sub, size=18, color=MUT)
    text(s, 1108, 660, 100, 24, f"{n:02d} / {TOTAL}", size=13, color=MUT,
         font=MONO, align=PP_ALIGN.RIGHT)
    if note:
        notes(s, note)
    return s


# ---------- 04 定位与问题 ----------

def p04():
    s = page("项目定位与要解决的四个问题", "POSITIONING", 4)
    rrect(s, 72, 156, 660, 130, CARD, BRD)
    text(s, 96, 178, 612, 30, "定位", size=14, color=ACC, font=MONO)
    text(s, 96, 206, 612, 66,
         "一个能真正用起来的终端编辑器，而不是跑通就行的教学 demo。\n"
         "判断标准很朴素：改大文件不卡、中文不乱码、崩溃了能复现。",
         size=17, color=TXT, spacing=1.7)

    probs = [("O(n) 的插入", "朴素字符串每次插入都要搬动后半段文本，改长文件会越来越卡"),
             ("中文列宽错位", "一个汉字 3 字节却只占 2 列，光标与视觉位置对不上"),
             ("跨平台编译", "Windows 没有 termios / unistd，POSIX 源码无法直接编译"),
             ("数据一致性", "权威文本与行视图缓存可能跑偏，且很难被肉眼发现")]
    for i, (t, d) in enumerate(probs):
        y = 312 + i * 78
        text(s, 76, y, 48, 32, f"0{i+1}", size=20, color=ACC, bold=True, font=MONO)
        text(s, 132, y, 220, 26, t, size=18, color=TXT, bold=True)
        text(s, 132, y + 30, 590, 40, d, size=15, color=MUT, spacing=1.5)

    # 右栏：解决路径
    rrect(s, 772, 156, 436, 448, CARD, ACC)
    text(s, 800, 182, 380, 26, "对应的技术选择", size=14, color=ACC, font=MONO)
    picks = [("分块表 + FHQ-Treap", "随机插入删除降到 O(log K)"),
             ("东亚宽度 + 字符单元", "三套下标统一，中文不错位"),
             ("平台兼容层", "src/ 下 POSIX 源码一行不改"),
             ("权威存储 + 可断言不变式", "一致性由随机差分测试保证")]
    for i, (t, d) in enumerate(picks):
        y = 224 + i * 96
        rect(s, 800, y, 26, 3, ACC)
        text(s, 800, y + 16, 380, 28, t, size=17, color=TXT, bold=True)
        text(s, 800, y + 46, 380, 40, d, size=14, color=MUT, spacing=1.5)
    notes(s, "先说清楚它是什么：一个能真正用起来的编辑器，不是跑通就完事的 demo。\n"
             "右边这四个问题是我从 kilo 那个教学项目里实际踩到的。\n"
             "最要命的是第一个：朴素字符串插入是 O(n) 的，改长文件会越来越卡。\n"
             "第二个是中文，一个汉字 3 字节但只占 2 列，光标位置和看到的字符对不上，"
             "这是很多终端编辑器的通病。\n"
             "最右边是每个问题对应的技术选择，后面几部分会逐一展开。")


# ---------- 05 核心原理（hero） ----------

def p05():
    s = page("核心原理：分块表与 FHQ-Treap", "CORE PRINCIPLE", 5)
    text(s, 72, 152, 560, 26, "随机插入加速比（20 万字符文档，2 万次随机插入）",
         size=14, color=MUT)
    text(s, 68, 172, 460, 140, "3.06x", size=104, color=ACC, bold=True, font=MONO)
    text(s, 72, 322, 470, 100,
         "文本以「片段」挂在 Treap 上，\n插入与删除只改动 O(log K) 个节点，\nK 是片段数而不是文档长度。",
         size=17, color=TXT, spacing=1.7)

    rrect(s, 72, 436, 470, 186, CARD, BRD)
    text(s, 96, 454, 420, 24, "代价也要说清楚", size=14, color=ACC, font=MONO)
    text(s, 96, 484, 420, 26, "顺序追加 20 万字符：", size=15, color=MUT)
    text(s, 96, 512, 420, 40, [[("0.03x", {"color": ACC, "bold": True, "font": MONO, "size": 24}),
                               ("  比 std::string 慢 30 倍", {"color": TXT})]], size=16)
    text(s, 96, 566, 420, 46, "分块表换来的优势在随机增删，不在顺序写——这是选型必须承认的代价。",
         size=14, color=MUT, spacing=1.5)

    # 右侧：双缓冲区示意 + 基准表
    rrect(s, 578, 152, 630, 214, CARD, BRD)
    text(s, 602, 172, 582, 24, "两个只增不减的缓冲区", size=14, color=ACC, font=MONO)
    for i, (nm, d) in enumerate([("original_", "装载时的原始文本"),
                                 ("add_", "后续所有新写入的字符")]):
        y = 210 + i * 74
        rect(s, 602, y, 190, 56, BG, BRD)
        text(s, 602, y + 16, 190, 26, nm, size=16, color=ACC, font=MONO,
             align=PP_ALIGN.CENTER)
        text(s, 810, y + 16, 380, 30, d, size=15, color=MUT)
    text(s, 602, 336, 582, 24, "因为缓冲区不被改写，撤销时可以安全地继续引用旧区间",
         size=13, color=MUT)

    rows = [("操作", "PieceTable", "std::string"),
            ("随机插入 2 万次", "8.2 ms", "25.0 ms"),
            ("随机删除 2 万次", "10.2 ms", "22.5 ms"),
            ("顺序追加 20 万字符", "6.0 ms", "0.2 ms"),
            ("撤销 2 万次插入", "6.7 ms", "—"),
            ("全量序列化（单次）", "0.004 ms", "—")]
    tb = s.shapes.add_table(len(rows), 3, px(578), px(390), px(630), px(212)).table
    tb.columns[0].width, tb.columns[1].width, tb.columns[2].width = px(270), px(180), px(180)
    for ri, row in enumerate(rows):
        for ci, val in enumerate(row):
            c = tb.cell(ri, ci)
            c.fill.solid()
            c.fill.fore_color.rgb = CARD if ri == 0 else (BG if ri % 2 else CARD)
            c.margin_left = px(12); c.margin_right = px(12)
            c.vertical_anchor = 3
            tf = c.text_frame; tf.word_wrap = True
            p = tf.paragraphs[0]
            p.alignment = PP_ALIGN.LEFT if ci == 0 else PP_ALIGN.CENTER
            r = p.add_run(); r.text = val
            r.font.size = fs(14 if ri else 13)
            r.font.bold = (ri == 0)
            r.font.name = MONO if ci else CN
            r.font.color.rgb = ACC if ri == 0 else (TXT if ci == 0 else MUT)
    notes(s, "这一页是整个项目的技术核心。\n"
             "分块表的思路是：文本不存成一整条字符串，而是拆成一段段片段，"
             "每段记录自己在缓冲区里的位置，再把这些片段挂成一棵平衡树。\n"
             "这样插入只要在树上定位、改几个节点，不用搬动后面所有字符。\n"
             "实测下来随机插入比 std::string 快三倍，删除快两倍。\n"
             "但我必须诚实地说另一面：顺序追加反而慢三十倍，因为每敲一个字符都要走一次树操作。\n"
             "所以分块表不是万能替换，它的价值就在随机增删这个场景。")


# ---------- 06 架构与数据流 ----------

def p06():
    s = page("架构与数据流", "ARCHITECTURE", 6)
    text(s, 72, 156, 620, 30, "核心不变式", size=15, color=ACC, font=MONO)
    rrect(s, 72, 190, 620, 118, CARD, BRD)
    text(s, 96, 208, 572, 80,
         "PieceTable 是唯一权威文本存储，rows 只是派生缓存。\n"
         "把 rows 用换行符连接起来，要么等于权威文本，要么等于它去掉末尾那一个换行——"
         "这条判据在随机编辑测试里被逐步校验。",
         size=15, color=TXT, spacing=1.7)

    text(s, 72, 330, 620, 30, "三套下标，靠「字符单元」对齐", size=15, color=ACC, font=MONO)
    for i, (k, v) in enumerate([("chars 字节下标", "编辑器坐标"),
                                ("render 字节下标", "写进终端的字节"),
                                ("显示列", "一个字符占几列")]):
        y = 368 + i * 56
        rect(s, 76, y + 8, 14, 14, ACC)
        text(s, 106, y, 260, 26, k, size=16, color=TXT, font=MONO)
        text(s, 380, y, 290, 26, v, size=15, color=MUT)
    text(s, 72, 548, 620, 56,
         "一个中文字符 3 字节但占 2 列，直接按字节算就会错位。\n"
         "Row 里的三张换算表把这件事一次算清，绘制与高亮共用同一套下标。",
         size=15, color=MUT, spacing=1.6)

    # 右侧分层架构图
    layers = [("Terminal", "裸模式 · 读键 · 转义序列"),
              ("Screen", "输出缓冲 · 窗口尺寸"),
              ("Editor", "主循环 · 绘制 · 存盘"),
              ("Cursor", "光标位置与滚动"),
              ("Syntax / Utf8", "高亮标记 · 列宽与字符切分"),
              ("Buffer", "行视图缓存 rows"),
              ("PieceTable", "权威文本 + 撤销历史")]
    for i, (nm, d) in enumerate(layers):
        y = 156 + i * 64
        w = 520 - (6 - i) * 30
        x = 716 + (520 - w) / 2
        is_core = nm == "PieceTable"
        sh = rrect(s, x, y, w, 50, CARD if not is_core else BRD,
                   ACC if is_core else BRD)
        text(s, x + 18, y + 8, w - 36, 22, nm, size=15,
             color=ACC if is_core else TXT, bold=True, font=MONO)
        text(s, x + 18, y + 28, w - 36, 20, d, size=12, color=MUT)
        if i < len(layers) - 1:
            rect(s, x + w / 2 - 1, y + 50, 2, 14, BRD)
    notes(s, "架构上是严格分层的，越往下越接近数据。\n"
             "最关键的是最底层的设计：PieceTable 是唯一的权威存储，"
             "上面那个 rows 只是派生出来的行视图缓存。\n"
             "为什么要分两层？因为编辑器需要按行列定位，而底层文本是连续的。"
             "分开的代价是要保证两者不跑偏，我的做法是写出一条可断言的不变式。\n"
             "右边这张图还体现了另一个细节：中文要同时维护三套下标，"
             "靠字符单元把它们对齐。")


# ---------- 08 功能与场景 ----------

def p08():
    s = page("功能清单与典型使用场景", "FEATURES", 8)
    cards = [("编辑与保存", "打开 / 编辑 / 保存，状态栏与消息栏，制表符展开，多行与退格合并"),
             ("撤销", "Ctrl-Z，由分块表的片段历史支撑；缓冲区只增不减，旧区间可安全引用"),
             ("搜索", "Ctrl-F 增量查找，上下键切换命中并回绕，Tab 切大小写，状态栏显示第几个"),
             ("语法高亮", "按扩展名识别 C/C++ 与 Python，关键字 / 类型 / 数字 / 字符串 / 多行注释"),
             ("中文排版", "按东亚宽度算列宽，方向键与退格按字符而非字节走"),
             ("跨平台", "Linux / macOS / WSL 与 Windows 原生构建，单二进制零依赖")]
    for i, (t, d) in enumerate(cards):
        x = 72 + (i % 3) * 384
        y = 156 + (i // 3) * 168
        rrect(s, x, y, 360, 148, CARD, BRD)
        rect(s, x, y, 360, 3, ACC)
        text(s, x + 22, y + 22, 316, 28, t, size=19, color=TXT, bold=True)
        text(s, x + 22, y + 58, 316, 76, d, size=14, color=MUT, spacing=1.6)

    rrect(s, 72, 500, 1136, 100, CARD, ACC)
    text(s, 96, 516, 200, 24, "典型场景", size=13, color=ACC, font=MONO)
    for i, sc in enumerate(["服务器上远程改配置", "没有图形界面时的快速查看",
                            "读一份日志并搜索关键字"]):
        x = 96 + i * 370
        text(s, x, 548, 340, 32, [[("· ", {"color": ACC}), (sc, {"color": TXT})]],
             size=16)
    notes(s, "功能上分六块。\n"
             "前四块是编辑能力：基本编辑保存、撤销、搜索、语法高亮。\n"
             "第五块是我比较在意的中文排版，这块很多同类编辑器做得并不好。\n"
             "第六块是跨平台，它是一个单二进制、零依赖的程序。\n"
             "最下面这三个场景是我自己实际会用的：远程改配置、没有图形界面时快速看一眼、"
             "读日志时搜关键字。")


# ---------- 09 测试与 CI ----------

def p09():
    s = page("工程化：三层测试与四平台 CI", "QUALITY", 9)
    text(s, 72, 152, 560, 30, "单元测试断言数（含随机差分与内存收支核对）",
         size=14, color=MUT)
    text(s, 68, 178, 620, 118, "18,964", size=88, color=ACC, bold=True, font=MONO)
    text(s, 72, 314, 620, 60,
         "数量本身不是价值，关键是里面包含对照 std::string 的随机差分测试，"
         "能抓出人眼看不出的错误。",
         size=16, color=TXT, spacing=1.7)

    for i, (t, d) in enumerate([
            ("单元测试", "差分模糊测试、规模测试、new/delete 收支核对"),
            ("端到端冒烟", "管道喂按键序列，核对落盘内容"),
            ("性能基准", "与 std::string 同组操作对照，不进 CI")]):
        y = 380 + i * 74
        rrect(s, 72, y, 620, 64, CARD, BRD)
        text(s, 92, y + 10, 200, 24, t, size=16, color=ACC, bold=True)
        text(s, 92, y + 36, 570, 22, d, size=14, color=MUT)

    rrect(s, 728, 152, 480, 372, CARD, BRD)
    text(s, 752, 174, 432, 24, "持续集成", size=14, color=ACC, font=MONO)
    text(s, 752, 204, 432, 44, "四个环境跑同一套测试，警告一律当错误",
         size=16, color=TXT, spacing=1.5)
    for i, (p, st) in enumerate([("Linux · GCC", "已通过"), ("macOS · Clang", "已通过"),
                                 ("Windows · MSVC", "零警告"), ("Windows · MinGW", "零警告")]):
        y = 264 + i * 56
        text(s, 752, y, 280, 24, p, size=15, color=TXT, font=MONO)
        rect(s, 1030, y + 6, 154, 24, BG, BRD)
        text(s, 1030, y + 10, 154, 20, st, size=12, color=ACC, font=MONO,
             align=PP_ALIGN.CENTER)
    text(s, 752, 490, 432, 24, "-Werror / /WX：宁可 CI 红，也不让新警告悄悄进仓库",
         size=13, color=MUT)

    rrect(s, 728, 544, 480, 58, CARD, ACC)
    text(s, 752, 558, 432, 36,
         [[("已知缺口：", {"color": ACC}), ("macOS 与 WSL 至今没有真机验证过", {"color": TXT})]],
         size=15)
    notes(s, "这一页讲质量保障。\n"
             "一万八千九百多项断言不是堆出来的，关键在于其中有对照 std::string 的随机差分测试——"
             "随机操作之后逐轮比对两边内容是否一致，这是人眼永远做不到的。\n"
             "还有一层是端到端冒烟：把按键序列喂给编辑器本体，让它自己走完编辑和保存，再核对落盘内容。\n"
             "CI 在四个平台跑同一套测试，而且把警告当错误。"
             "换到 CI 上编译器版本更新，正好能照出潜伏的写法。\n"
             "最后一行我如实说：macOS 和 WSL 到现在还没有真机跑过，这是目前唯一的验证缺口。")


# ---------- 10 亮点对比 ----------

def p10():
    s = page("项目亮点：与同类工具的差异", "HIGHLIGHTS", 10)
    rows = [("", "kalo", "kilo 原版", "nano / vim"),
            ("中文列宽", "正确", "错位", "部分支持"),
            ("语法高亮", "有", "无", "无"),
            ("大文件随机编辑", "O(log K)", "O(n)", "O(n)"),
            ("一致性保障", "可断言不变式", "无", "无"),
            ("跨平台", "四平台 CI", "依赖 POSIX", "依赖 POSIX"),
            ("可读性", "全源码可读", "可读", "体量大")]
    tb = s.shapes.add_table(len(rows), 4, px(72), px(156), px(700), px(300)).table
    for ci, w in enumerate([220, 170, 160, 150]):
        tb.columns[ci].width = px(w)
    for ri, row in enumerate(rows):
        for ci, val in enumerate(row):
            c = tb.cell(ri, ci)
            c.fill.solid()
            if ri == 0:
                c.fill.fore_color.rgb = CARD
            elif ci == 1:
                c.fill.fore_color.rgb = BRD
            else:
                c.fill.fore_color.rgb = BG if ri % 2 else CARD
            c.margin_left = px(14); c.margin_right = px(14)
            c.vertical_anchor = 3
            p = c.text_frame.paragraphs[0]
            r = p.add_run(); r.text = val
            r.font.size = fs(15 if ri else 13)
            r.font.bold = (ri == 0 or ci == 0 or ci == 1)
            r.font.name = CN
            r.font.color.rgb = (ACC if ri == 0 else
                                (TXT if ci == 0 else (ACC if ci == 1 else MUT)))

    for i, (t, d) in enumerate([
            ("一致性可被证明", "把「缓存没跑偏」写成可断言的不变式，并用随机差分测试守住"),
            ("中文是一等公民", "东亚宽度 + 字符单元，不是打补丁而是模型上支持"),
            ("代价被量化", "每一处取舍都有基准数据，包括对自己不利的那些"),
            ("架构不藏技术债", "标注了 24 处未修的已知问题及修法，而不是假装没有")]):
        y = 156 + i * 104
        rrect(s, 808, y, 400, 88, CARD, BRD)
        text(s, 830, y + 16, 356, 26, t, size=17, color=ACC, bold=True)
        text(s, 830, y + 46, 356, 34, d, size=13, color=MUT, spacing=1.5)

    rrect(s, 72, 480, 700, 122, CARD, ACC)
    text(s, 96, 496, 652, 26, "与 nano / vim 的关系", size=13, color=ACC, font=MONO)
    text(s, 96, 526, 652, 66,
         "kalo 不是要替代它们——那是一个几十年积累的项目。\n"
         "它的价值在于：全部源码可读、每一处设计取舍都有数据支撑，适合当作学习与实验的载体。",
         size=15, color=TXT, spacing=1.7)
    notes(s, "这一页我想讲清楚它和同类工具的差别。\n"
             "左边是横向对比：中文列宽、高亮、编辑复杂度、一致性保障、跨平台、可读性。\n"
             "右边这四条是我认为真正的差异点。\n"
             "第一条最关键：一致性不是靠小心翼翼，而是写成可断言的不变式，用随机差分测试守住。\n"
             "第四条可能有点特别——我在代码里标注了 24 处知道存在但没有修的问题，还写了修法，"
             "而不是假装它们不存在。\n"
             "最后我要说清楚：kalo 不是要替代 nano 或 vim，它们的成熟度不可同日而语。")


# ---------- 12 高亮现状与瓶颈 ----------

def p12():
    s = page("语法高亮的现状与三处表达力瓶颈", "CURRENT STATE", 12)
    rrect(s, 72, 156, 536, 200, CARD, BRD)
    text(s, 96, 176, 490, 24, "现在的做法", size=14, color=ACC, font=MONO)
    text(s, 96, 208, 490, 140,
         "规则本身已经是数据驱动的：一张 SyntaxRule 描述后缀、关键字、"
         "类型名、注释符号，由一个逐字符的状态机按优先级顺序判定。\n"
         "块注释状态会跨行传播，所以 /* */ 跨行也能正确着色。",
         size=15, color=TXT, spacing=1.7)

    rrect(s, 72, 380, 536, 222, CARD, BRD)
    text(s, 96, 400, 490, 24, "目前支持", size=14, color=ACC, font=MONO)
    for i, (lang, ext) in enumerate([("C / C++", ".c .h .cpp .hpp 等 8 种"),
                                     ("Python", ".py .pyw")]):
        y = 436 + i * 68
        text(s, 96, y, 200, 26, lang, size=17, color=TXT, bold=True, font=MONO)
        text(s, 300, y, 280, 24, ext, size=14, color=MUT, font=MONO)

    for i, (t, d) in enumerate([
            ("注释规则只能单值", "singlelineComment / multilineStart 各只支持一套串，"
             "无法表达 Rust 的 /// 与 /** */，也无法表达 Python 三引号与 JS 模板串"),
            ("关键字线性查找", "std::find 逐个比对。关键字表扩到几百个后，"
             "每行每个标识符都要扫一遍"),
            ("没有行内多行状态", "只有块注释能跨行。Python 三引号、JS 模板串这类跨行字符串目前无法正确着色")]):
        y = 156 + i * 152
        rrect(s, 648, y, 560, 136, CARD, ACC)
        text(s, 672, y + 18, 100, 30, f"0{i+1}", size=20, color=ACC, bold=True, font=MONO)
        text(s, 780, y + 18, 404, 26, t, size=18, color=TXT, bold=True)
        text(s, 672, y + 56, 512, 70, d, size=14, color=MUT, spacing=1.6)
    notes(s, "在讲未来计划之前，先说清楚现在做到了什么、卡在哪里。\n"
             "现在的规则已经是数据驱动的：一张规则表描述后缀、关键字、类型名、注释符号，"
             "由一个逐字符的状态机判定。块注释能跨行传播，所以跨行的块注释着色是对的。\n"
             "但有三个瓶颈。\n"
             "第一，注释规则每个槽位只能放一个字符串，"
             "这意味着 Rust 的文档注释、Python 的三引号、JS 的模板串都表达不了。\n"
             "第二，关键字查找是线性比对，扩到几百个关键字后会成为性能问题。\n"
             "第三，除了块注释之外没有行内的多行状态。")


# ---------- 13 路线图（hero） ----------

def p13():
    s = page("未来计划：多语言语法高亮的三个阶段", "ROADMAP", 13)
    stages = [
        ("阶段一", "填表扩语言", "1 – 2 天",
         ["规则注册表化，去掉 forFile 里的硬编码列表",
          "关键字改 unordered_set，补大小写敏感开关",
          "新增 Java / JavaScript / TypeScript / Go / Rust"],
         "不动语法引擎，只改数据与查找方式"),
        ("阶段二", "重构规则表达力", "1 周",
         ["注释与字符串定界符改为列表，支持多套规则",
          "抽出 Lexer 分层，把状态机的 if 链拆成规则匹配",
          "补 Python 三引号、JS 模板串与正则、Go 原始串"],
         "代价是要动 highlightRow，但测试已有 18964 项断言兜底"),
        ("阶段三", "规则外置为配置", "1 – 2 周",
         ["规则从 C++ 常量改为 JSON，加语言不必重编",
          "支持字符串跨行等行内多行状态",
          "为将来的语言包与第三方贡献留出接口"],
         "需自写约 300 行 JSON 解析，保持零第三方依赖")]
    text(s, 348, 136, 860, 24,
         "排序依据：先用零风险的方式把语言数量铺开，再回头补规则表达力，最后才做外置。",
         size=14, color=MUT)
    rect(s, 302, 200, 4, 320, BRD)          # 时间轴主干
    for i, (ph, name, dur, items, note) in enumerate(stages):
        y = 170 + i * 162
        shape(s, MSO_SHAPE.OVAL, 286, y + 18, 32, 32, ACC if i == 2 else BRD)
        text(s, 96, y + 16, 170, 26, ph, size=16, color=ACC if i == 2 else MUT,
             font=MONO, align=PP_ALIGN.RIGHT)
        text(s, 96, y + 46, 170, 26, dur, size=13, color=MUT, font=MONO,
             align=PP_ALIGN.RIGHT)
        rrect(s, 348, y, 860, 156, CARD, ACC if i == 2 else BRD)
        text(s, 374, y + 16, 400, 28, name, size=20, color=TXT, bold=True)
        text(s, 374, y + 52, 810, 66, "\n".join("· " + it for it in items),
             size=13, color=MUT, spacing=1.5)
        text(s, 374, y + 130, 810, 20, note, size=12, color=ACC, font=MONO)
    notes(s, "这一页是重点，我把多语言高亮拆成了三个阶段。\n"
             "阶段一是最划算的：规则本身已经是数据驱动的，我只需要把注册表化、"
             "把关键字查找换成哈希表，两三天就能加上 Java、JavaScript、Go、Rust。\n"
             "阶段二要动语法引擎，把注释和字符串规则从单值改成列表，"
             "这样才能支持三引号、模板串这些。这一步改动大，但测试兜得住。\n"
             "阶段三是把规则挪到配置文件里，以后加语言不用重编。\n"
             "排序的理由很直接：先用零风险的方式铺开语言数量，再回头补表达能力，最后才做架构升级。")


# ---------- 14 结束页 ----------

def p14():
    s = prs.slides.add_slide(prs.slide_layouts[6])
    rect(s, 0, 0, W, H, BG)
    rect(s, 0, 0, W, 4, ACC)
    text(s, 120, 132, 1040, 60, "可运行 · 可测量 · 可验证",
         size=52, color=TXT, bold=True)
    rect(s, 124, 216, 64, 4, ACC)
    text(s, 120, 244, 900, 40,
         "接下来先把阶段一做掉，再动选择区与多缓冲区。",
         size=20, color=MUT)

    for i, (num, lab) in enumerate([("3,467", "行 C++ 源码"),
                                    ("18,964", "项断言"),
                                    ("22", "项端到端用例"),
                                    ("4", "个平台 CI")]):
        x = 120 + i * 262
        text(s, x, 330, 250, 80, num, size=54, color=ACC if i == 0 else TXT,
             bold=True, font=MONO)
        text(s, x, 424, 250, 24, lab, size=14, color=MUT)
        if i < 3:
            rect(s, x + 244, 330, 1, 96, BRD)

    rrect(s, 120, 496, 1040, 92, CARD, BRD)
    text(s, 148, 518, 984, 26, "github.com/TanTaiwoo07/kalo", size=20,
         color=ACC, font=MONO)
    text(s, 148, 552, 984, 22,
         "项目是「人主导设计、AI 协助实现」的产物，README 里逐项标注了每一部分代码的来历。",
         size=14, color=MUT)
    notes(s, "最后总结一下。\n"
             "这个项目我想做到三件事：能跑、能测量、能验证。"
             "能跑是基本功；能测量意味着每个取舍都有数据，包括对我不利的那些；"
             "能验证意味着有一万八千多项断言和四平台 CI 兜着。\n"
             "下一步先把语法高亮的阶段一做掉，再动选择区和多缓冲区。\n"
             "另外说明一点：这个项目是人主导设计、AI 协助实现的产物，"
             "README 里把每一部分代码的来历都标注清楚了。欢迎大家去看，也欢迎提 issue。")


# ---------- 生成 ----------

for fn in (p01, p02,
           lambda: section("01", "定位与原理", "要解决什么问题，以及 Piece Table 为什么值得", 3,
                            "第一部分讲定位和原理。\n"
                            "我先说明这个项目是从哪里来的——它的蓝本是 antirez 的 kilo，"
                            "一个只有一千行的 C 语言教学编辑器。\n"
                            "我不会照着它抄，而是从零用 C++ 重写，同时解决四个具体问题。"
                            "接下来三页分别讲定位、核心原理和架构。"),
           p04, p05, p06,
           lambda: section("02", "功能与工程", "能做什么，以及怎么保证它一直是对的", 7,
                            "第二部分讲功能与工程。\n"
                            "前面讲的是为什么这么做，这一部分讲的是现在做到了什么，"
                            "以及我怎么保证它一直是对的。\n"
                            "功能上分六块，工程质量上靠三层测试和四平台 CI。"),
           p08, p09, p10,
           lambda: section("03", "未来计划", "多语言语法高亮的分阶段路线", 11,
                            "最后一部分是未来计划，也是这次介绍的重点。\n"
                            "语法高亮目前只支持 C/C++ 和 Python，我把它拆成了三个阶段："
                            "填表扩语言、重构规则表达力、规则外置为配置。\n"
                            "在展开之前，先用一页说明现在卡在哪三个地方。"),
           p12, p13, p14):
    fn()

out = r"C:\Users\Lenovo\WorkBuddy\2026-10-05-15-28-16\kalo-项目介绍\kalo-项目介绍.pptx"
prs.save(out)
print("已生成:", out)
print("页数:", len(prs.slides.__iter__.__self__._sldIdLst))