/*
 * 食刻 · 内模组 D7 示例：条码本地解码 + 云端兜底（2026-08-22 修复版）
 *
 * 帧管线（关键修复）：
 *   本地条码需要灰度图、云端识别需要 JPEG——本示例统一以 GRAYSCALE 抓帧，
 *   解码后 / 失败兜底时用 esp32-camera 自带的 fmt2jpg 现场编码 JPEG 上传，
 *   全程不切换摄像头格式（事件检测 03 仍独立烧录，避免抢帧）。
 *
 * 流程：按键 → 灰度帧 → 本地解码
 *   成功：打印条码号 → 转 JPEG 上传云端查库（品名/保质期）
 *   失败：自动转 JPEG 上传 → 云端视觉兜底（与设计基线 §2.3 决策树一致）
 *
 * 条码库说明（重要）：
 *   - 选项 A（推荐）：ZXing-C++（zxing-cpp）的 ESP32 移植，支持 EAN-13；
 *   - 选项 B：自实现 EAN-13 解码（模块宽度法），只支持 EAN-13、体积最小；
 *   - ❌ quirc 只能解 QR 码，不能解 EAN-13 条码，不要选它。
 *   库引入后只需实现 decodeBarcode()，其余管线已接好。
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include "esp_camera.h"
#include "img_converters.h"
#include "config.h"

const char* ACTION = ACTION_IN;   // 本示例固定"放入"；动作由外置平板下发（见完整链路 05）

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
  config.pixel_format = PIXFORMAT_GRAYSCALE;  // 条码需要灰度
  config.frame_size   = FRAMESIZE_VGA;        // 640x480，条码分辨率够用
  config.jpeg_quality = 12;
  config.fb_count     = 1;
  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("摄像头初始化失败");
    while (true) delay(1000);
  }
}

// ===== 条码解码接口（TODO：引入 ZXing-C++ 移植后实现）=====
// 输入：灰度图 buffer、宽、高；输出：条码号（成功返回 1，失败 0）
static int decodeBarcode(const uint8_t* buf, int width, int height, char* outCode) {
  // TODO(你)：
  // 1) 引入 ZXing-C++ 的 ESP32 移植库（支持 EAN-13；quirc 只解 QR，不能用）；
  // 2) 对 buf 做解码（可选：先做边缘/二值化预处理）；
  // 3) 成功后把条码号写入 outCode（如 "6901028076896"）并 return 1。
  (void)buf; (void)width; (void)height; (void)outCode;
  Serial.println("[条码] 解码库未接入（TODO）→ 自动走云端视觉兜底");
  return 0;
}

// 灰度帧 → fmt2jpg 编码 JPEG → multipart 上传（deviceId/action/container/timestamp）
static void uploadGrayAsJpeg(camera_fb_t* fb) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[上传] 未联网，跳过（断网队列见完整链路 05）");
    return;
  }

  uint8_t* jpegBuf = NULL;
  size_t jpegLen = 0;
  if (!fmt2jpg(fb->buf, fb->len, fb->width, fb->height, fb->format, 50, &jpegBuf, &jpegLen) || !jpegBuf) {
    Serial.println("[上传] JPEG 编码失败（请确认 esp32-camera 已启用 JPEG 编码）");
    return;
  }
  Serial.printf("[上传] 灰度→JPEG %u bytes\n", (unsigned)jpegLen);

  char ts[32];
  struct tm tinfo;
  if (getLocalTime(&tinfo, 0)) {
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tinfo);
  } else {
    strncpy(ts, "1970-01-01 00:00:00", sizeof ts);
  }

  HTTPClient http;
  http.begin(SERVER_URL);
  http.setInsecure();  // 测试阶段；商用版校验证书
  http.setTimeout(15000);
  String boundary = "----ShikeBoundary";
  String bodyStart = "--" + boundary + "\r\n"
                     "Content-Disposition: form-data; name=\"image\"; filename=\"photo.jpg\"\r\n"
                     "Content-Type: image/jpeg\r\n\r\n";
  String bodyEnd = "\r\n--" + boundary + "\r\n"
                   "Content-Disposition: form-data; name=\"deviceId\"\r\n\r\n" + String(DEVICE_ID) +
                   "\r\n--" + boundary + "\r\n"
                   "Content-Disposition: form-data; name=\"action\"\r\n\r\n" + String(ACTION) +
                   "\r\n--" + boundary + "\r\n"
                   "Content-Disposition: form-data; name=\"container\"\r\n\r\n" + String(CONTAINER) +
                   "\r\n--" + boundary + "\r\n"
                   "Content-Disposition: form-data; name=\"timestamp\"\r\n\r\n" + String(ts) +
                   "\r\n--" + boundary + "--\r\n";
  size_t totalLen = bodyStart.length() + jpegLen + bodyEnd.length();
  http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
  http.addHeader("Content-Length", String(totalLen));

  int httpCode = http.POST([&](uint8_t* buf, size_t maxLen, size_t total) -> size_t {
    size_t w = 0;
    if (total == 0) { memcpy(buf, bodyStart.c_str(), bodyStart.length()); w = bodyStart.length(); }
    else if (total < bodyStart.length() + jpegLen) {
      size_t o = total - bodyStart.length();
      size_t c = min(jpegLen - o, maxLen);
      memcpy(buf, jpegBuf + o, c); w = c;
    } else {
      size_t o = total - (bodyStart.length() + jpegLen);
      size_t c = min(bodyEnd.length() - o, maxLen);
      memcpy(buf, bodyEnd.c_str() + o, c); w = c;
    }
    return w;
  });

  if (httpCode > 0) {
    String resp = http.getString();
    Serial.printf("HTTP %d\n%s\n", httpCode, resp.c_str());
  } else {
    Serial.printf("上传失败: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
  free(jpegBuf);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  pinMode(SHUTTER_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  initCamera();
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("连接 Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println();
  configTime(8 * 3600, 0, "ntp.aliyun.com", "pool.ntp.org");  // 北京时间
  Serial.println("就绪：对准条码，按 GPIO2 扫描");
}

void loop() {
  if (digitalRead(SHUTTER_PIN) == LOW) {
    digitalWrite(LED_PIN, HIGH);   // 补光
    delay(300);
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb && fb->format == PIXFORMAT_GRAYSCALE) {
      char code[32] = {0};
      if (decodeBarcode(fb->buf, fb->width, fb->height, code)) {
        Serial.printf("[条码] %s → 云端查库\n", code);
      } else {
        Serial.println("[条码] 未识别，走云端视觉兜底");
      }
      uploadGrayAsJpeg(fb);  // 解码成功=查库，失败=视觉兜底
    }
    if (fb) esp_camera_fb_return(fb);
    digitalWrite(LED_PIN, LOW);
    delay(1500);
  }
  delay(50);
}
