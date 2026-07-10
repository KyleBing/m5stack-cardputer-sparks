#include "app_cursor.h"
#include "app_colors.h"
#include "app_common.h"
#include "app_config.h"
#include "app_header.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mbedtls/base64.h>
#include <time.h>

static constexpr const char* CURSOR_HOST = "https://cursor.com";
static constexpr int CURSOR_DAYS_MAX = 31;
static constexpr uint32_t CURSOR_REFRESH_MS = 60000;
static constexpr uint32_t CURSOR_WIFI_TIMEOUT_MS = 12000;

enum class CursorPhase {
    IDLE,
    WIFI,
    FETCHING,
    READY,
    ERROR,
};

enum class CursorChartRange {
    DAYS_7,
    DAYS_30,
};

struct CursorUsageData {
    bool valid;
    float auto_pct;
    float api_pct;
    int used_cents;
    int limit_cents;
    int remaining_cents;
    char reset_date[12];
    float daily_cents[CURSOR_DAYS_MAX];
    int day_count;
};

static CursorPhase g_phase = CursorPhase::IDLE;
static CursorChartRange g_chart_range = CursorChartRange::DAYS_7;
static bool g_screen_ready = false;
static bool g_fetch_pending = false;
static char g_error_msg[48] = "";
static char g_status_msg[32] = "";
static char g_cookie[CURSOR_API_KEY_MAX + 64] = "";
static int g_user_id = 0;
static CursorUsageData g_usage{};
static uint32_t g_last_fetch_ms = 0;
static uint32_t g_fetch_step = 0;
static bool g_chart_loading = false;

// 仅刷新内容区
static void redrawCursorContent() {
    if (g_screen_ready) {
        clearAppContentArea();
    } else {
        beginAppScreen("Cursor");
        g_screen_ready = true;
    }
}

// Base64url 解码 JWT payload 段
static bool decodeB64Url(const char* in, char* out, const size_t out_size, size_t* out_len) {
    if (in == nullptr || out == nullptr || out_size == 0) {
        return false;
    }

    String normalized(in);
    normalized.replace('-', '+');
    normalized.replace('_', '/');
    while (normalized.length() % 4 != 0) {
        normalized += '=';
    }

    size_t written = 0;
    const int rc = mbedtls_base64_decode(reinterpret_cast<unsigned char*>(out), out_size - 1, &written,
                                         reinterpret_cast<const unsigned char*>(normalized.c_str()),
                                         normalized.length());
    if (rc != 0) {
        return false;
    }
    out[written] = '\0';
    if (out_len != nullptr) {
        *out_len = written;
    }
    return true;
}

// 从 JWT 提取 sub，写入 sub_buf
static bool extractJwtSub(const char* jwt, char* sub_buf, const size_t sub_size) {
    const char* dot1 = strchr(jwt, '.');
    if (dot1 == nullptr) {
        return false;
    }
    const char* dot2 = strchr(dot1 + 1, '.');
    if (dot2 == nullptr) {
        return false;
    }

    const size_t payload_len = static_cast<size_t>(dot2 - dot1 - 1);
    if (payload_len == 0 || payload_len > 512) {
        return false;
    }

    char payload_b64[520];
    memcpy(payload_b64, dot1 + 1, payload_len);
    payload_b64[payload_len] = '\0';

    char payload_json[640];
    if (!decodeB64Url(payload_b64, payload_json, sizeof(payload_json), nullptr)) {
        return false;
    }

    JsonDocument doc;
    if (deserializeJson(doc, payload_json)) {
        return false;
    }

    const char* sub = doc["sub"];
    if (sub == nullptr || sub[0] == '\0') {
        return false;
    }

    strncpy(sub_buf, sub, sub_size - 1);
    sub_buf[sub_size - 1] = '\0';
    return true;
}

// 构造 WorkosCursorSessionToken Cookie
static bool buildCursorCookie(const char* api_key, char* cookie, const size_t cookie_size) {
    if (api_key == nullptr || api_key[0] == '\0' || cookie == nullptr || cookie_size < 32) {
        return false;
    }

    if (strstr(api_key, "::") != nullptr || strstr(api_key, "%3A%3A") != nullptr) {
        snprintf(cookie, cookie_size, "WorkosCursorSessionToken=%s", api_key);
        String encoded(cookie);
        encoded.replace("::", "%3A%3A");
        strncpy(cookie, encoded.c_str(), cookie_size - 1);
        cookie[cookie_size - 1] = '\0';
        return true;
    }

    char sub[96];
    if (!extractJwtSub(api_key, sub, sizeof(sub))) {
        return false;
    }

    snprintf(cookie, cookie_size, "WorkosCursorSessionToken=%s%%3A%%3A%s", sub, api_key);
    return true;
}

// HTTPS 请求（GET / POST）
static bool cursorHttpRequest(const char* method, const char* path, const char* post_body,
                              String& response, int& http_code) {
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(15000);

    HTTPClient http;
    const String url = String(CURSOR_HOST) + path;
    if (!http.begin(client, url)) {
        http_code = -1;
        return false;
    }

    http.setTimeout(15000);
    http.addHeader("Cookie", g_cookie);
    http.addHeader("Accept", "application/json");
    http.addHeader("User-Agent", "Cardputer/1.0");

    if (strcmp(method, "POST") == 0) {
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Origin", "https://cursor.com");
        http_code = http.POST(post_body == nullptr ? "" : post_body);
    } else {
        http_code = http.GET();
    }

    if (http_code > 0) {
        response = http.getString();
    } else {
        response = "";
    }
    http.end();
    return http_code == 200;
}

// 快速 NTP 同步（已有 WiFi 时）
static bool quickSyncTime() {
    configTime(8 * 3600, 0, "ntp.aliyun.com", "pool.ntp.org");
    for (int i = 0; i < 20; i++) {
        time_t now = time(nullptr);
        if (now > 1700000000) {
            return true;
        }
        delay(200);
    }
    return false;
}

// 当天 0 点 epoch 秒（本地时区）
static time_t dayStartEpoch(const int days_ago) {
    time_t now = time(nullptr);
    if (now <= 0) {
        return 0;
    }
    struct tm local_tm;
    if (localtime_r(&now, &local_tm) == nullptr) {
        return 0;
    }
    local_tm.tm_hour = 0;
    local_tm.tm_min = 0;
    local_tm.tm_sec = 0;
    time_t today = mktime(&local_tm);
    return today - static_cast<time_t>(days_ago) * 86400;
}

// 解析周期用量
static void parsePeriodUsage(const JsonDocument& doc, CursorUsageData& out) {
    JsonVariantConst plan_var = doc["planUsage"];
    if (plan_var.isNull()) {
        JsonVariantConst individual = doc["individualUsage"];
        if (!individual.isNull()) {
            plan_var = individual["plan"];
        }
    }
    if (!plan_var.isNull()) {
        out.auto_pct = plan_var["autoPercentUsed"] | 0.0f;
        out.api_pct = plan_var["apiPercentUsed"] | 0.0f;
        out.used_cents = plan_var["totalSpend"] | plan_var["used"] | 0;
        out.limit_cents = plan_var["limit"] | 0;
        out.remaining_cents = plan_var["remaining"] | 0;
        if (out.remaining_cents == 0 && out.limit_cents > 0 && out.used_cents > 0) {
            out.remaining_cents = out.limit_cents - out.used_cents;
            if (out.remaining_cents < 0) {
                out.remaining_cents = 0;
            }
        }
    }

    const char* reset_raw = doc["billingCycleEnd"];
    if (reset_raw != nullptr) {
        if (isdigit(static_cast<unsigned char>(reset_raw[0]))) {
            const int64_t ms = strtoll(reset_raw, nullptr, 10);
            if (ms > 0) {
                const time_t t = static_cast<time_t>(ms / 1000);
                struct tm utc_tm;
                if (gmtime_r(&t, &utc_tm) != nullptr) {
                    strftime(out.reset_date, sizeof(out.reset_date), "%m-%d", &utc_tm);
                }
            }
        } else {
            strncpy(out.reset_date, reset_raw, sizeof(out.reset_date) - 1);
            out.reset_date[10] = '\0';
            if (strlen(out.reset_date) >= 10) {
                out.reset_date[5] = out.reset_date[8];
                out.reset_date[6] = out.reset_date[9];
                out.reset_date[7] = '\0';
            }
        }
    }
}

// 将事件累计到每日数组（index 0 = 最早一天）
static void accumulateEventDay(const int64_t ts_ms, const float cents, const int range_days) {
  if (ts_ms <= 0 || range_days <= 0 || range_days > CURSOR_DAYS_MAX) {
    return;
  }

  const time_t event_sec = static_cast<time_t>(ts_ms / 1000);
  for (int i = 0; i < range_days; i++) {
    const time_t start = dayStartEpoch(range_days - 1 - i);
    const time_t end = start + 86400;
    if (event_sec >= start && event_sec < end) {
      g_usage.daily_cents[i] += cents;
      return;
    }
  }
}

// 拉取每日用量（分页事件）
static bool fetchDailyUsage(const int range_days) {
  const int64_t end_ms = static_cast<int64_t>(time(nullptr)) * 1000LL;
  const time_t start_sec = dayStartEpoch(range_days - 1);
  if (start_sec <= 0) {
    return false;
  }
  const int64_t start_ms = static_cast<int64_t>(start_sec) * 1000LL;

  memset(g_usage.daily_cents, 0, sizeof(g_usage.daily_cents));
  g_usage.day_count = range_days;

  char body[160];
  int page = 1;
  int fetched = 0;
  int total = 0;

  while (page <= 20) {
    snprintf(body, sizeof(body),
             "{\"teamId\":0,\"startDate\":\"%lld\",\"endDate\":\"%lld\",\"userId\":%d,"
             "\"page\":%d,\"pageSize\":100}",
             static_cast<long long>(start_ms), static_cast<long long>(end_ms), g_user_id, page);

    String response;
    int code = 0;
    if (!cursorHttpRequest("POST", "/api/dashboard/get-filtered-usage-events", body, response, code)) {
      return page == 1 ? false : true;
    }

    JsonDocument doc;
    JsonDocument filter;
    filter["totalUsageEventsCount"] = true;
    filter["usageEventsDisplay"][0]["timestamp"] = true;
    filter["usageEventsDisplay"][0]["tokenUsage"]["totalCents"] = true;
    filter["usageEventsDisplay"][0]["chargedCents"] = true;
    if (deserializeJson(doc, response, DeserializationOption::Filter(filter))) {
      return page == 1 ? false : true;
    }

    total = doc["totalUsageEventsCount"] | 0;
    JsonArray events = doc["usageEventsDisplay"].as<JsonArray>();
    if (events.isNull() || events.size() == 0) {
      break;
    }

    for (JsonObject ev : events) {
      const int64_t ts = ev["timestamp"].as<int64_t>();
      JsonObject tu = ev["tokenUsage"];
      float cents = 0.0f;
      if (!tu.isNull()) {
        cents = tu["totalCents"] | 0.0f;
      }
      if (cents <= 0.0f) {
        cents = ev["chargedCents"] | 0.0f;
      }
      accumulateEventDay(ts, cents, range_days);
      fetched++;
    }

    if (fetched >= total || events.size() < 100) {
      break;
    }
    page++;
  }
  return true;
}

// 拉取认证与周期用量（较快，先展示摘要）
static bool fetchCursorAuthPeriod() {
  const AppConfig& cfg = getAppConfig();
  if (!cfg.loaded || cfg.cursor_api_key[0] == '\0') {
    strncpy(g_error_msg, "no api_key in cfg", sizeof(g_error_msg));
    return false;
  }
  if (!buildCursorCookie(cfg.cursor_api_key, g_cookie, sizeof(g_cookie))) {
    strncpy(g_error_msg, "bad session token", sizeof(g_error_msg));
    return false;
  }

  String response;
  int code = 0;

  strncpy(g_status_msg, "auth...", sizeof(g_status_msg));
  if (!cursorHttpRequest("GET", "/api/auth/me", nullptr, response, code)) {
    snprintf(g_error_msg, sizeof(g_error_msg), "auth %d", code);
    return false;
  }

  JsonDocument me_doc;
  if (deserializeJson(me_doc, response)) {
    strncpy(g_error_msg, "auth json err", sizeof(g_error_msg));
    return false;
  }
  g_user_id = me_doc["id"] | 0;
  if (g_user_id == 0) {
    strncpy(g_error_msg, "no user id", sizeof(g_error_msg));
    return false;
  }

  memset(&g_usage, 0, sizeof(g_usage));
  strncpy(g_status_msg, "usage...", sizeof(g_status_msg));
  if (!cursorHttpRequest("POST", "/api/dashboard/get-current-period-usage", "{}", response, code)) {
    if (!cursorHttpRequest("GET", "/api/usage-summary", nullptr, response, code)) {
      snprintf(g_error_msg, sizeof(g_error_msg), "usage %d", code);
      return false;
    }
  }

  JsonDocument usage_doc;
  if (deserializeJson(usage_doc, response)) {
    strncpy(g_error_msg, "usage json err", sizeof(g_error_msg));
    return false;
  }
  parsePeriodUsage(usage_doc, g_usage);
  g_usage.valid = true;
  g_last_fetch_ms = millis();
  return true;
}

// 绘制横向进度条
static void drawPctBar(const int x, const int y, const int w, const int h, const float pct,
                       const uint16_t color) {
  M5Cardputer.Display.drawRect(x, y, w, h, DARKGREY);
  const int fill_w = static_cast<int>(w * (pct / 100.0f));
  if (fill_w > 0) {
    M5Cardputer.Display.fillRect(x, y, fill_w > w ? w : fill_w, h, color);
  }
}

static constexpr int CURSOR_BAR_LABEL_H = 10;

// 空间不足时稀疏显示日期（如 1 5 10 15 20 25 30）
static bool shouldShowBarDayLabel(const int i, const int days, const int bar_w) {
  if (days <= 7 || bar_w >= 10) {
    return true;
  }
  if (i == 0 || i == days - 1) {
    return true;
  }
  return (i % 5) == 0;
}

// 绘制每日 bar 图（30 天均分宽度；柱体不重叠，日期标签可重叠）
static void drawDailyBars(const int x, const int y, const int w, const int bar_h, const int days) {
  if (days <= 0 || bar_h <= 0 || w <= 0) {
    return;
  }

  float max_val = 0.01f;
  for (int i = 0; i < days; i++) {
    if (g_usage.daily_cents[i] > max_val) {
      max_val = g_usage.daily_cents[i];
    }
  }

  const bool even_split = (days == 30);
  const int gap = even_split ? 0 : 3;
  int bx = x;

  for (int i = 0; i < days; i++) {
    int bar_x;
    int bar_w;
    if (even_split) {
      // 固定 30 天，整宽均分，无间隙
      bar_x = x + (i * w) / days;
      const int next_x = x + ((i + 1) * w) / days;
      bar_w = next_x - bar_x;
    } else {
      const int total_bar_w = (w - gap * (days - 1)) / days;
      bar_x = bx;
      bar_w = total_bar_w;
      bx += total_bar_w + gap;
    }

    const int fill_h = static_cast<int>(bar_h * (g_usage.daily_cents[i] / max_val));
    const int by = y + bar_h - fill_h;
    const uint16_t color = (i == days - 1) ? APP_COLOR_OK : CYAN;
    if (fill_h > 0) {
      M5Cardputer.Display.fillRect(bar_x, by, bar_w, fill_h, color);
    }
    M5Cardputer.Display.drawRect(bar_x, y, bar_w, bar_h, DARKGREY);
  }

  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setTextColor(DARKGREY, BLACK);
  for (int i = 0; i < days; i++) {
    int bar_x;
    int bar_w;
    if (even_split) {
      bar_x = x + (i * w) / days;
      const int next_x = x + ((i + 1) * w) / days;
      bar_w = next_x - bar_x;
    } else {
      const int total_bar_w = (w - gap * (days - 1)) / days;
      bar_x = x + i * (total_bar_w + gap);
      bar_w = total_bar_w;
    }

    if (!shouldShowBarDayLabel(i, days, bar_w)) {
      continue;
    }

    const time_t day_ts = dayStartEpoch(days - 1 - i);
    int mday = i + 1;
    struct tm local_tm;
    if (day_ts > 0 && localtime_r(&day_ts, &local_tm) != nullptr) {
      mday = local_tm.tm_mday;
    }

    char lbl[4];
    snprintf(lbl, sizeof(lbl), "%d", mday);
    const int tw = M5Cardputer.Display.textWidth(lbl);
    const int tx = bar_x + (bar_w - tw) / 2;
    M5Cardputer.Display.setCursor(tx, y + bar_h + 1);
    M5Cardputer.Display.print(lbl);
  }
}

static void formatMoney(const int cents, char* buf, const size_t buf_size) {
  const int dollars = cents / 100;
  const int frac = abs(cents % 100);
  snprintf(buf, buf_size, "$%d.%02d", dollars, frac);
}

void drawCursorApp() {
  redrawCursorContent();

  int y = APP_CONTENT_Y;
  const int screen_w = M5Cardputer.Display.width();
  const int content_w = screen_w - APP_CONTENT_X * 2;

  if (g_phase == CursorPhase::WIFI || g_phase == CursorPhase::FETCHING) {
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
    if (g_phase == CursorPhase::WIFI) {
      M5Cardputer.Display.println("connecting...");
    } else {
      M5Cardputer.Display.println(g_status_msg[0] != '\0' ? g_status_msg : "loading...");
    }
    return;
  }

  if (g_phase == CursorPhase::ERROR) {
    drawInfoLineAt(APP_CONTENT_X, y, "err", g_error_msg, 2);
    y += INFO_LINE_H_2X;
    drawInfoLineAt(APP_CONTENT_X, y, "hint", "u web cfg", 2);
    y += INFO_LINE_H_2X;
    int cx = APP_CONTENT_X + drawKeyBadge(APP_CONTENT_X, y, 'r', 2);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print("retry");
    return;
  }

  const AppConfig& cfg = getAppConfig();
  if (!cfg.loaded || cfg.cursor_api_key[0] == '\0') {
    drawInfoLineAt(APP_CONTENT_X, y, "cfg", "no token", 2);
    y += INFO_LINE_H_2X;
    drawInfoLineAt(APP_CONTENT_X, y, "hint", "u web setup", 2);
    return;
  }

  if (!g_usage.valid) {
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
    M5Cardputer.Display.println("no data");
    return;
  }

  char buf[24];

  // Auto / API 百分比条
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setTextColor(INFO_LABEL_COLOR, BLACK);
  M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
  M5Cardputer.Display.print("Auto");
  drawPctBar(APP_CONTENT_X + 30, y, content_w - 30, 8, g_usage.auto_pct, APP_COLOR_OK);
  y += 10;

  M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
  M5Cardputer.Display.print("API");
  drawPctBar(APP_CONTENT_X + 30, y, content_w - 30, 8, g_usage.api_pct, ORANGE);
  y += 12;

  // 金额与重置（同一行，reset 单独配色）
  if (g_usage.limit_cents > 0) {
    char used_s[16];
    char left_s[16];
    formatMoney(g_usage.used_cents, used_s, sizeof(used_s));
    formatMoney(g_usage.remaining_cents, left_s, sizeof(left_s));

    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(INFO_LABEL_COLOR, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
    M5Cardputer.Display.print("used: ");
    M5Cardputer.Display.setTextColor(INFO_VALUE_COLOR, BLACK);
    M5Cardputer.Display.print(used_s);
    M5Cardputer.Display.print("/");
    M5Cardputer.Display.print(left_s);

    if (g_usage.reset_date[0] != '\0') {
      M5Cardputer.Display.print("  ");
      M5Cardputer.Display.setTextColor(APP_COLOR_WARN, BLACK);
      M5Cardputer.Display.print("reset: ");
      M5Cardputer.Display.setTextColor(ORANGE, BLACK);
      M5Cardputer.Display.print(g_usage.reset_date);
    }
    y += INFO_LINE_H;
  } else {
    snprintf(buf, sizeof(buf), "%.0f%%/%.0f%%", g_usage.api_pct, 100.0f - g_usage.api_pct);
    drawInfoLineAt(APP_CONTENT_X, y, "api", buf, 1);
    y += INFO_LINE_H;
  }

  // 每日 bar 图（30 天占满内容区宽度）
  const int days = (g_chart_range == CursorChartRange::DAYS_7) ? 7 : 30;
  const int chart_w = (days == 30) ? screen_w : content_w;
  const int chart_x = (days == 30) ? 0 : APP_CONTENT_X;
  const int bottom_hint_h = 12;
  const int chart_h = M5Cardputer.Display.height() - y - bottom_hint_h - CURSOR_BAR_LABEL_H;
  if (chart_h >= 20) {
    drawDailyBars(chart_x, y, chart_w, chart_h, days);
    if (g_chart_loading) {
      M5Cardputer.Display.setTextSize(1);
      M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
      M5Cardputer.Display.setCursor(APP_CONTENT_X, y + chart_h / 2);
      M5Cardputer.Display.print("loading...");
    }
    y += chart_h + CURSOR_BAR_LABEL_H + 2;
  }

  // 底部提示
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
  M5Cardputer.Display.setCursor(APP_CONTENT_X, M5Cardputer.Display.height() - 12);
  M5Cardputer.Display.print("r refresh  ");
  drawKeyBadge(M5Cardputer.Display.getCursorX(), M5Cardputer.Display.height() - 12, ',', 1);
  M5Cardputer.Display.print(" ");
  drawKeyBadge(M5Cardputer.Display.getCursorX(), M5Cardputer.Display.height() - 12, '.', 1);
  M5Cardputer.Display.print(" ");
  M5Cardputer.Display.print(g_chart_range == CursorChartRange::DAYS_7 ? "7d" : "30d");
}

static void beginCursorFetch() {
  g_phase = CursorPhase::WIFI;
  g_fetch_pending = true;
  g_fetch_step = 0;
  g_chart_loading = false;
  g_error_msg[0] = '\0';
  g_status_msg[0] = '\0';
  g_usage.valid = false;
  drawCursorApp();
}

static void beginCursorChartFetch() {
  memset(g_usage.daily_cents, 0, sizeof(g_usage.daily_cents));
  g_usage.day_count = 0;
  g_chart_loading = true;
  g_fetch_pending = true;
  g_fetch_step = 2;
  strncpy(g_status_msg, "chart...", sizeof(g_status_msg));
  drawCursorApp();
}

void enterCursorApp() {
  g_screen_ready = false;
  beginCursorFetch();
}

void updateCursorApp() {
  if (!g_fetch_pending) {
    if (g_phase == CursorPhase::READY && g_usage.valid &&
        millis() - g_last_fetch_ms >= CURSOR_REFRESH_MS) {
      beginCursorFetch();
    }
    return;
  }

  if (g_fetch_step == 0) {
    if (!ensureConfigWifi(CURSOR_WIFI_TIMEOUT_MS)) {
      g_phase = CursorPhase::ERROR;
      strncpy(g_error_msg, "wifi fail", sizeof(g_error_msg));
      g_fetch_pending = false;
      drawCursorApp();
      return;
    }
    quickSyncTime();
    g_phase = CursorPhase::FETCHING;
    g_fetch_step = 1;
    drawCursorApp();
    return;
  }

  if (g_fetch_step == 1) {
    if (fetchCursorAuthPeriod()) {
      g_phase = CursorPhase::READY;
      g_error_msg[0] = '\0';
      g_chart_loading = true;
      g_fetch_step = 2;
      drawCursorApp();
      return;
    }
    g_phase = CursorPhase::ERROR;
    g_fetch_pending = false;
    drawCursorApp();
    return;
  }

  if (g_fetch_step == 2) {
    const int range_days = (g_chart_range == CursorChartRange::DAYS_7) ? 7 : 30;
    if (!fetchDailyUsage(range_days)) {
      strncpy(g_error_msg, "chart fail", sizeof(g_error_msg));
    } else {
      g_error_msg[0] = '\0';
    }
    g_chart_loading = false;
    g_status_msg[0] = '\0';
    g_fetch_pending = false;
    drawCursorApp();
  }
}

void handleCursorApp(const String& key) {
  if (key == "r") {
    beginCursorFetch();
    return;
  }
  if (key == "," || key == ".") {
    g_chart_range = (g_chart_range == CursorChartRange::DAYS_7) ? CursorChartRange::DAYS_30
                                                                : CursorChartRange::DAYS_7;
    if (g_phase == CursorPhase::READY && g_usage.valid && g_user_id > 0) {
      beginCursorChartFetch();
    } else {
      drawCursorApp();
    }
  }
}
