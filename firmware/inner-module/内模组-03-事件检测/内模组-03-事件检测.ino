/*
 * 食刻 · 内模组 D4 示例：经过事件检测（运动检测 + 简化方向候选）
 * 原理：定时抓低分辨率灰度帧 → 帧间差分 → 运动区域
 *       - 画面突然出现大块新物体 = 放入候选
 *       - 画面中物体消失 = 取出候选（简化版）
 *
 * 诚实说明：本示例是"简化版"事件检测，用于验证事件链路；
 * 商用版方向判定 / 中途取出用 EXP-16 算法，本代码仅作冲刺演示。
 */

#include "esp_camera.h"
#include "config.h"

// LED_PIN / LDR_PIN 已统一到 config.h

// 低分辨率工作帧（缩小后做差分，省内存）
#define GRID_W 80
#define GRID_H 60
static uint8_t prevFrame[GRID_W * GRID_H];
static uint8_t hasPrev = 0;

static void initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = XIAO_CAM_PIN_D0;
  config.pin_d1       = XIAO_CAM_PIN_D1;
  config.pin_d2       = XIAO_CAM_PIN_D2;
  config.pin_d3       = XIAO_CAM_PIN_D3;
  config.pin_d4       = XIAO_CAM_PIN_D4;
  config.pin_d5       = XIAO_CAM_PIN_D5;
  config.pin_d6       = XIAO_CAM_PIN_D6;
  config.pin_d7       = XIAO_CAM_PIN_D7;
  config.pin_xclk     = XIAO_CAM_PIN_XCLK;
  config.pin_pclk     = XIAO_CAM_PIN_PCLK;
  config.pin_vsync    = XIAO_CAM_PIN_VSYNC;
  config.pin_href     = XIAO_CAM_PIN_HREF;
  config.pin_sccb_sda = XIAO_CAM_PIN_SIOD;
  config.pin_sccb_scl = XIAO_CAM_PIN_SIOC;
  config.pin_pwdn     = XIAO_CAM_PIN_PWDN;
  config.pin_reset    = XIAO_CAM_PIN_RESET;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_GRAYSCALE;  // 灰度，省内存快
  config.frame_size   = FRAMESIZE_QQVGA;      // 160x120
  config.jpeg_quality = 12;
  config.fb_count     = 1;
  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("摄像头初始化失败");
    while (true) delay(1000);
  }
}

// 取一帧并降采样到 GRID_W x GRID_H（160x120 → 80x60）
static int grabGrid(uint8_t* grid) {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) return -1;
  if (fb->format != PIXFORMAT_GRAYSCALE || fb->width != 160 || fb->height != 120) {
    esp_camera_fb_return(fb);
    return -1;
  }
  for (int y = 0; y < GRID_H; y++) {
    for (int x = 0; x < GRID_W; x++) {
      grid[y * GRID_W + x] = fb->buf[(y * 2) * 160 + (x * 2)];
    }
  }
  esp_camera_fb_return(fb);
  return 0;
}

// 帧差：返回变化像素比例 + 变化区域质心（x, y 归一化 0-1）
static float diffFrame(uint8_t* cur, float* cx, float* cy) {
  int changed = 0;
  long sx = 0, sy = 0;
  for (int i = 0; i < GRID_W * GRID_H; i++) {
    int d = abs((int)cur[i] - (int)prevFrame[i]);
    if (d > 30) {
      changed++;
      sx += i % GRID_W;
      sy += i / GRID_W;
    }
  }
  if (changed > 0) {
    *cx = (float)(sx / changed) / GRID_W;
    *cy = (float)(sy / changed) / GRID_H;
  }
  return (float)changed / (GRID_W * GRID_H);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  pinMode(LED_PIN, OUTPUT);
  pinMode(LDR_PIN, INPUT);
  initCamera();
  Serial.println("事件检测就绪：连续采样中");
}

void loop() {
  static uint8_t curFrame[GRID_W * GRID_H];
  static unsigned long lastEvent = 0;

  // 光敏低 → 开补光（模拟：手动遮挡光敏测试）
  digitalWrite(LED_PIN, digitalRead(LDR_PIN) == LOW ? HIGH : LOW);

  if (grabGrid(curFrame) != 0) return;
  if (hasPrev) {
    float cx = 0, cy = 0;
    float ratio = diffFrame(curFrame, &cx, &cy);

    if (ratio > 0.05f && millis() - lastEvent > 2000) {  // >5% 变化且 2s 防抖
      lastEvent = millis();
      // 简化方向候选：
      //   cy 偏上（<0.4）= 画面中部/上部出现变化 → 放入候选
      //   cy 偏下（>0.6）= 变化集中在下方 → 取出候选
      //   （真实方向判定需轨迹追踪，见 EXP-16）
      if (cy < 0.4f) {
        Serial.printf("[事件] 变化率 %.2f 质心(%.2f,%.2f) → 放入候选\n", ratio, cx, cy);
      } else {
        Serial.printf("[事件] 变化率 %.2f 质心(%.2f,%.2f) → 取出候选\n", ratio, cx, cy);
      }
    }
  }
  memcpy(prevFrame, curFrame, sizeof(prevFrame));
  hasPrev = 1;
  delay(150);  // ~6.6fps 采样
}
