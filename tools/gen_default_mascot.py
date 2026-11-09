#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成中性的默认立绘 (程序绘制的漫画小鲸鱼) -> firmware/include/mascot.h

仓库默认立绘必须是原创中性图案 (智谱鲸鱼娘等第三方 IP 素材不入库)。
想换自己的图: python tools/make_mascot.py 你的图片.png
"""
import os
from PIL import Image, ImageDraw

OUT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(OUT_DIR)
HDR_PATH = os.path.join(PROJ, "firmware", "include", "mascot.h")

W, H = 120, 100
PAPER = (255, 246, 230)
INK = (32, 36, 44)
BODY = (111, 185, 239)
DARK = (90, 166, 223)
BELLY = (234, 246, 255)
BLUSH = (255, 175, 203)


def main():
    img = Image.new("RGB", (W, H), PAPER)
    d = ImageDraw.Draw(img)

    # 尾鳍 (两个三角)
    d.polygon([(16, 50), (2, 38), (7, 54)], fill=BODY, outline=INK)
    d.polygon([(16, 56), (2, 68), (11, 59)], fill=BODY, outline=INK)
    # 身体
    d.ellipse((12, 34, 106, 86), fill=BODY, outline=INK, width=2)
    # 肚皮
    d.ellipse((28, 62, 92, 86), fill=BELLY)
    # 侧鳍
    d.ellipse((38, 70, 60, 86), fill=DARK, outline=INK)
    # 喷水
    d.line((84, 34, 88, 24), fill=DARK, width=2)
    d.line((92, 34, 95, 26), fill=DARK, width=2)
    d.ellipse((86, 18, 91, 23), fill=DARK)
    d.ellipse((94, 20, 99, 25), fill=DARK)
    # 弯弯眼 (开心)
    d.arc((74, 50, 88, 64), start=200, end=340, fill=INK, width=3)
    d.arc((90, 50, 104, 64), start=200, end=340, fill=INK, width=3)
    # 腮红
    d.ellipse((70, 68, 76, 74), fill=BLUSH)
    d.ellipse((98, 68, 104, 74), fill=BLUSH)
    # 微笑
    d.arc((78, 62, 92, 74), start=20, end=160, fill=INK, width=3)

    # 转 RGB565 头文件 (背景压平为纸色)
    pixels = list(img.getdata())
    rows = []
    for i in range(0, len(pixels), 12):
        chunk = pixels[i:i + 12]
        line = ", ".join(
            "0x{:04X}".format(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
            for r, g, b in chunk
        )
        rows.append("  " + line + ",")

    header = f"""#pragma once
// 默认立绘: 程序生成的中性漫画小鲸鱼 (tools/gen_default_mascot.py, 勿手改)
// 想换成自己的图片: python tools/make_mascot.py 你的图片.png
// 尺寸: {W}x{H}  RGB565  背景已压平为纸色
#include <pgmspace.h>

#define MASCOT_W {W}
#define MASCOT_H {H}

const uint16_t MASCOT_DATA[MASCOT_W * MASCOT_H] = {{
{chr(10).join(rows)}
}};
"""
    with open(HDR_PATH, "w", encoding="utf-8") as f:
        f.write(header)
    print("已生成默认立绘:", HDR_PATH)


if __name__ == "__main__":
    main()
