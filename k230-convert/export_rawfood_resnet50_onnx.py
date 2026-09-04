#!/usr/bin/env python3
"""raw-food ResNet-50 (ibrahimdaud/raw-food-recognition-models) -> ONNX 224x224.

HF 推理前处理（README）：Resize(224,224) -> ToTensor -> ImageNet mean/std。
ONNX 图本身不带归一化；nncase 编译时用 --mean/--std/--input-range 烤进 kmodel。
"""

import argparse
import sys


def load_state(checkpoint_path: str):
    import torch

    ckpt = torch.load(checkpoint_path, map_location="cpu", weights_only=True)
    if not isinstance(ckpt, dict):
        raise SystemExit("checkpoint is not a dict")
    sd = ckpt.get("model_state_dict", ckpt)
    for prefix in ("module.", "model."):
        if all(k.startswith(prefix) for k in sd):
            sd = {k[len(prefix):]: v for k, v in sd.items()}
            break
    return sd


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("weights")
    ap.add_argument("out")
    args = ap.parse_args()

    import torch
    import torchvision

    sd = load_state(args.weights)
    model = torchvision.models.resnet50(weights=None, num_classes=90)

    # 若自定义头名是 classifier.*，映射回 fc.*
    unexpected = set(sd)
    if any(k.startswith("classifier.") for k in sd):
        sd = {("fc." + k[len("classifier."):]) if k.startswith("classifier.") else k: v
              for k, v in sd.items()}
    missing, unexpected = model.load_state_dict(sd, strict=False)
    if missing:
        print("MISSING_KEYS:", missing[:20], file=sys.stderr)
        raise SystemExit(f"state dict mismatch: {len(missing)} missing keys")
    if unexpected:
        print("IGNORED_KEYS:", unexpected[:20], file=sys.stderr)

    model.eval()
    dummy = torch.randn(1, 3, 224, 224)
    torch.onnx.export(
        model,
        dummy,
        args.out,
        input_names=["images"],
        output_names=["output"],
        opset_version=17,
    )
    print("ONNX_OK", args.out)


if __name__ == "__main__":
    main()
