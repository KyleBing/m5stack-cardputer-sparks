#include "app_web.h"
#include "app_common.h"
#include "app_config.h"
#include "app_header.h"
#include <WebServer.h>
#include <WiFi.h>

static constexpr const char* AP_SSID = "Cardputer-Setup";
static constexpr const char* AP_PASS = "cardputer";
static constexpr const char* WEB_URL = "http://192.168.4.1";

static WebServer g_server(80);
static bool g_running = false;
static char g_web_status[48] = "ready";

// textarea 内容转义
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

static void sendHtmlPage(const String& body) {
    String html;
    html.reserve(body.length() + 512);
    html += F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
              "<meta name='viewport' content='width=device-width,initial-scale=1'>"
              "<title>Cardputer Config</title>"
              "<style>body{font-family:sans-serif;margin:16px;max-width:720px}"
              "textarea{width:100%;height:320px;font-family:monospace;font-size:13px}"
              "button{padding:10px 18px;margin-top:10px}"
              ".ok{color:#0a0}.err{color:#a00}pre{background:#111;color:#eee;padding:12px;"
              "overflow:auto;font-size:12px}</style></head><body>");
    html += body;
    html += F("</body></html>");
    g_server.send(200, "text/html", html);
}

static void handleRoot() {
    String cfg;
    if (!readAppConfigRaw(cfg) || cfg.isEmpty()) {
        cfg = F("{\n  \"wifi\": {\n    \"ssid\": \"your-ssid\",\n    \"password\": \"your-password\"\n  },\n  \"devices\": [\n    {\n      \"name\": \"living-room-light\",\n      \"id\": \"123456789\",\n      \"mac\": \"AA:BB:CC:DD:EE:FF\",\n      \"ip\": \"192.168.1.50\",\n      \"token\": \"0123456789abcdef0123456789abcdef\",\n      \"model\": \"yeelink.light.lamp2\"\n    }\n  ]\n}");
    }

    String body;
    body.reserve(cfg.length() + 512);
    body += F("<h1>Cardputer Config</h1>"
              "<p>连接热点后编辑并保存 <code>config.json</code>。</p>"
              "<form method='POST' action='/save'>"
              "<textarea name='config'>");
    body += escapeForTextarea(cfg);
    body += F("</textarea><br><button type='submit'>保存到设备</button></form>"
              "<p><a href='/example'>查看示例格式</a></p>");
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
        body += F("</p><p><a href='/'>返回编辑</a></p>");
        sendHtmlPage(body);
    } else {
        strncpy(g_web_status, "json error", sizeof(g_web_status));
        String body = F("<h1>保存失败</h1><p class='err'>JSON 格式无效，请检查后重试。</p>"
                        "<p><a href='/'>返回编辑</a></p>");
        sendHtmlPage(body);
    }
}

static void handleExample() {
    const char* example = R"({
  "wifi": {
    "ssid": "your-ssid",
    "password": "your-password"
  },
  "devices": [
    {
      "name": "台灯",
      "id": "434412341",
      "mac": "B4:60:ED:03:2E:8A",
      "ip": "192.168.1.50",
      "token": "0123456789abcdef0123456789abcdef",
      "model": "yeelink.light.lamp2"
    }
  ]
})";
  String body = F("<h1>示例 config.json</h1><p>米家控制使用 <code>ip</code> + <code>token</code>，"
                  "开关命令为 <code>set_power</code>。</p><pre>");
  body += example;
  body += F("</pre><p><a href='/'>返回编辑</a></p>");
  sendHtmlPage(body);
}

bool startConfigWebServer() {
    if (g_running) {
        return true;
    }

    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                      IPAddress(255, 255, 255, 0));
    if (!WiFi.softAP(AP_SSID, AP_PASS)) {
        strncpy(g_web_status, "ap fail", sizeof(g_web_status));
        return false;
    }

    g_server.on("/", HTTP_GET, handleRoot);
    g_server.on("/save", HTTP_POST, handleSave);
    g_server.on("/example", HTTP_GET, handleExample);
    g_server.onNotFound([]() { g_server.send(404, "text/plain", "not found"); });
    g_server.begin();

    g_running = true;
    strncpy(g_web_status, "running", sizeof(g_web_status));
    return true;
}

void stopConfigWebServer() {
    if (!g_running) {
        return;
    }
    g_server.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    g_running = false;
    strncpy(g_web_status, "stopped", sizeof(g_web_status));
}

void handleConfigWebServer() {
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
    return WEB_URL;
}

const char* getConfigWebStatus() {
    return g_web_status;
}

void drawWebApp() {
    beginAppScreen("Web");
    M5Cardputer.Display.setTextSize(1);

    int y = APP_CONTENT_Y;
    if (!isConfigWebServerRunning()) {
        drawInfoLine(APP_CONTENT_X, y, "status", "offline");
        drawInfoLine(APP_CONTENT_X, y, "hint", "re-enter u");
        return;
    }

    drawInfoLine(APP_CONTENT_X, y, "ap", getConfigWebApSsid());
    drawInfoLine(APP_CONTENT_X, y, "pass", getConfigWebApPass());
    drawInfoLine(APP_CONTENT_X, y, "url", getConfigWebUrl());
    drawInfoLine(APP_CONTENT_X, y, "state", getConfigWebStatus());

    const AppConfig& cfg = getAppConfig();
    if (cfg.loaded) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", cfg.device_count);
        drawInfoLine(APP_CONTENT_X, y, "cfg", buf);
    } else {
        drawInfoLine(APP_CONTENT_X, y, "cfg", "none");
    }

    M5Cardputer.Display.setTextColor(LIGHTGREY, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
    M5Cardputer.Display.println("phone connect AP");
    y += INFO_LINE_H;
    M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
    M5Cardputer.Display.println("open url in browser");
}

void enterWebApp() {
    if (startConfigWebServer()) {
        drawWebApp();
    } else {
        beginAppScreen("Web");
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setCursor(APP_CONTENT_X, APP_CONTENT_Y);
        M5Cardputer.Display.setTextColor(RED, BLACK);
        M5Cardputer.Display.println("AP start failed");
    }
}
