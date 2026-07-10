#include "app_web.h"
#include "app_common.h"
#include "app_config.h"
#include "app_device_icons.h"
#include "app_header.h"
#include <FS.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <cstring>

static constexpr const char* AP_SSID = "Cardputer-Setup";
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
      "id": "123456789",
      "mac": "AA:BB:CC:DD:EE:FF",
      "ip": "192.168.1.50",
      "token": "0123456789abcdef0123456789abcdef",
      "model": "yeelink.light.lamp2"
    }
  ],
  "cursor": {
    "api_key": "your-cursor-session-jwt"
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
              "<title>Cardputer Config</title>"
              "<style>"
              "*{box-sizing:border-box}"
              "body{font-family:system-ui,sans-serif;margin:0;padding:10px 12px;line-height:1.4;"
              "width:100%;max-width:100%;background:#121212;color:#e0e0e0}"
              "h1{font-size:1.2rem;margin:0 0 8px;color:#f0f0f0}"
              "input,textarea{width:100%;padding:6px 8px;font-size:13px;border:1px solid #444;"
              "border-radius:4px;font-family:inherit;background:#1e1e1e;color:#e8e8e8}"
              "textarea{resize:vertical;min-height:28px;font-family:ui-monospace,monospace;"
              "font-size:12px;line-height:1.35}"
              "label{font-size:12px;color:#aaa;display:block;margin-bottom:10px}"
              "button{padding:8px 14px;margin:0 6px 6px 0;border:1px solid #444;border-radius:4px;"
              "background:#2a2a2a;color:#e0e0e0;cursor:pointer;font-size:13px}"
              "button.primary{background:#1a73e8;color:#fff;border-color:#1a73e8}"
              "button.danger{color:#ff8a80;border-color:#5c3333;background:#2a1a1a}"
              "button.icon-btn{padding:3px 6px;font-size:12px;line-height:1;min-width:26px;"
              "white-space:nowrap}"
              "a.btn{display:inline-block;padding:8px 14px;margin:0 6px 6px 0;border:1px solid #444;"
              "border-radius:4px;background:#2a2a2a;color:#e0e0e0;cursor:pointer;font-size:13px;"
              "text-decoration:none}"
              "a.btn.primary{background:#1a73e8;color:#fff;border-color:#1a73e8}"
              ".topbar{display:flex;flex-wrap:wrap;align-items:center;justify-content:space-between;"
              "gap:8px;margin-bottom:8px}"
              ".nav{font-size:13px;margin:0}"
              ".nav a{color:#8ab4f8}"
              ".tabs{display:flex;gap:0;border-bottom:2px solid #333;margin-bottom:12px}"
              ".tab{padding:10px 18px;cursor:pointer;border:none;background:none;font-size:14px;"
              "color:#888;border-bottom:2px solid transparent;margin-bottom:-2px}"
              ".tab.active{color:#8ab4f8;border-bottom-color:#8ab4f8;font-weight:600}"
              ".panel{display:none}"
              ".panel.active{display:block}"
              ".hint{font-size:12px;color:#999;margin:0 0 10px}"
              ".toolbar{margin:10px 0;display:flex;flex-wrap:wrap;align-items:center;gap:6px}"
              ".toolbar .count{font-size:13px;color:#888;margin-left:auto}"
              ".table-wrap{width:100%;overflow-x:auto;-webkit-overflow-scrolling:touch}"
              "table.dev-table{width:100%;min-width:948px;border-collapse:collapse;table-layout:fixed}"
              ".dev-table th,.dev-table td{border:1px solid #333;padding:4px;vertical-align:top;"
              "background:#1a1a1a}"
              ".dev-table th{background:#222;font-size:12px;font-weight:600;text-align:left;"
              "padding:6px 4px;color:#ccc}"
              ".dev-table tr:nth-child(even) td{background:#161616}"
              ".dev-table tr:hover td{background:#1e2a3a!important}"
              ".dev-table .col-idx{width:36px;text-align:center;color:#777;font-size:12px;"
              "vertical-align:middle}"
              ".dev-table .col-act{width:168px;vertical-align:middle}"
              ".dev-table .col-act .act-stack{display:flex;flex-direction:row;flex-wrap:nowrap;"
              "gap:3px;align-items:center}"
              ".dev-table .col-act button{width:auto;margin:0;flex-shrink:0}"
              ".dev-table .col-name{width:14%}"
              ".dev-table .col-ip{width:11%}"
              ".dev-table .col-token{width:20%}"
              ".dev-table .col-model{width:22%}"
              ".dev-table .col-id{width:10%}"
              ".dev-table .col-mac{width:13%}"
              ".model-cell{display:flex;gap:6px;align-items:flex-start}"
              ".model-cell textarea{flex:1;min-width:0}"
              ".dev-icon{width:32px;height:32px;object-fit:contain;flex-shrink:0;margin-top:2px;"
              "background:#000;border-radius:4px}"
              ".wifi-grid{max-width:480px}"
              ".save-bar{margin-top:14px;padding-top:12px;border-top:1px solid #333}"
              ".result-actions{margin-top:16px;display:flex;flex-wrap:wrap;gap:6px;align-items:center}"
              ".ok{color:#81c784}.err{color:#ff8a80}"
              "code{background:#2a2a2a;padding:2px 4px;border-radius:3px;color:#e0e0e0}"
              "pre{background:#0d0d0d;color:#e0e0e0;padding:12px;overflow:auto;font-size:12px;"
              "border:1px solid #333;border-radius:4px}"
              "textarea.json-editor{height:min(70vh,520px);font-family:ui-monospace,monospace}"
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
    body += F("<div class='topbar'><h1>Cardputer Config</h1>"
              "<p class='nav'><a href='/advanced'>高级 JSON</a> · "
              "<a href='/example'>示例</a></p></div>"
              "<form id='save-form' method='POST' action='/save'>"
              "<input type='hidden' name='config' id='config-payload'>"
              "<div class='tabs'>"
              "<button type='button' class='tab active' data-tab='wifi'>WiFi</button>"
              "<button type='button' class='tab' data-tab='devices'>米家设备</button>"
              "<button type='button' class='tab' data-tab='cursor'>Cursor</button>"
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
              "<th class='col-act'>操作</th>"
              "<th class='col-ip'>IP</th>"
              "<th class='col-token'>Token</th>"
              "<th class='col-model'>型号</th>"
              "<th class='col-id'>ID</th>"
              "<th class='col-mac'>MAC</th>"
              "</tr></thead>"
              "<tbody id='dev-tbody'></tbody>"
              "</table></div></div>"
              "<div id='panel-cursor' class='panel'>"
              "<p class='hint'>Cursor 会话 Token（JWT 或 sub::jwt），"
              "可从 Cursor 登录态 / state.vscdb 获取。</p>"
              "<label>Session Token<textarea id='cursor-key' rows='4'></textarea></label>"
              "</div>"
              "<div class='save-bar'>"
              "<button type='submit' class='primary' id='btn-save'>保存到设备</button>"
              "</div></form>"
              "<script type='application/json' id='cfg-data'>");
    body += cfg;
    body += F("</script><script>");
    body += F("const DEV_MAX=");
    body += WEB_DEVICE_MAX;
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
        "let cfg={wifi:{ssid:'',password:''},devices:[],cursor:{api_key:''}};"
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
        "function collect(){cfg.wifi.ssid=document.getElementById('wifi-ssid').value;"
        "cfg.wifi.password=document.getElementById('wifi-pass').value;cfg.devices=[];"
        "if(!cfg.cursor)cfg.cursor={api_key:''};"
        "cfg.cursor.api_key=document.getElementById('cursor-key').value;"
        "document.querySelectorAll('#dev-tbody tr').forEach(row=>{"
        "const d={};row.querySelectorAll('[data-f]').forEach(el=>{d[el.dataset.f]=el.value;});"
        "cfg.devices.push(d);});}"
        "function render(){const tb=document.getElementById('dev-tbody');tb.innerHTML='';"
        "cfg.devices.forEach((d,i)=>{const tr=document.createElement('tr');tr.dataset.i=i;"
        "tr.innerHTML=`<td class='col-idx'>${i+1}</td>"
        "<td class='col-name'>${ta('name',d.name)}</td>"
        "<td class='col-act'><div class='act-stack'>"
        "<button type='button' class='icon-btn' data-act='up' title='上移'>↑</button>"
        "<button type='button' class='icon-btn' data-act='down' title='下移'>↓</button>"
        "<button type='button' class='icon-btn' data-act='top' title='置顶'>顶</button>"
        "<button type='button' class='icon-btn' data-act='bottom' title='置底'>底</button>"
        "<button type='button' class='danger icon-btn' data-act='del' title='删除'>删</button>"
        "</div></td>"
        "<td class='col-ip'>${ta('ip',d.ip)}</td>"
        "<td class='col-token'>${ta('token',d.token)}</td>"
        "<td class='col-model'>${modelCell(d.model)}</td>"
        "<td class='col-id'>${ta('id',d.id)}</td>"
        "<td class='col-mac'>${ta('mac',d.mac)}</td>`;tb.appendChild(tr);});"
        "document.getElementById('dev-count').textContent=`共 ${cfg.devices.length} / ${DEV_MAX} 台`;}"
        "function move(i,d){collect();const j=i+d;if(j<0||j>=cfg.devices.length)return;"
        "[cfg.devices[i],cfg.devices[j]]=[cfg.devices[j],cfg.devices[i]];render();}"
        "function moveTop(i){collect();if(i<=0)return;"
        "const item=cfg.devices.splice(i,1)[0];cfg.devices.unshift(item);render();}"
        "function moveBottom(i){collect();if(i>=cfg.devices.length-1)return;"
        "const item=cfg.devices.splice(i,1)[0];cfg.devices.push(item);render();}"
        "function switchTab(id){document.querySelectorAll('.tab').forEach(t=>{"
        "t.classList.toggle('active',t.dataset.tab===id);});"
        "document.querySelectorAll('.panel').forEach(p=>{"
        "p.classList.toggle('active',p.id==='panel-'+id);});}"
        "function init(){try{cfg=JSON.parse(document.getElementById('cfg-data').textContent);}"
        "catch(e){cfg={wifi:{ssid:'',password:''},devices:[],cursor:{api_key:''}};}"
        "if(!cfg.wifi)cfg.wifi={ssid:'',password:''};"
        "if(!cfg.devices)cfg.devices=[];"
        "if(!cfg.cursor)cfg.cursor={api_key:''};"
        "document.getElementById('wifi-ssid').value=cfg.wifi.ssid||'';"
        "document.getElementById('wifi-pass').value=cfg.wifi.password||'';"
        "document.getElementById('cursor-key').value=cfg.cursor.api_key||'';"
        "render();"
        "document.querySelectorAll('.tab').forEach(t=>{"
        "t.onclick=()=>switchTab(t.dataset.tab);});"
        "document.getElementById('btn-add').onclick=()=>{collect();"
        "if(cfg.devices.length>=DEV_MAX){alert('最多 '+DEV_MAX+' 台设备');return;}"
        "cfg.devices.push({name:'',id:'',mac:'',ip:'',token:'',model:''});render();"
        "switchTab('devices');};"
        "document.getElementById('dev-tbody').onclick=e=>{const b=e.target.closest('button');"
        "if(!b)return;const i=+b.closest('tr').dataset.i;"
        "if(b.dataset.act==='up')move(i,-1);"
        "else if(b.dataset.act==='down')move(i,1);"
        "else if(b.dataset.act==='top')moveTop(i);"
        "else if(b.dataset.act==='bottom')moveBottom(i);"
        "else if(b.dataset.act==='del'){collect();cfg.devices.splice(i,1);render();}};"
        // 输入 model 时即时刷新匹配图标
        "document.getElementById('dev-tbody').oninput=e=>{const el=e.target;"
        "if(!el||el.dataset.f!=='model')return;"
        "const img=el.closest('tr').querySelector('.dev-icon');"
        "if(!img)return;const base=iconBase(el.value);img.src=iconUrl(el.value);img.title=base;};"
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
    body += F("<h1>高级 JSON 编辑</h1>"
              "<p class='nav'><a href='/'>← 返回主页</a> · "
              "<a href='/example'>示例格式</a></p>"
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
    String body = F("<h1>示例 config.json</h1><p>米家控制使用 <code>ip</code> + <code>token</code>，"
                    "开关命令为 <code>set_power</code>。Cursor 用量需配置 <code>cursor.api_key</code> "
                    "（会话 JWT）。</p><pre>");
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
        WiFi.setSleep(false);
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
        if (tryServeDeviceIcon()) {
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

    const auto drawKeyHint = [&](const char key, const char* text, const int text_size = 1) {
        int cx = APP_CONTENT_X + drawKeyBadge(APP_CONTENT_X, y, key, text_size);
        M5Cardputer.Display.setTextSize(text_size);
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.setCursor(cx, y);
        M5Cardputer.Display.print(text);
        y += (text_size == 2) ? INFO_LINE_H_2X : INFO_LINE_H;
    };

    // 连接中：初始提示全部 2x
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

        drawKeyHint('a', "skip to AP mode", 2);
        return;
    }

    if (g_startup_phase == WebStartupPhase::FAILED) {
        drawLine1x("status", "failed");
        drawLine1x("state", g_web_status);
        drawKeyHint('a', "for AP");
        return;
    }

    if (!isConfigWebServerRunning()) {
        drawLine1x("status", "offline");
        drawLine1x("hint", "re-enter u");
        return;
    }

    if (isConfigWebStaMode()) {
        drawLineValue2x("mode", "LAN");
        drawLineValue2x("url", stripHttpPrefix(getConfigWebUrl()));
        drawLineValue2x("state", getConfigWebStatus());
        drawKeyHint('a', "switch to AP hotspot");
    } else {
        drawLine1x("mode", "AP");
        drawLineValue2x("ssid", getConfigWebApSsid());
        drawLineValue2x("pass", getConfigWebApPass());
        drawLineValue2x("url", stripHttpPrefix(getConfigWebUrl()));
        drawLine1x("state", getConfigWebStatus());
        drawLine1x("hint", "phone connect AP");
        drawLine1x("hint", "open url in browser");
        drawKeyHint('l', "retry LAN mode");
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
