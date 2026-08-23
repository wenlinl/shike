#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""「食刻」智能食品保质期扫描台 · 极简外观海报生成脚本。

输出：media/产品宣传图/食刻-极简外观海报.png（1200 x 1697，A3 竖版 @150dpi）
设计语言：米白底 + 近黑细线 + 单一绿色点缀，大留白。
"""

from PIL import Image, ImageDraw, ImageFont

SCALE = 2                      # 2x 渲染后降采样，保证边缘平滑
W, H = 1200 * SCALE, 1697 * SCALE
INK = (28, 27, 25)             # 近黑 #1C1B19
GREEN = (46, 107, 79)          # 鲜绿 #2E6B4F
GRAY = (154, 149, 139)         # 中灰 #9A958B
SOFT = (201, 196, 185)         # 浅灰 #C9C4B9
BG = (246, 244, 239)           # 米白 #F6F4EF
PANEL = (241, 237, 229)        # 面板暖白 #F1EDE5
TXT_DARK = (90, 86, 78)        # 深灰正文 #5A564E


def font(path, size, index=0):
    return ImageFont.truetype(path, size * SCALE, index=index)


HIR_W3 = "/System/Library/Fonts/Hiragino Sans GB.ttc"
HIR_W6 = "/System/Library/Fonts/Hiragino Sans GB.ttc"
NEUE = "/System/Library/Fonts/HelveticaNeue.ttc"


def tracked_width(draw, text, fnt, tracking):
    return sum(draw.textlength(ch, font=fnt) for ch in text) + tracking * (len(text) - 1)


def draw_tracked(draw, x, cy, text, fnt, tracking, fill, align="center"):
    """按字符绘制并附加字距。
    align: "left"（x 为左边缘）| "center"（x 为中心）| "right"（x 为右边缘）
    cy 为垂直中心。
    """
    total = tracked_width(draw, text, fnt, tracking)
    if align == "center":
        x -= total / 2
    elif align == "right":
        x -= total
    for ch in text:
        draw.text((x, cy), ch, font=fnt, fill=fill, anchor="lm")
        x += draw.textlength(ch, font=fnt) + tracking


def dashed_line(draw, p1, p2, color, width, dash=22, gap=16):
    import math
    x1, y1 = p1
    x2, y2 = p2
    dist = math.hypot(x2 - x1, y2 - y1)
    ux, uy = (x2 - x1) / dist, (y2 - y1) / dist
    d = 0
    while d < dist:
        e = min(d + dash, dist)
        draw.line((x1 + ux * d, y1 + uy * d, x1 + ux * e, y1 + uy * e),
                  fill=color, width=width)
        d = e + gap


def crop_marks(draw, inset=80, arm=56, width=5):
    for x0, y0 in ((inset, inset), (W - inset, inset),
                   (inset, H - inset), (W - inset, H - inset)):
        draw.line((x0 - arm, y0, x0 + arm, y0), fill=SOFT, width=width)
        draw.line((x0, y0 - arm, x0, y0 + arm), fill=SOFT, width=width)


def main():
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)
    cx = W // 2

    f_meta = font(HIR_W3, 40, 0)          # 顶部中文小字
    f_latin_s = font(NEUE, 34, 5)         # 超细拉丁小字
    f_latin_m = font(NEUE, 54, 5)         # 英文大字标
    f_title = font(HIR_W6, 200, 2)        # 主标题
    f_sub = font(HIR_W3, 62, 0)           # 副标题
    f_tag = font(HIR_W3, 50, 0)           # 标语
    f_num = font(NEUE, 44, 5)             # 特性编号
    f_ft = font(HIR_W6, 46, 2)            # 特性标题
    f_fd = font(HIR_W3, 34, 0)            # 特性说明
    f_time = font(NEUE, 30, 7)            # 屏幕内时间

    # ---------- 顶部信息行 ----------
    d.ellipse((220, 208, 252, 240), fill=GREEN)
    draw_tracked(d, 220 + 62, 224, "食刻 ShiKe · AI 食品扫描台",
                 f_meta, 8, INK, align="left")
    draw_tracked(d, W - 220, 224, "APPEARANCE · 001", f_latin_s, 10, GRAY, align="right")
    d.line((220, 330, W - 220, 330), fill=SOFT, width=3)

    # ---------- 右上装饰同心圆 ----------
    d.ellipse((2050 - 430, 560 - 430, 2050 + 430, 560 + 430), outline=SOFT, width=3)
    d.ellipse((2050 - 330, 560 - 330, 2050 + 330, 560 + 330), outline=SOFT, width=2)
    d.ellipse((2050 - 12, 560 - 12, 2050 + 12, 560 + 12), fill=GREEN)

    # ---------- 产品外观插画（扫描台 + 牛奶盒） ----------
    ground_y = 2205
    # 桌面线
    d.line((560, ground_y, 1840, ground_y), fill=INK, width=4)

    # 底座
    d.rounded_rectangle((860, 2070, 1540, ground_y), radius=34,
                        fill=PANEL, outline=INK, width=6)
    # 立柱 + 摄像头
    d.rounded_rectangle((1175, 1980, 1225, 2070), radius=16,
                        fill=PANEL, outline=INK, width=5)
    d.ellipse((1200 - 30, 2025 - 30, 1200 + 30, 2025 + 30), outline=INK, width=5)
    d.ellipse((1200 - 11, 2025 - 11, 1200 + 11, 2025 + 11), fill=INK)

    # T5 触控屏
    d.rounded_rectangle((920, 1180, 1480, 1980), radius=48,
                        fill=(255, 255, 255), outline=INK, width=6)
    # 屏内状态栏
    d.ellipse((1000 - 9, 1240 - 9, 1000 + 9, 1240 + 9), fill=GREEN)
    d.text((1030, 1240), "16:00", font=f_time, fill=GRAY, anchor="lm")
    # 扫描框
    d.ellipse((1200 - 180, 1450 - 180, 1200 + 180, 1450 + 180), outline=INK, width=5)
    d.ellipse((1200 - 15, 1450 - 15, 1200 + 15, 1450 + 15), fill=GREEN)
    # 扫描进度条（前 3 格绿色）
    seg_w, seg_gap, seg_y = 52, 16, 1780
    total_w = 6 * seg_w + 5 * seg_gap
    sx = cx - total_w / 2
    for i in range(6):
        x0 = sx + i * (seg_w + seg_gap)
        d.rounded_rectangle((x0, seg_y, x0 + seg_w, seg_y + 14), radius=7,
                            fill=GREEN if i < 3 else (216, 211, 200))

    # 牛奶盒（扫描对象）
    carton = [(1540, 1750), (1540, 1990), (1740, 1990), (1740, 1750),
              (1690, 1680), (1590, 1680)]
    d.polygon(carton, outline=INK, width=5)
    d.rounded_rectangle((1580, 1820, 1700, 1885), radius=8, fill=GREEN)
    # 扫描虚线：摄像头 → 牛奶盒
    dashed_line(d, (1200, 2025), (1640, 1900), GREEN, 6)

    # ---------- 标题区 ----------
    draw_tracked(d, cx, 2330, "XIANZHIDAO", f_latin_m, 36, GRAY)
    draw_tracked(d, cx, 2470, "食刻", f_title, 40, INK)
    draw_tracked(d, cx, 2710, "智能食品保质期扫描台", f_sub, 14, TXT_DARK)

    # 标语（两侧绿色方块）
    tag_text = "食品放到镜头前 · 扫一下就知道"
    tag_w = tracked_width(d, tag_text, f_tag, 8)
    draw_tracked(d, cx, 2845, tag_text, f_tag, 8, INK)
    d.rectangle((cx - tag_w / 2 - 34, 2845 - 12, cx - tag_w / 2 - 10, 2845 + 12), fill=GREEN)
    d.rectangle((cx + tag_w / 2 + 10, 2845 - 12, cx + tag_w / 2 + 34, 2845 + 12), fill=GREEN)

    # ---------- 分隔线 + 三大特性 ----------
    d.line((220, 2970, W - 220, 2970), fill=SOFT, width=3)
    feats = [
        ("01", "扫描", "拍照即入库 · 补给消耗"),
        ("02", "识别", "云端 AI 认出品名保质期"),
        ("03", "提醒", "临期置顶 · 过期不浪费"),
    ]
    cols = (580, cx, 1820)
    for (num, title, desc), x in zip(feats, cols):
        draw_tracked(d, x, 3100, num, f_num, 6, GREEN)
        draw_tracked(d, x, 3180, title, f_ft, 10, INK)
        draw_tracked(d, x, 3270, desc, f_fd, 4, GRAY)

    # ---------- 底部 ----------
    draw_tracked(d, cx, 3370, "SCAN · KNOW · REMIND", f_latin_s, 24, TXT_DARK)
    crop_marks(d)

    # 降采样输出
    out = img.resize((W // SCALE, H // SCALE), Image.LANCZOS)
    out.save("media/产品宣传图/食刻-极简外观海报.png")
    print("saved media/产品宣传图/食刻-极简外观海报.png", out.size)


if __name__ == "__main__":
    main()
