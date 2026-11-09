#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""GLM Coding Plan 用量查询（电脑端验证脚本）

用法:
  1. 在 config.json 中填入 api_key（或设置环境变量 ZHIPU_API_KEY）
  2. python check_usage.py          # 显示用量概览
  3. python check_usage.py --raw    # 打印原始 JSON 响应

接口实测（2026-11）:
  GET {base_url}/api/monitor/usage/quota/limit
  Authorization: <api_key>          # 直传即可，无需 Bearer
  响应: data.limits[] 中 type=CREDIT_LIMIT，含 unit/number（时间窗）、
  usage(总积分)/currentValue(已用)/remaining/percentage(已用%)/nextResetTime(epoch ms)
  按 nextResetTime 升序：第 1 个 = 5 小时窗口，第 2 个 = 7 天窗口
"""
import datetime
import json
import os
import sys
import urllib.error
import urllib.request

DIR = os.path.dirname(os.path.abspath(__file__))
CONFIG_PATH = os.path.join(DIR, "config.json")


def load_config():
    cfg = {}
    if os.path.exists(CONFIG_PATH):
        with open(CONFIG_PATH, encoding="utf-8") as f:
            cfg = json.load(f)
    cfg.setdefault("api_key", os.environ.get("ZHIPU_API_KEY", ""))
    cfg.setdefault("base_url", "https://open.bigmodel.cn")
    return cfg


def fetch_quota(cfg):
    url = cfg["base_url"].rstrip("/") + "/api/monitor/usage/quota/limit"
    headers = {
        "Authorization": cfg["api_key"],
        "Content-Type": "application/json",
        "User-Agent": "glm-usage-check/0.1",
    }
    req = urllib.request.Request(url, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=15) as resp:
            return json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        if e.code in (401, 403) and not cfg["api_key"].startswith("Bearer"):
            # 某些网关要求 Bearer 前缀，自动重试一次
            req2 = urllib.request.Request(
                url, headers={**headers, "Authorization": "Bearer " + cfg["api_key"]}
            )
            with urllib.request.urlopen(req2, timeout=15) as resp:
                return json.loads(resp.read().decode("utf-8"))
        raise


def bar(percentage, width=24):
    filled = round(percentage / 100 * width)
    return "[" + "█" * filled + "░" * (width - filled) + "]"


def reset_info(ms):
    dt = datetime.datetime.fromtimestamp(ms / 1000)
    delta_sec = (dt - datetime.datetime.now()).total_seconds()
    h, m = int(delta_sec // 3600), int(delta_sec % 3600 // 60)
    if delta_sec >= 0:
        return dt.strftime("%m-%d %H:%M"), f"{h}小时{m:02d}分后重置"
    return dt.strftime("%m-%d %H:%M"), "已过期，等待刷新"


def main():
    raw = "--raw" in sys.argv
    cfg = load_config()
    if not cfg["api_key"] or "填入" in cfg["api_key"]:
        print("未配置 API Key：请在 config.json 里填 api_key，或设置环境变量 ZHIPU_API_KEY")
        sys.exit(1)

    try:
        body = fetch_quota(cfg)
    except urllib.error.HTTPError as e:
        print(f"HTTP {e.code}: {e.read().decode('utf-8', 'replace')}")
        sys.exit(1)
    except urllib.error.URLError as e:
        print(f"网络错误: {e.reason}")
        sys.exit(1)

    if raw:
        print(json.dumps(body, ensure_ascii=False, indent=2))
        return

    if not body.get("success"):
        print(f"接口返回失败: {body.get('code')} {body.get('msg')}")
        sys.exit(1)

    data = body["data"]
    level = str(data.get("level", "?")).upper()
    limits = data.get("limits", [])

    # CREDIT_LIMIT（新）或 TOKENS_LIMIT（旧），按重置时间升序：[0]=5小时，[1]=7天
    credits = sorted(
        [l for l in limits if l.get("type") in ("CREDIT_LIMIT", "TOKENS_LIMIT")],
        key=lambda l: l.get("nextResetTime") or 0,
    )
    if not credits:
        print("响应中没有额度字段，请用 --raw 查看原始数据")
        sys.exit(1)

    print(f"GLM Coding Plan ({level})")
    print("-" * 46)
    for i, l in enumerate(credits[:2]):
        name = "5 小时额度" if i == 0 else "7 天额度  "
        used_pct = l.get("percentage", 0)
        print(f"{name}  {bar(used_pct)}  已用 {used_pct}%")
        total, remaining = l.get("usage"), l.get("remaining")
        if total is not None:
            print(f"          剩余 {remaining}/{total} 积分", end="")
            reset = l.get("nextResetTime")
            if reset:
                ts, rel = reset_info(reset)
                print(f"   {ts} ({rel})", end="")
            print()
    print("-" * 46)
    print(f"刷新时间: {datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print("数据来源: open.bigmodel.cn 配额接口（非官方公开文档，字段可能变动）")


if __name__ == "__main__":
    main()
