#include "app_web.h"
#include "app_common.h"
#include "app_config.h"
#include "app_connectivity.h"
#include "app_device_icons.h"
#include "app_header.h"
#include "app_screenshot.h"
#include "app_version.h"
#include <FS.h>
#include <LittleFS.h>
#include <SD.h>
#include <WebServer.h>
#include <WiFi.h>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <time.h>

static constexpr const char* AP_SSID = "cardputer";
static constexpr const char* AP_PASS = "cardputer";
static constexpr const char* AP_WEB_URL = "http://192.168.4.1";
static constexpr int WEB_DEVICE_MAX = MIJIA_DEVICE_MAX;

enum class ConfigWebMode {
    NONE,
    AP,
    STA,
};

static WebServer g_server(80);
static bool g_running = false;
static ConfigWebMode g_mode = ConfigWebMode::NONE;
static char g_web_url[32] = "";
static char g_web_status[48] = "ready";

enum class WebStartupPhase {
    IDLE,
    CONNECTING,
    READY,
    FAILED,
};

static WebStartupPhase g_startup_phase = WebStartupPhase::IDLE;
static uint32_t g_connect_deadline_ms = 0;
static bool g_wifi_begin_sent = false;
static bool g_force_ap_mode = false;
static bool g_web_screen_ready = false;
static bool g_web_help_visible = false;

static const char* DEFAULT_CONFIG = R"({
  "wifis": [
    {
      "ssid": "your-ssid",
      "password": "your-password"
    }
  ],
  "wifi_active": "your-ssid",
  "devices": [
    {
      "name": "living-room-light",
      "name_zh": "客厅灯",
      "id": "123456789",
      "mac": "AA:BB:CC:DD:EE:FF",
      "ip": "192.168.1.50",
      "token": "0123456789abcdef0123456789abcdef",
      "model": "yeelink.light.lamp2",
      "hotkey": "1"
    },
    {
      "name": "bedroom-light",
      "name_zh": "卧室灯",
      "id": "987654321",
      "mac": "AA:BB:CC:DD:EE:01",
      "ip": "192.168.1.51",
      "token": "0123456789abcdef0123456789abcdef",
      "model": "yeelink.light.lamp2",
      "hotkey": "2"
    },
    {
      "name": "Sensor_HT",
      "name_zh": "温湿度计",
      "id": "blt.3.example",
      "mac": "A4:C1:38:00:00:00",
      "model": "miaomiaoce.sensor_ht.t2",
      "ble": { "key": "0123456789abcdef0123456789abcdef" },
      "hotkey": "s"
    }
  ],
  "device_groups": [
    {
      "name": "AllLights",
      "name_zh": "全部灯光",
      "members": [
        { "id": "123456789", "name": "living-room-light", "name_zh": "客厅灯" },
        { "id": "987654321", "name": "bedroom-light", "name_zh": "卧室灯" }
      ]
    }
  ],
  "cursor": {
    "token": "your-cursor-session-jwt"
  },
  "system": {
    "week_start": "sunday"
  },
  "timezone": "CST-8",
  "screen": {
    "brightness": 30,
    "invert": false
  },
  "sound": {
    "time_key": true,
    "mijia_on_off": true,
    "volume": 25
  },
  "time": {
    "default": "up"
  },
  "infrared": {
    "default": "tv",
    "tv_brand": "samsung",
    "ac_brand": "midea"
  }
})";

// textarea / HTML 属性转义
static String escapeForTextarea(const String& text) {
    String out;
    out.reserve(text.length() + 16);
    for (size_t i = 0; i < text.length(); i++) {
        const char c = text[i];
        if (c == '&') {
            out += "&amp;";
        } else if (c == '<') {
            out += "&lt;";
        } else {
            out += c;
        }
    }
    return out;
}

// 嵌入 <script type="application/json"> 时防止截断标签
static String sanitizeJsonForHtml(const String& json) {
    String out;
    out.reserve(json.length() + 16);
    for (size_t i = 0; i < json.length(); i++) {
        if (json[i] == '<' && i + 1 < json.length() && json[i + 1] == '/') {
            out += "\\u003c";
        } else {
            out += json[i];
        }
    }
    return out;
}

static String loadConfigText() {
    String cfg;
    if (!readAppConfigRaw(cfg) || cfg.isEmpty()) {
        cfg = DEFAULT_CONFIG;
    }
    return cfg;
}

// 页面按需附加的 CSS 段
enum HtmlCssFlags : uint8_t {
    HTML_CSS_NONE = 0,
    HTML_CSS_DEVICES = 1u << 0, // 设备表 / WiFi 网格
    HTML_CSS_GROUPS = 1u << 1,  // 编组卡片
    HTML_CSS_CURSOR = 1u << 2,  // Cursor token 说明
    HTML_CSS_SYSTEM = 1u << 3,  // 系统设置
    HTML_CSS_SHOTS = 1u << 4,   // 截图网格 / 空间条
    HTML_CSS_FILES = 1u << 5,   // TF 文件管理
};

// 分块发送 HTML：不拼 head+body 大 String，避免 OOM / pending
static void sendHtmlPage(const String& body, const uint8_t css_flags = HTML_CSS_NONE) {
    g_server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    g_server.send(200, "text/html", "");
    g_server.sendContent_P(PSTR(
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<link rel='icon' href='/favicon.svg' type='image/svg+xml'>"
        "<title>Cardputer Config</title>"
        "<style>"
        "*{box-sizing:border-box}"
        ":root{color-scheme:light dark;"
        "--bg:#f5f5f5;--fg:#1a1a1a;--fg-h:#111;--input-bg:#fff;--input-bd:#ccc;"
        "--btn-bg:#eee;--btn-bd:#bbb;--tab-bd:#ddd;--tab-fg:#666;--tab-act:#1a73e8;"
        "--th-bg:#e8e8e8;--td-bg:#fff;--td-alt:#f7f7f7;--td-hover:#e8f0fe;"
        "--link:#1a73e8;--code-bg:#eee;--pre-bg:#f0f0f0;--pre-bd:#ccc;"
        "--save-bd:#ddd;--hint:#666;--icon-bg:#000}"
        "@media(prefers-color-scheme:dark){:root{"
        "--bg:#121212;--fg:#e0e0e0;--fg-h:#f0f0f0;--input-bg:#1e1e1e;--input-bd:#444;"
        "--btn-bg:#2a2a2a;--btn-bd:#444;--tab-bd:#333;--tab-fg:#888;--tab-act:#8ab4f8;"
        "--th-bg:#222;--td-bg:#1a1a1a;--td-alt:#161616;--td-hover:#1e2a3a;"
        "--link:#8ab4f8;--code-bg:#2a2a2a;--pre-bg:#0d0d0d;--pre-bd:#333;"
        "--save-bd:#333;--hint:#999;--icon-bg:#000}}"
        "body{font-family:system-ui,sans-serif;margin:0;padding:12px 14px 20px;line-height:1.4;"
        "width:100%;max-width:100%;background:var(--bg);color:var(--fg)}"
        ".topbar{margin-bottom:12px}"
        ".brand{display:flex;align-items:center;gap:10px;margin-bottom:10px}"
        ".site-logo{width:36px;height:36px;object-fit:contain;flex-shrink:0}"
        ".brand-text{min-width:0}"
        ".brand-text h1{font-size:1.2rem;margin:0;color:var(--fg-h)}"
        ".nav{display:flex;flex-wrap:wrap;gap:4px;margin:0;padding:0}"
        ".nav a{display:inline-block;padding:5px 10px;border-radius:6px;font-size:13px;"
        "color:var(--tab-fg);text-decoration:none;border:1px solid transparent;"
        "background:transparent}"
        ".nav a:hover{background:var(--td-hover);color:var(--fg)}"
        ".nav a.active{background:var(--tab-act);color:#fff;border-color:var(--tab-act);"
        "font-weight:600}"
        "@media(prefers-color-scheme:dark){.nav a.active{color:#121212}}"
        ".card{background:var(--td-bg);border:1px solid var(--tab-bd);border-radius:10px;"
        "padding:14px 16px;box-shadow:0 1px 3px rgba(0,0,0,.06)}"
        "h1{font-size:1.2rem;margin:0 0 8px;color:var(--fg-h)}"
        "h2{font-size:1.05rem;margin:0 0 8px;color:var(--fg-h)}"
        "input,textarea,select{width:100%;padding:6px 8px;font-size:13px;border:1px solid var(--input-bd);"
        "border-radius:4px;font-family:inherit;background:var(--input-bg);color:var(--fg)}"
        "textarea{resize:vertical;min-height:28px;font-family:ui-monospace,monospace;"
        "font-size:12px;line-height:1.35}"
        "label{font-size:12px;color:var(--hint);display:block;margin-bottom:10px}"
        "button{padding:8px 14px;margin:0 6px 6px 0;border:1px solid var(--btn-bd);border-radius:4px;"
        "background:var(--btn-bg);color:var(--fg);cursor:pointer;font-size:13px}"
        "button.primary{background:#1a73e8;color:#fff;border-color:#1a73e8}"
        "button.danger{color:#ff8a80;border-color:#5c3333;background:#2a1a1a}"
        "button.icon-btn{padding:3px 6px;font-size:12px;line-height:1;min-width:26px;"
        "white-space:nowrap}"
        "a.btn{display:inline-block;padding:8px 14px;margin:0 6px 6px 0;border:1px solid var(--btn-bd);"
        "border-radius:4px;background:var(--btn-bg);color:var(--fg);cursor:pointer;font-size:13px;"
        "text-decoration:none}"
        "a.btn.primary{background:#1a73e8;color:#fff;border-color:#1a73e8}"
        ".hint{font-size:12px;color:var(--hint);margin:0 0 10px}"
        ".hint a{color:var(--link)}"
        ".toolbar{margin:0 0 10px;display:flex;flex-wrap:wrap;align-items:center;gap:6px}"
        ".toolbar .count{font-size:13px;color:var(--hint);margin-left:auto}"
        ".table-wrap{width:100%;overflow-x:auto;-webkit-overflow-scrolling:touch}"
        // 列表底添加行：与表格同宽，仅显示 +
        ".btn-add-row{display:block;width:100%;margin:8px 0 0;padding:12px;font-size:22px;"
        "font-weight:600;line-height:1;text-align:center}"
        ".wifi-grid{max-width:480px}"
        ".wifi-table{width:100%;max-width:720px;border-collapse:collapse;table-layout:fixed}"
        ".wifi-table th,.wifi-table td{border:1px solid var(--tab-bd);padding:6px 8px;"
        "vertical-align:middle;background:var(--td-bg)}"
        ".wifi-table th{background:var(--th-bg);font-size:12px;font-weight:600;text-align:left;"
        "color:var(--fg)}"
        ".wifi-table tr:nth-child(even) td{background:var(--td-alt)}"
        ".wifi-table .col-active{width:56px;text-align:center}"
        ".wifi-table .col-active input{width:auto;margin:0;accent-color:#1a73e8}"
        ".wifi-table .col-ssid{width:36%}"
        ".wifi-table .col-pass{width:40%}"
        ".wifi-table .col-act{width:72px}"
        ".wifi-table input[type=text]{margin:0}"
        ".about-dl{margin:0;max-width:520px}"
        ".about-dl dt{font-size:12px;color:var(--hint);margin:12px 0 2px}"
        ".about-dl dd{margin:0;font-size:14px;color:var(--fg-h);word-break:break-all}"
        ".about-dl a{color:var(--link)}"
        ".save-bar{margin-top:14px;padding-top:12px;border-top:1px solid var(--save-bd)}"
        ".result-actions{margin-top:16px;display:flex;flex-wrap:wrap;gap:6px;align-items:center}"
        ".ok{color:#81c784}.err{color:#ff8a80}"
        "code{background:var(--code-bg);padding:2px 4px;border-radius:3px;color:var(--fg)}"
        "pre{background:var(--pre-bg);color:var(--fg);padding:12px;overflow:auto;font-size:12px;"
        "border:1px solid var(--pre-bd);border-radius:4px}"
        "textarea.json-editor{height:min(70vh,520px);font-family:ui-monospace,monospace}"
    ));
    if (css_flags & HTML_CSS_DEVICES) {
        g_server.sendContent_P(PSTR(
            "table.dev-table{width:100%;min-width:1000px;border-collapse:collapse;table-layout:fixed}"
            ".dev-table th,.dev-table td{border:1px solid var(--tab-bd);padding:4px;vertical-align:top;"
            "background:var(--td-bg)}"
            ".dev-table th{background:var(--th-bg);font-size:12px;font-weight:600;text-align:left;"
            "padding:6px 4px;color:var(--fg)}"
            ".dev-table tr:nth-child(even) td{background:var(--td-alt)}"
            ".dev-table tr:hover td{background:var(--td-hover)!important}"
            ".dev-table .col-idx{width:36px;text-align:center;color:var(--hint);font-size:12px;"
            "vertical-align:middle}"
            ".dev-table .col-act{width:168px;vertical-align:middle}"
            ".dev-table .col-act .act-stack{display:flex;flex-direction:row;flex-wrap:nowrap;"
            "gap:3px;align-items:center}"
            ".dev-table .col-act button{width:auto;margin:0;flex-shrink:0}"
            ".dev-table .col-name{width:10%}"
            ".dev-table .col-namezh{width:9%}"
            ".dev-table .col-hotkey{width:52px}"
            ".dev-table .col-hotkey textarea{text-align:center;text-transform:lowercase;"
            "font-weight:700;max-width:40px}"
            ".dev-table .col-ip{width:9%}"
            ".dev-table .col-token{width:13%}"
            ".dev-table .col-ble{width:13%}"
            ".dev-table .col-model{width:15%}"
            ".dev-table .col-id{width:10%}"
            ".dev-table .col-mac{width:9%}"
            ".model-cell{display:flex;gap:6px;align-items:flex-start}"
            ".model-cell textarea{flex:1;min-width:0}"
            ".dev-icon{width:32px;height:32px;object-fit:contain;flex-shrink:0;margin-top:2px;"
            "background:var(--icon-bg);border-radius:4px}"
        ));
    }
    if (css_flags & HTML_CSS_GROUPS) {
        g_server.sendContent_P(PSTR(
            ".group-card{border:1px solid var(--tab-bd);border-radius:6px;padding:10px 12px;"
            "margin:0 0 12px;background:var(--td-bg)}"
            ".group-card .group-head{display:flex;flex-wrap:wrap;gap:8px;align-items:flex-end;"
            "margin-bottom:8px}"
            ".group-card .group-head label{margin:0;flex:1;min-width:140px}"
            ".group-card .group-acts{display:flex;gap:4px;flex-wrap:wrap}"
            ".group-members{display:grid;grid-template-columns:repeat(auto-fill,minmax(220px,1fr));"
            "gap:6px;margin-top:6px}"
            ".group-members label.member{display:flex;gap:8px;align-items:flex-start;margin:0;"
            "padding:6px 8px;border:1px solid var(--tab-bd);border-radius:4px;font-size:12px;"
            "color:var(--fg);cursor:pointer;box-sizing:border-box}"
            ".group-members label.member.ble{opacity:.55}"
            ".group-members label.member input[type=checkbox]{width:auto;min-width:16px;"
            "margin:2px 0 0;padding:0;flex:0 0 auto;accent-color:#1a73e8}"
            ".group-members .member-meta{flex:1 1 auto;min-width:0;line-height:1.35;"
            "overflow-wrap:anywhere}"
            ".group-members .member-id{font-family:ui-monospace,monospace;font-size:11px;"
            "color:var(--hint);word-break:break-all}"
        ));
    }
    if (css_flags & HTML_CSS_CURSOR) {
        g_server.sendContent_P(PSTR(
            ".token-steps{margin:0 0 12px;padding:0 0 0 18px;font-size:12px;color:var(--hint)}"
            ".token-steps li{margin:0 0 6px}"
            ".token-method-title{font-size:13px;margin:14px 0 6px;color:var(--fg-h)}"
            ".token-paths{margin:0 0 8px;padding:0 0 0 18px;font-size:12px;color:var(--hint)}"
            ".token-paths li{margin:0 0 4px}"
            ".token-cmd{margin:0 0 12px;font-size:11px;line-height:1.4;white-space:pre-wrap;word-break:break-all}"
            ".token-formats{font-size:12px;color:var(--hint);margin:0 0 10px}"
        ));
    }
    if (css_flags & HTML_CSS_SYSTEM) {
        g_server.sendContent_P(PSTR(
            // 左侧分项 list + 右侧设置内容
            ".sys-wrap{display:flex;gap:14px;align-items:flex-start}"
            ".sys-nav{flex:0 0 150px;display:flex;flex-direction:column;gap:4px;"
            "position:sticky;top:12px}"
            ".sys-nav button{display:flex;flex-direction:column;align-items:flex-start;gap:1px;"
            "width:100%;margin:0;padding:7px 10px;border:1px solid transparent;border-radius:6px;"
            "background:transparent;color:var(--tab-fg);font-size:13px;text-align:left;"
            "cursor:pointer;line-height:1.25}"
            ".sys-nav button:hover{background:var(--td-hover);color:var(--fg)}"
            ".sys-nav button.active{background:var(--tab-act);color:#fff;"
            "border-color:var(--tab-act);font-weight:600}"
            "@media(prefers-color-scheme:dark){.sys-nav button.active{color:#121212}}"
            ".sys-nav .key{font-size:10px;font-weight:500;font-family:ui-monospace,monospace;"
            "opacity:.75}"
            ".sys-panes{flex:1 1 auto;min-width:0;max-width:520px}"
            ".sys-sec{display:none;border:1px solid var(--tab-bd);border-radius:8px;"
            "padding:12px 14px;background:var(--td-bg)}"
            ".sys-sec.active{display:block}"
            ".sys-sec h3{margin:0 0 10px;font-size:14px;font-weight:600;color:var(--fg-h);"
            "display:flex;align-items:baseline;gap:8px;flex-wrap:wrap}"
            ".sys-sec h3 .key{font-size:11px;font-weight:500;font-family:ui-monospace,monospace;"
            "color:var(--hint)}"
            ".sys-sec label{display:block;margin:0 0 10px}"
            ".sys-sec label:last-child{margin-bottom:0}"
            ".sys-sec .check-row{display:flex;align-items:center;gap:8px;margin:0 0 10px;"
            "font-size:13px;color:var(--fg);cursor:pointer}"
            ".sys-sec .check-row:last-child{margin-bottom:0}"
            ".sys-sec .check-row input{width:auto;margin:0;accent-color:#1a73e8}"
            ".sys-sec .bright-row{display:flex;align-items:center;gap:10px}"
            ".sys-sec .bright-row input[type=range]{flex:1;min-width:0;padding:0}"
            ".sys-sec .bright-val{min-width:2.5em;font-variant-numeric:tabular-nums;"
            "color:var(--fg)}"
            // 窄屏：分项列表转为横排
            "@media(max-width:640px){"
            ".sys-wrap{flex-direction:column;gap:10px}"
            ".sys-nav{flex:0 0 auto;flex-direction:row;flex-wrap:wrap;position:static}"
            ".sys-nav button{width:auto;padding:6px 10px}"
            ".sys-nav .key{display:none}"
            ".sys-panes{max-width:100%;width:100%}}"
        ));
    }
    if (css_flags & HTML_CSS_SHOTS) {
        g_server.sendContent_P(PSTR(
            ".shot-space{font-size:13px;margin:0 0 12px;padding:10px 12px;border:1px solid var(--tab-bd);"
            "border-radius:6px;background:var(--td-bg);line-height:1.5}"
            ".shot-space .bar{height:8px;border-radius:4px;background:var(--code-bg);margin:8px 0 0;"
            "overflow:hidden}"
            ".shot-space .bar>i{display:block;height:100%;background:var(--tab-act);border-radius:4px}"
            ".shot-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(200px,1fr));"
            "gap:12px;margin:12px 0}"
            ".shot-card{border:1px solid var(--tab-bd);border-radius:6px;padding:8px;"
            "background:var(--td-bg)}"
            ".shot-card a.thumb{display:block;background:#000;border-radius:4px;overflow:hidden;"
            "aspect-ratio:240/135}"
            ".shot-card img{width:100%;height:100%;object-fit:contain;display:block;"
            "image-rendering:pixelated}"
            ".shot-card .meta{margin-top:6px;font-size:12px;line-height:1.35}"
            ".shot-card .meta a{color:var(--link);word-break:break-all}"
            ".shot-card .size{color:var(--hint)}"
        ));
    }
    if (css_flags & HTML_CSS_FILES) {
        g_server.sendContent_P(PSTR(
            ".fm-crumb{display:flex;flex-wrap:wrap;align-items:center;gap:4px 2px;"
            "font-size:13px;margin:0 0 12px;padding:8px 10px;border:1px solid var(--tab-bd);"
            "border-radius:6px;background:var(--td-bg);word-break:break-all}"
            ".fm-crumb a{color:var(--link);text-decoration:none;padding:2px 4px;border-radius:4px}"
            ".fm-crumb a:hover{background:var(--td-hover)}"
            ".fm-crumb .sep{color:var(--hint);padding:0 2px}"
            ".fm-crumb .cur{color:var(--fg-h);font-weight:600;padding:2px 4px}"
            ".fm-toolbar{display:flex;flex-wrap:wrap;align-items:center;gap:8px;margin:0 0 12px}"
            ".fm-mkdir{display:flex;flex-wrap:wrap;align-items:center;gap:6px;flex:1;min-width:220px;"
            "margin:0;padding:8px 10px;border:1px solid var(--tab-bd);border-radius:6px;"
            "background:var(--td-bg)}"
            ".fm-mkdir label{margin:0;color:var(--hint);font-size:12px;white-space:nowrap}"
            ".fm-mkdir input[type=text]{flex:1;min-width:120px;margin:0;width:auto}"
            ".fm-mkdir button{margin:0}"
            ".fm-count{font-size:13px;color:var(--hint);margin-left:auto}"
            ".fm-flash{margin:0 0 12px;padding:8px 12px;border-radius:6px;font-size:13px}"
            ".fm-flash.ok{background:#1b3d2f;color:#81c784;border:1px solid #2e5a45}"
            ".fm-flash.err{background:#3d1b1b;color:#ff8a80;border:1px solid #5c3333}"
            "@media(prefers-color-scheme:light){"
            ".fm-flash.ok{background:#e8f5e9;color:#2e7d32;border-color:#a5d6a7}"
            ".fm-flash.err{background:#ffebee;color:#c62828;border-color:#ef9a9a}}"
            "table.fm-table{width:100%;min-width:560px;border-collapse:collapse;table-layout:fixed}"
            ".fm-table th,.fm-table td{border:1px solid var(--tab-bd);padding:6px 8px;"
            "vertical-align:middle;background:var(--td-bg);font-size:13px}"
            ".fm-table th{background:var(--th-bg);font-size:12px;font-weight:600;text-align:left;"
            "color:var(--fg);white-space:nowrap}"
            ".fm-table tr:nth-child(even) td{background:var(--td-alt)}"
            ".fm-table tr:hover td{background:var(--td-hover)!important}"
            ".fm-table .col-name{width:36%}"
            ".fm-table .col-size{width:12%;white-space:nowrap;font-variant-numeric:tabular-nums;"
            "color:var(--hint)}"
            ".fm-table .col-time{width:18%;white-space:nowrap;font-variant-numeric:tabular-nums;"
            "font-size:12px;color:var(--hint)}"
            ".fm-table .col-act{width:16%;white-space:nowrap}"
            ".fm-table a.name{color:var(--link);text-decoration:none;word-break:break-all}"
            ".fm-table a.name:hover{text-decoration:underline}"
            ".fm-table .tag{display:inline-block;margin-right:4px;padding:1px 5px;border-radius:3px;"
            "font-size:10px;font-weight:700;letter-spacing:.02em;color:var(--hint);"
            "background:var(--code-bg);vertical-align:middle}"
            ".fm-table .tag.dir{color:#b8860b}"
            ".fm-table .acts{display:inline-flex;flex-wrap:wrap;gap:4px;align-items:center}"
            ".fm-table .acts form{display:inline;margin:0}"
            ".fm-table .acts .btn,.fm-table .acts button{margin:0;padding:4px 8px;font-size:12px}"
            ".fm-empty{text-align:center;padding:28px 12px;border:1px dashed var(--tab-bd);"
            "border-radius:6px;color:var(--hint);font-size:13px;background:var(--td-bg)}"
            ".fm-empty strong{display:block;color:var(--fg-h);margin-bottom:4px;font-size:14px}"
        ));
    }
    g_server.sendContent_P(PSTR("</style></head><body>"));
    g_server.sendContent(body);
    g_server.sendContent_P(PSTR("</body></html>"));
    g_server.sendContent(""); // 结束 chunked 传输
}

// 顶栏当前 Tab（用于高亮）
enum class WebNavTab : uint8_t {
    None = 0,
    Wifi,
    Devices,
    Groups,
    Cursor,
    System,
    Shots,
    Files,
    Advanced,
    Example,
    About,
};

// nav 链接：当前页加 active
static void appendNavLink(String& body, const WebNavTab tab, const WebNavTab active, const char* href,
                          const char* label) {
    body += F("<a href='");
    body += href;
    body += '\'';
    if (tab == active) {
        body += F(" class='active'");
    }
    body += '>';
    body += label;
    body += F("</a>");
}

// 各页共用顶栏导航；并打开内容卡片
static void appendTopBar(String& body, const char* title, const WebNavTab active = WebNavTab::None) {
    body += F("<div class='topbar'><div class='brand'>"
              "<img class='site-logo' src='/favicon.svg' alt='' width='36' height='36'>"
              "<div class='brand-text'><h1>");
    body += title;
    body += F("</h1></div></div><nav class='nav'>");
    appendNavLink(body, WebNavTab::Wifi, active, "/wifi", "WiFi");
    appendNavLink(body, WebNavTab::Devices, active, "/", "米家设备");
    appendNavLink(body, WebNavTab::Groups, active, "/groups", "米家设备编组");
    appendNavLink(body, WebNavTab::Cursor, active, "/cursor", "Cursor");
    appendNavLink(body, WebNavTab::System, active, "/system", "系统");
    appendNavLink(body, WebNavTab::Shots, active, "/shots", "截图");
    appendNavLink(body, WebNavTab::Files, active, "/files", "TF文件");
    appendNavLink(body, WebNavTab::Advanced, active, "/advanced", "高级JSON");
    appendNavLink(body, WebNavTab::Example, active, "/example", "示例");
    appendNavLink(body, WebNavTab::About, active, "/about", "关于");
    body += F("</nav></div><div class='card'>");
}

// 关闭内容卡片（脚本放在卡片外）
static void appendCardEnd(String& body) {
    body += F("</div>");
}

// 嵌入 cfg JSON 供页面 JS 读取
static void appendCfgDataScript(String& body, const String& cfg) {
    body += F("<script type='application/json' id='cfg-data'>");
    body += cfg;
    body += F("</script>");
}

// 默认 cfg 对象字面量（JS）
static const char* JS_CFG_DEFAULT =
    "{wifis:[],wifi_active:'',devices:[],device_groups:[],cursor:{token:''},"
    "system:{week_start:'sunday'},"
    "timezone:'CST-8',screen:{brightness:30,invert:false},"
    "sound:{time_key:true,mijia_on_off:true,volume:25},"
    "time:{default:'up'},infrared:{default:'tv',tv_brand:'samsung',ac_brand:'midea'},"
    "hid_keyboard:{transport:'ble',imu_sensitivity:5}}";

// 加载并规范化 cfg（各编辑页共用）
static void appendJsLoadCfg(String& body) {
    body += F("let cfg=");
    body += JS_CFG_DEFAULT;
    body += F(";"
              "function loadCfg(){try{cfg=JSON.parse(document.getElementById('cfg-data').textContent);}"
              "catch(e){cfg=");
    body += JS_CFG_DEFAULT;
    body += F(";}"
              // 旧 wifi 对象迁移为 wifis[] + wifi_active
              "if((!cfg.wifis||!cfg.wifis.length)&&cfg.wifi&&cfg.wifi.ssid){"
              "cfg.wifis=[{ssid:cfg.wifi.ssid||'',password:cfg.wifi.password||''}];"
              "if(!cfg.wifi_active)cfg.wifi_active=cfg.wifi.ssid;}"
              "if(!cfg.wifis)cfg.wifis=[];"
              "if(cfg.wifi_active==null)cfg.wifi_active='';"
              "delete cfg.wifi;"
              "if(!cfg.devices)cfg.devices=[];"
              "if(!cfg.device_groups)cfg.device_groups=[];"
              "if(!cfg.cursor)cfg.cursor={token:''};"
              "if(!cfg.system)cfg.system={};"
              "if(cfg.system.week_start!=='monday')cfg.system.week_start='sunday';"
              "if(!cfg.timezone)cfg.timezone='CST-8';"
              // screen：兼容旧顶层 brightness
              "if(!cfg.screen)cfg.screen={};"
              "if(cfg.screen.brightness==null&&cfg.brightness!=null)cfg.screen.brightness=cfg.brightness;"
              "delete cfg.brightness;"
              "let bright=cfg.screen.brightness;if(bright==null||isNaN(+bright))bright=30;"
              "bright=+bright;if(bright>100)bright=Math.round(bright*100/255);"
              "if(bright<0)bright=0;if(bright>100)bright=100;cfg.screen.brightness=bright;"
              "if(cfg.screen.invert==null)cfg.screen.invert=false;"
              "if(!cfg.sound)cfg.sound={};"
              "if(cfg.sound.time_key==null)cfg.sound.time_key=true;"
              "if(cfg.sound.mijia_on_off==null)cfg.sound.mijia_on_off=true;"
              "let svol=cfg.sound.volume;if(svol==null||isNaN(+svol))svol=25;"
              "svol=+svol;if(svol<0)svol=0;if(svol>100)svol=100;cfg.sound.volume=svol;"
              "if(!cfg.time)cfg.time={};"
              "if(!cfg.time.default)cfg.time.default='up';"
              // infrared：兼容旧大写 Infrared
              "if(!cfg.infrared&&cfg.Infrared){cfg.infrared=cfg.Infrared;}"
              "delete cfg.Infrared;"
              "if(!cfg.infrared)cfg.infrared={};"
              "if(!cfg.infrared.default)cfg.infrared.default='tv';"
              "if(!cfg.infrared.tv_brand)cfg.infrared.tv_brand='samsung';"
              "if(!cfg.infrared.ac_brand)cfg.infrared.ac_brand='midea';"
              "if(!cfg.hid_keyboard)cfg.hid_keyboard={};"
              "if(cfg.hid_keyboard.transport!=='usb')cfg.hid_keyboard.transport='ble';"
              "let hs=+cfg.hid_keyboard.imu_sensitivity;"
              "if(isNaN(hs)||hs<1||hs>10)hs=5;"
              "cfg.hid_keyboard.imu_sensitivity=Math.round(hs);}");
}

// 米家设备页（原主页设备表）
static void handleFormRoot() {
    const String cfg = sanitizeJsonForHtml(loadConfigText());

    String body;
    body.reserve(cfg.length() + 6144);
    appendTopBar(body, "米家设备", WebNavTab::Devices);
    body += F("<form id='save-form' method='POST' action='/save'>"
              "<input type='hidden' name='config' id='config-payload'>"
              "<div class='toolbar'>"
              "<span class='count' id='dev-count'></span>"
              "</div>"
              "<div class='table-wrap'><table class='dev-table'>"
              "<thead><tr>"
              "<th class='col-idx'>#</th>"
              "<th class='col-name'>名称</th>"
              "<th class='col-namezh'>中文名</th>"
              "<th class='col-hotkey' title='快速选择快捷键 a-z/0-9，勿用 q'>快捷键</th>"
              "<th class='col-act'>操作</th>"
              "<th class='col-ip'>IP</th>"
              "<th class='col-token'>Token</th>"
              "<th class='col-ble'>BLE Key</th>"
              "<th class='col-model'>型号</th>"
              "<th class='col-id'>ID</th>"
              "<th class='col-mac'>MAC</th>"
              "</tr></thead>"
              "<tbody id='dev-tbody'></tbody>"
              "</table></div>"
              // 列表最下方全宽添加按钮
              "<button type='button' id='btn-add' class='btn-add-row' title='添加设备'>+</button>"
              "<div class='save-bar'>"
              "<button type='submit' class='primary' id='btn-save'>保存到设备</button>"
              "</div></form>");
    appendCardEnd(body);
    appendCfgDataScript(body, cfg);
    body += F("<script>");
    body += F("const DEV_MAX=");
    body += WEB_DEVICE_MAX;
    body += F(";");
    body += F("const ICON_NAMES=[");
    for (const char* const* name = deviceIconNames(); *name != nullptr; ++name) {
        body += '\'';
        body += *name;
        body += F("',");
    }
    body += F("];");
    appendJsLoadCfg(body);
    body += F(
        "function esc(s){return String(s==null?'':s).replace(/&/g,'&amp;').replace(/\"/g,'&quot;')"
        ".replace(/</g,'&lt;');}"
        "function ta(f,v){return `<textarea data-f='${f}' rows='1'>${esc(v)}</textarea>`;}"
        "function iconBase(model){const m=String(model||'').toLowerCase();"
        "for(const n of ICON_NAMES){if(m.includes(n))return n;}"
        "if(m.includes('light'))return 'light';return 'default';}"
        "function iconUrl(model){return '/icon/device/'+iconBase(model)+'.png';}"
        "function modelCell(model){return `<div class='model-cell'>"
        "<img class='dev-icon' src='${iconUrl(model)}' alt='' title='${iconBase(model)}'>"
        "${ta('model',model)}</div>`;}"
        "function bleKeyOf(d){return (d.ble&&d.ble.key)||d.ble_key||'';}"
        "function memberIdOf(m){return typeof m==='string'?m:(m&&m.id)||'';}"
        "function collectDevices(){cfg.devices=[];"
        "document.querySelectorAll('#dev-tbody tr').forEach(row=>{"
        "const d={};row.querySelectorAll('[data-f]').forEach(el=>{d[el.dataset.f]=el.value;});"
        "const bk=d.ble_key||'';delete d.ble_key;"
        "if(bk)d.ble={key:bk};else delete d.ble;"
        "if(!d.name_zh)delete d.name_zh;"
        "let hk=String(d.hotkey||'').trim().toLowerCase();"
        "hk=hk.length?hk[0]:'';"
        "if(/^[a-z0-9]$/.test(hk)&&hk!=='q')d.hotkey=hk;else delete d.hotkey;"
        "cfg.devices.push(d);});}"
        "function dedupeDeviceHotkeys(){const seen=new Set();"
        "(cfg.devices||[]).forEach(d=>{if(!d||!d.hotkey)return;"
        "const h=String(d.hotkey).toLowerCase()[0];"
        "if(!h||seen.has(h)){delete d.hotkey;return;}seen.add(h);d.hotkey=h;});}"
        // 仅写回本页设备字段，其余 cfg 原样提交
        "function collect(){collectDevices();dedupeDeviceHotkeys();}"
        "function renderDevices(){const tb=document.getElementById('dev-tbody');tb.innerHTML='';"
        "cfg.devices.forEach((d,i)=>{const tr=document.createElement('tr');tr.dataset.i=i;"
        "tr.innerHTML=`<td class='col-idx'>${i+1}</td>"
        "<td class='col-name'>${ta('name',d.name)}</td>"
        "<td class='col-namezh'>${ta('name_zh',d.name_zh||d.name_cn||'')}</td>"
        "<td class='col-hotkey'>${ta('hotkey',d.hotkey||'')}</td>"
        "<td class='col-act'><div class='act-stack'>"
        "<button type='button' class='icon-btn' data-act='up' title='上移'>↑</button>"
        "<button type='button' class='icon-btn' data-act='down' title='下移'>↓</button>"
        "<button type='button' class='icon-btn' data-act='top' title='置顶'>顶</button>"
        "<button type='button' class='icon-btn' data-act='bottom' title='置底'>底</button>"
        "<button type='button' class='danger icon-btn' data-act='del' title='删除'>删</button>"
        "</div></td>"
        "<td class='col-ip'>${ta('ip',d.ip)}</td>"
        "<td class='col-token'>${ta('token',d.token)}</td>"
        "<td class='col-ble'>${ta('ble_key',bleKeyOf(d))}</td>"
        "<td class='col-model'>${modelCell(d.model)}</td>"
        "<td class='col-id'>${ta('id',d.id)}</td>"
        "<td class='col-mac'>${ta('mac',d.mac)}</td>`;tb.appendChild(tr);});"
        "document.getElementById('dev-count').textContent=`共 ${cfg.devices.length} / ${DEV_MAX} 台`;}"
        "function move(i,d){collect();const j=i+d;if(j<0||j>=cfg.devices.length)return;"
        "[cfg.devices[i],cfg.devices[j]]=[cfg.devices[j],cfg.devices[i]];renderDevices();}"
        "function moveTop(i){collect();if(i<=0)return;"
        "const item=cfg.devices.splice(i,1)[0];cfg.devices.unshift(item);renderDevices();}"
        "function moveBottom(i){collect();if(i>=cfg.devices.length-1)return;"
        "const item=cfg.devices.splice(i,1)[0];cfg.devices.push(item);renderDevices();}"
        "function init(){loadCfg();renderDevices();"
        "document.getElementById('btn-add').onclick=()=>{collect();"
        "if(cfg.devices.length>=DEV_MAX){alert('最多 '+DEV_MAX+' 台设备');return;}"
        "cfg.devices.push({name:'',name_zh:'',id:'',mac:'',ip:'',token:'',model:'',hotkey:'',ble:{key:''}});"
        "renderDevices();};"
        "document.getElementById('dev-tbody').onclick=e=>{const b=e.target.closest('button');"
        "if(!b)return;const i=+b.closest('tr').dataset.i;"
        "if(b.dataset.act==='up')move(i,-1);"
        "else if(b.dataset.act==='down')move(i,1);"
        "else if(b.dataset.act==='top')moveTop(i);"
        "else if(b.dataset.act==='bottom')moveBottom(i);"
        "else if(b.dataset.act==='del'){collect();const removed=cfg.devices.splice(i,1)[0];"
        "const rid=removed&&removed.id;if(rid){(cfg.device_groups||[]).forEach(g=>{"
        "g.members=(g.members||[]).filter(m=>memberIdOf(m)!==rid);});}"
        "renderDevices();}};"
        "document.getElementById('dev-tbody').oninput=e=>{const el=e.target;"
        "if(!el||el.dataset.f!=='model')return;"
        "const img=el.closest('tr').querySelector('.dev-icon');"
        "if(!img)return;const base=iconBase(el.value);img.src=iconUrl(el.value);img.title=base;};"
        "document.getElementById('save-form').onsubmit=()=>{collect();"
        "document.getElementById('config-payload').value=JSON.stringify(cfg,null,2);};}"
        "init();");
    body += F("</script>");
    sendHtmlPage(body, HTML_CSS_DEVICES);
}

// WiFi 配置页：管理 wifis[] + wifi_active（最多 5 条）
static void handleWifiPage() {
    const String cfg = sanitizeJsonForHtml(loadConfigText());

    String body;
    body.reserve(cfg.length() + 4096);
    appendTopBar(body, "WiFi", WebNavTab::Wifi);
    body += F("<form id='save-form' method='POST' action='/save'>"
              "<input type='hidden' name='config' id='config-payload'>"
              "<h2>WiFi</h2>"
              "<p class='hint'>对应 <code>wifis[]</code> + <code>wifi_active</code>；最多 "
              "<strong>5</strong> 条。选中 Active 为当前联网凭据；设备 WiFi App 也可切换。</p>"
              "<div class='toolbar'>"
              "<span class='count' id='wifi-count'></span>"
              "</div>"
              "<div class='table-wrap'>"
              "<table class='wifi-table' id='wifi-table'>"
              "<thead><tr>"
              "<th class='col-active'>Active</th>"
              "<th class='col-ssid'>SSID</th>"
              "<th class='col-pass'>密码</th>"
              "<th class='col-act'></th>"
              "</tr></thead>"
              "<tbody id='wifi-tbody'></tbody>"
              "</table></div>"
              "<button type='button' class='btn-add-row' id='btn-add-wifi' title='添加'>+</button>"
              "<div class='save-bar'>"
              "<button type='submit' class='primary' id='btn-save'>保存到设备</button>"
              "</div></form>");
    appendCardEnd(body);
    appendCfgDataScript(body, cfg);
    body += F("<script>");
    appendJsLoadCfg(body);
    // 列表渲染 / 增删 / 收集；上限与固件 WIFI_PROFILE_MAX 一致
    body += F(
        "const WIFI_MAX=5;"
        "function ensureWifis(){"
        "if(!Array.isArray(cfg.wifis))cfg.wifis=[];"
        "if(cfg.wifis.length>WIFI_MAX)cfg.wifis=cfg.wifis.slice(0,WIFI_MAX);"
        "if(cfg.wifi_active==null)cfg.wifi_active='';"
        "const names=cfg.wifis.map(w=>w&&w.ssid).filter(Boolean);"
        "if(cfg.wifi_active&&names.indexOf(cfg.wifi_active)<0)cfg.wifi_active=names[0]||'';"
        "if(!cfg.wifi_active&&names.length)cfg.wifi_active=names[0];"
        "delete cfg.wifi;}"
        "function updateCount(){"
        "const n=(cfg.wifis||[]).length;"
        "document.getElementById('wifi-count').textContent=n+' / '+WIFI_MAX;"
        "document.getElementById('btn-add-wifi').disabled=n>=WIFI_MAX;}"
        "function renderWifiRows(){"
        "ensureWifis();"
        "const tb=document.getElementById('wifi-tbody');"
        "tb.innerHTML='';"
        "cfg.wifis.forEach((w,i)=>{"
        "const tr=document.createElement('tr');"
        "const act=document.createElement('td');act.className='col-active';"
        "const radio=document.createElement('input');"
        "radio.type='radio';radio.name='wifi_active';radio.value=String(i);"
        "radio.checked=!!(w.ssid&&w.ssid===cfg.wifi_active);"
        "radio.onchange=()=>{if(w.ssid)cfg.wifi_active=w.ssid;};"
        "act.appendChild(radio);"
        "const tdS=document.createElement('td');tdS.className='col-ssid';"
        "const inS=document.createElement('input');inS.type='text';inS.autocomplete='off';"
        "inS.value=w.ssid||'';inS.placeholder='SSID';"
        "inS.oninput=()=>{const old=w.ssid||'';w.ssid=inS.value;"
        "if(cfg.wifi_active===old||radio.checked)cfg.wifi_active=w.ssid;};"
        "tdS.appendChild(inS);"
        "const tdP=document.createElement('td');tdP.className='col-pass';"
        "const inP=document.createElement('input');inP.type='text';inP.autocomplete='off';"
        "inP.value=w.password||'';inP.placeholder='password';"
        "inP.oninput=()=>{w.password=inP.value;};"
        "tdP.appendChild(inP);"
        "const tdA=document.createElement('td');tdA.className='col-act';"
        "const del=document.createElement('button');del.type='button';"
        "del.className='danger icon-btn';del.textContent='删';"
        "del.onclick=()=>{"
        "const was=w.ssid||'';"
        "cfg.wifis.splice(i,1);"
        "if(cfg.wifi_active===was){"
        "cfg.wifi_active=(cfg.wifis[0]&&cfg.wifis[0].ssid)||'';}"
        "renderWifiRows();};"
        "tdA.appendChild(del);"
        "tr.appendChild(act);tr.appendChild(tdS);tr.appendChild(tdP);tr.appendChild(tdA);"
        "tb.appendChild(tr);});"
        "updateCount();}"
        "function collect(){"
        "ensureWifis();"
        "const rows=[...document.querySelectorAll('#wifi-tbody tr')];"
        "const next=[];"
        "let active='';"
        "rows.forEach((tr,i)=>{"
        "const ssid=(tr.querySelector('.col-ssid input')||{}).value||'';"
        "const pass=(tr.querySelector('.col-pass input')||{}).value||'';"
        "const s=ssid.trim();"
        "if(!s)return;"
        "if(next.length>=WIFI_MAX)return;"
        "next.push({ssid:s,password:pass});"
        "const radio=tr.querySelector('.col-active input');"
        "if(radio&&radio.checked)active=s;});"
        "cfg.wifis=next;"
        "if(!active&&next.length)active=next[0].ssid;"
        "if(active&&!next.some(w=>w.ssid===active))active=next.length?next[0].ssid:'';"
        "cfg.wifi_active=active;"
        "delete cfg.wifi;}"
        "function init(){loadCfg();ensureWifis();renderWifiRows();"
        "document.getElementById('btn-add-wifi').onclick=()=>{"
        "ensureWifis();"
        "if(cfg.wifis.length>=WIFI_MAX)return;"
        "cfg.wifis.push({ssid:'',password:''});"
        "renderWifiRows();"
        "const last=document.querySelector('#wifi-tbody tr:last-child .col-ssid input');"
        "if(last)last.focus();};"
        "document.getElementById('save-form').onsubmit=()=>{collect();"
        "document.getElementById('config-payload').value=JSON.stringify(cfg,null,2);};}"
        "init();");
    body += F("</script>");
    sendHtmlPage(body, HTML_CSS_DEVICES);
}

// 编组页
static void handleGroupsPage() {
    const String cfg = sanitizeJsonForHtml(loadConfigText());

    String body;
    body.reserve(cfg.length() + 4096);
    appendTopBar(body, "米家设备编组", WebNavTab::Groups);
    body += F("<form id='save-form' method='POST' action='/save'>"
              "<input type='hidden' name='config' id='config-payload'>"
              "<p class='hint'>用设备 <code>id</code> 组成米家设备编组；改名不影响成员。"
              "成员里的 name / name_zh 仅方便阅读，保存时会从设备表同步。"
              "BLE 只读设备可勾选但设备端开/关会跳过。"
              "请先在 <a href='/'>米家设备</a> 填写设备 id。</p>"
              "<div class='toolbar'>"
              "<button type='button' id='btn-add-group'>+ 添加米家设备编组</button>"
              "<span class='count' id='group-count'></span>"
              "</div>"
              "<div id='group-list'></div>"
              "<div class='save-bar'>"
              "<button type='submit' class='primary'>保存到设备</button>"
              "</div></form>");
    appendCardEnd(body);
    appendCfgDataScript(body, cfg);
    body += F("<script>");
    body += F("const GROUP_MAX=");
    body += String(MIJIA_GROUP_MAX);
    body += F(";const GROUP_MEMBER_MAX=");
    body += String(MIJIA_GROUP_MEMBER_MAX);
    body += F(";");
    appendJsLoadCfg(body);
    body += F(
        "function esc(s){return String(s==null?'':s).replace(/&/g,'&amp;').replace(/\"/g,'&quot;')"
        ".replace(/</g,'&lt;');}"
        "function bleKeyOf(d){return (d.ble&&d.ble.key)||d.ble_key||'';}"
        "function isBleDev(d){return !!bleKeyOf(d);}"
        "function memberIdOf(m){return typeof m==='string'?m:(m&&m.id)||'';}"
        "function syncGroupMembers(g){const byId={};"
        "(cfg.devices||[]).forEach(d=>{if(d&&d.id)byId[d.id]=d;});"
        "const seen=new Set();const out=[];"
        "(g.members||[]).forEach(m=>{const id=memberIdOf(m);if(!id||seen.has(id))return;"
        "const d=byId[id];if(!d)return;seen.add(id);"
        "const row={id:id,name:d.name||''};"
        "const zh=d.name_zh||d.name_cn||'';if(zh)row.name_zh=zh;"
        "out.push(row);});"
        "g.members=out.slice(0,GROUP_MEMBER_MAX);return g;}"
        "function collectGroups(){cfg.device_groups=[];"
        "document.querySelectorAll('#group-list .group-card').forEach(card=>{"
        "const g={name:card.querySelector('[data-gf=name]').value||'',"
        "name_zh:card.querySelector('[data-gf=name_zh]').value||'',members:[]};"
        "card.querySelectorAll('.group-members input[type=checkbox]:checked').forEach(cb=>{"
        "if(g.members.length>=GROUP_MEMBER_MAX)return;"
        "const id=cb.value;if(!id)return;"
        "const d=(cfg.devices||[]).find(x=>x&&x.id===id);"
        "const row={id:id,name:(d&&d.name)||cb.dataset.name||''};"
        "const zh=(d&&(d.name_zh||d.name_cn))||cb.dataset.namezh||'';"
        "if(zh)row.name_zh=zh;g.members.push(row);});"
        "if(!g.name_zh)delete g.name_zh;"
        "cfg.device_groups.push(g);});}"
        "function collect(){collectGroups();"
        "(cfg.device_groups||[]).forEach(syncGroupMembers);}"
        "function renderGroups(){const list=document.getElementById('group-list');list.innerHTML='';"
        "if(!cfg.device_groups)cfg.device_groups=[];"
        "cfg.device_groups.forEach(syncGroupMembers);"
        "cfg.device_groups.forEach((g,gi)=>{const selected=new Set((g.members||[]).map(memberIdOf));"
        "const card=document.createElement('div');card.className='group-card';card.dataset.gi=gi;"
        "let membersHtml='';"
        "(cfg.devices||[]).forEach(d=>{if(!d||!d.id)return;"
        "const zh=d.name_zh||d.name_cn||'';"
        "const label=zh?`${esc(zh)} (${esc(d.name||'')})`:`${esc(d.name||'(unnamed)')}`;"
        "const ble=isBleDev(d);"
        "membersHtml+=`<label class='member${ble?' ble':''}'>"
        "<input type='checkbox' value='${esc(d.id)}' data-name='${esc(d.name||'')}' "
        "data-namezh='${esc(zh)}' ${selected.has(d.id)?'checked':''}>"
        "<span class='member-meta'><span>${label}${ble?' · BLE':''}</span>"
        "<div class='member-id'>id: ${esc(d.id)}</div></span></label>`;});"
        "if(!membersHtml)membersHtml='<p class=\"hint\">请先在「米家设备」里填写设备 id</p>';"
        "card.innerHTML=`<div class='group-head'>"
        "<label>名称<input data-gf='name' value='${esc(g.name||'')}'></label>"
        "<label>中文名<input data-gf='name_zh' value='${esc(g.name_zh||g.name_cn||'')}'></label>"
        "<div class='group-acts'>"
        "<button type='button' class='icon-btn' data-gact='up' title='上移'>↑</button>"
        "<button type='button' class='icon-btn' data-gact='down' title='下移'>↓</button>"
        "<button type='button' class='danger icon-btn' data-gact='del' title='删除'>删</button>"
        "</div></div>"
        "<div class='hint'>成员 ${selected.size} / ${GROUP_MEMBER_MAX}</div>"
        "<div class='group-members'>${membersHtml}</div>`;"
        "list.appendChild(card);});"
        "document.getElementById('group-count').textContent="
        "`共 ${cfg.device_groups.length} / ${GROUP_MAX} 组`;}"
        "function moveGroup(i,d){collect();const j=i+d;if(j<0||j>=cfg.device_groups.length)return;"
        "[cfg.device_groups[i],cfg.device_groups[j]]=[cfg.device_groups[j],cfg.device_groups[i]];"
        "renderGroups();}"
        "function init(){loadCfg();renderGroups();"
        "document.getElementById('btn-add-group').onclick=()=>{collect();"
        "if(cfg.device_groups.length>=GROUP_MAX){alert('最多 '+GROUP_MAX+' 组');return;}"
        "cfg.device_groups.push({name:'',name_zh:'',members:[]});renderGroups();};"
        "document.getElementById('group-list').onclick=e=>{const b=e.target.closest('button');"
        "if(!b||!b.dataset.gact)return;const i=+b.closest('.group-card').dataset.gi;"
        "if(b.dataset.gact==='up')moveGroup(i,-1);"
        "else if(b.dataset.gact==='down')moveGroup(i,1);"
        "else if(b.dataset.gact==='del'){collect();cfg.device_groups.splice(i,1);renderGroups();}};"
        "document.getElementById('save-form').onsubmit=()=>{collect();"
        "document.getElementById('config-payload').value=JSON.stringify(cfg,null,2);};}"
        "init();");
    body += F("</script>");
    sendHtmlPage(body, HTML_CSS_GROUPS);
}

// Cursor token 页
static void handleCursorPage() {
    const String cfg = sanitizeJsonForHtml(loadConfigText());

    String body;
    body.reserve(cfg.length() + 2048);
    appendTopBar(body, "Cursor", WebNavTab::Cursor);
    body += F("<form id='save-form' method='POST' action='/save'>"
              "<input type='hidden' name='config' id='config-payload'>"
              "<h2>Cursor Session Token</h2>"
              "<p class='hint'>用于 Cursor 应用拉取用量数据，写入 "
              "<code>cursor.token</code>。</p>"
              "<h3 class='token-method-title'>方式一：浏览器 Cookie</h3>"
              "<ol class='token-steps'>"
              "<li>在电脑浏览器登录 <code>https://cursor.com</code></li>"
              "<li>打开开发者工具 → Application（应用）→ Cookies → "
              "<code>cursor.com</code></li>"
              "<li>找到 <code>WorkosCursorSessionToken</code>，复制其 Value</li>"
              "<li>粘贴到下方输入框并保存</li>"
              "</ol>"
              "<h3 class='token-method-title'>方式二：Cursor IDE 本地 SQLite</h3>"
              "<p class='hint'>在电脑安装并登录 Cursor IDE 后，从 "
              "<code>state.vscdb</code> 读取 JWT（需本机已安装 "
              "<code>sqlite3</code>）。</p>"
              "<ul class='token-paths'>"
              "<li>macOS：<code>~/Library/Application Support/Cursor/User/globalStorage/state.vscdb</code></li>"
              "<li>Linux：<code>~/.config/Cursor/User/globalStorage/state.vscdb</code></li>"
              "<li>Windows：<code>%APPDATA%\\Cursor\\User\\globalStorage\\state.vscdb</code></li>"
              "</ul>"
              "<pre class='token-cmd'>sqlite3 \"&lt;path-to&gt;/state.vscdb\" "
              "\"SELECT value FROM ItemTable WHERE key='cursorAuth/accessToken';\"</pre>"
              "<p class='hint'>输出为 JWT（<code>eyJ...</code> 开头），可直接粘贴；"
              "固件会从 JWT 解析 <code>sub</code> 并组装 Cookie。</p>"
              "<p class='token-formats'>支持格式：完整 Cookie 值 "
              "（<code>sub::jwt</code> 或 <code>sub%3A%3Ajwt</code>）；"
              "或仅粘贴 JWT。</p>"
              "<label>Session Token<textarea id='cursor-key' rows='4'></textarea></label>"
              "<div class='save-bar'>"
              "<button type='submit' class='primary'>保存到设备</button>"
              "</div></form>");
    appendCardEnd(body);
    appendCfgDataScript(body, cfg);
    body += F("<script>");
    appendJsLoadCfg(body);
    body += F(
        "function collect(){if(!cfg.cursor)cfg.cursor={token:''};"
        "cfg.cursor.token=document.getElementById('cursor-key').value;"
        "delete cfg.cursor.api_key;}"
        "function init(){loadCfg();"
        "document.getElementById('cursor-key').value=cfg.cursor.token||'';"
        "document.getElementById('save-form').onsubmit=()=>{collect();"
        "document.getElementById('config-payload').value=JSON.stringify(cfg,null,2);};}"
        "init();");
    body += F("</script>");
    sendHtmlPage(body, HTML_CSS_CURSOR);
}

// 系统设置页
static void handleSystemPage() {
    const String cfg = sanitizeJsonForHtml(loadConfigText());

    String body;
    body.reserve(cfg.length() + 3072);
    appendTopBar(body, "系统设置", WebNavTab::System);
    body += F("<form id='save-form' method='POST' action='/save'>"
              "<input type='hidden' name='config' id='config-payload'>"
              "<p class='hint'>左侧选择分项，右侧编辑对应设置。亮度 screen.brightness（0~100）"
              "会换算为背光 0~255；反色写入 screen.invert。</p>"
              "<div class='sys-wrap'>"
              // 左侧分项 list
              "<nav class='sys-nav' id='sys-nav'>"
              "<button type='button' data-pane='system' class='active'>系统"
              "<span class='key'>system</span></button>"
              "<button type='button' data-pane='screen'>屏幕"
              "<span class='key'>screen</span></button>"
              "<button type='button' data-pane='sound'>声音"
              "<span class='key'>sound</span></button>"
              "<button type='button' data-pane='time'>Time"
              "<span class='key'>time</span></button>"
              "<button type='button' data-pane='infrared'>红外"
              "<span class='key'>infrared</span></button>"
              "<button type='button' data-pane='keyboard'>键盘"
              "<span class='key'>hid_keyboard</span></button>"
              "</nav>"
              // 右侧设置内容
              "<div class='sys-panes'>"
              // 系统：timezone（顶层）+ system.week_start
              "<section class='sys-sec active' data-pane='system'>"
              "<h3>系统 <span class='key'>system / timezone</span></h3>"
              "<label>时区（POSIX TZ）"
              "<input id='sys-timezone' placeholder='CST-8' autocomplete='off'></label>"
              "<label>日历每周起始日"
              "<select id='sys-week-start'>"
              "<option value='sunday'>周日</option>"
              "<option value='monday'>周一</option>"
              "</select></label>"
              "</section>"
              // 屏幕
              "<section class='sys-sec' data-pane='screen'>"
              "<h3>屏幕 <span class='key'>screen</span></h3>"
              "<label>亮度（0~100）"
              "<div class='bright-row'>"
              "<input id='sys-brightness' type='range' min='0' max='100' step='1'>"
              "<span class='bright-val' id='sys-brightness-val'>30</span>"
              "</div></label>"
              "<label class='check-row'>"
              "<input id='sys-screen-invert' type='checkbox'>"
              "<span>屏幕反色（invert）</span></label>"
              "</section>"
              // 声音
              "<section class='sys-sec' data-pane='sound'>"
              "<h3>声音 <span class='key'>sound</span></h3>"
              "<label>喇叭音量（0~100）"
              "<div class='bright-row'>"
              "<input id='sys-sound-volume' type='range' min='0' max='100' step='1'>"
              "<span class='bright-val' id='sys-sound-volume-val'>25</span>"
              "</div></label>"
              "<label class='check-row'>"
              "<input id='sys-sound-time-key' type='checkbox'>"
              "<span>Time 按键声（stopwatch / countdown）</span></label>"
              "<label class='check-row'>"
              "<input id='sys-sound-mijia' type='checkbox'>"
              "<span>米家开/关提示音</span></label>"
              "</section>"
              // Time 应用
              "<section class='sys-sec' data-pane='time'>"
              "<h3>Time <span class='key'>time</span></h3>"
              "<label>默认模块"
              "<select id='sys-time-default'>"
              "<option value='up'>Uptime</option>"
              "<option value='ntp'>Clock</option>"
              "<option value='countdown'>Countdown</option>"
              "<option value='stopwatch'>Stopwatch</option>"
              "</select></label>"
              "</section>"
              // 红外
              "<section class='sys-sec' data-pane='infrared'>"
              "<h3>红外 <span class='key'>infrared</span></h3>"
              "<label>默认功能块"
              "<select id='sys-ir-default'>"
              "<option value='tv'>电视 TV</option>"
              "<option value='ac'>空调 AC</option>"
              "</select></label>"
              "<label>默认电视品牌"
              "<select id='sys-ir-tv-brand'>"
              "<option value='samsung'>Samsung</option>"
              "<option value='sony'>Sony</option>"
              "<option value='lg'>LG</option>"
              "<option value='panasonic'>Panasonic</option>"
              "<option value='nec'>NEC</option>"
              "</select></label>"
              "<label>默认空调品牌"
              "<select id='sys-ir-ac-brand'>"
              "<option value='midea'>Midea</option>"
              "<option value='gree'>Gree</option>"
              "<option value='haier'>Haier</option>"
              "<option value='aux'>AUX</option>"
              "<option value='hisense'>Hisense</option>"
              "<option value='xiaomi'>Xiaomi</option>"
              "</select></label>"
              "</section>"
              // HID 键盘
              "<section class='sys-sec' data-pane='keyboard'>"
              "<h3>键盘 <span class='key'>hid_keyboard</span></h3>"
              "<label>默认传输方式"
              "<select id='sys-hid-transport'>"
              "<option value='ble'>Bluetooth LE</option>"
              "<option value='usb'>USB</option>"
              "</select></label>"
              "<label>IMU 鼠标灵敏度（1~10）"
              "<div class='bright-row'>"
              "<input id='sys-hid-sensitivity' type='range' min='1' max='10' step='1'>"
              "<span class='bright-val' id='sys-hid-sensitivity-val'>5</span>"
              "</div></label>"
              "</section>"
              "</div></div>"
              "<div class='save-bar'>"
              "<button type='submit' class='primary'>保存到设备</button>"
              "</div></form>");
    appendCardEnd(body);
    appendCfgDataScript(body, cfg);
    body += F("<script>");
    appendJsLoadCfg(body);
    body += F(
        "function collect(){"
        "cfg.timezone=document.getElementById('sys-timezone').value||'CST-8';"
        "if(!cfg.system)cfg.system={};"
        "cfg.system.week_start=document.getElementById('sys-week-start').value||'sunday';"
        "if(!cfg.screen)cfg.screen={};"
        "let b=+document.getElementById('sys-brightness').value;if(isNaN(b))b=30;"
        "if(b<0)b=0;if(b>100)b=100;cfg.screen.brightness=b;"
        "cfg.screen.invert=document.getElementById('sys-screen-invert').checked;"
        "delete cfg.brightness;"
        "if(!cfg.sound)cfg.sound={};"
        "cfg.sound.time_key=document.getElementById('sys-sound-time-key').checked;"
        "cfg.sound.mijia_on_off=document.getElementById('sys-sound-mijia').checked;"
        "let v=+document.getElementById('sys-sound-volume').value;if(isNaN(v))v=25;"
        "if(v<0)v=0;if(v>100)v=100;cfg.sound.volume=v;"
        "if(!cfg.time)cfg.time={};"
        "cfg.time.default=document.getElementById('sys-time-default').value||'up';"
        "delete cfg.Infrared;"
        "if(!cfg.infrared)cfg.infrared={};"
        "cfg.infrared.default=document.getElementById('sys-ir-default').value||'tv';"
        "cfg.infrared.tv_brand=document.getElementById('sys-ir-tv-brand').value||'samsung';"
        "cfg.infrared.ac_brand=document.getElementById('sys-ir-ac-brand').value||'midea';"
        "if(!cfg.hid_keyboard)cfg.hid_keyboard={};"
        "cfg.hid_keyboard.transport=document.getElementById('sys-hid-transport').value||'ble';"
        "let hs=+document.getElementById('sys-hid-sensitivity').value;"
        "if(isNaN(hs)||hs<1||hs>10)hs=5;"
        "cfg.hid_keyboard.imu_sensitivity=Math.round(hs);}"
        // 切换左侧分项，右侧只显示对应分区（隐藏分区仍在表单内，保存不受影响）
        "function showPane(name){"
        "document.querySelectorAll('#sys-nav button').forEach(b=>{"
        "b.classList.toggle('active',b.dataset.pane===name);});"
        "document.querySelectorAll('.sys-panes .sys-sec').forEach(s=>{"
        "s.classList.toggle('active',s.dataset.pane===name);});}"
        "function initPanes(){"
        "const nav=document.getElementById('sys-nav');"
        "nav.onclick=e=>{const b=e.target.closest('button[data-pane]');"
        "if(!b)return;showPane(b.dataset.pane);"
        "history.replaceState(null,'','#'+b.dataset.pane);};"
        "const want=(location.hash||'').slice(1);"
        "if(/^[a-z_]+$/.test(want)&&nav.querySelector('button[data-pane=\"'+want+'\"]'))"
        "showPane(want);}"
        "function init(){loadCfg();"
        "document.getElementById('sys-timezone').value=cfg.timezone||'CST-8';"
        "document.getElementById('sys-week-start').value=cfg.system.week_start||'sunday';"
        "document.getElementById('sys-brightness').value=String(cfg.screen.brightness);"
        "document.getElementById('sys-brightness-val').textContent=String(cfg.screen.brightness);"
        "document.getElementById('sys-screen-invert').checked=!!cfg.screen.invert;"
        "document.getElementById('sys-sound-time-key').checked=!!cfg.sound.time_key;"
        "document.getElementById('sys-sound-mijia').checked=!!cfg.sound.mijia_on_off;"
        "document.getElementById('sys-sound-volume').value=String(cfg.sound.volume);"
        "document.getElementById('sys-sound-volume-val').textContent=String(cfg.sound.volume);"
        "document.getElementById('sys-time-default').value=cfg.time.default||'up';"
        "document.getElementById('sys-ir-default').value=cfg.infrared.default||'tv';"
        "document.getElementById('sys-ir-tv-brand').value=cfg.infrared.tv_brand||'samsung';"
        "document.getElementById('sys-ir-ac-brand').value=cfg.infrared.ac_brand||'midea';"
        "document.getElementById('sys-hid-transport').value=cfg.hid_keyboard.transport||'ble';"
        "document.getElementById('sys-hid-sensitivity').value=String(cfg.hid_keyboard.imu_sensitivity);"
        "document.getElementById('sys-hid-sensitivity-val').textContent="
        "String(cfg.hid_keyboard.imu_sensitivity);"
        "document.getElementById('sys-brightness').oninput=e=>{"
        "document.getElementById('sys-brightness-val').textContent=e.target.value;};"
        "document.getElementById('sys-sound-volume').oninput=e=>{"
        "document.getElementById('sys-sound-volume-val').textContent=e.target.value;};"
        "document.getElementById('sys-hid-sensitivity').oninput=e=>{"
        "document.getElementById('sys-hid-sensitivity-val').textContent=e.target.value;};"
        "initPanes();"
        "document.getElementById('save-form').onsubmit=()=>{collect();"
        "document.getElementById('config-payload').value=JSON.stringify(cfg,null,2);};}"
        "init();");
    body += F("</script>");
    sendHtmlPage(body, HTML_CSS_SYSTEM);
}

// 高级 JSON 编辑页
static void handleAdvancedRoot() {
    const String cfg = loadConfigText();

    String body;
    body.reserve(cfg.length() + 512);
    appendTopBar(body, "高级 JSON", WebNavTab::Advanced);
    body += F("<h2>高级 JSON 编辑</h2>"
              "<form method='POST' action='/save'>"
              "<textarea class='json-editor' name='config'>");
    body += escapeForTextarea(cfg);
    body += F("</textarea><br><button type='submit' class='primary'>保存到设备</button></form>");
    appendCardEnd(body);
    sendHtmlPage(body);
}

static void handleSave() {
    if (!g_server.hasArg("config")) {
        strncpy(g_web_status, "no body", sizeof(g_web_status));
        g_server.send(400, "text/plain", "missing config");
        return;
    }

    const String json = g_server.arg("config");
    if (saveAppConfigJson(json.c_str())) {
        // 保存后立即生效时区、亮度与反色
        applyLocalTimezone();
        M5Cardputer.Display.setBrightness(brightnessPercentToHw(getAppConfig().brightness));
        M5Cardputer.Display.invertDisplay(getAppConfig().screen_invert);
        snprintf(g_web_status, sizeof(g_web_status), "saved %d dev",
                 getAppConfig().device_count);
        String body;
        appendTopBar(body, "已保存");
        body += F("<p class='ok'>config.json 写入成功。</p>"
                  "<p>设备数: ");
        body += getAppConfig().device_count;
        body += F("</p><div class='result-actions'>"
                  "<a href='/' class='btn primary'>返回米家设备</a>"
                  "<a href='/wifi' class='btn'>WiFi</a>"
                  "<a href='/groups' class='btn'>米家设备编组</a>"
                  "<a href='/advanced' class='btn'>高级 JSON</a></div>");
        appendCardEnd(body);
        sendHtmlPage(body);
    } else {
        strncpy(g_web_status, "json error", sizeof(g_web_status));
        String body;
        appendTopBar(body, "保存失败");
        body += F("<p class='err'>JSON 格式无效，请检查后重试。</p>"
                  "<div class='result-actions'>"
                  "<a href='/' class='btn primary'>返回米家设备</a>"
                  "<a href='/advanced' class='btn'>高级 JSON</a></div>");
        appendCardEnd(body);
        sendHtmlPage(body);
    }
}

static void handleExample() {
    String body;
    appendTopBar(body, "示例 config.json", WebNavTab::Example);
    body += F("<p>WiFi 米家用 <code>ip</code> + <code>token</code>；"
              "BLE 传感器用 <code>mac</code> + <code>ble.key</code>，可加 <code>name_zh</code>。"
              "可选 <code>hotkey</code>（a-z/0-9，勿用 q）供设备端 Q 快速选择；重复时保留靠前第一个。"
              "米家设备编组见 <code>device_groups</code>，成员用设备 <code>id</code> 引用。"
              "系统项见 <a href='/system'>系统设置</a>（时区 / 屏幕 / 提示音 / infrared）。"
              "Cursor 用量见 <a href='/cursor'>Cursor</a> 页。</p><pre>");
    body += DEFAULT_CONFIG;
    body += F("</pre>");
    appendCardEnd(body);
    sendHtmlPage(body);
}

// 关于页：固件与仓库信息
static void handleAboutPage() {
    String body;
    body.reserve(1024);
    appendTopBar(body, "关于", WebNavTab::About);
    body += F("<h2>Sparks</h2>"
              "<p class='hint'>M5Stack Cardputer 多应用固件：字母键启动器，覆盖米家控制、"
              "红外遥控、配网、时间工具、Cursor 用量与硬件调试等。</p>"
              "<dl class='about-dl'>"
              "<dt>版本</dt><dd>");
    body += APP_VERSION;
    body += F("</dd><dt>更新日期</dt><dd>");
    body += APP_UPDATE_TIME;
    body += F("</dd><dt>作者</dt><dd>");
    body += APP_AUTHOR;
    body += F("</dd><dt>邮箱</dt><dd><a href='mailto:");
    body += APP_EMAIL;
    body += F("'>");
    body += APP_EMAIL;
    body += F("</a></dd><dt>GitHub</dt><dd>"
              "<a href='https://github.com/KyleBing/m5stack-cardputer-sparks' "
              "target='_blank' rel='noopener'>"
              "github.com/KyleBing/m5stack-cardputer-sparks</a></dd>"
              "</dl>");
    appendCardEnd(body);
    sendHtmlPage(body);
}

// 人类可读字节数（如 1.2 MB）
static String formatBytesHuman(const size_t n) {
    char buf[32];
    if (n >= 1024u * 1024u) {
        const float mb = static_cast<float>(n) / (1024.0f * 1024.0f);
        snprintf(buf, sizeof(buf), "%.2f MB", static_cast<double>(mb));
    } else if (n >= 1024u) {
        const float kb = static_cast<float>(n) / 1024.0f;
        snprintf(buf, sizeof(buf), "%.1f KB", static_cast<double>(kb));
    } else {
        snprintf(buf, sizeof(buf), "%u B", static_cast<unsigned>(n));
    }
    return String(buf);
}

// 文件时间格式化（调用方需已设置 TZ）；无效则 —
static String formatFileTime(const time_t t) {
    if (t <= 0) {
        return String("—");
    }
    struct tm local_tm;
    if (localtime_r(&t, &local_tm) == nullptr) {
        return String("—");
    }
    char buf[24];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &local_tm);
    return String(buf);
}

// 通过 VFS stat 取修改/创建时间（挂载点 /sd）
static void fmReadTimes(const String& sd_path, time_t& mtime, time_t& ctime_out) {
    mtime = 0;
    ctime_out = 0;
    String vfs = "/sd";
    if (sd_path == "/") {
        vfs += "/";
    } else {
        vfs += sd_path;
    }
    struct stat st;
    if (stat(vfs.c_str(), &st) != 0) {
        return;
    }
    mtime = st.st_mtime;
    ctime_out = st.st_ctime;
}

// enumScreenshots 回调：往 HTML 网格追加一张
static void appendShotCard(const char* storage, const char* basename, const size_t size, void* user) {
    String* body = static_cast<String*>(user);
    *body += F("<div class='shot-card'>"
               "<a class='thumb' href='/shot/");
    *body += basename;
    *body += F("' target='_blank' rel='noopener'>"
               "<img src='/shot/");
    *body += basename;
    *body += F("' alt='");
    *body += basename;
    *body += F("' loading='lazy'></a>"
               "<div class='meta'><a href='/shot/");
    *body += basename;
    *body += F("?dl=1' download='");
    *body += basename;
    *body += F("'>");
    *body += basename;
    *body += F("</a><div class='size'>");
    *body += storage;
    *body += F(" · ");
    *body += formatBytesHuman(size);
    *body += F("</div></div></div>");
}

// 截图列表页：TF / Flash 分区；Fn+s 优先 TF，否则 Flash
static void handleShotsList() {
    size_t fs_total = 0;
    size_t fs_used = 0;
    size_t fs_free = 0;
    getFlashDataSpace(&fs_total, &fs_used, &fs_free);
    size_t sd_total = 0;
    size_t sd_used = 0;
    size_t sd_free = 0;
    getSdDataSpace(&sd_total, &sd_used, &sd_free);
    const bool sd_ok = isScreenshotSdReady();
    const int tf_count = countTfScreenshots();
    const int flash_count = countFlashScreenshots();
    const size_t tf_bytes = screenshotsUsedBytesTf();
    const size_t flash_bytes = screenshotsUsedBytesFlash();
    const int used_pct =
        (fs_total > 0) ? static_cast<int>((fs_used * 100u) / fs_total) : 0;
    const int sd_pct =
        (sd_total > 0) ? static_cast<int>((sd_used * 100u) / sd_total) : 0;

    String body;
    body.reserve(4096);
    appendTopBar(body, "截图", WebNavTab::Shots);
    body += F("<p class='hint'>任意界面按 <code>Fn+s</code> 截图："
              "有 TF 卡优先存卡，否则存 Flash；"
              "文件名 <code>app_&lt;界面&gt;_001.bmp</code> 序号递增。"
              "本页仅在 Config 联网时可预览/下载。</p>"
              "<p class='hint'>新截图将存到：<strong>");
    body += sd_ok ? F("TF 卡") : F("Flash（LittleFS）");
    body += F("</strong></p>");

    // —— TF 卡截图 ——
    if (sd_ok) {
        body += F("<div class='shot-space'>"
                  "<div><strong>TF 卡（SD）截图</strong></div>"
                  "<div>总容量：");
        body += formatBytesHuman(sd_total);
        body += F(" · 已占用：");
        body += formatBytesHuman(sd_used);
        body += F(" · 剩余：");
        body += formatBytesHuman(sd_free);
        body += F("</div><div>截图：");
        body += String(tf_count);
        body += F(" 张 · 占用 ");
        body += formatBytesHuman(tf_bytes);
        body += F("</div><div class='bar'><i style='width:");
        body += String(sd_pct);
        body += F("%'></i></div></div>");

        if (tf_count > 0) {
            body += F("<div class='toolbar'>"
                      "<form method='POST' action='/shots/clear-tf' style='margin:0'>"
                      "<button type='submit' class='danger' "
                      "onclick=\"return confirm('删除 TF 卡上的全部截图？')\">"
                      "清空 TF 截图</button></form>"
                      "<span class='count'>TF ");
            body += String(tf_count);
            body += F(" 张</span></div><div class='shot-grid'>");
            enumTfScreenshots(appendShotCard, &body);
            body += F("</div>");
        } else {
            body += F("<p class='hint'>TF 卡上暂无截图。</p>");
        }
    } else {
        body += F("<p class='hint'>未检测到 TF 卡；插入后新截图会优先存卡。</p>");
    }

    // —— Flash 截图 ——
    body += F("<div class='shot-space' style='margin-top:16px'>"
              "<div><strong>Flash（LittleFS）截图</strong></div>"
              "<div>总容量：");
    body += formatBytesHuman(fs_total);
    body += F(" · 已占用：");
    body += formatBytesHuman(fs_used);
    body += F(" · 剩余：");
    body += formatBytesHuman(fs_free);
    body += F("</div><div>截图：");
    body += String(flash_count);
    body += F(" 张 · 占用 ");
    body += formatBytesHuman(flash_bytes);
    body += F("</div><div class='bar'><i style='width:");
    body += String(used_pct);
    body += F("%'></i></div></div>");

    if (flash_count > 0) {
        body += F("<div class='toolbar'>"
                  "<form method='POST' action='/shots/clear-flash' style='margin:0'>"
                  "<button type='submit' class='danger' "
                  "onclick=\"return confirm('删除 Flash 上的全部截图？')\">"
                  "清空 Flash 截图</button></form>"
                  "<span class='count'>Flash ");
        body += String(flash_count);
        body += F(" 张</span></div><div class='shot-grid'>");
        enumFlashScreenshots(appendShotCard, &body);
        body += F("</div>");
    } else {
        body += F("<p class='hint'>Flash 上暂无截图。</p>");
    }

    if (tf_count == 0 && flash_count == 0) {
        body += F("<p class='hint'>到任意界面按 Fn+s 后再刷新本页。</p>");
    }

    appendCardEnd(body);
    sendHtmlPage(body, HTML_CSS_SHOTS);
}

// 清空结果页
static void sendShotsClearResult(const char* title, const int n, const char* where) {
    String body;
    appendTopBar(body, title);
    body += F("<p>已从 ");
    body += where;
    body += F(" 删除 ");
    body += String(n);
    body += F(" 张截图。</p>"
              "<p><a href='/shots'>返回截图</a></p>");
    appendCardEnd(body);
    sendHtmlPage(body);
}

// 清空 TF 卡截图
static void handleShotsClearTf() {
    sendShotsClearResult("已清空 TF", clearTfScreenshots(), "TF 卡");
}

// 清空 Flash 截图
static void handleShotsClearFlash() {
    sendShotsClearResult("已清空 Flash", clearFlashScreenshots(), "Flash");
}

// ===== TF 卡文件管理（仅 Config Web）=====
static constexpr int FM_MAX_ENTRIES = 150;
static constexpr size_t FM_PATH_MAX = 180;

struct FmEntry {
    char name[64];
    bool is_dir;
    size_t size;
    time_t mtime; // 修改时间
    time_t ctime; // 创建/状态变更时间（FAT 上常与 mtime 相同）
};

// HTML 文本转义（属性/正文）
static String escapeHtmlText(const String& text) {
    String out;
    out.reserve(text.length() + 8);
    for (size_t i = 0; i < text.length(); i++) {
        const char c = text[i];
        if (c == '&') {
            out += F("&amp;");
        } else if (c == '<') {
            out += F("&lt;");
        } else if (c == '>') {
            out += F("&gt;");
        } else if (c == '"') {
            out += F("&quot;");
        } else {
            out += c;
        }
    }
    return out;
}

// 路径 query 编码（保留 /）
static String urlEncodePath(const String& path) {
    String out;
    out.reserve(path.length() * 2);
    for (size_t i = 0; i < path.length(); i++) {
        const unsigned char c = static_cast<unsigned char>(path[i]);
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '/' || c == '.' || c == '_' || c == '-' || c == '~') {
            out += static_cast<char>(c);
        } else {
            char hex[8];
            snprintf(hex, sizeof(hex), "%%%02X", c);
            out += hex;
        }
    }
    return out;
}

// 规范化 SD 绝对路径：禁止 ..、空段过多、超长
static bool sanitizeSdPath(const String& in, String& out) {
    String raw = in;
    raw.trim();
    if (raw.isEmpty()) {
        raw = "/";
    }
    if (raw[0] != '/') {
        raw = String("/") + raw;
    }
    // 注意：String::indexOf('\0') 会命中结尾 NUL，不能用来检测内嵌空字符
    if (raw.indexOf('\\') >= 0) {
        return false;
    }
    // 折叠 //，拒绝 .
    String norm = "/";
    int start = 1;
    while (start <= static_cast<int>(raw.length())) {
        int slash = raw.indexOf('/', start);
        if (slash < 0) {
            slash = static_cast<int>(raw.length());
        }
        const String seg = raw.substring(start, slash);
        if (seg.length() > 0) {
            if (seg == ".." || seg == ".") {
                return false;
            }
            if (norm.length() > 1) {
                norm += "/";
            }
            norm += seg;
        }
        start = slash + 1;
        if (slash >= static_cast<int>(raw.length())) {
            break;
        }
    }
    if (norm.length() > FM_PATH_MAX) {
        return false;
    }
    out = norm;
    return true;
}

static const char* fmBaseName(const char* name) {
    const char* slash = strrchr(name, '/');
    return slash != nullptr ? slash + 1 : name;
}

static const char* mimeForFileName(const char* name) {
    if (name == nullptr) {
        return "application/octet-stream";
    }
    const char* dot = strrchr(name, '.');
    if (dot == nullptr) {
        return "application/octet-stream";
    }
    char ext[8];
    size_t n = 0;
    for (const char* p = dot + 1; *p != '\0' && n + 1 < sizeof(ext); ++p) {
        ext[n++] = static_cast<char>(tolower(static_cast<unsigned char>(*p)));
    }
    ext[n] = '\0';
    if (strcmp(ext, "bmp") == 0) {
        return "image/bmp";
    }
    if (strcmp(ext, "png") == 0) {
        return "image/png";
    }
    if (strcmp(ext, "jpg") == 0 || strcmp(ext, "jpeg") == 0) {
        return "image/jpeg";
    }
    if (strcmp(ext, "gif") == 0) {
        return "image/gif";
    }
    if (strcmp(ext, "webp") == 0) {
        return "image/webp";
    }
    if (strcmp(ext, "txt") == 0 || strcmp(ext, "log") == 0 || strcmp(ext, "json") == 0 ||
        strcmp(ext, "csv") == 0 || strcmp(ext, "md") == 0) {
        return "text/plain; charset=utf-8";
    }
    if (strcmp(ext, "wav") == 0) {
        return "audio/wav";
    }
    if (strcmp(ext, "mp3") == 0) {
        return "audio/mpeg";
    }
    return "application/octet-stream";
}

// 拼父目录；根目录仍为 /
static String fmParentPath(const String& path) {
    if (path.length() <= 1) {
        return "/";
    }
    const int slash = path.lastIndexOf('/');
    if (slash <= 0) {
        return "/";
    }
    return path.substring(0, slash);
}

// 拼接子路径
static String fmJoinPath(const String& dir, const char* name) {
    if (dir == "/") {
        return String("/") + name;
    }
    return dir + "/" + name;
}

// 目录项比较：目录优先，再按名
static int fmEntryCmp(const void* a, const void* b) {
    const FmEntry* ea = static_cast<const FmEntry*>(a);
    const FmEntry* eb = static_cast<const FmEntry*>(b);
    if (ea->is_dir != eb->is_dir) {
        return ea->is_dir ? -1 : 1;
    }
    return strcasecmp(ea->name, eb->name);
}

// 合法单层文件/文件夹名（禁止路径分隔与控制字符）
static bool fmValidEntryName(const String& name) {
    if (name.isEmpty() || name.length() > 48) {
        return false;
    }
    if (name == "." || name == "..") {
        return false;
    }
    for (size_t i = 0; i < name.length(); i++) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (c < 32 || c == '/' || c == '\\' || c == '"' || c == '\'' || c == '<' || c == '>') {
            return false;
        }
    }
    return true;
}

// 递归删除目录（先清子项再 rmdir；深度上限防爆栈）
static bool fmRemoveTree(const String& path, const int depth = 0) {
    if (depth > 16 || path == "/") {
        return false;
    }
    File probe = SD.open(path);
    if (!probe) {
        return false;
    }
    const bool is_dir = probe.isDirectory();
    probe.close();
    if (!is_dir) {
        return SD.remove(path);
    }

    // 每次删一个子项后重开目录，避免迭代器失效
    for (;;) {
        File dir = SD.open(path);
        if (!dir || !dir.isDirectory()) {
            if (dir) {
                dir.close();
            }
            return false;
        }
        File f = dir.openNextFile();
        while (f) {
            const char* base = fmBaseName(f.name());
            if (base[0] == '\0' || (base[0] == '.' && (base[1] == '\0' || base[1] == '.'))) {
                f = dir.openNextFile();
                continue;
            }
            break;
        }
        if (!f) {
            dir.close();
            break;
        }
        const char* base = fmBaseName(f.name());
        const String child = fmJoinPath(path, base);
        const bool child_dir = f.isDirectory();
        f.close();
        dir.close();
        if (child_dir) {
            if (!fmRemoveTree(child, depth + 1)) {
                return false;
            }
        } else if (!SD.remove(child)) {
            return false;
        }
    }
    return SD.rmdir(path);
}

// 列表页跳转（带可选提示）
static void fmRedirect(const String& path, const char* msg, const bool ok) {
    String loc = "/files?path=";
    loc += urlEncodePath(path);
    if (msg != nullptr && msg[0] != '\0') {
        loc += ok ? "&ok=" : "&err=";
        loc += urlEncodePath(String(msg));
    }
    g_server.sendHeader("Location", loc);
    g_server.send(303, "text/plain", ok ? "ok" : "fail");
}

// TF 文件列表页：?path=/foo[&ok=|&err=]
static void handleFilesList() {
    if (!isScreenshotSdReady()) {
        String body;
        appendTopBar(body, "TF 文件", WebNavTab::Files);
        body += F("<p class='err'>未检测到 TF 卡，或挂载失败。插入 microSD 后刷新本页。</p>");
        appendCardEnd(body);
        sendHtmlPage(body, HTML_CSS_FILES | HTML_CSS_SHOTS);
        return;
    }

    String path;
    const String raw_path = g_server.hasArg("path") ? g_server.arg("path") : String("/");
    if (!sanitizeSdPath(raw_path, path)) {
        g_server.send(400, "text/plain", "bad path");
        return;
    }

    size_t sd_total = 0;
    size_t sd_used = 0;
    size_t sd_free = 0;
    getSdDataSpace(&sd_total, &sd_used, &sd_free);
    const int sd_pct =
        (sd_total > 0) ? static_cast<int>((sd_used * 100u) / sd_total) : 0;

    File dir = SD.open(path);
    if (!dir || !dir.isDirectory()) {
        if (dir) {
            dir.close();
        }
        String body;
        appendTopBar(body, "TF 文件", WebNavTab::Files);
        body += F("<p class='err'>目录不存在：");
        body += escapeHtmlText(path);
        body += F("</p>");
        appendCardEnd(body);
        sendHtmlPage(body, HTML_CSS_FILES | HTML_CSS_SHOTS);
        return;
    }

    FmEntry* entries = static_cast<FmEntry*>(malloc(sizeof(FmEntry) * FM_MAX_ENTRIES));
    int count = 0;
    bool truncated = false;
    if (entries != nullptr) {
        File f = dir.openNextFile();
        while (f) {
            const char* base = fmBaseName(f.name());
            // 跳过隐藏元数据
            if (base[0] != '\0' && !(base[0] == '.' && (base[1] == '\0' || base[1] == '.'))) {
                if (count >= FM_MAX_ENTRIES) {
                    truncated = true;
                    f.close();
                    break;
                }
                FmEntry& e = entries[count];
                strncpy(e.name, base, sizeof(e.name) - 1);
                e.name[sizeof(e.name) - 1] = '\0';
                e.is_dir = f.isDirectory();
                e.size = e.is_dir ? 0 : static_cast<size_t>(f.size());
                // 优先用已打开文件的 mtime；ctime 走 VFS stat
                e.mtime = f.getLastWrite();
                e.ctime = 0;
                const String full = fmJoinPath(path, e.name);
                time_t st_m = 0;
                time_t st_c = 0;
                fmReadTimes(full, st_m, st_c);
                if (e.mtime <= 0 && st_m > 0) {
                    e.mtime = st_m;
                }
                e.ctime = st_c > 0 ? st_c : e.mtime;
                count++;
            }
            f = dir.openNextFile();
        }
        qsort(entries, static_cast<size_t>(count), sizeof(FmEntry), fmEntryCmp);
    }
    dir.close();

    String body;
    body.reserve(5120 + static_cast<size_t>(count) * 320);
    // 列表时间按配置时区显示
    setenv("TZ", getAppTimezone(), 1);
    tzset();
    appendTopBar(body, "TF 文件", WebNavTab::Files);
    body += F("<div class='shot-space'>"
              "<div><strong>TF 卡（SD）</strong></div>"
              "<div>总容量：");
    body += formatBytesHuman(sd_total);
    body += F(" · 已占用：");
    body += formatBytesHuman(sd_used);
    body += F(" · 剩余：");
    body += formatBytesHuman(sd_free);
    body += F("</div><div class='bar'><i style='width:");
    body += String(sd_pct);
    body += F("%'></i></div></div>");

    // 操作结果提示
    if (g_server.hasArg("ok")) {
        body += F("<div class='fm-flash ok'>");
        body += escapeHtmlText(g_server.arg("ok"));
        body += F("</div>");
    } else if (g_server.hasArg("err")) {
        body += F("<div class='fm-flash err'>");
        body += escapeHtmlText(g_server.arg("err"));
        body += F("</div>");
    }

    // 面包屑
    body += F("<nav class='fm-crumb'><a href='/files?path=%2F'>根目录</a>");
    if (path != "/") {
        String acc = "";
        int start = 1;
        while (start <= static_cast<int>(path.length())) {
            int slash = path.indexOf('/', start);
            if (slash < 0) {
                slash = static_cast<int>(path.length());
            }
            const String seg = path.substring(start, slash);
            if (seg.length() > 0) {
                acc += "/";
                acc += seg;
                body += F("<span class='sep'>/</span>");
                if (slash >= static_cast<int>(path.length())) {
                    body += F("<span class='cur'>");
                    body += escapeHtmlText(seg);
                    body += F("</span>");
                } else {
                    body += F("<a href='/files?path=");
                    body += urlEncodePath(acc);
                    body += F("'>");
                    body += escapeHtmlText(seg);
                    body += F("</a>");
                }
            }
            start = slash + 1;
            if (slash >= static_cast<int>(path.length())) {
                break;
            }
        }
    }
    body += F("</nav>");

    // 新建文件夹 + 计数
    body += F("<div class='fm-toolbar'>"
              "<form class='fm-mkdir' method='POST' action='/files/mkdir'>"
              "<input type='hidden' name='path' value='");
    body += escapeHtmlText(path);
    body += F("'>"
              "<label for='fm-new-dir'>新建文件夹</label>"
              "<input id='fm-new-dir' type='text' name='name' maxlength='48' "
              "placeholder='文件夹名' required autocomplete='off'>"
              "<button type='submit' class='primary'>创建</button>"
              "</form><span class='fm-count'>");
    body += String(count);
    body += F(" 项");
    if (truncated) {
        body += F("（已截断，仅显示前 ");
        body += String(FM_MAX_ENTRIES);
        body += F(" 项）");
    }
    body += F("</span></div>");

    if (entries == nullptr) {
        body += F("<p class='err'>内存不足，无法列出目录。</p>");
        appendCardEnd(body);
        sendHtmlPage(body, HTML_CSS_FILES | HTML_CSS_SHOTS);
        return;
    }

    if (count == 0) {
        body += F("<div class='fm-empty'><strong>此目录为空</strong>"
                  "可在上方创建文件夹，或用电脑往 TF 卡拷入文件后刷新。</div>");
        free(entries);
        appendCardEnd(body);
        sendHtmlPage(body, HTML_CSS_FILES | HTML_CSS_SHOTS);
        return;
    }

    body += F("<div class='table-wrap'><table class='fm-table'>"
              "<thead><tr>"
              "<th class='col-name'>名称</th>"
              "<th class='col-size'>大小</th>"
              "<th class='col-time'>修改时间</th>"
              "<th class='col-time'>创建时间</th>"
              "<th class='col-act'>操作</th>"
              "</tr></thead><tbody>");
    for (int i = 0; i < count; i++) {
        const FmEntry& e = entries[i];
        const String full = fmJoinPath(path, e.name);
        const String enc = urlEncodePath(full);
        const String safe_name = escapeHtmlText(String(e.name));

        body += F("<tr><td class='col-name'>");
        if (e.is_dir) {
            body += F("<span class='tag dir'>DIR</span>"
                      "<a class='name' href='/files?path=");
            body += enc;
            body += F("'>");
            body += safe_name;
            body += F("/</a>");
        } else {
            body += F("<a class='name' href='/sd?path=");
            body += enc;
            body += F("' target='_blank' rel='noopener'>");
            body += safe_name;
            body += F("</a>");
        }
        body += F("</td><td class='col-size'>");
        if (e.is_dir) {
            body += F("—");
        } else {
            body += formatBytesHuman(e.size);
        }
        body += F("</td><td class='col-time'>");
        body += formatFileTime(e.mtime);
        body += F("</td><td class='col-time'>");
        body += formatFileTime(e.ctime);
        body += F("</td><td class='col-act'><div class='acts'>");
        if (e.is_dir) {
            body += F("<a class='btn' href='/files?path=");
            body += enc;
            body += F("'>打开</a>");
        } else {
            body += F("<a class='btn' href='/sd?path=");
            body += enc;
            body += F("&dl=1'>下载</a>");
        }
        body += F("<form method='POST' action='/files/delete'>"
                  "<input type='hidden' name='path' value='");
        body += escapeHtmlText(full);
        body += F("'>"
                  "<button type='submit' class='danger' data-msg=\"");
        if (e.is_dir) {
            body += F("删除文件夹 ");
            body += safe_name;
            body += F(" 及其全部内容？此操作不可恢复。");
        } else {
            body += F("删除文件 ");
            body += safe_name;
            body += F("？");
        }
        body += F("\" onclick=\"return confirm(this.getAttribute('data-msg'))\">"
                  "删除</button></form></div></td></tr>");
    }
    body += F("</tbody></table></div>");
    free(entries);
    appendCardEnd(body);
    sendHtmlPage(body, HTML_CSS_FILES | HTML_CSS_SHOTS);
}

// 新建文件夹 POST path=父目录&name=名称
static void handleFilesMkdir() {
    if (!isScreenshotSdReady()) {
        g_server.send(503, "text/plain", "no sd");
        return;
    }
    if (!g_server.hasArg("path") || !g_server.hasArg("name")) {
        g_server.send(400, "text/plain", "missing args");
        return;
    }
    String parent;
    if (!sanitizeSdPath(g_server.arg("path"), parent)) {
        g_server.send(400, "text/plain", "bad path");
        return;
    }
    String name = g_server.arg("name");
    name.trim();
    if (!fmValidEntryName(name)) {
        fmRedirect(parent, "文件夹名无效", false);
        return;
    }
    const String full = fmJoinPath(parent, name.c_str());
    if (full.length() > FM_PATH_MAX) {
        fmRedirect(parent, "路径过长", false);
        return;
    }
    if (SD.exists(full)) {
        fmRedirect(parent, "已存在同名项", false);
        return;
    }
    if (!SD.mkdir(full)) {
        fmRedirect(parent, "创建失败", false);
        return;
    }
    fmRedirect(parent, "已创建文件夹", true);
}

// 删除 TF 上文件或目录（非空目录递归删）
static void handleFilesDelete() {
    if (!isScreenshotSdReady()) {
        g_server.send(503, "text/plain", "no sd");
        return;
    }
    if (!g_server.hasArg("path")) {
        g_server.send(400, "text/plain", "missing path");
        return;
    }
    String path;
    if (!sanitizeSdPath(g_server.arg("path"), path) || path == "/") {
        g_server.send(400, "text/plain", "bad path");
        return;
    }

    const bool ok = fmRemoveTree(path);
    const String parent = fmParentPath(path);
    fmRedirect(parent, ok ? "已删除" : "删除失败", ok);
}

// 提供 /sd?path=... ：图片默认内联；?dl=1 强制下载
static bool tryServeSdFile() {
    if (g_server.uri() != "/sd") {
        return false;
    }
    if (!isScreenshotSdReady()) {
        g_server.send(503, "text/plain", "no sd");
        return true;
    }
    if (!g_server.hasArg("path")) {
        g_server.send(400, "text/plain", "missing path");
        return true;
    }
    String path;
    if (!sanitizeSdPath(g_server.arg("path"), path) || path == "/") {
        g_server.send(400, "text/plain", "bad path");
        return true;
    }

    File file = SD.open(path, "r");
    if (!file || file.isDirectory()) {
        if (file) {
            file.close();
        }
        g_server.send(404, "text/plain", "not found");
        return true;
    }

    const char* base = fmBaseName(path.c_str());
    const char* mime = mimeForFileName(base);
    char disposition[128];
    if (g_server.hasArg("dl")) {
        snprintf(disposition, sizeof(disposition), "attachment; filename=\"%s\"", base);
    } else {
        snprintf(disposition, sizeof(disposition), "inline; filename=\"%s\"", base);
    }
    g_server.sendHeader("Content-Disposition", disposition);
    g_server.streamFile(file, mime);
    file.close();
    return true;
}

// 提供 /shot/app_*.bmp：默认内联预览；?dl=1 强制下载（SD 优先）
static bool tryServeShotFile() {
    const String uri = g_server.uri();
    File file;
    if (!openScreenshotFile(uri, file)) {
        return false;
    }
    const char* base = uri.c_str() + strlen("/shot/");
    char disposition[96];
    // 预览用 inline；带 ?dl=1 时 attachment 下载
    if (g_server.hasArg("dl")) {
        snprintf(disposition, sizeof(disposition), "attachment; filename=\"%s\"", base);
    } else {
        snprintf(disposition, sizeof(disposition), "inline; filename=\"%s\"", base);
    }
    g_server.sendHeader("Content-Disposition", disposition);
    g_server.streamFile(file, "image/bmp");
    file.close();
    return true;
}

// 重置并进入连接流程
static void beginWebStartup(const bool force_ap) {
    if (g_running) {
        g_server.stop();
        if (g_mode == ConfigWebMode::AP) {
            WiFi.softAPdisconnect(true);
        }
        g_running = false;
        g_mode = ConfigWebMode::NONE;
        g_web_url[0] = '\0';
    }
    // AP/STA 切换需立刻关射频
    forceShutdownStaWifi();

    g_force_ap_mode = force_ap;
    g_startup_phase = WebStartupPhase::CONNECTING;
    g_wifi_begin_sent = false;
    g_connect_deadline_ms = 0;
    strncpy(g_web_status, "starting", sizeof(g_web_status));
}

// 开始尝试连接路由器（非阻塞）
static void startWifiConnectAttempt() {
    const AppConfig& cfg = getAppConfig();
    if (!g_force_ap_mode && cfg.loaded && cfg.wifi_ssid[0] != '\0') {
        claimStaWifi();
        WiFi.mode(WIFI_STA);
        applyWifiRadioSleepPolicy();
        WiFi.begin(cfg.wifi_ssid, cfg.wifi_password);
        g_connect_deadline_ms = millis() + 12000;
        strncpy(g_web_status, "wifi...", sizeof(g_web_status));
    } else {
        // 无 WiFi 配置或强制 AP：立即走热点
        g_connect_deadline_ms = millis();
        strncpy(g_web_status, "ap...", sizeof(g_web_status));
    }
    g_wifi_begin_sent = true;
}

// 连接中动画省略号
static void loadingDots(char* buf, const size_t buf_size) {
    const int n = static_cast<int>((millis() / 400) % 4);
    size_t i = 0;
    for (; i < static_cast<size_t>(n) && i + 1 < buf_size; i++) {
        buf[i] = '.';
    }
    buf[i] = '\0';
}

// 从 LittleFS 提供 /favicon.svg
static bool tryServeFavicon() {
    if (g_server.uri() != "/favicon.svg") {
        return false;
    }

    File file = LittleFS.open("/favicon.svg", "r");
    if (!file) {
        return false;
    }
    g_server.streamFile(file, "image/svg+xml");
    file.close();
    return true;
}

// 从 LittleFS 提供图标资源（png / rgb565；配置页预览仍用 png）
static bool tryServeDeviceIcon() {
    const String uri = g_server.uri();
    const bool is_device = uri.startsWith("/icon/device/");
    const bool is_ir = uri.startsWith("/icon/ir/");
    const bool is_logo = uri.startsWith("/logo_") && (uri.endsWith(".png") || uri.endsWith(".rgb565"));
    if (!is_device && !is_ir && !is_logo) {
        return false;
    }
    if (uri.indexOf("..") >= 0 || uri.length() > 80) {
        return false;
    }
    if (!uri.endsWith(".png") && !uri.endsWith(".rgb565")) {
        return false;
    }
    const char* p = uri.c_str();
    for (; *p != '\0'; ++p) {
        const bool ok = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                        (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' || *p == '.' || *p == '/';
        if (!ok) {
            return false;
        }
    }

    File file = LittleFS.open(uri, "r");
    if (!file) {
        return false;
    }
    const char* mime = "application/octet-stream";
    if (uri.endsWith(".png")) {
        mime = "image/png";
    }
    g_server.streamFile(file, mime);
    file.close();
    return true;
}

// M5GFX 烘焙 PNG → RGB565（写入 LittleFS，供固件与拉取脚本使用）
static void handleBakeRgb565() {
    const int n = bakeAllPngIconsToRgb565();
    char buf[48];
    snprintf(buf, sizeof(buf), "{\"ok\":true,\"baked\":%d}", n);
    g_server.send(200, "application/json", buf);
}

// 注册 HTTP 路由（仅一次，避免重复 on()）
static bool g_routes_registered = false;
static void registerWebRoutes() {
    if (g_routes_registered) {
        return;
    }
    g_routes_registered = true;
    g_server.on("/", HTTP_GET, handleFormRoot);
    g_server.on("/wifi", HTTP_GET, handleWifiPage);
    g_server.on("/groups", HTTP_GET, handleGroupsPage);
    g_server.on("/cursor", HTTP_GET, handleCursorPage);
    g_server.on("/system", HTTP_GET, handleSystemPage);
    g_server.on("/advanced", HTTP_GET, handleAdvancedRoot);
    g_server.on("/save", HTTP_POST, handleSave);
    g_server.on("/example", HTTP_GET, handleExample);
    g_server.on("/about", HTTP_GET, handleAboutPage);
    g_server.on("/shots", HTTP_GET, handleShotsList);
    g_server.on("/shots/clear-tf", HTTP_POST, handleShotsClearTf);
    g_server.on("/shots/clear-flash", HTTP_POST, handleShotsClearFlash);
    g_server.on("/files", HTTP_GET, handleFilesList);
    g_server.on("/files/delete", HTTP_POST, handleFilesDelete);
    g_server.on("/files/mkdir", HTTP_POST, handleFilesMkdir);
    g_server.on("/bake-rgb565", HTTP_POST, handleBakeRgb565);
    g_server.on("/sd", HTTP_GET, []() { tryServeSdFile(); });
    g_server.onNotFound([]() {
        if (tryServeFavicon() || tryServeDeviceIcon() || tryServeShotFile() || tryServeSdFile()) {
            return;
        }
        g_server.send(404, "text/plain", "not found");
    });
}

// 已连路由器时直接在局域网 IP 上提供配置页
static bool startStaConfigWebServer() {
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    registerWebRoutes();
    g_server.begin();

    snprintf(g_web_url, sizeof(g_web_url), "http://%s", WiFi.localIP().toString().c_str());
    g_mode = ConfigWebMode::STA;
    g_running = true;
    strncpy(g_web_status, "sta ok", sizeof(g_web_status));
    return true;
}

// 未联网时开 AP 热点配网
static bool startApConfigWebServer() {
    // softAP + WebServer 在低堆/碎片时易 panic，宁可不启也不重启
    if (ESP.getFreeHeap() < 40000 || ESP.getMaxAllocHeap() < 20000) {
        Serial.printf("[web] ap skip lowmem heap=%u max=%u\n", ESP.getFreeHeap(),
                      ESP.getMaxAllocHeap());
        strncpy(g_web_status, "low mem", sizeof(g_web_status));
        return false;
    }
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                      IPAddress(255, 255, 255, 0));
    if (!WiFi.softAP(AP_SSID, AP_PASS)) {
        strncpy(g_web_status, "ap fail", sizeof(g_web_status));
        return false;
    }

    registerWebRoutes();
    g_server.begin();

    strncpy(g_web_url, AP_WEB_URL, sizeof(g_web_url));
    g_mode = ConfigWebMode::AP;
    g_running = true;
    strncpy(g_web_status, "ap ok", sizeof(g_web_status));
    return true;
}

bool startConfigWebServer() {
    beginWebStartup(false);
    return true;
}

void stopConfigWebServer() {
    if (g_running) {
        g_server.stop();
        if (g_mode == ConfigWebMode::AP) {
            WiFi.softAPdisconnect(true);
        }
    }
    releaseConfigWifi();

    g_running = false;
    g_mode = ConfigWebMode::NONE;
    g_web_url[0] = '\0';
    g_startup_phase = WebStartupPhase::IDLE;
    g_wifi_begin_sent = false;
    strncpy(g_web_status, "stopped", sizeof(g_web_status));
}

// 推进连接并在就绪后处理 HTTP
void updateWebApp() {
    if (g_startup_phase == WebStartupPhase::CONNECTING) {
        if (!g_wifi_begin_sent) {
            startWifiConnectAttempt();
        }

        if (!g_force_ap_mode && WiFi.status() == WL_CONNECTED) {
            if (startStaConfigWebServer()) {
                g_startup_phase = WebStartupPhase::READY;
                drawWebApp();
            } else {
                g_startup_phase = WebStartupPhase::FAILED;
                strncpy(g_web_status, "sta fail", sizeof(g_web_status));
                drawWebApp();
            }
        } else if (g_force_ap_mode || static_cast<int32_t>(millis() - g_connect_deadline_ms) >= 0) {
            if (startApConfigWebServer()) {
                g_startup_phase = WebStartupPhase::READY;
                drawWebApp();
            } else {
                g_startup_phase = WebStartupPhase::FAILED;
                drawWebApp();
            }
        } else {
            static uint32_t last_draw_ms = 0;
            if (millis() - last_draw_ms >= 400) {
                last_draw_ms = millis();
                drawWebApp();
            }
        }
        return;
    }

    if (g_running) {
        g_server.handleClient();
    }
}

bool isConfigWebServerRunning() {
    return g_running;
}

const char* getConfigWebApSsid() {
    return AP_SSID;
}

const char* getConfigWebApPass() {
    return AP_PASS;
}

const char* getConfigWebUrl() {
    return g_web_url[0] != '\0' ? g_web_url : AP_WEB_URL;
}

bool isConfigWebStaMode() {
    return g_running && g_mode == ConfigWebMode::STA;
}

const char* getConfigWebStatus() {
    return g_web_status;
}

static const char* stripHttpPrefix(const char* url) {
    if (url == nullptr) {
        return "";
    }
    if (strncmp(url, "http://", 7) == 0) {
        return url + 7;
    }
    return url;
}

// Help 分栏标题
static int drawWebHelpColHeader(const int x, const int y, const int w, const char* title) {
    M5Cardputer.Display.fillRect(x, y, w, 11, APP_COLOR_LABEL);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(BLACK, APP_COLOR_LABEL);
    M5Cardputer.Display.setCursor(x + 2, y + 1);
    M5Cardputer.Display.print(title);
    return y + 13;
}

// Help 按键说明；徽章后恢复说明文字颜色
static int drawWebHelpKey(const int x, const int y, const char key, const char* text) {
    const int cx = x + drawKeyBadge(x, y, key, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print(text);
    return y + 11;
}

// Help 功能说明
static int drawWebHelpText(const int x, const int y, const char* text) {
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(x, y);
    M5Cardputer.Display.print(text);
    return y + 11;
}

static void drawWebHelpPage() {
    beginAppScreen("Help");
    constexpr int col_gap = 4;
    const int screen_w = M5Cardputer.Display.width();
    const int col_w = (screen_w - col_gap) / 2;
    const int manual_x = col_w + col_gap;
    const int col_y = APP_CONTENT_Y_NO_TAP_TO_HEADER;
    M5Cardputer.Display.drawFastVLine(col_w + col_gap / 2, col_y,
                                     M5Cardputer.Display.height() - col_y, DARKGREY);

    int y = drawWebHelpColHeader(0, col_y, col_w, "keymap");
    y = drawWebHelpKey(2, y, 'a', "switch to AP");
    y = drawWebHelpKey(2, y, 'l', "retry LAN");

    y = drawWebHelpColHeader(manual_x, col_y, screen_w - manual_x, "manual");
    y = drawWebHelpText(manual_x + 2, y, "browser config");
    y = drawWebHelpText(manual_x + 2, y, "LAN uses saved WiFi");
    y = drawWebHelpText(manual_x + 2, y, "AP on LAN timeout");
    y = drawWebHelpText(manual_x + 2, y, "edit WiFi/devices");
    y = drawWebHelpText(manual_x + 2, y, "set Cursor/system");
    y = drawWebHelpText(manual_x + 2, y, "save to config.json");
    y = drawWebHelpText(manual_x + 2, y, "Fn+s any screen");
    y = drawWebHelpText(manual_x + 2, y, "then /shots DL");

    drawHelpHintRight("close");
    updateAppHeaderStatus();
}

void drawWebApp() {
    if (g_web_help_visible) {
        drawWebHelpPage();
        return;
    }

    // Ready 时 header 显示当前 Mode（AP / LAN）
    const char* mode_accent = nullptr;
    if (g_startup_phase == WebStartupPhase::READY && isConfigWebServerRunning()) {
        mode_accent = isConfigWebStaMode() ? "LAN" : "AP";
    }
    if (!g_web_screen_ready) {
        if (mode_accent != nullptr) {
            beginAppScreenAccent("Config ", mode_accent, APP_COLOR_LABEL);
        } else {
            beginAppScreen("Config");
        }
        g_web_screen_ready = true;
    } else {
        clearAppContentArea();
        if (mode_accent != nullptr) {
            drawAppScreenHeaderAccent("Config ", mode_accent, APP_COLOR_LABEL);
        } else {
            drawAppScreenHeader("Config");
        }
    }

    int y = APP_CONTENT_INSET_Y;

    const auto drawLine1x = [&](const char* label, const char* value) {
        drawInfoLineAt(APP_CONTENT_X, y, label, value, 1);
        y += INFO_LINE_H;
    };

    // label / value 均为 2x
    const auto drawLine2x = [&](const char* label, const char* value) {
        drawInfoLineAt(APP_CONTENT_X, y, label, value, 2);
        y += INFO_LINE_H_2X;
    };

    const auto drawLineValue2x = [&](const char* label, const char* value) {
        M5Cardputer.Display.setTextSize(2);
        M5Cardputer.Display.setTextColor(INFO_LABEL_COLOR, BLACK);
        M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
        M5Cardputer.Display.print(label);
        M5Cardputer.Display.print(": ");
        const int value_x = M5Cardputer.Display.getCursorX();
        M5Cardputer.Display.setTextColor(INFO_VALUE_COLOR, BLACK);
        M5Cardputer.Display.setCursor(value_x, y);
        M5Cardputer.Display.println(value);
        y += infoLineHeight(2);
    };

    const int hint_y = M5Cardputer.Display.height() - 12;

    const auto drawKeyHintAt = [&](const int hy, const char key, const char* text,
                                   const int text_size = 1) {
        int cx = APP_CONTENT_X + drawKeyBadge(APP_CONTENT_X, hy, key, text_size);
        M5Cardputer.Display.setTextSize(text_size);
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.setCursor(cx, hy);
        M5Cardputer.Display.print(text);
    };

    const auto drawTextHintAt = [&](const int hy, const char* text) {
        drawInfoLineAt(APP_CONTENT_X, hy, "hint", text, 1);
    };

    // 连接中：状态信息 2x，底栏 tip 1x
    if (g_startup_phase == WebStartupPhase::CONNECTING) {
        char dots[5];
        loadingDots(dots, sizeof(dots));
        char status[20];
        snprintf(status, sizeof(status), "%s%s", g_web_status, dots);
        drawLine2x("status", status);

        const AppConfig& cfg = getAppConfig();
        if (!g_force_ap_mode && cfg.loaded && cfg.wifi_ssid[0] != '\0') {
            drawLine2x("wifi", cfg.wifi_ssid);
            drawLine2x("plan", "LAN then AP");
        } else {
            drawLine2x("plan", "AP hotspot");
        }

        drawKeyHintAt(hint_y, 'a', "skip to AP mode");
        drawHelpHintRight("help");
        return;
    }

    if (g_startup_phase == WebStartupPhase::FAILED) {
        drawLine1x("status", "failed");
        drawLine1x("state", g_web_status);
        drawKeyHintAt(hint_y, 'a', "for AP");
        drawHelpHintRight("help");
        return;
    }

    if (!isConfigWebServerRunning()) {
        drawLine1x("status", "offline");
        drawTextHintAt(hint_y, "re-enter u");
        drawHelpHintRight("help");
        return;
    }

    if (isConfigWebStaMode()) {
        // Mode 已在 header；内容区只显示 url / state
        drawLineValue2x("url", stripHttpPrefix(getConfigWebUrl()));
        drawLineValue2x("state", getConfigWebStatus());
        drawKeyHintAt(hint_y, 'a', "switch to AP hotspot");
    } else {
        // AP：无底栏 tip；state 用 2x
        drawLineValue2x("ssid", getConfigWebApSsid());
        drawLineValue2x("pass", getConfigWebApPass());
        drawLineValue2x("url", stripHttpPrefix(getConfigWebUrl()));
        drawLineValue2x("state", getConfigWebStatus());
    }
    drawHelpHintRight("help");
}

void handleWebApp(const String& key) {
    if (key == "h") {
        g_web_help_visible = !g_web_help_visible;
        if (!g_web_help_visible) {
            g_web_screen_ready = false;
        }
        drawWebApp();
        return;
    }
    if (g_web_help_visible) {
        return;
    }
    if (key == "a") {
        beginWebStartup(true);
        drawWebApp();
        return;
    }
    if (key == "l" && g_startup_phase == WebStartupPhase::READY && !isConfigWebStaMode()) {
        beginWebStartup(false);
        drawWebApp();
    }
}

void enterWebApp() {
    g_web_screen_ready = false;
    g_web_help_visible = false;
    // 已在跑则只刷新界面，避免打断后台截图服务
    if (g_running && g_startup_phase == WebStartupPhase::READY) {
        drawWebApp();
        return;
    }
    beginWebStartup(false);
    drawWebApp();
}
