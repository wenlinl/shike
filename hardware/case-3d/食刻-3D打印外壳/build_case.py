#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
食刻 · T5-E1 (SPARKLEIOT T5AI DEV) 冰箱贴智能屏外壳 —— 参数化 3D 建模
====================================================================

造型参考「食刻」产品示意图：扁平小房子造型（米白机身 + 红色人字形屋顶 +
左侧烟囱 + 屋顶右侧摄像头孔），正面下方大屏幕窗口，背面磁吸后盖。
红色屋顶是独立件（3D 打印红色），用胶水贴到机身正面上部。

输出（binary STL，单位 mm，可直接导入 Bambu Studio）：
  食刻-前壳.stl         米白机身（屏幕窗口、摄像头通道、USB/按键/喇叭口）
  食刻-屋顶-红色.stl    红色人字形屋顶（烟囱 + 摄像头孔），贴在前壳正面
  食刻-后盖.stl         平板后盖（磁铁沉孔 + 螺丝孔 + 主板支撑柱）
  食刻-主板参考.stl     主板占位模型（仅做装配检查，不打印）

所有尺寸集中在 CONFIG，改参数后重新运行即可。

用法：
  python3 build_case.py --part all --res 0.45
  python3 build_case.py --part front
  python3 build_case.py --part roof
  python3 build_case.py --part cover
  python3 build_case.py --part board
"""

import argparse
import os

import numpy as np
from mcubes import marching_cubes
from stl import mesh as stl_mesh


# =====================================================================
# 配置（单位 mm）—— 标注【待实测】的项以上手实测为准
# =====================================================================
CFG = {
    # ---------- 主板 SPARKLEIOT T5AI DEV / T5-E1 ----------
    "board_w": 50.0,      # 主板宽 X（实测 87×50×20，竖装：50 为宽）
    "board_h": 87.0,      # 主板高 Y
    "board_t": 20.0,      # 主板厚 Z（实测）
    "lcd_w": 48.0,        # 屏幕（可视+边框）宽（实测 67×48，竖装：48 为宽）
    "lcd_h": 67.0,        # 屏幕（可视+边框）高
    "lcd_d": 3.8,         # 屏玻璃到 PCB 正面总高（约）
    "cam_x": 0.0,         # 摄像头中心 X（正中间）
    "cam_y": 40.0,        # 摄像头中心 Y（板上部，正对开口）【待实测】
    "cam_d": 11.0,        # 摄像头通道直径（镜头外径+余量）
    "cam_sq": 17.0,       # 方形摄像头开口边长（正中间 17×17mm）
    "usb_w": 12.0,        # 充电口宽（底部正中间 12mm）
    "usb_h": 17.0,        # 充电口高（从底部往上 17mm）
    "btn_d": 6.5,         # 侧键孔直径
    "btn_y": -32.0,       # 侧键孔 Y（右侧面）

    # ---------- 外壳结构 ----------
    "wall": 2.0,          # 壳体壁厚
    "cover_t": 2.8,       # 后盖厚度（磁铁处留 1.2mm 贴冰箱壁）
    "margin_x": 6.0,      # 主板两侧到壳壁的间隙
    "margin_y": 8.0,      # 主板上下到壳壁的间隙
    "rib": 3.0,           # 内腔相对壳体各缩进的加强筋量
    "round_r": 4.0,       # 屋身圆角半径
    "screw_d": 2.2,       # M2 螺丝孔直径（2.2 = 自攻 M2）
    "boss_d": 6.0,        # 螺丝柱外径
    "magnet_d": 10.0,     # 磁铁直径（4 颗 N52 Ø10×2）
    "magnet_dp": 1.6,     # 磁铁沉孔深度（留 1.2mm 贴冰箱壁）

    # ---------- 输出 ----------
    "out_dir": os.path.dirname(os.path.abspath(__file__)),
}


def cfg_derived(c):
    """由主板尺寸推导外壳尺寸。"""
    d = dict(c)
    d["body_w"] = c["board_w"] + 2 * c["wall"] + 2 * c["margin_x"]
    d["body_h"] = c["board_h"] + 2 * c["wall"] + 2 * c["margin_y"]
    d["body_d"] = c["wall"] + c["board_t"] + 2.0 + c["cover_t"]   # 前壁+板+余量+后盖（扁平）
    d["split_z"] = d["body_d"] / 2.0 - c["cover_t"]               # 前壳/后盖分模面
    d["cav_w"] = d["body_w"] - 2 * c["wall"] - 2 * c["rib"]
    d["cav_h"] = d["body_h"] - 2 * c["wall"] - 2 * c["rib"]
    d["cav_z0"] = -d["body_d"] / 2.0 + c["wall"]                  # 内腔前壁
    d["cav_d"] = d["split_z"] - d["cav_z0"]
    d["front_z"] = -d["body_d"] / 2.0                             # 屋身前面
    d["board_lcd_z"] = d["front_z"] + 0.2                         # 屏玻璃前表面（略凸出外壁）
    d["board_back_z"] = d["board_lcd_z"] + c["board_t"]           # 主板背面 Z
    # 屏幕窗口贯通范围：外壁外侧 0.2mm 到屏玻璃后方 0.2mm
    d["win_z0"] = d["front_z"] - 0.2
    d["win_z1"] = d["board_lcd_z"] + c["lcd_d"] - 0.2
    d["win_zc"] = (d["win_z0"] + d["win_z1"]) / 2.0
    d["win_zh"] = (d["win_z1"] - d["win_z0"]) / 2.0
    # 屏幕窗口：居中，比屏大 1.5mm 间隙，圆角 4
    d["win_w"] = c["lcd_w"] + 1.5
    d["win_h"] = c["lcd_h"] + 1.5
    # 摄像头：前壁竖直通道（正中间方形开口背后）
    d["tun_r"] = max(8.0, c["cam_d"] / 2 + 1.5)    # 通道半径
    d["tun_zc"] = d["front_z"] + 1.5               # 通道中心 Z（正对镜头）
    d["tun_y0"] = 35.0                             # 通道起点 Y（屏幕窗口上方）
    d["tun_y1"] = 52.0                             # 通道终点 Y（方形开口处）
    d["cam_win_y"] = 44.0                          # 方形开口中心 Y（正中间）
    # 红色屋顶件（独立件）：山墙式，屋脊在机身顶部之上，左右出檐
    d["roof_base_y"] = d["body_h"] / 2.0           # 屋顶底边 = 机身顶
    d["roof_peak_y"] = d["body_h"] / 2.0 + 8.5     # 屋脊高度（高出机身 8.5mm）
    d["roof_half_w"] = d["body_w"] / 2.0 + 3.0     # 屋顶半宽（左右出檐 3mm）
    d["roof_z_half"] = d["body_d"] / 2.0 + 2.0     # 屋顶前后出檐 2mm
    d["chim_xy"] = (-20.0, 58.5)                   # 烟囱中心（左坡上）
    d["chim_size"] = (10.0, 11.0, 6.0)             # 烟囱尺寸（凸出 4mm）
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
    d["std_h"] = max(1.0, d["split_z"] - d["board_back_z"])      # 支撑柱把板压向窗口
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


# =====================================================================
# 四个部件
# =====================================================================
def sdf_front(p, d):
    c = d
    # 屋身（米白机身，扁平圆角盒）
    body = sd_rounded_box(p, (0.0, 0.0, 0.0),
                          (c["body_w"], c["body_h"], c["body_d"]), c["round_r"])
    sd = body

    # 内腔（背面敞开）
    cav = sd_box(p, (0.0, 0.0, c["cav_z0"] + c["cav_d"] / 2.0),
                 (c["cav_w"], c["cav_h"], c["cav_d"]))
    sd = np.maximum(sd, -cav)

    # 屏幕窗口（深度覆盖整块屏玻璃，并切穿前壁露出开口）
    win = sd_rounded_box(p, (0.0, 0.0, c["win_zc"]),
                         (c["win_w"], c["win_h"], 2.0 * c["win_zh"]), 2.0)
    sd = np.maximum(sd, -win)

    # 摄像头：正中间 17×17 方形开口（完全贯通前壁）+ 前壁竖直通道
    cam_win = sd_box(p, (0.0, c["cam_win_y"], c["cav_z0"] - 1.0),
                     (c["cam_sq"], c["cam_sq"], 3.0))
    sd = np.maximum(sd, -cam_win)
    # 方孔背后打通到内腔（避免透过方孔看到实心顶壁）
    cam_back = sd_box(p, (0.0, c["cam_win_y"], (c["cav_z0"] + c["split_z"]) / 2.0),
                      (c["cam_sq"] + 4.0, c["cam_sq"], c["cav_d"]))
    sd = np.maximum(sd, -cam_back)
    sd = np.maximum(sd, -camera_port_sdf(p, c))

    # USB-C 底部开口
    usb = sd_box(p, (0.0, -c["body_h"] / 2.0 + c["usb_h"] / 2.0, 0.0),
                 (c["usb_w"], c["usb_h"], c["body_d"] + 2.0))
    sd = np.maximum(sd, -usb)

    # 右侧喇叭孔（2×2 Ø2.5）
    for sz in (-3.0, 3.0):
        for sy in (-10.0, -16.0):
            spk = sd_cyl_axis(p, (c["body_w"] / 2.0 + 1.0, sy, sz),
                              (1.0, 0.0, 0.0), 1.25, c["wall"] + 2.0)
            sd = np.maximum(sd, -spk)

    # 右侧按键孔
    btn = sd_cyl_axis(p, (c["body_w"] / 2.0 + 1.0, c["btn_y"], 0.0),
                      (1.0, 0.0, 0.0), c["btn_d"] / 2.0, c["wall"] + 2.0)
    sd = np.maximum(sd, -btn)

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


def camera_port_sdf(p, d):
    """摄像头通道（竖直井，正中间方形开口背后），负值在通道内。"""
    c = d
    tun = sd_cyl_axis(p, (c["cam_x"], (c["tun_y0"] + c["tun_y1"]) / 2.0, c["tun_zc"]),
                      (0.0, 1.0, 0.0), c["tun_r"], c["tun_y1"] - c["tun_y0"])
    return tun


def sdf_roof(p, d):
    """红色屋顶件：山墙式人字屋顶（屋脊在机身顶部之上）+ 左坡烟囱。"""
    c = d
    yb = c["roof_base_y"]
    yp = c["roof_peak_y"]
    wh = c["roof_half_w"]
    A = np.array([-wh, yb])
    B = np.array([wh, yb])
    C = np.array([0.0, yp])
    v_l = C - A
    n_l = np.array([v_l[1], -v_l[0]]) / np.linalg.norm(v_l)   # 左坡内向（指向屋脊右侧）
    v_r = C - B
    n_r = np.array([-v_r[1], v_r[0]]) / np.linalg.norm(v_r)   # 右坡内向（指向屋脊左侧）
    d_l = (p[:, :2] - A) @ n_l
    d_r = (p[:, :2] - B) @ n_r
    d_b = p[:, 1] - yb
    tri = np.maximum.reduce([-d_l, -d_r, yb - p[:, 1]])
    zz = np.abs(p[:, 2]) - c["roof_z_half"]
    sd = np.maximum(tri, zz)

    # 烟囱（左坡前方凸起）
    chim = sd_rounded_box(p, (c["chim_xy"][0], c["chim_xy"][1],
                              -c["roof_z_half"] - c["chim_size"][2] + 1.0),
                          c["chim_size"], 2.0)
    sd = np.minimum(sd, chim)
    return sd


def sdf_cover(p, d):
    c = d
    plate = sd_rounded_box(p, (0.0, 0.0, c["body_d"] / 2.0 - c["cover_t"] / 2.0),
                           (c["body_w"], c["body_h"], c["cover_t"]), 1.5)
    sd = plate

    # 磁铁沉孔（4×Ø10×1.6，留 1.2mm 贴冰箱壁）
    for mx, my in c["mag_xy"]:
        pk = sd_cyl_axis(p, (mx, my, c["body_d"] / 2.0),
                         (0.0, 0.0, 1.0), c["magnet_d"] / 2.0, c["magnet_dp"])
        sd = np.maximum(sd, -pk)

    # 主板支撑柱（4 个，从后盖内面伸向主板背面，把板压向屏幕窗口）
    for sx, sy in c["std_xy"]:
        st = sd_cyl_axis(p, (sx, sy, c["split_z"] - c["std_h"] / 2.0),
                         (0.0, 0.0, 1.0), 2.25, c["std_h"])
        sd = np.minimum(sd, st)

    # M2 螺丝通孔 + 沉头
    for bx, by in c["boss_xy"]:
        h1 = sd_cyl_axis(p, (bx, by, c["body_d"] / 2.0 - 1.0),
                         (0.0, 0.0, 1.0), c["screw_d"] / 2.0, c["cover_t"] + 1.0)
        sd = np.maximum(sd, -h1)
        h2 = sd_cyl_axis(p, (bx, by, c["body_d"] / 2.0 - 0.8),
                         (0.0, 0.0, 1.0), 2.1, 1.6)
        sd = np.maximum(sd, -h2)

    # 后盖主体在分模面之后，但支撑柱要伸入前腔内压住主板背面
    sd = np.maximum(sd, (c["board_back_z"] - 1.5) - p[:, 2])
    return sd


def sdf_board(p, d):
    """主板占位参考模型（不打印）。"""
    c = d
    bd_z = c["board_back_z"] - c["board_t"] / 2.0                 # 主板盒中心 Z
    sd = sd_box(p, (0.0, 0.0, bd_z), (c["board_w"], c["board_h"], c["board_t"]))
    lcd_z = c["board_lcd_z"] + c["lcd_d"] / 2.0                   # 屏在主板正面
    lcd = sd_rounded_box(p, (0.0, 0.0, lcd_z),
                         (c["lcd_w"], c["lcd_h"], c["lcd_d"]), 1.5)
    sd = np.minimum(sd, lcd)
    cam = sd_box(p, (c["cam_x"], c["cam_y"], c["board_lcd_z"] + 2.0),
                 (c["cam_sq"], c["cam_sq"], 6.0))
    sd = np.minimum(sd, cam)
    usb = sd_box(p, (0.0, -c["board_h"] / 2.0 - 0.5, c["board_lcd_z"] + 2.0),
                 (11.0, 4.0, 3.0))
    sd = np.minimum(sd, usb)
    btn = sd_cyl_axis(p, (c["board_w"] / 2.0 + 0.5, c["btn_y"], bd_z),
                      (1.0, 0.0, 0.0), 3.0, 5.0)
    sd = np.minimum(sd, btn)
    return sd


# =====================================================================
# 网格化 + 导出
# =====================================================================
PART_SDF = {"front": sdf_front, "roof": sdf_roof,
            "cover": sdf_cover, "board": sdf_board}


def part_bounds(name, d):
    b = d
    if name == "front":
        x = b["body_w"] / 2.0 + 6.0
        y_hi = b["body_h"] / 2.0 + 6.0
        y_lo = -(b["body_h"] / 2.0) - 6.0
        z_lo = -(b["body_d"] / 2.0) - 8.0
        z_hi = b["split_z"] + 2.0
    elif name == "roof":
        x = b["roof_half_w"] + 4.0
        y_hi = b["roof_peak_y"] + 4.0
        y_lo = b["roof_base_y"] - 4.0
        z_lo = -(b["roof_z_half"] + b["chim_size"][2] + 3.0)
        z_hi = b["roof_z_half"] + 3.0
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


def build_stl(name, res=0.45):
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

    # SDF 约定为负值在内部，mcubes 的“正侧”朝向需要取反才得到朝外的法线
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
    ap.add_argument("--part", default="all",
                    choices=["all", "front", "roof", "cover", "board"])
    ap.add_argument("--res", type=float, default=0.45, help="网格分辨率 mm")
    args = ap.parse_args()

    out = CFG["out_dir"]
    names = ["front", "roof", "cover", "board"] if args.part == "all" else [args.part]
    files = {
        "front": "食刻-前壳.stl",
        "roof": "食刻-屋顶-红色.stl",
        "cover": "食刻-后盖.stl",
        "board": "食刻-主板参考.stl",
    }
    print(f"网格分辨率: {args.res} mm")
    for n in names:
        print(f"生成 {files[n]} ...")
        verts, faces = build_stl(n, args.res)
        save_stl(verts, faces, os.path.join(out, files[n]))
    print("完成。")


if __name__ == "__main__":
    main()
