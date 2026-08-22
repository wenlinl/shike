/*
 * 食刻 · 内模组共享配置（唯一基准）
 *
 * ⚠️ 本文件是各示例 config.h 的基准：
 *    修改后请复制到每个示例文件夹（02/03/04/05/D2），保持完全一致。
 *
 * 修复记录（2026-08-22）：
 * 1. 摄像头引脚统一为 XIAO ESP32S3 Sense 官方定义（XIAO_CAM_PIN_*），
 *    原代码误用 ESP32-S3-EYE 风格引脚，且 VSYNC/HREF(6/7) 与 I2S 音频冲突；
 * 2. 设备号独立为 shike-xiao-001，不再与外置平板（xzd-t5e1-001）共用；
 * 3. 放入/取出改由外置平板触屏经云端下发（POST/GET /api/device/task），
 *    内模组按键仅作扫码触发，队列按帧记录动作。
 */
#pragma once

// ---- 网络 ----
const char* WIFI_SSID  = "你的WiFi名";
const char* WIFI_PASS  = "你的WiFi密码";
const char* SERVER_URL = "https://shike.live/api/scan";
const char* DEVICE_TASK_URL = "https://shike.live/api/device/task";  // 平板下发动作（待后端新增）

// 设备身份：内模组独立设备号（勿与外置平板共用）
const char* DEVICE_ID  = "shike-xiao-001";
const char* CONTAINER  = "冰箱";

#define ACTION_IN   "in"
#define ACTION_OUT  "out"

// ---- 交互 ----
#define SHUTTER_PIN  2    // 扫码触发：动作由外置平板触屏经云端下发（见 DEVICE_TASK_URL）
#define LED_PIN      3    // 补光 LED（KY-009 混白）
#define LDR_PIN      1    // 光敏（门开检测，03 事件检测示例）

// ⚠️ GPIO3/7/8/9 是 Sense 扩展板 SD 卡引脚（CS/SCK/MISO/MOSI）。
//    冲刺期不使用 SD 卡，因此可复用作 LED/音频；启用 SD 前需调整引脚方案。

// ---- I2S 音频（MAX98357A）----
#define I2S_BCLK 5
#define I2S_LRC  6
#define I2S_DIN  7

// ---- 摄像头：XIAO ESP32S3 Sense 官方引脚（Seeed Wiki / esp32-camera
//     CAMERA_MODEL_XIAO_ESP32S3 定义），与官方一致，勿改 ----
#define XIAO_CAM_PIN_XCLK  10
#define XIAO_CAM_PIN_PCLK  13
#define XIAO_CAM_PIN_VSYNC 38
#define XIAO_CAM_PIN_HREF  47
#define XIAO_CAM_PIN_SIOD  40
#define XIAO_CAM_PIN_SIOC  39
#define XIAO_CAM_PIN_D0    15
#define XIAO_CAM_PIN_D1    17
#define XIAO_CAM_PIN_D2    18
#define XIAO_CAM_PIN_D3    16
#define XIAO_CAM_PIN_D4    14
#define XIAO_CAM_PIN_D5    12
#define XIAO_CAM_PIN_D6    11
#define XIAO_CAM_PIN_D7    48
#define XIAO_CAM_PIN_PWDN  -1
#define XIAO_CAM_PIN_RESET -1
