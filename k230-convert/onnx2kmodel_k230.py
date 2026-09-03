#!/usr/bin/env python3
"""方案 A 第 2 步：ONNX -> K230 kmodel（nncase 2.11.0 + INT8 PTQ 校准）。

按嘉楠官方 end2end_cls_doc/scripts/to_kmodel.py 写法实现：
- onnxsim 简化；
- CompileOptions: target=k230, preprocess=True, input uint8 NCHW,
  input_range/mean/std 与训练侧一致（默认适配 ultralytics：0-1 归一化，无均值减除）；
- PTQ 校准：默认用随机 uint8 图片（无真实校准集时的兜底），
  有校准目录时优先读真实图片（--calib-dir）。

用法:
    pip install nncase==2.11.0 onnx onnxsim pillow numpy
    python3 onnx2kmodel_k230.py model.onnx model.kmodel [--calib-dir ./calib] [--random-calib 100]
"""

import argparse
import glob
import os
import shutil

import numpy as np
from PIL import Image


def onnx_simplify(path, img_size):
    import onnx
    import onnxsim

    model = onnx.load(path)
    model = onnx.shape_inference.infer_shapes(model)
    model, check = onnxsim.simplify(
        model,
        overwrite_input_shapes={"images": [1, 3, img_size[1], img_size[0]]},
    )
    assert check, "onnxsim validate failed"
    onnx.save_model(model, path)
    return path


def random_calib(shape, count, is_float=False):
    if is_float:
        return [[np.random.uniform(0.0, 1.0, shape).astype(np.float32)] for _ in range(count)]
    return [[np.random.randint(0, 256, shape).astype(np.uint8)] for _ in range(count)]


def real_calib(shape, count, calib_dir, is_float=False):
    files = sorted(glob.glob(os.path.join(calib_dir, "*.jpg")) +
                   glob.glob(os.path.join(calib_dir, "*.png")) +
                   glob.glob(os.path.join(calib_dir, "*.jpeg")))
    data = []
    h, w = shape[2], shape[3]
    for i in range(count):
        p = files[i % len(files)]
        img = Image.open(p).convert("RGB").resize((w, h), Image.BILINEAR)
        arr = np.asarray(img, dtype=np.float32).transpose(2, 0, 1)
        if is_float:
            arr /= 255.0
        else:
            arr = arr.astype(np.uint8)
        data.append([arr[np.newaxis, ...]])
    return data


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("onnx_path")
    ap.add_argument("kmodel_path")
    ap.add_argument("--img", type=int, default=224)
    ap.add_argument("--input-range", default="0,1")
    ap.add_argument("--mean", default="0,0,0")
    ap.add_argument("--std", default="1,1,1")
    ap.add_argument("--calib-dir", default="")
    ap.add_argument("--random-calib", type=int, default=100)
    ap.add_argument("--no-preprocess", action="store_true",
                    help="不在 kmodel 内做 resize/归一化（由运行侧 ai2d/YOLO 预处理），适合 YOLO 系列模型")
    args = ap.parse_args()

    import nncase

    img_size = [args.img, args.img]
    input_range = [float(x) for x in args.input_range.split(",")]
    mean = [float(x) for x in args.mean.split(",")]
    std = [float(x) for x in args.std.split(",")]

    print("[1/4] onnxsim simplify")
    onnx_simplify(args.onnx_path, img_size)

    print("[2/4] compile options")
    co = nncase.CompileOptions()
    co.target = "k230"
    co.preprocess = not args.no_preprocess
    co.input_shape = [1, 3, img_size[1], img_size[0]]
    co.input_type = "uint8"
    co.input_range = input_range
    co.mean = mean
    co.std = std
    co.input_layout = "NCHW"
    co.quant_type = "uint8"

    compiler = nncase.Compiler(co)
    with open(args.onnx_path, "rb") as f:
        compiler.import_onnx(f.read(), nncase.ImportOptions())

    print("[3/4] PTQ calibration")
    ptq = nncase.PTQTensorOptions()
    if args.calib_dir and os.path.isdir(args.calib_dir):
        n_calib = min(len(glob.glob(os.path.join(args.calib_dir, "*.jpg")) +
                          glob.glob(os.path.join(args.calib_dir, "*.png"))), 200)
        ptq.samples_count = n_calib
        ptq.set_tensor_data(real_calib([1, 3, img_size[1], img_size[0]], n_calib, args.calib_dir,
                                       is_float=args.no_preprocess))
        print(f"    real calib: {n_calib} images")
    else:
        ptq.samples_count = args.random_calib
        ptq.set_tensor_data(random_calib([1, 3, img_size[1], img_size[0]], args.random_calib,
                                         is_float=args.no_preprocess))
        print(f"    random calib: {args.random_calib} (accuracy may drop; 有真实图后重跑更稳)")
    compiler.use_ptq(ptq)

    print("[4/4] compile & save")
    compiler.compile()
    kmodel = compiler.gencode_tobytes()
    with open(args.kmodel_path, "wb") as f:
        f.write(kmodel)
    print("KMODEL:", args.kmodel_path, len(kmodel), "bytes")


if __name__ == "__main__":
    main()
