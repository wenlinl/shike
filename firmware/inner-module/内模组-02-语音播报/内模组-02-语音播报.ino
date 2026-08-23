/*
 * 食刻 · 内模组 D3 示例：拍照 → 上云识别 → "叮"音播报 + 结果打印
 * 在 01-拍照上云 基础上增加 MAX98357A 语音反馈
 *
 * 硬件：XIAO ESP32S3 Sense + XIAO 扩展板 + MAX98357A + 喇叭
 * 接线（经扩展板）：
 *   MAX98357A VIN  → 扩展板 5V
 *   MAX98357A GND  → GND
 *   MAX98357A BCLK → GPIO5
 *   MAX98357A LRC  → GPIO6
 *   MAX98357A DIN  → GPIO7
 *   （引脚以 Seeed 官方 I2S 示例为准）
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include "esp_camera.h"
#include "driver/i2s.h"
#include "config.h"

// ===== 配置：见同目录 config.h（网络 / 设备号 / 容器 / 引脚）=====
const char* ACTION = ACTION_IN;   // 本示例固定"放入"；动作由外置平板下发（见完整链路 05）

// ===== 8kHz 16bit "叮"音（约 150ms，正弦衰减包络）=====
#define TONE_SAMPLES 1200
int16_t dingPCM[TONE_SAMPLES];

static void buildDingTone() {
  for (int i = 0; i < TONE_SAMPLES; i++) {
    float t = (float)i / 8000.0f;                 // 8kHz 采样
    float env = 1.0f - (float)i / TONE_SAMPLES;   // 线性衰减
    float v = sinf(2.0f * PI * 1800.0f * t) * env;
    dingPCM[i] = (int16_t)(v * 12000.0f);
  }
}

static void initI2S() {
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = 8000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 256,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };
  i2s_pin_config_t pins = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRC,
    .data_out_num = I2S_DIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };
  i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pins);
  buildDingTone();
  Serial.println("I2S OK");
}

static void playDing() {
  size_t written = 0;
  i2s_write(I2S_NUM_0, dingPCM, sizeof(dingPCM), &written, portMAX_DELAY);
}

// ===== 摄像头（同 01，XIAO ESP32S3 Sense 官方引脚）=====
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
  config.frame_size   = FRAMESIZE_VGA;
  config.jpeg_quality = 12;
  config.fb_count     = 1;
  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("摄像头初始化失败");
    while (true) delay(1000);
  }
}

static void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nWi-Fi OK");
}

static void uploadPhoto(camera_fb_t* fb) {
  HTTPClient http;
  WiFiClientSecure httpClient;
  httpClient.setInsecure();  // 测试阶段；商用版校验证书
  http.begin(httpClient, SERVER_URL);
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

  // esp32 core 2.0.x 无流式 POST：整包拼装到 PSRAM 后发送
  uint8_t* bodyAll = (uint8_t*)ps_malloc(totalLen);
  if (!bodyAll) {
    Serial.println("上传失败: PSRAM 不足");
    http.end();
    return;
  }
  memcpy(bodyAll, bodyStart.c_str(), bodyStart.length());
  memcpy(bodyAll + bodyStart.length(), fb->buf, fb->len);
  memcpy(bodyAll + bodyStart.length() + fb->len, bodyEnd.c_str(), bodyEnd.length());
  int httpCode = http.POST(bodyAll, totalLen);
  free(bodyAll);

  if (httpCode > 0) {
    String resp = http.getString();
    Serial.printf("HTTP %d\n%s\n", httpCode, resp.c_str());
    // 识别成功 → 即时"叮"
    playDing();
    // 名称播报：板端中文 TTS 后置（冲刺版用串口/屏幕显示名称）
    Serial.println(">>> 播报名称（TTS 后置，冲刺版以屏幕/串口显示代替）");
  } else {
    Serial.printf("上传失败: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
}

void setup() {
  Serial.begin(115200);
  delay(500);
  pinMode(SHUTTER_PIN, INPUT_PULLUP);
  initI2S();
  initCamera();
  connectWiFi();
  configTime(8 * 3600, 0, "ntp.aliyun.com", "pool.ntp.org");  // 北京时间
  Serial.println("就绪：按 GPIO2 触发 拍照→识别→叮");
}

void loop() {
  if (digitalRead(SHUTTER_PIN) == LOW) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) {
      uploadPhoto(fb);
      esp_camera_fb_return(fb);
    }
    delay(2000);
  }
  delay(50);
}
