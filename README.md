# 🐳 GLM 鲸量计 · AI Coding Plan 桌面用量显示屏

> 把你的 AI 编程订阅余量做成一台可爱的桌面状态屏。
> Turn your AI coding-plan quota into a cute desk status display.

![preview](docs/images/preview.jpg)

<!-- 英文简介见文末 English -->

## ✨ 特性

- 📊 **双窗口余量显示**：5 小时 / 7 天滚动配额，圆环仪表 + 剩余积分 + 重置倒计时
- 🎨 **日式漫画风 UI**：奶油纸底、半调网点、粗墨线描边、漫画对话框
- 🐳 **鲸鱼娘立绘**：默认智谱鲸鱼娘漫画立绘（已抠背景压平），支持一键替换成任意图片
- 😊 **表情联动**：剩余量充足时笑 + 喷水；不足 50% 变紧张（冒汗），台词同步切换
- 🌈 **三级预警配色**：主题色（充裕）→ 橙（<50%）→ 红（<20%）
- 🔘 **BOOT 键交互**：短按息屏/亮屏，长按立即刷新
- 📶 **WiFi 信息**：信号格 + 时钟 + 套餐等级徽章

## 🔧 硬件

| 项目 | 说明 |
|------|------|
| 主控 | ESP32-S3-WROOM-1（N16R8：16MB Flash + 8MB PSRAM）|
| 屏幕 | 1.9" IPS ST7789，170×320，SPI |
| 验证机型 | **Sunton ESP32-1732S019N**（淘宝/拼多多 Cyd 家族，约 40 元）|

屏幕引脚已在 `firmware/include/config.h` 中定义（MOSI=13 / SCLK=12 / CS=10 / DC=11 / RST=1 / 背光=14），其他 ST7789 板子按实际引脚修改即可。

## 🚀 快速开始

1. **安装 [PlatformIO](https://platformio.org/)**（VS Code 插件或 CLI）
2. **配置私密信息**（不会提交到 git）：
   ```bash
   cd firmware/include
   cp secrets.h.example secrets.h
   # 编辑 secrets.h，填入 WiFi 和智谱 API Key（控制台首页可见，形如 abcd1234.efghijkl）
   ```
3. **编译烧录**：
   ```bash
   pio run -t upload -d firmware
   ```
4. 插上板子，等待自动流程：连接 WiFi → NTP 校时 → 显示用量（每 5 分钟自动刷新）

### 按键操作

| 操作 | 功能 |
|------|------|
| 短按 BOOT | 息屏 / 亮屏 |
| 长按 BOOT | 立即刷新用量 |

## 🖼️ 立绘说明与更换

默认立绘是**智谱鲸鱼娘**（原图见 `assets/mascot_girl_reference.png`）。版权归原始权利人所有，此处仅作非商业的社区展示用途；如权利人认为不妥，提 Issue 即可移除。

```bash
# 重新从仓库内置原图生成鲸鱼娘点阵
python tools/make_mascot.py assets/mascot_girl_reference.png

# 或者换成你自己的任意图片
python tools/make_mascot.py 你的图片.png

# 想要中立的原创小鲸鱼（程序生成，无第三方素材）
python tools/gen_default_mascot.py

# 重新烧录
pio run -t upload -d firmware
```

自动完成：边缘泛洪抠背景 → 等比缩放到 100px 高 → 压平为纸色 → 生成 RGB565 点阵 → RLE 渲染。

## 💻 电脑命令行版（无硬件也能用）

```bash
cp config.example.json config.json   # 填入 api_key
python check_usage.py
```

## 📡 配额接口说明（非官方）

```
GET https://open.bigmodel.cn/api/monitor/usage/quota/limit
Authorization: <你的APIKey>
```

响应中 `data.limits[]` 为 `CREDIT_LIMIT` 条目（含 `percentage` 已用百分比、`remaining`/`usage` 剩余/总量、`nextResetTime` 重置时间戳 epoch ms），按 `nextResetTime` 升序排列后：第 1 个为 5 小时窗口，第 2 个为 7 天窗口；`data.level` 为套餐等级（lite/pro/max）。

> ⚠️ **免责声明**：此接口未在官方文档中公开，属于社区逆向发现，字段与行为可能随时变动，本项目不对可用性作任何保证。国际版用户可将 `BASE_URL` 换为 `https://api.z.ai`。

## 🛠️ 构建

- 框架：Arduino (espressif32 6.x) + [LovyanGFX 1.2.0](https://github.com/lovyan03/LovyanGFX) + ArduinoJson 7
- CI：push 自动编译，Release 附 `firmware.bin`
- 注意：本项目实测需 **LovyanGFX 1.2.0**——1.3.0 在 ESP32-S3 + ST7789 上 `pushImage` 像素流有花屏问题，立绘渲染已改为 RLE + `fillRect` 方案绕开

## 🗺️ Roadmap

- [ ] v1.0 网页配网 + 配置门户（免改代码）
- [ ] v1.0 多服务商适配器（GLM 国际版 / Kimi / Anthropic …）
- [ ] v1.1 用量历史曲线
- [ ] v1.1 更多硬件（CYD 家族其他型号 / 自定义引脚）
- [ ] v1.2 Home Assistant / MQTT 集成

## 📄 License

MIT。`assets/` 中的鲸鱼娘立绘版权归原作者（智谱）所有，本项目为非商业社区作品，与其无隶属关系；如权利人认为不妥，提 Issue 即可移除。GLM 等商标归其权利人所有。

---

## English

A cute manga-style desk display (ESP32-S3 + 1.9" ST7789) that shows your **AI coding-plan quota** — the 5-hour and 7-day rolling windows — with gauges, remaining credits, reset countdowns, and a mascot that reacts to your remaining quota.

**Quick start**: `cd firmware/include && cp secrets.h.example secrets.h` (fill in WiFi + Zhipu API key) → `pio run -t upload -d firmware`. Works out-of-the-box on the Sunton ESP32-1732S019N (~$6); adapt pins in `config.h` for other ST7789 boards. The quota endpoint is an **undocumented community discovery** and may break at any time. MIT licensed. Roadmap: web-based provisioning, multi-provider adapters (z.ai / Kimi / Anthropic), usage history, MQTT.
