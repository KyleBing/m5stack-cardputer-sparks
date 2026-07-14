#include "app_web.h"
#include "app_common.h"
#include "app_config.h"
#include "app_connectivity.h"
#include "app_device_icons.h"
#include "app_header.h"
#include <FS.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <cstring>

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

static const char* DEFAULT_CONFIG = R"({
  "wifi": {
    "ssid": "your-ssid",
    "password": "your-password"
  },
  "devices": [
    {
      "name": "living-room-light",
      "name_zh": "客厅灯",
      "id": "123456789",
      "mac": "AA:BB:CC:DD:EE:FF",
      "ip": "192.168.1.50",
      "token": "0123456789abcdef0123456789abcdef",
      "model": "yeelink.light.lamp2"
    },
    {
      "name": "bedroom-light",
      "name_zh": "卧室灯",
      "id": "987654321",
      "mac": "AA:BB:CC:DD:EE:01",
      "ip": "192.168.1.51",
      "token": "0123456789abcdef0123456789abcdef",
      "model": "yeelink.light.lamp2"
    },
    {
      "name": "Sensor_HT",
      "name_zh": "温湿度计",
      "id": "blt.3.example",
      "mac": "A4:C1:38:00:00:00",
      "model": "miaomiaoce.sensor_ht.t2",
      "ble": { "key": "0123456789abcdef0123456789abcdef" }
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
  "timezone": "CST-8",
  "brightness": 30,
  "sound": {
    "time_key": true,
    "mijia_on_off": true
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

static void sendHtmlPage(const String& body) {
    String html;
    html.reserve(body.length() + 4096);
    html += F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
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
              "body{font-family:system-ui,sans-serif;margin:0;padding:10px 12px;line-height:1.4;"
              "width:100%;max-width:100%;background:var(--bg);color:var(--fg)}"
              ".topbar{margin-bottom:8px}"
              ".brand{display:flex;align-items:center;gap:10px}"
              ".site-logo{width:36px;height:36px;object-fit:contain;flex-shrink:0}"
              ".brand-text{min-width:0}"
              ".brand-text h1{font-size:1.2rem;margin:0 0 2px;color:var(--fg-h)}"
              ".nav{font-size:13px;margin:2px 0 0}"
              ".nav a{color:var(--link)}"
              "h1{font-size:1.2rem;margin:0 0 8px;color:var(--fg-h)}"
              "h2{font-size:1.05rem;margin:0 0 8px;color:var(--fg-h)}"
              "input,textarea{width:100%;padding:6px 8px;font-size:13px;border:1px solid var(--input-bd);"
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
              ".tabs{display:flex;gap:0;border-bottom:2px solid var(--tab-bd);margin-bottom:12px}"
              ".tab{padding:10px 18px;cursor:pointer;border:none;background:none;font-size:14px;"
              "color:var(--tab-fg);border-bottom:2px solid transparent;margin-bottom:-2px}"
              ".tab.active{color:var(--tab-act);border-bottom-color:var(--tab-act);font-weight:600}"
              ".panel{display:none}"
              ".panel.active{display:block}"
              ".hint{font-size:12px;color:var(--hint);margin:0 0 10px}"
              ".toolbar{margin:10px 0;display:flex;flex-wrap:wrap;align-items:center;gap:6px}"
              ".toolbar .count{font-size:13px;color:var(--hint);margin-left:auto}"
              ".table-wrap{width:100%;overflow-x:auto;-webkit-overflow-scrolling:touch}"
              "table.dev-table{width:100%;min-width:948px;border-collapse:collapse;table-layout:fixed}"
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
              ".dev-table .col-namezh{width:10%}"
              ".dev-table .col-ip{width:9%}"
              ".dev-table .col-token{width:14%}"
              ".dev-table .col-ble{width:14%}"
              ".dev-table .col-model{width:16%}"
              ".dev-table .col-id{width:10%}"
              ".dev-table .col-mac{width:10%}"
              ".model-cell{display:flex;gap:6px;align-items:flex-start}"
              ".model-cell textarea{flex:1;min-width:0}"
              ".dev-icon{width:32px;height:32px;object-fit:contain;flex-shrink:0;margin-top:2px;"
              "background:var(--icon-bg);border-radius:4px}"
              ".wifi-grid{max-width:480px}"
              ".save-bar{margin-top:14px;padding-top:12px;border-top:1px solid var(--save-bd)}"
              ".result-actions{margin-top:16px;display:flex;flex-wrap:wrap;gap:6px;align-items:center}"
              ".ok{color:#81c784}.err{color:#ff8a80}"
              "code{background:var(--code-bg);padding:2px 4px;border-radius:3px;color:var(--fg)}"
              "pre{background:var(--pre-bg);color:var(--fg);padding:12px;overflow:auto;font-size:12px;"
              "border:1px solid var(--pre-bd);border-radius:4px}"
              "textarea.json-editor{height:min(70vh,520px);font-family:ui-monospace,monospace}"
              ".token-steps{margin:0 0 12px;padding:0 0 0 18px;font-size:12px;color:var(--hint)}"
              ".token-steps li{margin:0 0 6px}"
              ".token-method-title{font-size:13px;margin:14px 0 6px;color:var(--fg-h)}"
              ".token-paths{margin:0 0 8px;padding:0 0 0 18px;font-size:12px;color:var(--hint)}"
              ".token-paths li{margin:0 0 4px}"
              ".token-cmd{margin:0 0 12px;font-size:11px;line-height:1.4;white-space:pre-wrap;word-break:break-all}"
              ".token-formats{font-size:12px;color:var(--hint);margin:0 0 10px}"
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
              /* 覆盖全局 input{width:100%}，避免 checkbox 撑满把文字挤成竖排 */
              ".group-members label.member input[type=checkbox]{width:auto;min-width:16px;"
              "margin:2px 0 0;padding:0;flex:0 0 auto;accent-color:#1a73e8}"
              ".group-members .member-meta{flex:1 1 auto;min-width:0;line-height:1.35;"
              "overflow-wrap:anywhere}"
              ".group-members .member-id{font-family:ui-monospace,monospace;font-size:11px;"
              "color:var(--hint);word-break:break-all}"
              ".sys-grid{max-width:480px}"
              ".sys-grid .check-row{display:flex;align-items:center;gap:8px;margin:0 0 10px;"
              "font-size:13px;color:var(--fg);cursor:pointer}"
              ".sys-grid .check-row input{width:auto;margin:0;accent-color:#1a73e8}"
              ".sys-grid .bright-row{display:flex;align-items:center;gap:10px}"
              ".sys-grid .bright-row input[type=range]{flex:1;min-width:0;padding:0}"
              ".sys-grid .bright-val{min-width:2.5em;font-variant-numeric:tabular-nums;"
              "color:var(--fg)}"
              "</style></head><body>");
    html += body;
    html += F("</body></html>");
    g_server.send(200, "text/html", html);
}

// 表单编辑页（Tab：WiFi / 米家设备）
static void handleFormRoot() {
    const String cfg = sanitizeJsonForHtml(loadConfigText());

    String body;
    body.reserve(cfg.length() + 8192);
    body += F("<div class='topbar'><div class='brand'>"
              "<img class='site-logo' src='/favicon.svg' alt='' width='36' height='36'>"
              "<div class='brand-text'><h1>Cardputer Config</h1>"
              "<p class='nav'><a href='/advanced'>高级 JSON</a> · "
              "<a href='/example'>示例</a></p></div></div></div>"
              "<form id='save-form' method='POST' action='/save'>"
              "<input type='hidden' name='config' id='config-payload'>"
              "<div class='tabs'>"
              "<button type='button' class='tab active' data-tab='wifi'>WiFi</button>"
              "<button type='button' class='tab' data-tab='devices'>米家设备</button>"
              "<button type='button' class='tab' data-tab='groups'>编组</button>"
              "<button type='button' class='tab' data-tab='cursor'>Cursor</button>"
              "<button type='button' class='tab' data-tab='system'>系统设置</button>"
              "</div>"
              "<div id='panel-wifi' class='panel active'>"
              "<div class='wifi-grid'>"
              "<label>SSID<input id='wifi-ssid' autocomplete='off'></label>"
              "<label>密码<input id='wifi-pass' autocomplete='off'></label>"
              "</div></div>"
              "<div id='panel-devices' class='panel'>"
              "<div class='toolbar'>"
              "<button type='button' id='btn-add'>+ 添加设备</button>"
              "<span class='count' id='dev-count'></span>"
              "</div>"
              "<div class='table-wrap'><table class='dev-table'>"
              "<thead><tr>"
              "<th class='col-idx'>#</th>"
              "<th class='col-name'>名称</th>"
              "<th class='col-namezh'>中文名</th>"
              "<th class='col-act'>操作</th>"
              "<th class='col-ip'>IP</th>"
              "<th class='col-token'>Token</th>"
              "<th class='col-ble'>BLE Key</th>"
              "<th class='col-model'>型号</th>"
              "<th class='col-id'>ID</th>"
              "<th class='col-mac'>MAC</th>"
              "</tr></thead>"
              "<tbody id='dev-tbody'></tbody>"
              "</table></div></div>"
              "<div id='panel-groups' class='panel'>"
              "<p class='hint'>用设备 <code>id</code> 编组；改名不影响成员。"
              "成员里的 name / name_zh 仅方便阅读，保存时会从设备表同步。"
              "BLE 只读设备可勾选但设备端开/关会跳过。</p>"
              "<div class='toolbar'>"
              "<button type='button' id='btn-add-group'>+ 添加编组</button>"
              "<span class='count' id='group-count'></span>"
              "</div>"
              "<div id='group-list'></div>"
              "</div>"
              "<div id='panel-cursor' class='panel'>"
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
              "</div>"
              "<div id='panel-system' class='panel'>"
              "<h2>系统设置</h2>"
              "<p class='hint'>时区、亮度与提示音。亮度配置为 0~100，"
              "设备端会换算为背光 0~255。</p>"
              "<div class='sys-grid'>"
              "<label>时区（POSIX TZ）"
              "<input id='sys-timezone' placeholder='CST-8' autocomplete='off'></label>"
              "<label>亮度（0~100）"
              "<div class='bright-row'>"
              "<input id='sys-brightness' type='range' min='0' max='100' step='1'>"
              "<span class='bright-val' id='sys-brightness-val'>30</span>"
              "</div></label>"
              "<label class='check-row'>"
              "<input id='sys-sound-time-key' type='checkbox'>"
              "<span>Time 按键声（stopwatch / countdown）</span></label>"
              "<label class='check-row'>"
              "<input id='sys-sound-mijia' type='checkbox'>"
              "<span>米家开/关提示音</span></label>"
              "<label>Time 默认模块"
              "<select id='sys-time-default'>"
              "<option value='up'>UP（运行时长）</option>"
              "<option value='ntp'>NTP（时钟）</option>"
              "<option value='countdown'>Countdown</option>"
              "<option value='stopwatch'>Stopwatch</option>"
              "</select></label>"
              "</div></div>"
              "<div class='save-bar'>"
              "<button type='submit' class='primary' id='btn-save'>保存到设备</button>"
              "</div></form>"
              "<script type='application/json' id='cfg-data'>");
    body += cfg;
    body += F("</script><script>");
    body += F("const DEV_MAX=");
    body += WEB_DEVICE_MAX;
    body += F(";const GROUP_MAX=");
    body += String(MIJIA_GROUP_MAX);
    body += F(";const GROUP_MEMBER_MAX=");
    body += String(MIJIA_GROUP_MEMBER_MAX);
    body += F(";");
    // 与固件 deviceIconBasenameForModel 同一套 basename 列表
    body += F("const ICON_NAMES=[");
    for (const char* const* name = deviceIconNames(); *name != nullptr; ++name) {
        body += '\'';
        body += *name;
        body += F("',");
    }
    body += F("];");
    body += F(
        "let cfg={wifi:{ssid:'',password:''},devices:[],device_groups:[],cursor:{token:''},"
        "timezone:'CST-8',brightness:30,sound:{time_key:true,mijia_on_off:true},"
        "time:{default:'up'}};"
        "function esc(s){return String(s==null?'':s).replace(/&/g,'&amp;').replace(/\"/g,'&quot;')"
        ".replace(/</g,'&lt;');}"
        "function ta(f,v){return `<textarea data-f='${f}' rows='1'>${esc(v)}</textarea>`;}"
        // 与固件匹配规则一致：子串忽略大小写，较长名优先，再回退 light/default
        "function iconBase(model){const m=String(model||'').toLowerCase();"
        "for(const n of ICON_NAMES){if(m.includes(n))return n;}"
        "if(m.includes('light'))return 'light';return 'default';}"
        "function iconUrl(model){return '/icon/device/'+iconBase(model)+'.png';}"
        "function modelCell(model){return `<div class='model-cell'>"
        "<img class='dev-icon' src='${iconUrl(model)}' alt='' title='${iconBase(model)}'>"
        "${ta('model',model)}</div>`;}"
        "function bleKeyOf(d){return (d.ble&&d.ble.key)||d.ble_key||'';}"
        "function isBleDev(d){return !!bleKeyOf(d);}"
        "function memberIdOf(m){return typeof m==='string'?m:(m&&m.id)||'';}"
        // 从设备表同步成员 name/name_zh；剔除无效 id
        "function syncGroupMembers(g){const byId={};"
        "(cfg.devices||[]).forEach(d=>{if(d&&d.id)byId[d.id]=d;});"
        "const seen=new Set();const out=[];"
        "(g.members||[]).forEach(m=>{const id=memberIdOf(m);if(!id||seen.has(id))return;"
        "const d=byId[id];if(!d)return;seen.add(id);"
        "const row={id:id,name:d.name||''};"
        "const zh=d.name_zh||d.name_cn||'';if(zh)row.name_zh=zh;"
        "out.push(row);});"
        "g.members=out.slice(0,GROUP_MEMBER_MAX);return g;}"
        "function collectDevices(){cfg.devices=[];"
        "document.querySelectorAll('#dev-tbody tr').forEach(row=>{"
        "const d={};row.querySelectorAll('[data-f]').forEach(el=>{d[el.dataset.f]=el.value;});"
        "const bk=d.ble_key||'';delete d.ble_key;"
        "if(bk)d.ble={key:bk};else delete d.ble;"
        "if(!d.name_zh)delete d.name_zh;"
        "cfg.devices.push(d);});}"
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
        "function collect(){cfg.wifi.ssid=document.getElementById('wifi-ssid').value;"
        "cfg.wifi.password=document.getElementById('wifi-pass').value;"
        "if(!cfg.cursor)cfg.cursor={token:''};"
        "cfg.cursor.token=document.getElementById('cursor-key').value;"
        "cfg.timezone=document.getElementById('sys-timezone').value||'CST-8';"
        "let b=+document.getElementById('sys-brightness').value;if(isNaN(b))b=30;"
        "if(b<0)b=0;if(b>100)b=100;cfg.brightness=b;"
        "if(!cfg.sound)cfg.sound={};"
        "cfg.sound.time_key=document.getElementById('sys-sound-time-key').checked;"
        "cfg.sound.mijia_on_off=document.getElementById('sys-sound-mijia').checked;"
        "if(!cfg.time)cfg.time={};"
        "cfg.time.default=document.getElementById('sys-time-default').value||'up';"
        "collectDevices();collectGroups();"
        "(cfg.device_groups||[]).forEach(syncGroupMembers);}"
        "function renderDevices(){const tb=document.getElementById('dev-tbody');tb.innerHTML='';"
        "cfg.devices.forEach((d,i)=>{const tr=document.createElement('tr');tr.dataset.i=i;"
        "tr.innerHTML=`<td class='col-idx'>${i+1}</td>"
        "<td class='col-name'>${ta('name',d.name)}</td>"
        "<td class='col-namezh'>${ta('name_zh',d.name_zh||d.name_cn||'')}</td>"
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
        "function render(){renderDevices();renderGroups();}"
        "function move(i,d){collect();const j=i+d;if(j<0||j>=cfg.devices.length)return;"
        "[cfg.devices[i],cfg.devices[j]]=[cfg.devices[j],cfg.devices[i]];render();}"
        "function moveTop(i){collect();if(i<=0)return;"
        "const item=cfg.devices.splice(i,1)[0];cfg.devices.unshift(item);render();}"
        "function moveBottom(i){collect();if(i>=cfg.devices.length-1)return;"
        "const item=cfg.devices.splice(i,1)[0];cfg.devices.push(item);render();}"
        "function moveGroup(i,d){collect();const j=i+d;if(j<0||j>=cfg.device_groups.length)return;"
        "[cfg.device_groups[i],cfg.device_groups[j]]=[cfg.device_groups[j],cfg.device_groups[i]];"
        "renderGroups();}"
        "function switchTab(id){document.querySelectorAll('.tab').forEach(t=>{"
        "t.classList.toggle('active',t.dataset.tab===id);});"
        "document.querySelectorAll('.panel').forEach(p=>{"
        "p.classList.toggle('active',p.id==='panel-'+id);});}"
        "function init(){try{cfg=JSON.parse(document.getElementById('cfg-data').textContent);}"
        "catch(e){cfg={wifi:{ssid:'',password:''},devices:[],device_groups:[],cursor:{token:''},"
        "timezone:'CST-8',brightness:30,sound:{time_key:true,mijia_on_off:true},"
        "time:{default:'up'}};}"
        "if(!cfg.wifi)cfg.wifi={ssid:'',password:''};"
        "if(!cfg.devices)cfg.devices=[];"
        "if(!cfg.device_groups)cfg.device_groups=[];"
        "if(!cfg.cursor)cfg.cursor={token:''};"
        "if(!cfg.cursor.token&&cfg.cursor.api_key)cfg.cursor.token=cfg.cursor.api_key;"
        "if(!cfg.timezone)cfg.timezone='CST-8';"
        "let bright=cfg.brightness;if(bright==null||isNaN(+bright))bright=30;"
        "bright=+bright;if(bright>100)bright=Math.round(bright*100/255);"
        "if(bright<0)bright=0;if(bright>100)bright=100;cfg.brightness=bright;"
        "if(!cfg.sound)cfg.sound={};"
        "if(cfg.sound.time_key==null)cfg.sound.time_key=true;"
        "if(cfg.sound.mijia_on_off==null)cfg.sound.mijia_on_off=true;"
        "if(!cfg.time)cfg.time={};"
        "if(!cfg.time.default)cfg.time.default='up';"
        "document.getElementById('wifi-ssid').value=cfg.wifi.ssid||'';"
        "document.getElementById('wifi-pass').value=cfg.wifi.password||'';"
        "document.getElementById('cursor-key').value=cfg.cursor.token||'';"
        "document.getElementById('sys-timezone').value=cfg.timezone||'CST-8';"
        "document.getElementById('sys-brightness').value=String(cfg.brightness);"
        "document.getElementById('sys-brightness-val').textContent=String(cfg.brightness);"
        "document.getElementById('sys-sound-time-key').checked=!!cfg.sound.time_key;"
        "document.getElementById('sys-sound-mijia').checked=!!cfg.sound.mijia_on_off;"
        "document.getElementById('sys-time-default').value=cfg.time.default||'up';"
        "document.getElementById('sys-brightness').oninput=e=>{"
        "document.getElementById('sys-brightness-val').textContent=e.target.value;};"
        "render();"
        "document.querySelectorAll('.tab').forEach(t=>{"
        "t.onclick=()=>switchTab(t.dataset.tab);});"
        "document.getElementById('btn-add').onclick=()=>{collect();"
        "if(cfg.devices.length>=DEV_MAX){alert('最多 '+DEV_MAX+' 台设备');return;}"
        "cfg.devices.push({name:'',name_zh:'',id:'',mac:'',ip:'',token:'',model:'',ble:{key:''}});"
        "render();"
        "switchTab('devices');};"
        "document.getElementById('btn-add-group').onclick=()=>{collect();"
        "if(cfg.device_groups.length>=GROUP_MAX){alert('最多 '+GROUP_MAX+' 组');return;}"
        "cfg.device_groups.push({name:'',name_zh:'',members:[]});renderGroups();"
        "switchTab('groups');};"
        "document.getElementById('dev-tbody').onclick=e=>{const b=e.target.closest('button');"
        "if(!b)return;const i=+b.closest('tr').dataset.i;"
        "if(b.dataset.act==='up')move(i,-1);"
        "else if(b.dataset.act==='down')move(i,1);"
        "else if(b.dataset.act==='top')moveTop(i);"
        "else if(b.dataset.act==='bottom')moveBottom(i);"
        "else if(b.dataset.act==='del'){collect();const removed=cfg.devices.splice(i,1)[0];"
        "const rid=removed&&removed.id;if(rid){(cfg.device_groups||[]).forEach(g=>{"
        "g.members=(g.members||[]).filter(m=>memberIdOf(m)!==rid);});}"
        "render();}};"
        // 输入 model 时即时刷新匹配图标
        "document.getElementById('dev-tbody').oninput=e=>{const el=e.target;"
        "if(!el||el.dataset.f!=='model')return;"
        "const img=el.closest('tr').querySelector('.dev-icon');"
        "if(!img)return;const base=iconBase(el.value);img.src=iconUrl(el.value);img.title=base;};"
        "document.getElementById('group-list').onclick=e=>{const b=e.target.closest('button');"
        "if(!b||!b.dataset.gact)return;const i=+b.closest('.group-card').dataset.gi;"
        "if(b.dataset.gact==='up')moveGroup(i,-1);"
        "else if(b.dataset.gact==='down')moveGroup(i,1);"
        "else if(b.dataset.gact==='del'){collect();cfg.device_groups.splice(i,1);renderGroups();}};"
        "document.getElementById('save-form').onsubmit=()=>{collect();"
        "document.getElementById('config-payload').value=JSON.stringify(cfg,null,2);};}"
        "init();");
    body += F("</script>");
    sendHtmlPage(body);
}

// 高级 JSON 编辑页
static void handleAdvancedRoot() {
    const String cfg = loadConfigText();

    String body;
    body.reserve(cfg.length() + 512);
    body += F("<div class='topbar'><div class='brand'>"
              "<img class='site-logo' src='/favicon.svg' alt='' width='36' height='36'>"
              "<div class='brand-text'><h1>Cardputer Config</h1>"
              "<p class='nav'><a href='/'>← 返回主页</a> · "
              "<a href='/example'>示例格式</a></p></div></div></div>"
              "<h2>高级 JSON 编辑</h2>"
              "<form method='POST' action='/save'>"
              "<textarea class='json-editor' name='config'>");
    body += escapeForTextarea(cfg);
    body += F("</textarea><br><button type='submit' class='primary'>保存到设备</button></form>");
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
        // 保存后立即生效时区与亮度
        applyLocalTimezone();
        M5Cardputer.Display.setBrightness(brightnessPercentToHw(getAppConfig().brightness));
        snprintf(g_web_status, sizeof(g_web_status), "saved %d dev",
                 getAppConfig().device_count);
        String body = F("<h1>已保存</h1><p class='ok'>config.json 写入成功。</p>"
                        "<p>设备数: ");
        body += getAppConfig().device_count;
        body += F("</p><div class='result-actions'>"
                  "<a href='/' class='btn primary'>返回主页</a>"
                  "<a href='/advanced' class='btn'>高级 JSON</a></div>");
        sendHtmlPage(body);
    } else {
        strncpy(g_web_status, "json error", sizeof(g_web_status));
        String body = F("<h1>保存失败</h1><p class='err'>JSON 格式无效，请检查后重试。</p>"
                        "<div class='result-actions'>"
                        "<a href='/' class='btn primary'>返回主页</a>"
                        "<a href='/advanced' class='btn'>高级 JSON</a></div>");
        sendHtmlPage(body);
    }
}

static void handleExample() {
    String body = F("<h1>示例 config.json</h1><p>WiFi 米家用 <code>ip</code> + <code>token</code>；"
                    "BLE 传感器用 <code>mac</code> + <code>ble.key</code>，可加 <code>name_zh</code>。"
                    "编组见 <code>device_groups</code>，成员用设备 <code>id</code> 引用。"
                    "系统项见主页 <strong>系统设置</strong>（时区 / 亮度 0~100 / 提示音）。"
                    "Cursor 用量见主页 "
                    "<strong>Cursor</strong> 标签页。</p><pre>");
    body += DEFAULT_CONFIG;
    body += F("</pre><p class='nav'><a href='/'>← 返回主页</a></p>");
    sendHtmlPage(body);
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
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

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

// 从 LittleFS 提供 /icon/device/*.png（供配置页预览）
static bool tryServeDeviceIcon() {
    const String uri = g_server.uri();
    if (!uri.startsWith("/icon/device/") || !uri.endsWith(".png")) {
        return false;
    }
    if (uri.indexOf("..") >= 0 || uri.length() > 64) {
        return false;
    }
    // 仅允许字母数字与 _-
    const char* p = uri.c_str() + strlen("/icon/device/");
    if (*p == '\0') {
        return false;
    }
    for (const char* c = p; *c != '\0'; ++c) {
        const bool ok = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') ||
                        (*c >= '0' && *c <= '9') || *c == '_' || *c == '-' || *c == '.';
        if (!ok) {
            return false;
        }
    }

    File file = LittleFS.open(uri, "r");
    if (!file) {
        return false;
    }
    g_server.streamFile(file, "image/png");
    file.close();
    return true;
}

// 注册 HTTP 路由
static void registerWebRoutes() {
    g_server.on("/", HTTP_GET, handleFormRoot);
    g_server.on("/advanced", HTTP_GET, handleAdvancedRoot);
    g_server.on("/save", HTTP_POST, handleSave);
    g_server.on("/example", HTTP_GET, handleExample);
    g_server.onNotFound([]() {
        if (tryServeFavicon() || tryServeDeviceIcon()) {
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

void drawWebApp() {
    if (!g_web_screen_ready) {
        beginAppScreen("Config Setup");
        g_web_screen_ready = true;
    } else {
        clearAppContentArea();
    }

    int y = APP_CONTENT_Y;

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
        return;
    }

    if (g_startup_phase == WebStartupPhase::FAILED) {
        drawLine1x("status", "failed");
        drawLine1x("state", g_web_status);
        drawKeyHintAt(hint_y, 'a', "for AP");
        return;
    }

    if (!isConfigWebServerRunning()) {
        drawLine1x("status", "offline");
        drawTextHintAt(hint_y, "re-enter u");
        return;
    }

    if (isConfigWebStaMode()) {
        drawLineValue2x("mode", "LAN");
        drawLineValue2x("url", stripHttpPrefix(getConfigWebUrl()));
        drawLineValue2x("state", getConfigWebStatus());
        drawKeyHintAt(hint_y, 'a', "switch to AP hotspot");
    } else {
        drawLine1x("mode", "AP");
        drawLineValue2x("ssid", getConfigWebApSsid());
        drawLineValue2x("pass", getConfigWebApPass());
        drawLineValue2x("url", stripHttpPrefix(getConfigWebUrl()));
        drawLine1x("state", getConfigWebStatus());
        drawTextHintAt(hint_y - INFO_LINE_H * 2, "phone connect AP");
        drawTextHintAt(hint_y - INFO_LINE_H, "open url in browser");
        drawKeyHintAt(hint_y, 'l', "retry LAN mode");
    }
}

void handleWebApp(const String& key) {
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
    beginWebStartup(false);
    drawWebApp();
}
