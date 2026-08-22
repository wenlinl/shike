#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
食刻 · T5+AI 开发板（T5AI-Board）冰箱贴智能屏外壳 —— 参数化 3D 建模
====================================================================

按产品示意图造型：红色坡屋顶（右高左低）+ 左烟囱、奶白圆角屋身、
正面圆角屏幕窗口（红色描边框）、右上白色拱形摄像头环、底部 USB 槽、
右侧喇叭孔、磁吸后盖。整机扁平（深度约 30 mm）。

主板数据来自用户提供的真实 STL（T5+AI开发板.stl）：
  整板 110×87×22.8 mm，屏子板顶面 108×85 mm，
  摄像头模组中心位于板面坐标 (X=67.5, Y=5.2)，凸出屏面约 2 mm。
  按键(RST/IO)在板 Y=84 边，喇叭格栅在板 X=108 边（Y≈27~55）。

输出（binary STL，单位 mm，可直接导入 Bambu Studio）：
  食刻-T5前壳.stl      屋身 + 屋顶一体（打印面：正面朝下）
  食刻-T5后盖.stl      平板后盖（磁铁沉孔 + 螺丝孔 + 主板支撑柱）
  食刻-T5主板参考.stl  主板占位模型（仅做装配检查，不要打印）

用法：
  python3 build_case_t5.py --part all --res 0.5
  python3 build_case_t5.py --part front
  python3 build_case_t5.py --part cover
  python3 build_case_t5.py --part board
"""

import argparse
import os

import numpy as np
from mcubes import marching_cubes
from stl import mesh as stl_mesh


# =====================================================================
# 配置（单位 mm）
# =====================================================================
CFG = {
    # ---------- 主板 T5+AI 开发板（T5AI-Board = T5-Board + 3.5" LCD 子板 + GC2145） ----------
    "board_w": 110.0,     # 主板宽 X（STL 实测）
    "board_h": 87.0,      # 主板高 Y（STL 实测）
    "board_t": 22.8,      # 主板厚 Z（屏面到背板最厚处）
    "lcd_w": 108.0,       # 屏子板顶面宽（正面窗口）
    "lcd_h": 85.0,        # 屏子板顶面高
    "lcd_d": 8.0,         # 屏子板厚度（窗口深度）
    "cam_x": 12.5,        # 摄像头中心 X（板中心为原点，向右为正）
    "cam_y": 38.3,        # 摄像头中心 Y（板中心为原点，向上为正；板 Y=0 边朝上）
    "cam_d": 9.0,         # 摄像头开孔直径（镜头外径 + 余量）
    "usb_w": 15.0,        # USB-C 底部开口宽
    "usb_h": 9.0,         # USB-C 底部开口高
    "btn_d": 6.0,         # 底部调试按键孔直径（RST，位置可微调）
    "btn_x": -28.5,       # 按键孔 X（底部边）
    "btn_y": -40.5,       # 按键孔 Y（底部边）

    # ---------- 外壳结构 ----------
    "wall": 2.0,          # 壳体壁厚
    "cover_t": 3.0,       # 后盖厚度
    "gap": 1.5,           # 主板背面到后盖内壁的间隙
    "margin_x": 6.0,      # 主板两侧到壳壁的间隙
    "margin_y": 8.0,      # 主板上下到壳壁的间隙
    "rib": 3.0,           # 内腔相对壳体各缩进的加强筋量
    "round_r": 5.0,       # 屋身圆角半径

    # ---------- 屋顶（右高左低坡屋顶 + 左烟囱） ----------
    "roof_h_right": 22.0, # 屋顶右侧（高侧）高度
    "roof_h_left": 7.0,   # 屋顶左侧（低侧）高度
    "ovf_f": 10.0,        # 屋顶前檐伸出量
    "ovf_b": 4.0,         # 屋顶后檐伸出量
    "ovf_x": 3.0,         # 屋顶左右伸出量
    "chim_w": 15.0,       # 烟囱宽 X
    "chim_h": 16.0,       # 烟囱高 Y
    "chim_d": 13.0,       # 烟囱深 Z
    "chim_x": -42.0,      # 烟囱中心 X（左侧屋顶）
    "chim_z": 1.0,        # 烟囱中心 Z（略偏前）

    # ---------- 细节造型 ----------
    "win_gap": 1.0,       # 屏幕窗口相对屏子板顶面的间隙（负值=更贴）
    "frame_w": 2.5,       # 屏幕红色描边框宽度
    "frame_h": 0.8,       # 屏幕红色描边框凸起高度
    "cam_ring_d": 16.0,   # 摄像头白色拱形环外径
    "cam_ring_h": 1.2,    # 摄像头环凸起高度

    # ---------- 固定件 ----------
    "screw_d": 2.2,       # M2 螺丝孔直径（2.2 = 自攻 M2）
    "boss_d": 6.0,        # 螺丝柱外径
    "magnet_d": 10.0,     # 磁铁直径（4 颗 N52 Ø10×2）
    "magnet_dp": 2.2,     # 磁铁沉孔深度

    # ---------- 输出 ----------
    "out_dir": os.path.dirname(os.path.abspath(__file__)),
}


def cfg_derived(c):
    """由主板尺寸推导外壳尺寸。"""
    d = dict(c)
    d["body_w"] = c["board_w"] + 2 * c["wall"] + 2 * c["margin_x"]
    d["body_h"] = c["board_h"] + 2 * c["wall"] + 2 * c["margin_y"]
    d["body_d"] = c["wall"] + c["board_t"] + c["gap"] + c["cover_t"]
    d["split_z"] = d["body_d"] / 2.0 - c["cover_t"]
    d["cav_w"] = d["body_w"] - 2 * c["wall"] - 2 * c["rib"]
    d["cav_h"] = d["body_h"] - 2 * c["wall"] - 2 * c["rib"]
    d["cav_z0"] = -d["body_d"] / 2.0 + c["wall"]
    d["cav_d"] = d["split_z"] - d["cav_z0"]
    d["front_z"] = -d["body_d"] / 2.0
    d["board_lcd_z"] = d["front_z"] + 0.2
    d["board_back_z"] = d["board_lcd_z"] + c["board_t"]
    d["win_z0"] = d["front_z"] - 0.2
    d["win_z1"] = d["board_lcd_z"] + c["lcd_d"] - 0.2
    d["win_zc"] = (d["win_z0"] + d["win_z1"]) / 2.0
    d["win_zh"] = (d["win_z1"] - d["win_z0"]) / 2.0
    d["y0"] = d["body_h"] / 2.0
    d["win_w"] = c["lcd_w"] + c["win_gap"]
    d["win_h"] = c["lcd_h"] + c["win_gap"]
    d["roof_x_half"] = d["body_w"] / 2.0 + c["ovf_x"]
    d["roof_slope"] = (c["roof_h_right"] - c["roof_h_left"]) / (2.0 * d["roof_x_half"])
    d["cam_ring_z"] = d["front_z"] - c["cam_ring_h"] / 2.0
    # 螺丝柱 / 磁铁 / 板撑位置
    d["boss_xy"] = [(-d["body_w"] / 2 + 7.0, -d["body_h"] / 2 + 7.0),
                    (+d["body_w"] / 2 - 7.0, -d["body_h"] / 2 + 7.0),
                    (-d["body_w"] / 2 + 7.0, +d["body_h"] / 2 - 7.0),
                    (+d["body_w"] / 2 - 7.0, +d["body_h"] / 2 - 7.0)]
    d["mag_xy"] = [(-d["body_w"] / 2 + 15.0, -d["body_h"] / 2 + 15.0),
                   (+d["body_w"] / 2 - 15.0, -d["body_h"] / 2 + 15.0),
                   (-d["body_w"] / 2 + 15.0, +d["body_h"] / 2 - 15.0),
                   (+d["body_w"] / 2 - 15.0, +d["body_h"] / 2 - 15.0)]
    d["std_xy"] = [(-c["board_w"] / 2 + 4.0, -c["board_h"] / 2 + 4.0),
                   (+c["board_w"] / 2 - 4.0, -c["board_h"] / 2 + 4.0),
                   (-c["board_w"] / 2 + 4.0, +c["board_h"] / 2 - 4.0),
                   (+c["board_w"] / 2 - 4.0, +c["board_h"] / 2 - 4.0)]
    d["std_h"] = max(1.0, d["split_z"] - d["board_back_z"])
    return d


# =====================================================================
# SDF 基元（负值在内部）
# =====================================================================
def sd_box(p, c, s):
    q = np.abs(p - np.asarray(c, np.float64)) - 0.5 * np.asarray(s, np.float64)
    return (np.linalg.norm(np.maximum(q, 0.0), axis=-1)
            + np.minimum(np.maximum(q[:, 0], np.maximum(q[:, 1], q[:, 2])), 0.0))


def sd_rounded_box(p, c, s, r):
    q = np.abs(p - np.asarray(c, np.float64)) - 0.5 * np.asarray(s, np.float64) + r
    return (np.linalg.norm(np.maximum(q, 0.0), axis=-1)
            + np.minimum(np.maximum(q[:, 0], np.maximum(q[:, 1], q[:, 2])), 0.0) - r)


def sd_cyl_axis(p, a, u, r, length):
    """有限长圆柱：a=轴心点, u=单位轴向, r=半径, length=总长"""
    v = p - np.asarray(a, np.float64)
    u = np.asarray(u, np.float64)
    u = u / np.linalg.norm(u)
    along = v @ u
    perp = v - along[:, None] * u
    d_perp = np.linalg.norm(perp, axis=-1) - r
    d_along = np.abs(along) - 0.5 * length
    return np.maximum(d_perp, d_along)


def sd_roof_shed(p, d):
    """右高左低单坡屋顶（屋脊沿 Z 方向，右端高、左端低），负值在屋顶内。"""
    c = d
    xh = c["roof_x_half"]
    y0 = c["y0"]
    y_r = y0 + c["roof_h_left"] + c["roof_slope"] * (p[:, 0] + xh)
    d1 = p[:, 1] - y_r                       # 屋顶斜面下方
    d2 = y0 - p[:, 1]                        # 屋身顶面上方
    d3 = np.abs(p[:, 0]) - xh                # 左右端墙
    zf = c["ovf_f"] + c["body_d"] / 2.0
    zb = c["ovf_b"] + c["body_d"] / 2.0
    d4 = np.maximum(-zf - p[:, 2], p[:, 2] - zb)   # 前后挑檐
    return np.maximum.reduce([d1, d2, d3, d4])


def sd_chimney(p, d):
    """左屋顶上的烟囱（圆角方柱），负值在烟囱内。"""
    c = d
    xh = c["roof_x_half"]
    y_r = c["y0"] + c["roof_h_left"] + c["roof_slope"] * (c["chim_x"] + xh)
    cy = y_r + c["chim_h"] / 2.0
    return sd_rounded_box(p, (c["chim_x"], cy, c["chim_z"]),
                          (c["chim_w"], c["chim_h"], c["chim_d"]), 3.0)


def sd_frame_ring(p, d):
    """屏幕窗口四周的描边框（凸出正面 0.8mm），负值在框内。"""
    c = d
    zw = c["front_z"] - c["frame_h"] / 2.0
    outer = sd_rounded_box(p, (0.0, 0.0, zw),
                           (c["win_w"] + 2 * c["frame_w"],
                            c["win_h"] + 2 * c["frame_w"],
                            c["frame_h"] + 0.5), 3.0)
    inner = sd_rounded_box(p, (0.0, 0.0, zw),
                           (c["win_w"], c["win_h"], c["frame_h"] + 1.0), 2.5)
    return np.maximum(outer, -inner)


def sd_cam_ring(p, d):
    """摄像头白色拱形环（凸出正面 1.2mm，中心孔与摄像头开孔同轴）。"""
    c = d
    outer = sd_cyl_axis(p, (c["cam_x"], c["cam_y"], c["cam_ring_z"]),
                        (0.0, 0.0, 1.0), c["cam_ring_d"] / 2.0, c["cam_ring_h"] + 0.5)
    inner = sd_cyl_axis(p, (c["cam_x"], c["cam_y"], c["cam_ring_z"]),
                        (0.0, 0.0, 1.0), c["cam_d"] / 2.0 + 0.3, c["cam_ring_h"] + 1.0)
    return np.maximum(outer, -inner)


def camera_tunnel_sdf(p, d):
    """摄像头孔：穿过白色环 + 前壁 + 内腔（给镜头留 2mm 凸出余量）。"""
    c = d
    zc = c["front_z"] - 1.0
    return sd_cyl_axis(p, (c["cam_x"], c["cam_y"], zc),
                       (0.0, 0.0, 1.0), c["cam_d"] / 2.0 + 0.3, 8.0)


# =====================================================================
# 三个部件
# =====================================================================
def sdf_front(p, d):
    c = d
    # 屋身 + 屋顶 + 烟囱
    body = sd_rounded_box(p, (0.0, 0.0, 0.0),
                          (c["body_w"], c["body_h"], c["body_d"]), c["round_r"])
    sd = np.minimum(body, sd_roof_shed(p, c))
    sd = np.minimum(sd, sd_chimney(p, c))

    # 内腔（背面敞开）
    cav = sd_box(p, (0.0, 0.0, c["cav_z0"] + c["cav_d"] / 2.0),
                 (c["cav_w"], c["cav_h"], c["cav_d"]))
    sd = np.maximum(sd, -cav)

    # 屏幕窗口
    win = sd_rounded_box(p, (0.0, 0.0, c["win_zc"]),
                         (c["win_w"], c["win_h"], 2.0 * c["win_zh"]), 2.0)
    sd = np.maximum(sd, -win)

    # 摄像头孔
    sd = np.maximum(sd, -camera_tunnel_sdf(p, c))

    # 屏幕描边框 + 摄像头白色环
    sd = np.minimum(sd, sd_frame_ring(p, c))
    sd = np.minimum(sd, sd_cam_ring(p, c))

    # USB-C 底部开口
    usb = sd_box(p, (0.0, -c["body_h"] / 2.0 + c["usb_h"] / 2.0, 0.0),
                 (c["usb_w"], c["usb_h"], c["body_d"] + 2.0))
    sd = np.maximum(sd, -usb)

    # 底部调试按键孔
    btn = sd_cyl_axis(p, (c["btn_x"], -c["body_h"] / 2.0 + 1.0, 0.0),
                      (0.0, 1.0, 0.0), c["btn_d"] / 2.0, c["wall"] + 2.0)
    sd = np.maximum(sd, -btn)

    # 右侧喇叭孔（2 列 × 3 行，对应板 X=108 边 Y≈27~55 的喇叭格栅）
    for sz in (-6.0, 6.0):
        for sy in (-10.0, 2.0, 14.0):
            spk = sd_cyl_axis(p, (c["body_w"] / 2.0 + 1.0, sy, sz),
                              (1.0, 0.0, 0.0), 1.25, c["wall"] + 2.0)
            sd = np.maximum(sd, -spk)

    # 螺丝柱 + 沉头盲孔
    boss_h = c["split_z"] - c["cav_z0"]
    for bx, by in c["boss_xy"]:
        boss = sd_cyl_axis(p, (bx, by, c["cav_z0"] + boss_h / 2.0),
                           (0.0, 0.0, 1.0), c["boss_d"] / 2.0, boss_h)
        sd = np.minimum(sd, boss)
        bh = sd_cyl_axis(p, (bx, by, c["split_z"] - 0.5),
                         (0.0, 0.0, 1.0), c["screw_d"] / 2.0, 10.0)
        sd = np.maximum(sd, -bh)

    # 分模面截断（z <= split_z）
    sd = np.maximum(sd, p[:, 2] - c["split_z"])
    return sd


def sdf_cover(p, d):
    c = d
    plate = sd_rounded_box(p, (0.0, 0.0, c["body_d"] / 2.0 - c["cover_t"] / 2.0),
                           (c["body_w"], c["body_h"], c["cover_t"]), 2.0)
    sd = plate

    # 磁铁沉孔（4×Ø10×2.2，留 1.2mm 贴冰箱壁）
    for mx, my in c["mag_xy"]:
        pk = sd_cyl_axis(p, (mx, my, c["body_d"] / 2.0),
                         (0.0, 0.0, 1.0), c["magnet_d"] / 2.0, c["magnet_dp"])
        sd = np.maximum(sd, -pk)

    # 主板支撑柱（4 个，从后盖内面伸向主板背面）
    for sx, sy in c["std_xy"]:
        st = sd_cyl_axis(p, (sx, sy, c["split_z"] - c["std_h"] / 2.0),
                         (0.0, 0.0, 1.0), 2.25, c["std_h"])
        sd = np.minimum(sd, st)

    # M2 螺丝通孔 + 沉头
    for bx, by in c["boss_xy"]:
        h1 = sd_cyl_axis(p, (bx, by, c["body_d"] / 2.0 - 1.0),
                         (0.0, 0.0, 1.0), c["screw_d"] / 2.0, c["cover_t"] + 1.0)
        sd = np.maximum(sd, -h1)
        h2 = sd_cyl_axis(p, (bx, by, c["body_d"] / 2.0 - 1.0),
                         (0.0, 0.0, 1.0), 2.1, 2.0)
        sd = np.maximum(sd, -h2)

    # 后盖主体在分模面之后，支撑柱伸入前腔内
    sd = np.maximum(sd, (c["board_back_z"] - 1.5) - p[:, 2])
    return sd


def sdf_board(p, d):
    """主板占位参考模型（不打印）：底板 + 屏子板 + 摄像头模组。"""
    c = d
    bd_z = c["board_back_z"] - c["board_t"] / 2.0
    sd = sd_box(p, (0.0, 0.0, bd_z), (c["board_w"], c["board_h"], c["board_t"]))
    lcd_z = c["board_lcd_z"] + c["lcd_d"] / 2.0
    lcd = sd_rounded_box(p, (0.0, 0.0, lcd_z),
                         (c["lcd_w"], c["lcd_h"], c["lcd_d"]), 2.0)
    sd = np.minimum(sd, lcd)
    cam = sd_cyl_axis(p, (c["cam_x"], c["cam_y"], c["board_lcd_z"] + c["lcd_d"] + 1.0),
                      (0.0, 0.0, 1.0), 4.0, 6.0)
    sd = np.minimum(sd, cam)
    return sd


# =====================================================================
# 网格化 + 导出
# =====================================================================
PART_SDF = {"front": sdf_front, "cover": sdf_cover, "board": sdf_board}


def part_bounds(name, d):
    b = d
    if name == "front":
        x = b["body_w"] / 2.0 + b["ovf_x"] + 6.0
        y_hi = b["y0"] + max(b["roof_h_right"], b["chim_h"] + b["roof_h_left"]) + 6.0
        y_lo = -(b["body_h"] / 2.0) - 6.0
        z_lo = -(b["body_d"] / 2.0) - b["ovf_f"] - 6.0
        z_hi = max(b["split_z"], b["body_d"] / 2.0 + b["ovf_b"]) + 2.0
    elif name == "cover":
        x = b["body_w"] / 2.0 + 4.0
        y_hi = b["body_h"] / 2.0 + 4.0
        y_lo = -y_hi
        z_lo = min(b["split_z"], b["board_back_z"]) - 3.0
        z_hi = b["body_d"] / 2.0 + 2.0
    else:  # board
        x = b["board_w"] / 2.0 + 6.0
        y_hi = b["board_h"] / 2.0 + 6.0
        y_lo = -y_hi
        z_lo = b["front_z"] - 8.0
        z_hi = b["split_z"] + 2.0
    return x, y_lo, y_hi, z_lo, z_hi


def build_stl(name, res=0.5):
    d = cfg_derived(CFG)
    x_max, y_lo, y_hi, z_lo, z_hi = part_bounds(name, d)
    nx = int(np.ceil((2 * x_max) / res)) + 1
    ny = int(np.ceil((y_hi - y_lo) / res)) + 1
    nz = int(np.ceil((z_hi - z_lo) / res)) + 1
    xs = np.linspace(-x_max, x_max, nx)
    ys = np.linspace(y_lo, y_hi, ny)
    zs = np.linspace(z_lo, z_hi, nz)
    X, Y, Z = np.meshgrid(xs, ys, zs, indexing="ij")
    p = np.stack([X.ravel(), Y.ravel(), Z.ravel()], axis=-1).astype(np.float32)
    sdf = PART_SDF[name](p, d).astype(np.float32)
    sdf = sdf.reshape(nx, ny, nz)

    verts, faces = marching_cubes(-sdf, 0.0)
    verts = verts * res
    verts[:, 0] += -x_max
    verts[:, 1] += y_lo
    verts[:, 2] += z_lo
    return verts, faces


def save_stl(verts, faces, path):
    m = stl_mesh.Mesh(np.zeros(len(faces), dtype=stl_mesh.Mesh.dtype))
    m.vectors = verts[faces]
    m.update_normals()
    m.save(path)
    bb = verts.min(axis=0), verts.max(axis=0)
    print(f"  {os.path.basename(path)}: {len(verts)} 顶点 / {len(faces)} 三角面, "
          f"包围盒 X{bb[0][0]:.1f}~{bb[1][0]:.1f}  Y{bb[0][1]:.1f}~{bb[1][1]:.1f}  "
          f"Z{bb[0][2]:.1f}~{bb[1][2]:.1f} mm")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--part", default="all", choices=["all", "front", "cover", "board"])
    ap.add_argument("--res", type=float, default=0.5, help="网格分辨率 mm")
    args = ap.parse_args()

    out = CFG["out_dir"]
    names = ["front", "cover", "board"] if args.part == "all" else [args.part]
    files = {
        "front": "食刻-T5前壳.stl",
        "cover": "食刻-T5后盖.stl",
        "board": "食刻-T5主板参考.stl",
    }
    print(f"网格分辨率: {args.res} mm")
    for n in names:
        print(f"生成 {files[n]} ...")
        verts, faces = build_stl(n, args.res)
        save_stl(verts, faces, os.path.join(out, files[n]))
    print("完成。")


if __name__ == "__main__":
    main()
