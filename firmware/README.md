# 食刻 固件（T5-E1 · TuyaOpen）

> 硬件烧录源码：放在冰箱 / 零食柜旁的扫描台固件，T5-E1（SPARKLEIOT T5AI DEV）单板。

## 功能

待机（临期提醒 + 放入 / 取出）→ 3 秒半透明倒计时 → 板载 GC2145 拍照 → “咔嚓” → HTTPS 上传 `https://tuvmkt.com/api/scan` → 结果页（品名 / 时间 / 保质期 / 建议容器）→ 确认或重拍；断网显示本地最近记录。

## 目录

```text
firmware/
├── CMakeLists.txt        # 构建脚本
├── app_default.config    # 板卡选择（SPARKLEIOT_T5AI_DEV）
├── include/              # app_xzd_*.h（含配置 app_xzd_cfg.h）
└── src/                  # app_xzd.c 状态机 / net 上传 / ui / audio / font
```

## 构建与烧录

```bash
# 构建（TuyaOpen SDK 环境）
cd source/embedded && tos.py build
# 产物：source/embedded/dist/copy_1.0.0/copy_QIO_1.0.0.bin

# 烧录（端口以 tyutool list-ports 为准）
tyutool_cli --plain write -d t5 \
  -f source/embedded/dist/copy_1.0.0/copy_QIO_1.0.0.bin \
  -p /dev/cu.usbmodemXXXX -b 460800
```

## 配置（include/app_xzd_cfg.h）

- `XZD_SERVER_HOST / PATH`：tuvmkt.com /api/scan（线上后端）；
- `XZD_DEVICE_ID`：设备标识（xzd-t5e1-001）；
- `XZD_DEFAULT_CONTAINER`：默认容器（冰箱）；
- `XZD_WIFI_SSID / PASS`：⚠️ 已打码为占位符，**烧录前请填现场网络**，不要提交真实凭据。

> 本工程是 TuyaOpen 应用工程，TuyaOpenSDK 需单独拉取（/Users/lwl/TuyaOpenIDE/TuyaOpenSDK）。
