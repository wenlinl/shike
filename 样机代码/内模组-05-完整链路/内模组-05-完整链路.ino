/*
 * 食刻 · 内模组 D8 整合版：完整链路（2026-08-22 修复版 v2）
 *  按键（扫码触发）→ 云端取平板下发的动作（放入/取出）→ 拍照 → 识别上传
 *  （在线直传 / 断网入队）→ 成功播"叮"（取出播"叮叮"）；Wi-Fi 恢复自动重传
 *
 * 交互（与设计基线 §3.1 一致）：
 *   放入/取出由【外置平板触屏】选择，平板经云端 POST /api/device/task 下发；
 *   内模组按键只做扫码触发，扫描前 GET /api/device/task 获取当前动作；
 *   无下发 / 断网时默认"放入"。
 *
 * 修复记录（2026-08-22）：
 * 1. 摄像头引脚改为 XIAO ESP32S3 Sense 官方定义（见 config.h），
 *    原硬编码引脚为 ESP32-S3-EYE 风格，且 VSYNC/HREF(6/7) 与 I2S 音频冲突；
 * 2. 设备号独立为 shike-xiao-001（不再与外置平板 xzd-t5e1-001 共用）；
 * 3. 放入/取出改由外置平板触屏经云端下发（删除内模组长按切换），
 *    断网队列仍按帧记录当时动作，重传不串；
 * 4. 上传时间戳改用 NTP 本地时间（北京时间），不再写死。
 *
 * 说明：
 * - 事件检测（03 示例）因摄像头格式冲突（JPEG vs 灰度）独立烧录演示；
 * - 条码本地解码 + 灰度 fmt2jpg 管线见 04 示例，本版为云端识别主链路；
 * - 队列用 PSRAM 存 JPEG 帧，最多 QUEUE_MAX 张（VGA 每张约 50-100KB）。
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include "esp_camera.h"
#include "driver/i2s.h"
#include "config.h"

// 动作由外置平板触屏经云端下发（fetchPendingAction），本文件无本地切换

// ===== 断网队列（PSRAM），每项记录当时的动作（0=in 1=out）=====
#define QUEUE_MAX 6
static uint8_t* queue[QUEUE_MAX];
static size_t   queueLen[QUEUE_MAX];
static uint8_t  queueAction[QUEUE_MAX];
static int      queueHead = 0, queueTail = 0, queueCount = 0;

static int enqueueFrame(camera_fb_t* fb, uint8_t act) {
  if (queueCount >= QUEUE_MAX) { Serial.println("[队列] 已满，丢弃最旧"); return -1; }
  uint8_t* copy = (uint8_t*)ps_malloc(fb->len);
  if (!copy) { Serial.println("[队列] PSRAM 不足"); return -1; }
  memcpy(copy, fb->buf, fb->len);
  queue[queueTail] = copy;
  queueLen[queueTail] = fb->len;
  queueAction[queueTail] = act;
  queueTail = (queueTail + 1) % QUEUE_MAX;
  queueCount++;
  Serial.printf("[队列] 入队 %u bytes（%s），待传 %d 条\n",
                (unsigned)fb->len, act == 0 ? "放入" : "取出", queueCount);
  return 0;
}

static camera_fb_t* dequeueFrame(uint8_t* act) {
  if (queueCount == 0) return NULL;
  static camera_fb_t fb;
  fb.buf = queue[queueHead];
  fb.len = queueLen[queueHead];
  if (act) *act = queueAction[queueHead];
  queue[queueHead] = NULL;
  queueHead = (queueHead + 1) % QUEUE_MAX;
  queueCount--;
  return &fb;
}

// ===== 叮 / 咚（8kHz 16bit 正弦衰减包络）=====
#define TONE_SAMPLES 1200
static int16_t dingPCM[TONE_SAMPLES];
static int16_t dongPCM[TONE_SAMPLES];

static void buildTones() {
  for (int i = 0; i < TONE_SAMPLES; i++) {
    float t = (float)i / 8000.0f;
    float env = 1.0f - (float)i / TONE_SAMPLES;
    dingPCM[i] = (int16_t)(sinf(2.0f * PI * 1800.0f * t) * env * 12000.0f);
    dongPCM[i] = (int16_t)(sinf(2.0f * PI * 900.0f * t) * env * 12000.0f);
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
  buildTones();
}

static void playPCM(const int16_t* pcm) {
  size_t written = 0;
  i2s_write(I2S_NUM_0, pcm, sizeof(int16_t) * TONE_SAMPLES, &written, portMAX_DELAY);
}

static void playDing() { playPCM(dingPCM); }
static void playDong() { playPCM(dongPCM); }
static void playSuccess(const char* action) {
  if (strcmp(action, ACTION_OUT) == 0) { playDing(); delay(120); playDing(); }
  else { playDing(); }
}

// ===== 摄像头（XIAO ESP32S3 Sense 官方引脚，JPEG 直拍直传）=====
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
  Serial.print("连接 Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println();
  configTime(8 * 3600, 0, "ntp.aliyun.com", "pool.ntp.org");  // 北京时间
}

// ===== 取平板下发的动作（GET /api/device/task；无下发/失败默认"放入"）=====
static const char* fetchPendingAction() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[任务] 断网，默认放入");
    return ACTION_IN;
  }
  HTTPClient http;
  http.begin(String(DEVICE_TASK_URL) + "?deviceId=" + DEVICE_ID);
  http.setInsecure();
  http.setTimeout(5000);
  int code = http.GET();
  if (code > 0) {
    String resp = http.getString();
    Serial.printf("[任务] HTTP %d %s\n", code, resp.c_str());
    int p = resp.indexOf("\"action\"");
    if (p >= 0) {
      String rest = resp.substring(p);
      if (rest.indexOf("\"out\"") >= 0) return ACTION_OUT;
      if (rest.indexOf("\"in\"") >= 0) return ACTION_IN;
    }
  } else {
    Serial.printf("[任务] 获取失败: %s，默认放入\n", http.errorToString(code).c_str());
  }
  http.end();
  return ACTION_IN;
}

// ===== 上传（时间戳用 NTP 本地时间）=====
static void uploadOne(camera_fb_t* fb, const char* action) {
  char ts[32];
  struct tm tinfo;
  if (getLocalTime(&tinfo, 0)) {
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tinfo);
  } else {
    strncpy(ts, "1970-01-01 00:00:00", sizeof ts);
  }

  HTTPClient http;
  http.begin(SERVER_URL);
  http.setInsecure();
  http.setTimeout(15000);
  String boundary = "----ShikeBoundary";
  String bodyStart = "--" + boundary + "\r\n"
                     "Content-Disposition: form-data; name=\"image\"; filename=\"photo.jpg\"\r\n"
                     "Content-Type: image/jpeg\r\n\r\n";
  String bodyEnd = "\r\n--" + boundary + "\r\n"
                   "Content-Disposition: form-data; name=\"deviceId\"\r\n\r\n" + String(DEVICE_ID) +
                   "\r\n--" + boundary + "\r\n"
                   "Content-Disposition: form-data; name=\"action\"\r\n\r\n" + String(action) +
                   "\r\n--" + boundary + "\r\n"
                   "Content-Disposition: form-data; name=\"container\"\r\n\r\n" + String(CONTAINER) +
                   "\r\n--" + boundary + "\r\n"
                   "Content-Disposition: form-data; name=\"timestamp\"\r\n\r\n" + String(ts) +
                   "\r\n--" + boundary + "--\r\n";
  size_t totalLen = bodyStart.length() + fb->len + bodyEnd.length();
  http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
  http.addHeader("Content-Length", String(totalLen));
  int code = http.POST([&](uint8_t* buf, size_t maxLen, size_t total) -> size_t {
    size_t w = 0;
    if (total == 0) { memcpy(buf, bodyStart.c_str(), bodyStart.length()); w = bodyStart.length(); }
    else if (total < bodyStart.length() + fb->len) {
      size_t o = total - bodyStart.length();
      size_t c = min(fb->len - o, maxLen);
      memcpy(buf, fb->buf + o, c); w = c;
    } else {
      size_t o = total - (bodyStart.length() + fb->len);
      size_t c = min(bodyEnd.length() - o, maxLen);
      memcpy(buf, bodyEnd.c_str() + o, c); w = c;
    }
    return w;
  });
  if (code > 0) {
    String resp = http.getString();
    Serial.printf("HTTP %d\n%s\n", code, resp.c_str());
    playSuccess(action);
    Serial.println(">>> 名称播报：TTS 后置（冲刺版屏幕/串口显示）");
  } else {
    Serial.printf("上传失败: %s\n", http.errorToString(code).c_str());
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
  Serial.println("就绪：按键=扫码触发（动作由外置平板下发）；断网自动入队，恢复后重传");
}

void loop() {
  // 1) 队列重传（Wi-Fi 可用且有待传）
  if (WiFi.status() == WL_CONNECTED && queueCount > 0) {
    uint8_t act = 0;
    camera_fb_t* fb = dequeueFrame(&act);
    if (fb) {
      Serial.printf("[重传] 剩余 %d 条\n", queueCount);
      uploadOne(fb, act == 0 ? ACTION_IN : ACTION_OUT);
      free(fb->buf);
    }
  }

  // 2) 按键 = 扫码触发；动作先向云端取（外置平板触屏下发），无下发默认"放入"
  if (digitalRead(SHUTTER_PIN) == LOW) {
    delay(80);  // 防抖
    if (digitalRead(SHUTTER_PIN) == LOW) {
      const char* action = fetchPendingAction();
      bool isOut = (strcmp(action, ACTION_OUT) == 0);
      if (isOut) playDong();   // 取出模式提示音（低音）
      else       playDing();   // 放入模式提示音（高音）
      Serial.printf("[动作] 平板下发：%s\n", isOut ? "取出" : "放入");

      camera_fb_t* fb = esp_camera_fb_get();
      if (fb) {
        if (WiFi.status() == WL_CONNECTED) {
          uploadOne(fb, action);
        } else {
          Serial.println("[断网] 拍照入队，等待恢复");
          enqueueFrame(fb, isOut ? 1 : 0);
        }
        esp_camera_fb_return(fb);
      }
      delay(1500);  // 防连按
    }
  }
  delay(50);
}
