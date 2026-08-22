# 食刻 · Arduino 环境搭建 + ZXing 条码库接入

> 适用对象：内模组（Seeed XIAO ESP32S3 Sense）。外置平板（T5-E1）用 TuyaOpen 烧录，不在此文档范围。
> 更新时间：2026-08-22。硬件未到手前即可完成本页全部步骤（最后一步"选端口烧录"除外）。

## 结论先行（三件事）

| 事项 | 结论 |
|---|---|
| Arduino IDE | 需要装：官网下载 IDE 2.x |
| esp32 板卡包 ≥ 2.0.8 | 需要装：板卡管理器装 `esp32 by Espressif Systems`（推荐 2.0.14）|
| esp32-camera | **不需要单独装**：esp32 板卡包自带 `esp_camera.h`，装完板卡包即可 `#include "esp_camera.h"`。不要再去库管理器装第三方 "ESP32 Camera"，会重复定义报错 |
| ZXing 条码库 | 需要接入：用 `micro-zxing`（zxing-cpp 的 ESP32 维护移植），复制为 Arduino 库后即可用（详见下文第五节）|

一个关键坑：esp32 板卡包 **2.0.x 默认 C++11**，而 zxing-cpp 需要 **C++17**。装完必须做第六节的 C++17 配置，否则条码库编译报错。

---

## 一、安装 Arduino IDE 2.x

1. 打开 <https://www.arduino.cc/en/software>，下载 **Arduino IDE 2.x**（macOS 选对应芯片的 dmg）。
2. 安装后打开，首次启动会初始化，稍等片刻。

> 备选：命令行安装 `brew install --cask arduino-ide`。Windows/Linux 同理，只是后面的数据目录路径不同（见第六节）。

## 二、安装 esp32 板卡包

1. Arduino IDE 菜单：**Arduino IDE → Settings…（设置）**。
2. 找到 **Additional boards manager URLs（附加开发板管理器网址）**，填入：

   ```text
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```

3. 菜单 **Tools（工具）→ Board（开发板）→ Boards Manager…（开发板管理器）**。
4. 搜索 `esp32`，安装 **esp32 by Espressif Systems**。
   - 推荐装 **2.0.14**（下拉选择版本）：社区验证最多、与 XIAO ESP32S3 兼容性好；装完按第六节配 C++17。
   - 装 3.x 也可以（默认已是 C++17），但 3.x 对部分库的兼容改动较大，如遇怪问题先退回 2.0.14 排查。
   - 最低要求 ≥ 2.0.8，本仓库代码按 2.0.14 / 3.x 均验证过的写法编写。

## 三、选择开发板与端口

1. **Tools → Board → esp32 → Seeed XIAO ESP32S3**（注意选带 Sense 型号的板型，无需额外设置）。
2. 用 USB-C 连接 XIAO，**Tools → Port** 选择出现的 `cu.usbmodem*` 端口（macOS 免驱，插上即有）。
3. 若端口不出现或上传失败：**按住板上的 BOOT 键不放，再插 USB**，等端口出现后松开，再上传。

## 四、esp32-camera：已内置，零安装

esp32 板卡包自带摄像头驱动。写代码时直接：

```cpp
#include "esp_camera.h"
```

本仓库 04/05 示例已经这样引用。若编译报 `esp_camera.h not found`，说明板卡包没装好（重新执行第二节），而不是缺库。

## 五、接入 ZXing 条码库（micro-zxing）

### 为什么选 micro-zxing

- 它是 zxing-cpp（ZXing 官方 C++ 版）针对 ESP32 的持续维护移植（2025-06 同步过上游源码），支持 EAN-13 / UPC-A / EAN-8 / Code128 等条码和 QR 码；
- 官方示例本身就是 Arduino 框架（PlatformIO + `framework = arduino`），灰度帧可直接 `ZXing::ReadBarcodes` 解码，无需额外二值化；
- 纯源码无外部依赖，Arduino 化只需"复制 + 一个 library.properties"。

> ❌ 不要用 `quirc`：它只能解 QR 码，解不了 EAN-13 商品条码。

### 安装步骤（macOS 示例）

```bash
# 1) 下载 micro-zxing 并解压
cd /tmp
curl -L -o micro-zxing.zip https://github.com/rzeldent/micro-zxing/archive/refs/heads/main.zip
unzip -q micro-zxing.zip

# 2) 组装成 Arduino 库：把 zxing-cpp 源码复制为 ZXing/src
mkdir -p ~/Documents/Arduino/libraries/ZXing/src
cp -R micro-zxing-main/lib/zxing/* ~/Documents/Arduino/libraries/ZXing/src/

# 3) 复制本仓库提供的 library.properties 模板（内容见 firmware/inner-module/ZXing-library.properties）
cp "/Users/lwl/Documents/ChatGPT/HsHH/firmware/inner-module/ZXing-library.properties" \
   ~/Documents/Arduino/libraries/ZXing/library.properties
```

完成后目录结构应是：

```text
~/Documents/Arduino/libraries/ZXing/
├── library.properties
└── src/
    ├── ReadBarcode.h          ← 代码里 #include <ReadBarcode.h>
    ├── Barcode.cpp / *.cpp
    ├── oned/ qrcode/ aztec/ datamatrix/ pdf417/ maxicode/ libzueci/
    └── ...
```

4. 重启 Arduino IDE，让库扫描生效。
5. 打开 `内模组-04-条码解码` 示例，编译通过即接入成功（此时 `decodeBarcode()` 已由仓库实现好，见第七节）。

> 其他系统：库目录是 `~/Arduino/libraries/`（Linux）或 `%USERPROFILE%\Documents\Arduino\libraries\`（Windows），其余相同。

## 六、C++17 配置（关键，装完必须做）

esp32 板卡包 2.0.x 默认 `-std=gnu++11`，zxing-cpp 需要 C++17。macOS 数据目录下给板卡包加一个本地配置即可（不破坏原文件）：

```bash
# 版本号目录按你实际安装的版本（2.0.14 示例）
mkdir -p ~/Library/Arduino15/packages/esp32/hardware/esp32/2.0.14
printf 'compiler.cpp.extra_flags=-std=gnu++17\n' > \
  ~/Library/Arduino15/packages/esp32/hardware/esp32/2.0.14/platform.local.txt
```

- 装的是 **3.x**：默认已是 C++17+，跳过本节。
- Windows 路径：`%LOCALAPPDATA%\Arduino15\packages\esp32\hardware\esp32\<版本>\platform.local.txt`。
- 验证：重开 Arduino IDE，编译 04 示例，若 `-std` 相关报错消失即生效。

## 七、编译与烧录（拿到硬件后）

1. **Tools → Board → Seeed XIAO ESP32S3**，**Port** 选对端口。
2. 打开 04（或 05）示例，点 **→**（上传）。首次编译较慢（约 1–3 分钟），属正常。
3. 串口监视器波特率 **115200**。
4. 若报 `not enough space`：**Tools → Partition Scheme** 选大 App 分区（如 `Huge APP (3MB No OTA/1MB SPIFFS)`），ZXing 全格式解码库体积较大。
5. 若端口丢失/上传失败：按住 BOOT 键再插 USB，重试。

## 八、常见问题

| 现象 | 原因与处理 |
|---|---|
| `esp_camera.h: No such file` | esp32 板卡包未装好；重装（第二节），不是缺库 |
| `ReadBarcode.h: No such file` | ZXing 库没放对位置；核对第五节目录结构，重启 IDE |
| `error: ... -std=gnu++11 ...` 或 zxing 源码编译报语法错 | 没做第六节 C++17 配置 |
| `esp_camera_init` 失败 / 花屏 | 引脚按 `config.h` 的 `XIAO_CAM_PIN_*`（已修成 XIAO 官方定义）；排线松、补光不足 |
| 上传失败/找不到端口 | 按住 BOOT 键再插 USB 进入 BootLoader |
| 解码慢或识别率低 | 镜头对准、条码占画面 1/3 以上、避免反光；`setTryHarder(true)` 已开，若仍慢可在 04 的 hints 里去掉 Code128（省一档工作量） |

## 相关文件

- 条码示例与实现：`firmware/inner-module/内模组-04-条码解码/`
- `library.properties` 模板：`firmware/inner-module/ZXing-library.properties`
- 固件说明：`firmware/README.md`
- 烧录需求汇总：`docs/02-硬件/烧录需求.md`
