/**
 * GLM Coding Plan 用量显示屏 —— 鲸鱼娘漫画风
 * 硬件: Sunton ESP32-1732S019N (ESP32-S3 N16R8, 1.9" ST7789 170x320 IPS)
 *
 * 显示逻辑: 圆环显示【剩余】百分比 (100 - 已用%)，
 *   剩余 >=50% 用各卡片主题色，20~49% 橙色警告，<20% 红色警告。
 * 美术风格: 日式漫画 —— 奶油纸底、半调网点、粗黑描边、对话框，
 *   鲸鱼娘表情和台词跟随剩余量变化。
 */
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <LovyanGFX.hpp>
#include <time.h>
#include <algorithm>

#include "config.h"
#include "mascot.h"

// ==================== 屏幕 ====================

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel;
  lgfx::Bus_SPI _bus;

public:
  LGFX() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 3;
      cfg.freq_write = 24000000;
      cfg.freq_read = 0;
      cfg.pin_sclk = PIN_LCD_SCLK;
      cfg.pin_mosi = PIN_LCD_MOSI;
      cfg.pin_dc   = PIN_LCD_DC;
      cfg.spi_3wire = false;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs   = PIN_LCD_CS;
      cfg.pin_rst  = PIN_LCD_RST;
      cfg.pin_busy = -1;
      cfg.panel_width  = 170;
      cfg.panel_height = 320;
      cfg.offset_x = 35;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.invert   = true;
      _panel.config(cfg);
      _panel.setRotation(1);             // 横屏 320x170
    }
    setPanel(&_panel);
  }
};

static LGFX lcd;

// ==================== 配色 (RGB565, 日式漫画风) ====================

#define C_PAPER   0xFFBC   // 漫画纸奶油底 #FFF7E6
#define C_INK     0x2125   // 墨线 #20242C
#define C_WAVE    0xBF3E   // 水面 #BFE6F5
#define C_WAVED   0x8E9D   // 水面暗部 #8FD0EC
#define C_WHALE   0x6DDD   // 鲸鱼身体 #6FB9EF
#define C_WHALED  0x5D3B   // 鲸鱼深色 #5AA6DF
#define C_BELLY   0xEFBF   // 鲸鱼肚皮 #EAF6FF
#define C_PINK    0xFC77   // 横幅粉 #FF8FB8
#define C_PINKL   0xFE1A   // 浅粉网点 #FFC2D6
#define C_BLUSH   0xFD79   // 腮红 #FFAFCB
#define C_RED     0xE249   // 警告红 #E5484D
#define C_ORG     0xF524   // 警告橙 #F5A623
#define C_BLUE    0x4C9B   // 5小时主题蓝 #4A90D9
#define C_PINKA   0xFBF4   // 7天主题粉 #FF7FA5
#define C_TRACKP  0xEEEC   // 圆环底轨 #ECD9E4 (加深, 太浅会看不见)
#define C_WHITE   0xFFFF
#define C_SEC     0x6BD1   // 次要文字 #6B7A8C

// 剩余量 -> 颜色
static uint16_t remColor(int rem, uint16_t normal) {
  if (rem < 20) return C_RED;
  if (rem < 50) return C_ORG;
  return normal;
}

// ==================== 数据 ====================

struct Window {
  bool valid = false;
  int percent = 0;         // 已用百分比（接口原始值）
  int used = 0;
  int remaining = 0;
  int total = 0;
  int64_t resetMs = 0;
  int remPct() const { return 100 - percent; }   // 剩余百分比
};

static Window g_5h, g_7d;
static String g_level = "?";
static String g_lastSync = "--:--:--";
static String g_errorMsg;
static bool g_screenOn = true;

// ==================== 工具 ====================

static String fmtCountdown(int64_t resetMs) {
  // 时间未同步 (NTP 未完成) 时显示占位, 避免出现几十万小时的荒谬值
  if ((int64_t)time(nullptr) < 1735689600LL) return "--";
  int64_t diff = resetMs / 1000 - (int64_t)time(nullptr);
  if (diff < 0) diff = 0;
  char buf[16];
  if (diff >= 3600)
    snprintf(buf, sizeof(buf), "%lldh", (long long)(diff / 3600));
  else
    snprintf(buf, sizeof(buf), "%lldm", (long long)(diff / 60));
  return String(buf);
}

static String nowClock(bool seconds) {
  time_t t = time(nullptr);
  struct tm tmv;
  localtime_r(&t, &tmv);
  char buf[12];
  if (seconds)
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
  else
    snprintf(buf, sizeof(buf), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
  return String(buf);
}

// 漫画半调网点
static void halftone(int x, int y, int w, int h, uint16_t color, int step = 5) {
  for (int py = y; py < y + h; py += step)
    for (int px = x + ((py - y) / step % 2) * (step / 2); px < x + w; px += step)
      lcd.fillCircle(px, py, 1, color);
}

// ==================== 拉取用量 ====================

static bool fetchUsage() {
  WiFiClientSecure tls;
  tls.setInsecure();
  HTTPClient http;
  if (!http.begin(tls, API_ENDPOINT)) {
    g_errorMsg = "HTTP begin failed";
    return false;
  }
  http.addHeader("Authorization", API_KEY);
  http.setTimeout(10000);
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    g_errorMsg = "HTTP " + String(code);
    return false;
  }
  String body = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    g_errorMsg = "JSON parse failed";
    return false;
  }
  if (!doc["success"].as<bool>()) {
    g_errorMsg = String("API: ") + (const char*)(doc["msg"] | "?");
    return false;
  }
  g_level = String((const char*)(doc["data"]["level"] | "?"));
  std::transform(g_level.begin(), g_level.end(), g_level.begin(), ::toupper);

  Window wins[4];
  int n = 0;
  for (JsonObject item : doc["data"]["limits"].as<JsonArray>()) {
    if (String((const char*)(item["type"] | "")) != "CREDIT_LIMIT") continue;
    if (n >= 4) break;
    Window& w = wins[n++];
    w.percent   = item["percentage"] | 0;
    w.used      = item["currentValue"] | 0;
    w.total     = item["usage"] | 0;
    w.remaining = item["remaining"] | 0;
    w.resetMs   = item["nextResetTime"].isNull() ? 0 : (int64_t)item["nextResetTime"];
    w.valid     = true;
  }
  if (n < 2) {
    g_errorMsg = "expect 2 windows, got " + String(n);
    return false;
  }
  if (wins[0].resetMs > wins[1].resetMs) std::swap(wins[0], wins[1]);
  g_5h = wins[0];
  g_7d = wins[1];
  return true;
}

// ==================== 绘制: 场景 ====================

static void drawBackground() {
  lcd.fillScreen(C_PAPER);

  // 半调网点装饰
  halftone(146, 36, 20, 52, C_PINKL);        // 中缝竖条
  halftone(4, 140, 64, 12, C_PINKL);         // 左下角
  halftone(290, 4, 26, 18, C_PINKL);         // 右上角

  // 底部水波
  lcd.fillRect(0, 158, 320, 12, C_WAVE);
  for (int x = 0; x <= 320; x += 14) {
    lcd.fillCircle(x, 158, 7, C_WAVE);
    lcd.drawCircle(x, 158, 7, C_WAVED);
  }
  lcd.fillRect(0, 160, 320, 10, C_WAVE);
  lcd.drawFastHLine(0, 170 - 1, 320, C_WAVED);
}

// 4 格 WiFi 信号
static void drawWifi(int x, int baseY) {
  int rssi = WiFi.RSSI();
  int bars = (rssi >= -55) ? 4 : (rssi >= -65) ? 3 : (rssi >= -75) ? 2 : 1;
  for (int i = 0; i < 4; i++) {
    int h = 3 + i * 3;
    lcd.fillRect(x + i * 6, baseY - h, 4, h, (i < bars) ? C_INK : C_WAVED);
  }
}

// 顶部: 漫画横幅标题 + 套餐徽章 + WiFi + 时钟
static void drawBanner() {
  // 粉色斜切横幅 (加高到 28, 24 号字上下留出空隙)
  lcd.fillRoundRect(6, 2, 150, 28, 7, C_PINK);
  lcd.drawRoundRect(6, 2, 150, 28, 7, C_INK);
  lcd.drawRoundRect(7, 3, 148, 26, 6, C_INK);
  lcd.setFont(&fonts::efontCN_24_b);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setTextColor(C_WHITE, C_PINK);
  lcd.drawString("GLM鲸量计", 81, 16);

  // LITE 黑色小标签
  lcd.fillRoundRect(162, 7, 44, 16, 5, C_INK);
  lcd.setFont(&fonts::FreeSansBold9pt7b);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setTextColor(C_WHITE, C_INK);
  lcd.drawString(g_level, 182, 15);
  lcd.fillCircle(198, 15, 2, C_PINK);

  drawWifi(212, 25);

  // 时钟盒
  lcd.fillRoundRect(246, 5, 66, 20, 5, C_WHITE);
  lcd.drawRoundRect(246, 5, 66, 20, 5, C_INK);
  lcd.setFont(&fonts::FreeSansBold9pt7b);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setTextColor(C_INK, C_WHITE);
  lcd.drawString(nowClock(false), 279, 15);
}

static void updateClock() {
  lcd.fillRoundRect(246, 5, 66, 20, 5, C_WHITE);
  lcd.drawRoundRect(246, 5, 66, 20, 5, C_INK);
  lcd.setFont(&fonts::FreeSansBold9pt7b);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setTextColor(C_INK, C_WHITE);
  lcd.drawString(nowClock(false), 279, 15);
}

// ==================== 绘制: 鲸鱼娘 ====================

// mood: 0=开心(余量足) 1=紧张(余量低)
// 立绘渲染: pushImage 像素流在这块屏上不可靠 (文字/填充都正常),
// 因此把点阵转成 RLE 行程, 只用 fillRect 画 —— 全程不碰像素流通道。
struct RleRun { uint16_t color; uint16_t x, y, len; };
static RleRun* g_runs = nullptr;
static int g_runCount = 0;

static void initMascot() {
  // 1) flash -> RAM
  size_t bytes = MASCOT_W * MASCOT_H * sizeof(uint16_t);
  uint16_t* ram = (uint16_t*)malloc(bytes);
  if (!ram) {
    Serial.println("[mascot] RAM alloc FAILED");
    return;
  }
  memcpy_P(ram, MASCOT_DATA, bytes);

  // 2) 生成 RLE 行程 (按行游程压缩)
  int count = 0;
  for (int y = 0; y < MASCOT_H; y++) {
    int x = 0;
    while (x < MASCOT_W) {
      uint16_t c = ram[y * MASCOT_W + x];
      int len = 1;
      while (x + len < MASCOT_W && ram[y * MASCOT_W + x + len] == c) len++;
      count++;
      x += len;
    }
  }
  g_runs = (RleRun*)malloc(count * sizeof(RleRun));
  if (!g_runs) {
    Serial.println("[mascot] RLE alloc FAILED");
    free(ram);
    return;
  }
  int i = 0;
  for (int y = 0; y < MASCOT_H; y++) {
    int x = 0;
    while (x < MASCOT_W) {
      uint16_t c = ram[y * MASCOT_W + x];
      int len = 1;
      while (x + len < MASCOT_W && ram[y * MASCOT_W + x + len] == c) len++;
      g_runs[i++] = { c, (uint16_t)x, (uint16_t)y, (uint16_t)len };
      x += len;
    }
  }
  g_runCount = count;
  free(ram);   // 行程数据已足够, 点阵不再需要
  Serial.printf("[mascot] RLE ready, runs=%d (%dKB)\n", count, count * (int)sizeof(RleRun) / 1024);
}

static void drawMascot(int mood) {
  if (g_runs) {
    lcd.startWrite();
    for (int i = 0; i < g_runCount; i++) {
      const RleRun& r = g_runs[i];
      lcd.writeFillRect(8 + r.x, 56 + r.y, r.len, 1, r.color);
    }
    lcd.endWrite();
  }
  if (mood == 1) {
    // 紧张时加汗滴
    lcd.fillCircle(119, 67, 3, C_WAVED);
    lcd.fillTriangle(119, 60, 116, 67, 122, 67, C_WAVED);
    lcd.fillCircle(125, 80, 2, C_WAVED);
  }
}

// 对话框: w 自适应文字宽度
static void drawBubble(const String& text) {
  lcd.setFont(&fonts::efontCN_16);
  int tw = lcd.textWidth(text);
  int bw = std::min(tw + 18, 160);
  int bx = 8, by = 34, bh = 24;
  lcd.fillRoundRect(bx, by, bw, bh, 8, C_WHITE);
  lcd.drawRoundRect(bx, by, bw, bh, 8, C_INK);
  lcd.drawRoundRect(bx + 1, by + 1, bw - 2, bh - 2, 8, C_INK);
  // 尾巴
  lcd.fillTriangle(bx + 30, by + bh - 1, bx + 50, by + bh - 1, bx + 40, by + bh + 10, C_INK);
  lcd.fillTriangle(bx + 33, by + bh - 2, bx + 47, by + bh - 2, bx + 40, by + bh + 6, C_WHITE);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setTextColor(C_INK, C_WHITE);
  lcd.drawString(text, bx + bw / 2, by + bh / 2);
}

static const char* moodText(int remPct) {
  if (remPct >= 50) return "余量充足，冲！";
  if (remPct >= 20) return "还剩一半，悠着点";
  return "快用完啦！";
}

// ==================== 绘制: 仪表卡片 ====================

static void drawCard(int x, int y, const char* label, uint16_t accent, const Window& w) {
  // 网点投影
  halftone(x + 4, y + 4, 140, 58, C_PINKL);

  // 白卡 + 粗墨线描边
  lcd.fillRoundRect(x, y, 140, 58, 10, C_WHITE);
  lcd.drawRoundRect(x, y, 140, 58, 10, C_INK);
  lcd.drawRoundRect(x + 1, y + 1, 138, 56, 9, C_INK);

  int cx = x + 30, cy = y + 29;
  int rem = w.remPct();

  // 圆环: 底轨 + 剩余弧
  lcd.fillCircle(cx, cy, 24, C_TRACKP);
  lcd.fillCircle(cx, cy, 16, C_WHITE);
  int sweep = std::min(std::max(rem, 0), 100) * 360 / 100;
  if (sweep > 0) lcd.fillArc(cx, cy, 24, 16, 0, sweep, remColor(rem, accent));
  lcd.setFont(&fonts::FreeSansBold12pt7b);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setTextColor(C_INK, C_WHITE);
  lcd.drawString(String(rem) + "%", cx, cy);

  // 标签行: 彩点 + "5小时剩余"
  lcd.fillCircle(x + 57, y + 20, 3, accent);
  lcd.setFont(&fonts::efontCN_14);
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setTextColor(C_INK, C_WHITE);
  String lab = String(label) + "剩余";
  lcd.drawString(lab, x + 63, y + 20);

  // 倒计时
  lcd.setFont(&fonts::efontCN_14);
  lcd.setTextColor(C_SEC, C_WHITE);
  lcd.drawString(fmtCountdown(w.resetMs) + "后重置", x + 63, y + 40);
}

// ==================== 整屏 ====================

static void drawScreen() {
  int mood = (g_5h.valid && g_5h.remPct() >= 50) ? 0 : 1;
  drawBackground();
  drawBanner();
  drawMascot(mood);
  drawBubble(moodText(g_5h.remPct()));

  // 小星星点缀
  lcd.drawLine(129, 72, 135, 72, C_PINK);
  lcd.drawLine(132, 69, 132, 75, C_PINK);
  lcd.drawLine(130, 106, 134, 106, C_PINKL);
  lcd.drawLine(132, 104, 132, 108, C_PINKL);

  drawCard(172, 30, "5小时", C_BLUE, g_5h);
  drawCard(172, 92, "7天", C_PINKA, g_7d);
}

static void drawError() {
  drawBackground();
  drawBanner();
  drawBubble("拉取失败…");
  drawMascot(1);
  lcd.setFont(&fonts::FreeSans9pt7b);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setTextColor(C_RED, C_PAPER);
  lcd.drawString(g_errorMsg, 242, 96);
  lcd.setTextColor(C_SEC, C_PAPER);
  lcd.drawString("retry " + String(RETRY_SECONDS) + "s", 242, 116);
}

// ==================== Wi-Fi / 时间 ====================

static void ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  drawBackground();
  drawBanner();
  drawMascot(0);
  lcd.setFont(&fonts::efontCN_16);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setTextColor(C_INK, C_PAPER);
  lcd.drawString("连接Wi-Fi中…", 226, 80);
  lcd.setFont(&fonts::FreeSans9pt7b);
  lcd.setTextColor(C_SEC, C_PAPER);
  lcd.drawString(WIFI_SSID, 226, 100);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WiFi] connected, IP: %s  RSSI: %d dBm\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
  } else {
    Serial.println("[WiFi] connect FAILED, will retry");
  }
}

static void setupTime() {
  configTime(TZ_OFFSET_HOURS * 3600, 0, "ntp.aliyun.com", "ntp1.aliyun.com", "pool.ntp.org");
  // 屏幕提示进入校时阶段 (否则这 15 秒画面不动, 像卡死)
  lcd.fillRect(140, 60, 172, 60, C_PAPER);   // 清掉 Wi-Fi 提示文字
  lcd.setFont(&fonts::efontCN_16);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setTextColor(C_INK, C_PAPER);
  lcd.drawString("校准时间中…", 226, 80);
  Serial.print("[time] syncing NTP");
  uint32_t start = millis();
  while ((int64_t)time(nullptr) < 1735689600LL && millis() - start < 15000) {
    delay(250);
    Serial.print('.');
  }
  Serial.println(((int64_t)time(nullptr) >= 1735689600LL) ? " OK" : " TIMEOUT");
}

// ==================== 主流程 ====================

void setup() {
  Serial.begin(115200);
  Serial.println("\n[GLM Usage Display] boot");

  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, HIGH);
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);   // BOOT 键

  lcd.init();
  initMascot();
  drawBackground();
  drawMascot(0);
  lcd.setFont(&fonts::efontCN_16);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setTextColor(C_INK, C_PAPER);
  lcd.drawString("启动中…", 226, 80);

  ensureWiFi();
  setupTime();
}

void loop() {
  static uint32_t lastFetch = 0;
  static uint32_t lastClock = 0;

  if (WiFi.status() != WL_CONNECTED) ensureWiFi();

  bool due = (lastFetch == 0) || (millis() - lastFetch >= REFRESH_MINUTES * 60000UL);
  if (due) {
    if (fetchUsage()) {
      g_lastSync = nowClock(true);
      if (g_screenOn) drawScreen();
      Serial.printf("[quota] 5h remaining: %d%%  7d remaining: %d%%  level: %s\n",
                    g_5h.remPct(), g_7d.remPct(), g_level.c_str());
      lastFetch = millis();
    } else {
      if (g_screenOn) drawError();
      Serial.printf("[quota] fetch failed: %s\n", g_errorMsg.c_str());
      lastFetch = millis() - (REFRESH_MINUTES * 60000UL - RETRY_SECONDS * 1000UL);
    }
  }

  // ---- BOOT 键 (GPIO0): 短按=息屏/亮屏, 长按=立即刷新 ----
  static bool btnPrev = false;
  static uint32_t btnDownAt = 0;
  static bool longFired = false;
  bool btnNow = (digitalRead(PIN_BOOT_BTN) == LOW);
  if (btnNow && !btnPrev) {                 // 按下
    btnDownAt = millis();
    longFired = false;
  }
  if (btnNow && btnDownAt && !longFired && millis() - btnDownAt >= 800) {
    longFired = true;                       // 长按触发 (不用等松手)
    Serial.println("[button] long press -> refresh now");
    lastFetch = 0;
  }
  if (!btnNow && btnPrev && btnDownAt && !longFired) {
    g_screenOn = !g_screenOn;               // 短按: 背光开关
    digitalWrite(PIN_LCD_BL, g_screenOn ? HIGH : LOW);
    if (g_screenOn) drawScreen();
    Serial.printf("[button] short press -> screen %s\n", g_screenOn ? "ON" : "OFF");
  }
  if (!btnNow && btnPrev) btnDownAt = 0;
  btnPrev = btnNow;

  if (lastFetch != 0 && millis() - lastClock >= 15000) {
    updateClock();
    lastClock = millis();
  }

  delay(200);
}
