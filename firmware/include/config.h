#pragma once

// ==================== 私密配置 (不入库) ====================
// WiFi 和 API Key 请写在 include/secrets.h 中 (已被 .gitignore 排除, 不会上传到仓库)。
// 步骤: 复制 secrets.h.example 为 secrets.h, 填入你的真实信息。
#if __has_include("secrets.h")
#include "secrets.h"
#endif

// ---- 以下为占位默认值, 强烈建议在 secrets.h 中覆盖 ----
#ifndef WIFI_SSID
#define WIFI_SSID       "your-wifi-ssid"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD   "your-wifi-password"
#endif
#ifndef API_KEY
#define API_KEY         "your-api-key"   // 智谱开放平台 API Key, 形如 xxx.yyy
#endif

// ==================== 接口配置 ====================
#define BASE_URL        "https://open.bigmodel.cn"
#define API_ENDPOINT    BASE_URL "/api/monitor/usage/quota/limit"

#define REFRESH_MINUTES 5      // 正常自动刷新间隔（分钟）
#define RETRY_SECONDS   60     // 请求失败后的重试间隔（秒）
#define TZ_OFFSET_HOURS 8      // 时区：北京时间 UTC+8

// ==================== 屏幕引脚（ESP32-1732S019N 实测板定义，勿改）====================

#define PIN_LCD_SCLK 12
#define PIN_LCD_MOSI 13
#define PIN_LCD_DC   11
#define PIN_LCD_CS   10
#define PIN_LCD_RST  1
#define PIN_LCD_BL   14        // 背光

#define PIN_BOOT_BTN 0         // BOOT 键 (GPIO0): 短按=息屏/亮屏, 长按=立即刷新
