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
 * 条码库（已接入）：
 *   - micro-zxing（zxing-cpp 的 ESP32 维护移植），灰度帧直接 ZXing::ReadBarcodes 解码；
 *   - 安装步骤见 docs/03-软件/Arduino环境与条码库接入.md（含 C++17 配置，2.0.x 默认 C++11 会编译失败）；
 *   - ❌ quirc 只能解 QR 码，不能解 EAN-13 条码。
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include "esp_camera.h"
#include "img_converters.h"
#include <ReadBarcode.h>  // ZXing（micro-zxing/lib/zxing → Arduino 库 ZXing/src）
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

// ===== 条码解码接口（已接入 micro-zxing = zxing-cpp ESP32 移植）=====
// 输入：灰度图 buffer、宽、高；输出：条码号（成功返回 1，失败 0）
static int decodeBarcode(const uint8_t* buf, int width, int height, char* outCode) {
  if (!buf || width <= 0 || height <= 0) return 0;

  // 只启用货架/商品需要的格式：EAN-13（国内商品主编码）、UPC-A、EAN-8、Code128（生鲜称重签常见）。
  // 比 BarcodeFormat::Any 快且误报少；如后续需要别的格式再放宽。
  auto hints = ZXing::DecodeHints()
                   .setFormats(ZXing::BarcodeFormat::EAN13 | ZXing::BarcodeFormat::UPCA |
                               ZXing::BarcodeFormat::EAN8 | ZXing::BarcodeFormat::Code128)
                   .setMaxNumberOfSymbols(1)
                   .setTryRotate(true)
                   .setTryHarder(true);

  // 灰度帧直接构造 ImageView，零拷贝；ReadBarcodes 失败返回空列表（不抛异常）。
  ZXing::ImageView image(buf, width, height, ZXing::ImageFormat::Lum);
  uint32_t t0 = micros();
  auto results = ZXing::ReadBarcodes(image, hints);
  uint32_t dt = micros() - t0;

  if (results.empty()) {
    Serial.printf("[条码] 本地未识别（解码耗时 %lu us）→ 走云端视觉兜底\n", (unsigned long)dt);
    return 0;
  }

  Serial.printf("[条码] 格式=%s 解码耗时 %lu us\n",
                ZXing::ToString(results[0].format()).c_str(), (unsigned long)dt);
  strncpy(outCode, results[0].text().c_str(), 31);
  outCode[31] = '\0';
  return 1;
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
  WiFiClientSecure httpClient;
  httpClient.setInsecure();  // 测试阶段；商用版校验证书
  http.begin(httpClient, SERVER_URL);
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

  // esp32 core 2.0.x 无流式 POST：整包拼装到 PSRAM 后发送（VGA JPEG + 头尾 <150KB）
  uint8_t* bodyAll = (uint8_t*)ps_malloc(totalLen);
  if (!bodyAll) {
    Serial.println("[上传] PSRAM 不足，跳过");
    http.end();
    free(jpegBuf);
    return;
  }
  memcpy(bodyAll, bodyStart.c_str(), bodyStart.length());
  memcpy(bodyAll + bodyStart.length(), jpegBuf, jpegLen);
  memcpy(bodyAll + bodyStart.length() + jpegLen, bodyEnd.c_str(), bodyEnd.length());
  int httpCode = http.POST(bodyAll, totalLen);
  free(bodyAll);

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
