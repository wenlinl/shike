# 硬件烧录需求文档（给 AI 硬件平台）

> 项目：食刻（Shike）· 冰箱食品保质期 + 零食存储柜
> 产品名：食刻 ｜ Slogan：食刻在场，不浪费每一份食材
> 日期：2026-08-15

## 0. 一句话需求

把「食刻」固件烧录到 **T5-E1（SPARKLEIOT T5AI DEV）单板**，烧录后设备作为交互主界面：**待机显示临期 / 过期提醒 → 触摸选择「补给 / 消耗」→ 板载 GC2145 摄像头完成“扫一下”识别闭环**。

## 1. 目标硬件

| 部件 | 型号 / 规格 | 角色 |
|---|---|---|
| 主控 + 屏 + 摄像头（单板） | **T5-E1（SPARKLEIOT T5AI DEV）**：ST7789 240×320 触控彩屏 + CST816X 触控 + GC2145 DVP 摄像头 + 内置音频 codec/功放 + microSD | 主控 + 交互 + 拍照：待机提醒、补给 / 消耗选择、结果显示、板载拍照 |
| 相机 | 板载 **GC2145 DVP**（640×480 @ 15fps 预览，可拍照） | 拍照上传（**替代外挂 XIAO Sense**） |
| 声音 | 内置音频 codec + 功放（spk_en GPIO7） | “咔嚓”快门声 + 提醒音（蜂鸣器可省） |
| 蜂鸣器（可选） | 有源蜂鸣器 3.3V（三极管驱动） | 备选提示音 |
| 温度（可选） | DS18B20 | 冰箱温度异常提醒 |
| 门磁（可选） | 干簧管 + 磁铁 | 开门亮屏提示（辅助） |

> 注：T5-E1 板载摄像头已确认可用（GC2145 DVP），**外挂 XIAO Sense 不再需要**。

## 2. 平台与工程环境

| 项 | 值 |
|---|---|
| IDE | TuyaOpen IDE（VSCode / Cursor 扩展） |
| SDK | TuyaOpenSDK（`/Users/lwl/TuyaOpenIDE/TuyaOpenSDK`） |
| 目标平台 | `t5ai` |
| 板卡 | **T5-E1 / SPARKLEIOT T5AI DEV**（id：`sparkleiot-t5ai-dev`，kconfig：`SPARKLEIOT_T5AI_DEV`） |
| 板卡配置 | `SPARKLEIOT_T5AI_DEV.config` |
| 产品 PID | `pf1xhzdr2zbc2nhw` |
| 当前工程 | `copy`（Tap-to-take-photo 样板：板载摄像头实时预览 + 触控定格 + UART0 上报，改造为「食刻」） |

## 3. 固件烧录需求

### 3.1 构建产物（`source/embedded/dist/`）

| 镜像 | 用途 |
|---|---|
| `copy_QIO_1.0.0.bin` | **全量镜像，烧录主用**（当前产物：`source/embedded/dist/copy_1.0.0/`） |
| `copy_UA_1.0.0.bin` | 应用升级包（后续 OTA / 差分升级用） |
| `copy_UG_1.0.0.bin` | 通用升级包（备用，不与 QIO 混用） |

构建命令：`cd source/embedded && tos.py build`

### 3.2 烧录工具与参数

- 工具：`tyutool_cli` v3.2.8（`$OPEN_SDK_ROOT/tools/tyutool/tyutool_cli`，先 `. ./export.sh` 加载环境）
- 芯片：`-d t5`
- 镜像：QIO 全量 `embedded_QIO_1.0.1.bin`
- 端口：**以 `tyutool_cli list-ports` 实际输出为准**（T5 枚举为双串口；当前示例：`/dev/cu.usbmodem5AAE1670681` / `...683`；历史端口曾为 `/dev/cu.usbserial-110`）
- 波特率：`-b 460800`

### 3.3 烧录步骤

```bash
# 1) 加载 TuyaOpen 环境
cd /Users/lwl/TuyaOpenIDE/TuyaOpenSDK && . ./export.sh

# 2) 识别 T5 端口（插上开发板后执行，输出 JSON 列表）
"$OPEN_SDK_ROOT/tools/tyutool/tyutool_cli" --plain list-ports --json

# 3) 烧录全量镜像（端口替换为 list-ports 输出的主口）
"$OPEN_SDK_ROOT/tools/tyutool/tyutool_cli" --plain write -d t5 \
  -f /Users/lwl/TuyaOpenIDE/projects/copy/source/embedded/dist/copy_1.0.0/copy_QIO_1.0.0.bin \
  -p /dev/cu.usbmodem5AAE1670681 -b 460800

# 4) 烧录完成后重新上电 / 复位，观察屏幕
```

### 3.4 烧录验收（必须全过）

- [ ] 屏幕点亮，显示「食刻」待机页（临期 / 过期提醒置顶）
- [ ] 触摸屏幕可弹出「补给 / 消耗」两个按钮
- [ ] 板载 GC2145 摄像头取景 / 拍照正常（640×480 @ 15fps）
- [ ] 拍照上传云端并回显结果（品名 + 日期 + 保质期）
- [ ] Wi-Fi 连接涂鸦云成功（PID 有效、授权码正常）

### 3.5 字体与字符集（屏显约束）

- 屏显中文**必须使用 16×16 GB2312 点阵字库（HZK16）**，数字 / 字母用 8×16 或 16×16 ASCII 点阵；
- **禁止矢量 TTF、抗锯齿、灰度、亚像素渲染**：输出纯 1-bit 二值点阵，字符像素严格对齐网格，防止缺笔画、碎像素；
- 字符集限定 GB2312 简体中文 + 数字字母 + 常用符号，界面文案须全部在字库字符集内（不要生僻字、全量 Unicode，避免缺字乱码）；
- 大字（倒计时 3→2→1）用点阵按**整数倍最近邻放大**，禁止非整数缩放。

## 4. 固件功能需求（烧录后运行什么）

1. **待机态**：从云端 / 本地拉取库存，屏幕常显临期 / 过期置顶列表；
2. **触摸选择**：点击屏幕 → 弹出「补给 / 消耗」按钮；
3. **扫描引导**：选择后提示“请把食品放到镜头前”，等待按【扫描】（板载按键 GPIO8 或屏幕按钮）；
4. **板载拍照**：GC2145 抓一帧（640×480 JPEG/YUV）→ 组装 JSON 直接上传云端（**不再依赖外挂 XIAO Sense**）；
5. **结果展示**：收到识别 JSON 后，屏幕显示**品名、当天日期时间、保质期 / 到期日、建议容器**；
6. **库存变更**：用户确认 → 上报涂鸦云 / 中继，库存 +1（补给）/ -1（消耗）；
7. **声音反馈**：扫描时“咔嚓”（板载音频或蜂鸣器），过期时警示音；
8. **断网降级**：显示本地最近库存 + “网络不好，先不扫描”。

## 5. 外设 / 引脚需求

| 外设 | 连接 |
|---|---|
| 显示 ST7789 240×320（SPI0） | SCK=44、MOSI=46、CS=45、DC=47、RST=6、BL=9（板载） |
| 触控 CST816X（I2C1） | SCL=20、SDA=21、RST=23（板载） |
| 摄像头 GC2145（DVP） | 控制 I2C0：SCL=0、SDA=1；24MHz MCLK；DVP 数据总线平台固定引脚（板载） |
| 音频 | 内置 codec + 功放，spk_en=GPIO7（高有效）（板载） |
| 按键 | GPIO8（低有效，下降沿中断）（板载） |
| microSD（SDIO MODE1） | CLK=14、CMD=15、D0–D3=16~19（板载） |
| UART0（调试 / 扩展） | TX=11、RX=10（115200） |
| DS18B20（可选） | 空闲 GPIO + 4.7kΩ 上拉 |
| 干簧管（可选） | 空闲 GPIO，INPUT_PULLUP + 中断 |
| 供电 | USB 5V（板载稳压；无需外接相机 / 蜂鸣器电源） |

## 6. 交付物与验收

- 可烧录 QIO 镜像 + 源码（烧录 SOP 附命令与端口确认步骤）；
- 烧录后三项必验：**待机提醒屏显、补给 / 消耗触控、UART 指令闭环**；
- 设备可上涂鸦云（PID `pf1xhzdr2zbc2nhw`、授权码有效）。

## 7. 注意事项

- **端口每次插拔会变**：先 `list-ports` 再烧，不要写死端口；
- T5 是双串口设备：烧录用主口，失败换另一个口重试；
- QIO 为全量镜像，UG / UA 不混用；后续升级用 UA；
- **T5-E1 为单板方案**：屏 / 触控 / 摄像头 / 音频全部板载（见 §5 引脚表），外挂 XIAO Sense 已不需要；
- `copy` 工程即「点按拍照」样板（板载 GC2145 预览 + 触控定格 + UART0 上报），是食刻固件的改造起点；
- 产品名已定：**食刻**（食刻在场，不浪费每一份食材）；固件屏显文字同步改为「食刻」。
