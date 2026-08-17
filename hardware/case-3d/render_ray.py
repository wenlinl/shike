#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""SDF 光线追踪渲染预览图（正面 / 背面 / 装配爆炸图），无需三角面排序。"""

import os

import numpy as np
from PIL import Image

from build_case import (sdf_front, sdf_roof, sdf_cover, sdf_board,
                        camera_port_sdf,
                        cfg_derived, CFG)


OUT = os.path.dirname(os.path.abspath(__file__))
ROOF_RED = (214, 69, 65)
BODY_CREAM = (243, 234, 216)
COVER_CREAM = (239, 227, 204)
BOARD_GREEN = (95, 158, 110)
BG = (255, 255, 255)


def rgb(f):
    return (np.asarray(f, dtype=float) / 255.0)


def hit_normal(sdf_fn, d, pts):
    """中心差分求 SDF 梯度（法线）。"""
    h = 0.15
    nrm = np.zeros_like(pts)
    for ax in range(3):
        p1 = pts.copy(); p1[:, ax] += h
        p0 = pts.copy(); p0[:, ax] -= h
        nrm[:, ax] = sdf_fn(p1, d) - sdf_fn(p0, d)
    ln = np.linalg.norm(nrm, axis=1, keepdims=True)
    ln[ln == 0] = 1.0
    return nrm / ln


def render_sdf(sdf_fn, d, xs, ys, z0, z1, dz, color_fn, look="+z"):
    """正交投影光线步进。look='+z' 相机在 -z 看向 +z。"""
    X, Y = np.meshgrid(xs, ys)
    Xf = X.ravel().astype(np.float32)
    Yf = Y.ravel().astype(np.float32)
    n = Xf.size
    depth = np.full(n, np.inf, dtype=np.float32)
    color = np.zeros((n, 3), dtype=np.float32)
    z = z0
    prev = np.ones(n, dtype=np.float32)
    direction = 1.0 if dz > 0 else -1.0
    while (direction * (z - z1) < 0) and np.isinf(depth).any():
        p = np.stack([Xf, Yf, np.full(n, z, np.float32)], axis=-1)
        s = sdf_fn(p, d).astype(np.float32)
        hit = (prev >= 0) & (s < 0) & np.isinf(depth)
        if hit.any():
            idx = np.where(hit)[0]
            denom = prev[idx] - s[idx]
            denom[denom == 0] = 1e-6
            t = prev[idx] / denom
            zh = (z - dz) + t * dz
            pts = np.stack([Xf[idx], Yf[idx], zh], axis=-1)
            nrm = hit_normal(sdf_fn, d, pts)
            facing = -nrm[:, 2] if look == "+z" else nrm[:, 2]
            depth[idx] = zh
            color[idx] = color_fn(pts, nrm, facing)
        prev = s
        z += dz
    return depth.reshape(len(ys), len(xs)), color.reshape(len(ys), len(xs), 3)


def shade(rgb, facing):
    f = np.clip(0.68 + 0.32 * facing, 0.25, 1.0)
    return rgb[None, None, :] * f[..., None]


def render_front():
    d = cfg_derived(CFG)
    xs = np.linspace(-36.0, 36.0, 800)
    ys = np.linspace(-56.0, 76.0, 1460)

    def c_body(pts, nrm, facing):
        c = np.full((len(pts), 3), rgb(BODY_CREAM))
        port = camera_port_sdf(pts, d) < 0.0
        c = np.where(port[:, None], rgb((35, 32, 30)), c)
        return c * np.clip(0.68 + 0.32 * facing, 0.25, 1.0)[:, None]

    def c_roof(pts, nrm, facing):
        return rgb(ROOF_RED) * np.clip(0.68 + 0.32 * facing, 0.25, 1.0)[:, None]

    d1, c1 = render_sdf(sdf_front, d, xs, ys, -27.0, 13.0, 0.4, c_body)
    d2, c2 = render_sdf(sdf_roof, d, xs, ys, -27.0, 16.0, 0.4, c_roof)
    ds = np.stack([d1, d2])
    cs = np.stack([c1, c2])
    i = np.argmin(ds, axis=0)
    dep = np.take_along_axis(ds, i[None], axis=0)[0]
    col = np.take_along_axis(cs, i[None, ..., None], axis=0)[0]
    img = np.where(np.isinf(dep)[..., None], rgb(BG), col)
    Image.fromarray((img * 255).astype(np.uint8)).save(
        os.path.join(OUT, "预览-正面.png"))
    print("预览-正面.png 已生成")


def render_back():
    d = cfg_derived(CFG)
    xs = np.linspace(-36.0, 36.0, 800)
    ys = np.linspace(-56.0, 56.0, 1240)

    def color_fn(pts, nrm, facing):
        return rgb(COVER_CREAM) * np.clip(0.68 + 0.32 * facing, 0.25, 1.0)[:, None]

    dep, col = render_sdf(sdf_cover, d, xs, ys, 17.0, 4.0, -0.4, color_fn, look="-z")
    img = np.where(np.isinf(dep)[..., None], rgb(BG), col)
    Image.fromarray((img * 255).astype(np.uint8)).save(
        os.path.join(OUT, "预览-背面.png"))
    print("预览-背面.png 已生成")


def render_assembly():
    d = cfg_derived(CFG)
    xs = np.linspace(-48.0, 48.0, 1000)
    ys = np.linspace(-62.0, 80.0, 1480)

    def shifted(fn, dz):
        return lambda p, d=d: fn(p + np.array([0.0, 0.0, dz], np.float32), d)

    def c_front(pts, nrm, facing):
        c = np.full((len(pts), 3), rgb(BODY_CREAM))
        inside = (pts[:, 2:3] + 34.0) > -10.0         # 前壳内壁（原始坐标 z > -10）
        c = np.where(inside, c * 0.5, c)
        port = camera_port_sdf(pts + np.array([0.0, 0.0, 34.0]), d) < 0.0
        c = np.where(port[:, None], rgb((35, 32, 30)), c)
        return c * np.clip(0.68 + 0.32 * facing, 0.25, 1.0)[:, None]

    def c_board(pts, nrm, facing):
        return rgb(BOARD_GREEN) * np.clip(0.68 + 0.32 * facing, 0.25, 1.0)[:, None]

    def c_cover(pts, nrm, facing):
        return rgb(COVER_CREAM) * np.clip(0.68 + 0.32 * facing, 0.25, 1.0)[:, None]

    def c_roof(pts, nrm, facing):
        return rgb(ROOF_RED) * np.clip(0.68 + 0.32 * facing, 0.25, 1.0)[:, None]

    # 前壳（显示坐标 z-34）/ 主板（z+6）/ 后盖（z+36）
    d1, c1 = render_sdf(shifted(sdf_front, 34.0), d, xs, ys, -56.0, -20.0, 0.4, c_front)
    d2, c2 = render_sdf(shifted(sdf_board, -6.0), d, xs, ys, -12.0, 13.0, 0.4, c_board)
    d3, c3 = render_sdf(shifted(sdf_cover, -36.0), d, xs, ys, 40.0, 52.0, 0.4, c_cover)
    d4, c4 = render_sdf(shifted(sdf_roof, 38.0), d, xs, ys, -58.0, -20.0, 0.4, c_roof)

    ds = np.stack([d1, d2, d3, d4])
    cs = np.stack([c1, c2, c3, c4])
    i = np.argmin(ds, axis=0)
    dep = np.take_along_axis(ds, i[None], axis=0)[0]
    col = np.take_along_axis(cs, i[None, ..., None], axis=0)[0]
    img = np.where(np.isinf(dep)[..., None], rgb(BG), col)
    Image.fromarray((img * 255).astype(np.uint8)).save(
        os.path.join(OUT, "预览-装配图.png"))
    print("预览-装配图.png 已生成")


if __name__ == "__main__":
    render_front()
    render_back()
    render_assembly()
