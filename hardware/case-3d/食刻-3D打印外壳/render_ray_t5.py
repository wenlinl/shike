#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""食刻 T5 版外壳预览渲染（SDF 光线追踪）：正面 / 背面 / 装配爆炸图。"""

import os

import numpy as np
from PIL import Image

from build_case_t5 import (sdf_front, sdf_cover, sdf_board, sd_frame_ring,
                           sd_cam_ring, camera_tunnel_sdf, sd_roof_shed,
                           sd_chimney, cfg_derived, CFG)


OUT = os.path.dirname(os.path.abspath(__file__))
ROOF_RED = (214, 69, 65)
BODY_CREAM = (243, 234, 216)
COVER_CREAM = (239, 227, 204)
FRAME_RED = (196, 48, 46)
CAM_RING = (248, 244, 236)
BOARD_GREEN = (95, 158, 110)
HOLE_DARK = (35, 32, 30)
BG = (255, 255, 255)


def rgb(f):
    return np.asarray(f, dtype=float) / 255.0


def hit_normal(sdf_fn, d, pts):
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


def shade(rgbv, facing):
    f = np.clip(0.68 + 0.32 * facing, 0.25, 1.0)
    return rgbv[None, None, :] * f[..., None]


def front_color_fn(d):
    y0 = d["y0"]
    zw = d["front_z"]
    hw = d["win_w"] / 2.0
    hh = d["win_h"] / 2.0
    fw = d["frame_w"]
    fh = d["frame_h"]
    cam_c = np.array([d["cam_x"], d["cam_y"]])
    ring_r = d["cam_ring_d"] / 2.0
    hole_r = d["cam_d"] / 2.0 + 0.3

    def color_fn(pts, nrm, facing):
        x = pts[:, 0]; y = pts[:, 1]; z = pts[:, 2]
        in_roof = y > y0 + 0.2
        # 屏幕红色描边框：在正面附近、位于窗口外圈矩形带内
        in_frame = ((np.abs(z - zw) < fh + 0.8) &
                    (np.abs(x) < hw + fw + 0.5) &
                    (np.abs(y) < hh + fw + 0.5) &
                    ((np.abs(x) > hw - 0.5) | (np.abs(y) > hh - 0.5)))
        # 摄像头：白色环 / 黑色孔
        r = np.sqrt((x - cam_c[0]) ** 2 + (y - cam_c[1]) ** 2)
        near_front = np.abs(z - zw) < 4.0
        in_ring = near_front & (r > hole_r + 0.3) & (r < ring_r + 0.5)
        in_hole = near_front & (r < hole_r + 0.5)
        # 屏幕窗口 / USB / 按键 / 喇叭开口：显示为深色
        in_win = (np.abs(x) < hw) & (np.abs(y) < hh) & near_front
        in_usb = (np.abs(x) < d["usb_w"] / 2.0 + 0.6) & \
                 (y < -d["body_h"] / 2.0 + d["usb_h"] + 1.0) & near_front
        in_btn = (np.abs(x - d["btn_x"]) < d["btn_d"] / 2.0 + 0.6) & \
                 (np.abs(y + d["body_h"] / 2.0) < 4.0) & near_front
        in_spk = (np.abs(x - d["body_w"] / 2.0) < 3.0) & \
                 (np.abs(y - 2.0) < 14.0) & near_front
        in_open = in_win | in_usb | in_btn | in_spk
        c = np.where(in_roof[:, None], rgb(ROOF_RED), rgb(BODY_CREAM))
        c = np.where(in_frame[:, None], rgb(FRAME_RED), c)
        c = np.where(in_open[:, None], rgb((70, 82, 100)), c)
        c = np.where(in_ring[:, None], rgb(CAM_RING), c)
        c = np.where(in_hole[:, None], rgb(HOLE_DARK), c)
        return c * np.clip(0.68 + 0.32 * facing, 0.25, 1.0)[:, None]
    return color_fn


def render_front():
    d = cfg_derived(CFG)
    xm = d["body_w"] / 2 + d["ovf_x"] + 8
    y_hi = d["y0"] + max(d["roof_h_right"], d["chim_h"] + d["roof_h_left"]) + 8
    y_lo = -(d["body_h"] / 2) - 8
    z0 = -(d["body_d"] / 2) - d["ovf_f"] - 6
    z1 = max(d["split_z"] + 3, d["body_d"] / 2 + d["ovf_b"] + 3)
    xs = np.linspace(-xm, xm, 900)
    ys = np.linspace(y_lo, y_hi, 780)
    dep, col = render_sdf(sdf_front, d, xs, ys, z0, z1, 0.45, front_color_fn(d))
    # 后处理：穿过窗口/USB/按键/喇叭开口的像素（未命中实体）显示为深色开口
    X2, Y2 = np.meshgrid(xs, ys)
    hw = d["win_w"] / 2.0
    hh = d["win_h"] / 2.0
    r_cam = np.sqrt((X2 - d["cam_x"]) ** 2 + (Y2 - d["cam_y"]) ** 2)
    cam_zone = r_cam < d["cam_ring_d"] / 2.0 + 0.5
    in_win = (np.abs(X2) < hw) & (np.abs(Y2) < hh) & ~cam_zone
    in_usb = (np.abs(X2) < d["usb_w"] / 2.0 + 0.6) & \
             (Y2 < -d["body_h"] / 2.0 + d["usb_h"] + 1.0)
    in_btn = (np.abs(X2 - d["btn_x"]) < d["btn_d"] / 2.0 + 0.6) & \
             (np.abs(Y2 + d["body_h"] / 2.0) < 4.0)
    in_spk = (np.abs(X2 - d["body_w"] / 2.0) < 3.0) & (np.abs(Y2 - 2.0) < 14.0)
    open_mask = in_win | in_usb | in_btn | in_spk
    open_col = np.array([70, 82, 100], dtype=float) / 255.0
    img = np.where(np.isinf(dep)[..., None], rgb(BG), col)
    img[open_mask] = open_col[None, :]
    img = img[::-1]
    Image.fromarray((img * 255).astype(np.uint8)).save(
        os.path.join(OUT, "预览-T5正面.png"))
    print("预览-T5正面.png 已生成")


def render_back():
    d = cfg_derived(CFG)
    xm = d["body_w"] / 2 + 8
    ym = d["body_h"] / 2 + 8
    xs = np.linspace(-xm, xm, 760)
    ys = np.linspace(-ym, ym, 660)

    def color_fn(pts, nrm, facing):
        return rgb(COVER_CREAM) * np.clip(0.68 + 0.32 * facing, 0.25, 1.0)[:, None]

    dep, col = render_sdf(sdf_cover, d, xs, ys, d["body_d"] / 2 + 4, -3.0, -0.45,
                          color_fn, look="-z")
    img = np.where(np.isinf(dep)[..., None], rgb(BG), col)[::-1]
    Image.fromarray((img * 255).astype(np.uint8)).save(
        os.path.join(OUT, "预览-T5背面.png"))
    print("预览-T5背面.png 已生成")


def render_assembly():
    d = cfg_derived(CFG)
    xm = d["body_w"] / 2 + d["ovf_x"] + 12
    y_hi = d["y0"] + max(d["roof_h_right"], d["chim_h"] + d["roof_h_left"]) + 10
    y_lo = -(d["body_h"] / 2) - 10
    xs = np.linspace(-xm, xm, 780)
    ys = np.linspace(y_lo, y_hi, 690)

    def shifted(fn, dz):
        return lambda p, d=d: fn(p + np.array([0.0, 0.0, dz], np.float32), d)

    def c_front(pts, nrm, facing):
        return front_color_fn(d)(pts, nrm, facing)

    def c_board(pts, nrm, facing):
        return rgb(BOARD_GREEN) * np.clip(0.68 + 0.32 * facing, 0.25, 1.0)[:, None]

    def c_cover(pts, nrm, facing):
        return rgb(COVER_CREAM) * np.clip(0.68 + 0.32 * facing, 0.25, 1.0)[:, None]

    # 前壳（显示坐标 z-38）/ 主板（z+8）/ 后盖（z+40）
    fz0 = -(d["body_d"] / 2) - d["ovf_f"] - 6
    d1, c1 = render_sdf(shifted(sdf_front, 38.0), d, xs, ys,
                        fz0 - 38.0, d["split_z"] - 35.0, 0.45, c_front)
    d2, c2 = render_sdf(shifted(sdf_board, -8.0), d, xs, ys, -8.0, 8.0, 0.45, c_board)
    d3, c3 = render_sdf(shifted(sdf_cover, -40.0), d, xs, ys, 34.0, 48.0, 0.45, c_cover)

    ds = np.stack([d1, d2, d3])
    cs = np.stack([c1, c2, c3])
    i = np.argmin(ds, axis=0)
    dep = np.take_along_axis(ds, i[None], axis=0)[0]
    col = np.take_along_axis(cs, i[None, ..., None], axis=0)[0]
    img = np.where(np.isinf(dep)[..., None], rgb(BG), col)[::-1]
    Image.fromarray((img * 255).astype(np.uint8)).save(
        os.path.join(OUT, "预览-T5装配图.png"))
    print("预览-T5装配图.png 已生成")


if __name__ == "__main__":
    render_front()
    render_back()
    render_assembly()
