#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把参考图去背景后转成固件点阵数据 (RGB565) 和预览 PNG

用法: python tools/make_mascot.py [图片路径]
不传参数时默认使用 local/reference.png
"""
import os
import sys
from collections import deque
from PIL import Image

PROJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HDR_PATH = os.path.join(PROJ, "firmware", "include", "mascot.h")
PNG_PATH = os.path.join(PROJ, "mascot_preview.png")

SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.join(PROJ, "assets", "mascot_girl_reference.png")

PAPER = (255, 246, 230)          # 固件 C_PAPER 渲染色 #FFF6E6
TARGET_H = 100                    # 屏上高度 (像素)
BG = (248, 248, 248)              # 参考图背景色
TOL = 7                           # 背景容差


def cut_background(img):
    """从四边泛洪抠掉与边缘连通的背景色 (不碰内部的白色服装)"""
    w, h = img.size
    px = img.load()

    def is_bg(x, y):
        r, g, b = px[x, y][:3]
        return abs(r - BG[0]) <= TOL and abs(g - BG[1]) <= TOL and abs(b - BG[2]) <= TOL

    visited = bytearray(w * h)
    mask = bytearray(w * h)   # 1 = 背景待抠除
    q = deque()
    for x in range(w):
        q.append((x, 0))
        q.append((x, h - 1))
    for y in range(h):
        q.append((0, y))
        q.append((w - 1, y))
    while q:
        x, y = q.popleft()
        if x < 0 or y < 0 or x >= w or y >= h:
            continue
        idx = y * w + x
        if visited[idx]:
            continue
        visited[idx] = 1
        if not is_bg(x, y):
            continue
        mask[idx] = 1
        q.append((x + 1, y))
        q.append((x - 1, y))
        q.append((x, y + 1))
        q.append((x, y - 1))

    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    op = out.load()
    for y in range(h):
        for x in range(w):
            if not mask[y * w + x]:
                r, g, b = px[x, y][:3]
                op[x, y] = (r, g, b, 255)
    removed = sum(mask)
    print(f"泛洪抠背景: 去除 {removed} 像素 ({100.0 * removed / (w * h):.1f}%)")
    return out


def main():
    img = Image.open(SRC).convert("RGB")
    print("原图:", img.size)

    cut = cut_background(img)

    # 裁掉透明边
    bbox = cut.getbbox()
    cut = cut.crop(bbox)
    print("裁剪后:", cut.size)

    # 等比缩放到目标高度
    w = round(cut.width * TARGET_H / cut.height)
    cut = cut.resize((w, TARGET_H), Image.LANCZOS)
    print("缩放后:", cut.size)

    # 透明区域压到纸色 (屏幕没有 alpha)
    bg = Image.new("RGBA", cut.size, PAPER + (255,))
    flat = Image.alpha_composite(bg, cut).convert("RGB")

    # 导出 mockup 用的 PNG
    flat.save(PNG_PATH)

    # 生成 RGB565 数组
    pixels = list(flat.getdata())
    c_arr = []
    for i in range(0, len(pixels), 12):
        chunk = pixels[i:i + 12]
        line = ", ".join(
            "0x{:04X}".format(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
            for r, g, b in chunk
        )
        c_arr.append("  " + line + ",")

    header = f"""#pragma once
// 鲸鱼娘立绘点阵 (由 make_mascot.py 自动生成, 勿手改)
// 尺寸: {w}x{TARGET_H}  RGB565  背景已压平为纸色
#include <pgmspace.h>

#define MASCOT_W {w}
#define MASCOT_H {TARGET_H}

const uint16_t MASCOT_DATA[MASCOT_W * MASCOT_H] = {{
{chr(10).join(c_arr)}
}};
"""
    with open(HDR_PATH, "w", encoding="utf-8") as f:
        f.write(header)
    print("已生成:", HDR_PATH, f"({os.path.getsize(HDR_PATH)//1024} KB)")
    print("预览图:", PNG_PATH)


if __name__ == "__main__":
    main()
