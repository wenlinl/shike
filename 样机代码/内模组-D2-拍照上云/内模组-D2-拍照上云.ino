/*
 * 食刻 · 内模组 D2 示例：拍照 → 上传 shike.live → 串口打印识别结果
 * 板型：XIAO ESP32S3 Sense（esp32-camera 内置 CAMERA_MODEL_XIAO_ESP32S3）
 * 环境：Arduino IDE + espressif esp32 包(>=2.0.8) + esp32-camera 库
 * 用法：改下方 WiFi 配置 → 编译上传 → 串口监视器(115200) → 按 BOOT 旁按键或发任意字符拍照
 *
 * 注意：
 * 1. 测试阶段使用 setInsecure() 跳过证书校验，便于跑通链路；商用版必须校验证书。
 * 2. 摄像头引脚使用 CAMERA_MODEL_XIAO_ESP32S3 官方定义（见 config.h 的 XIAO_CAM_PIN_*）。
 * 3. 若库管理器搜不到 esp32-camera，按 Seeed Wiki 指引手动添加。
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include "esp_camera.h"
#include "config.h"

// ===== 配置：见同目录 config.h（网络 / 设备号 / 容器 / 引脚）=====
const char* ACTION = ACTION_IN;   // 本示例固定"放入"；动作由外置平板下发（见完整链路 05）

// ===== 2. 摄像头初始化（XIAO ESP32S3 Sense 官方配置）=====
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
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size   = FRAMESIZE_VGA;      // 640x480，条码/识别够用
  config.jpeg_quality = 12;                 // 0-63，越小越清晰
  config.fb_count     = 1;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("摄像头初始化失败: 0x%x\n", err);
    while (true) delay(1000);
  }
  Serial.println("摄像头 OK");
}

// ===== 3. Wi-Fi 连接 =====
static void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("连接 Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi 已连接");
}

// ===== 4. 拍照 =====
static camera_fb_t* takePhoto() {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("拍照失败");
    return NULL;
  }
  Serial.printf("拍照成功: %u bytes\n", fb->len);
  return fb;
}

// ===== 5. multipart 上传到 shike.live =====
static void uploadPhoto(camera_fb_t* fb) {
  HTTPClient http;
  http.begin(SERVER_URL);
  http.setInsecure();  // 测试阶段跳过证书校验（商用必须去掉此行并配置 CA）
  http.setTimeout(15000);

  char ts[32];
  struct tm tinfo;
  if (getLocalTime(&tinfo, 0)) {
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tinfo);
  } else {
    strncpy(ts, "1970-01-01 00:00:00", sizeof ts);
  }

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

  size_t totalLen = bodyStart.length() + fb->len + bodyEnd.length();
  http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
  http.addHeader("Content-Length", String(totalLen));

  int httpCode = http.POST([&](uint8_t* buf, size_t maxLen, size_t total) -> size_t {
    (void)total;
    size_t written = 0;
    if (total == 0) {
      memcpy(buf, bodyStart.c_str(), bodyStart.length());
      written = bodyStart.length();
    } else if (total < bodyStart.length() + fb->len) {
      size_t imgStart = bodyStart.length();
      size_t offset = total - imgStart;
      size_t toCopy = min(fb->len - offset, maxLen);
      memcpy(buf, fb->buf + offset, toCopy);
      written = toCopy;
    } else {
      size_t endOffset = total - (bodyStart.length() + fb->len);
      size_t toCopy = min(bodyEnd.length() - endOffset, maxLen);
      memcpy(buf, bodyEnd.c_str() + endOffset, toCopy);
      written = toCopy;
    }
    return written;
  });

  if (httpCode > 0) {
    String resp = http.getString();
    Serial.printf("HTTP %d\n%s\n", httpCode, resp.c_str());
  } else {
    Serial.printf("上传失败: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
}

void setup() {
  Serial.begin(115200);
  delay(500);
  pinMode(SHUTTER_PIN, INPUT_PULLUP);
  initCamera();
  connectWiFi();
  configTime(8 * 3600, 0, "ntp.aliyun.com", "pool.ntp.org");  // 北京时间
  Serial.println("就绪：按拍照按键(GPIO2)或串口发任意字符");
}

void loop() {
  bool trigger = (digitalRead(SHUTTER_PIN) == LOW) || Serial.available();
  if (trigger) {
    while (Serial.available()) Serial.read();  // 清空串口
    camera_fb_t* fb = takePhoto();
    if (fb) {
      uploadPhoto(fb);
      esp_camera_fb_return(fb);
    }
    delay(2000);  // 防抖
  }
  delay(50);
}
