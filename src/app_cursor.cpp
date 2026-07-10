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
static constexpr uint32_t CURSOR_IDLE_TRIGGER_MS = 60000;
static constexpr uint32_t CURSOR_REFRESH_INTERVAL_MS = 300000;
static constexpr uint32_t CURSOR_WIFI_TIMEOUT_MS = 5000;
static constexpr int CURSOR_BAR_MARGIN_X = 5;

enum class CursorPhase {
    IDLE,
    WIFI,
    FETCHING,
    READY,
    ERROR,
};

enum class CursorPage {
    SUMMARY = 0,
    CHART_7 = 1,
    CHART_30 = 2,
};

enum class CursorFetchMode {
    FULL,
    PERIOD,
    CHART,
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
static CursorPage g_page = CursorPage::SUMMARY;
static CursorFetchMode g_fetch_mode = CursorFetchMode::FULL;
static bool g_screen_ready = false;
static bool g_fetch_pending = false;
static bool g_silent_fetch = false;
static char g_error_msg[48] = "";
static char g_status_msg[32] = "";
static char g_cookie[CURSOR_API_KEY_MAX + 64] = "";
static int g_user_id = 0;
static CursorUsageData g_usage{};
static uint32_t g_last_period_fetch_ms = 0;
static uint32_t g_last_chart_7_fetch_ms = 0;
static uint32_t g_last_chart_30_fetch_ms = 0;
static uint32_t g_fetch_step = 0;
static float g_chart_7_cents[7]{};
static float g_chart_30_cents[30]{};
static bool g_chart_7_ready = false;
static bool g_chart_30_ready = false;
static int g_chart_fetch_days = 7;
static uint32_t g_last_activity_ms = 0;
static uint32_t g_last_scheduled_refresh_ms = 0;
static bool g_periodic_refresh_active = false;

static void beginCursorFetch(const CursorFetchMode mode);
static void beginCursorChartFetch(const int days, const bool silent);

// 记录用户操作，重置空闲刷新计时
static void noteCursorActivity() {
  g_last_activity_ms = millis();
  g_periodic_refresh_active = false;
}

// 拉取结束后断开 WiFi
static void finishCursorWifi() {
  releaseConfigWifi();
}

// 是否到达空闲首次刷新或周期刷新时间点
static bool shouldScheduleCursorRefresh() {
  if (g_phase != CursorPhase::READY || !g_usage.valid) {
    return false;
  }
  const uint32_t now = millis();
  const uint32_t idle_ms = now - g_last_activity_ms;
  if (!g_periodic_refresh_active) {
    return idle_ms >= CURSOR_IDLE_TRIGGER_MS;
  }
  return now - g_last_scheduled_refresh_ms >= CURSOR_REFRESH_INTERVAL_MS;
}

// 触发后台静默刷新（按需连 WiFi）
static void triggerCursorIdleRefresh() {
  const uint32_t now = millis();
  if (!g_periodic_refresh_active) {
    g_periodic_refresh_active = true;
  }
  g_last_scheduled_refresh_ms = now;

  if (g_page == CursorPage::SUMMARY) {
    beginCursorFetch(CursorFetchMode::PERIOD);
  } else if (g_page == CursorPage::CHART_7 && g_chart_7_ready) {
    beginCursorChartFetch(7, true);
  } else if (g_page == CursorPage::CHART_30 && g_chart_30_ready) {
    beginCursorChartFetch(30, true);
  }
}

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
static void accumulateEventDay(const int64_t ts_ms, const float cents, const int range_days,
                               float* daily_out) {
  if (ts_ms <= 0 || range_days <= 0 || range_days > CURSOR_DAYS_MAX || daily_out == nullptr) {
    return;
  }

  const time_t event_sec = static_cast<time_t>(ts_ms / 1000);
  for (int i = 0; i < range_days; i++) {
    const time_t start = dayStartEpoch(range_days - 1 - i);
    const time_t end = start + 86400;
    if (event_sec >= start && event_sec < end) {
      daily_out[i] += cents;
      return;
    }
  }
}

// 拉取每日用量（分页事件）
static bool fetchDailyUsage(const int range_days, float* daily_out) {
  if (daily_out == nullptr) {
    return false;
  }
  const int64_t end_ms = static_cast<int64_t>(time(nullptr)) * 1000LL;
  const time_t start_sec = dayStartEpoch(range_days - 1);
  if (start_sec <= 0) {
    return false;
  }
  const int64_t start_ms = static_cast<int64_t>(start_sec) * 1000LL;

  memset(daily_out, 0, sizeof(float) * static_cast<size_t>(range_days));

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
      accumulateEventDay(ts, cents, range_days, daily_out);
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
  g_last_period_fetch_ms = millis();
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

static constexpr int CURSOR_BAR_LABEL_PAD = 3;
static constexpr int CURSOR_BAR_LABEL_H = CURSOR_BAR_LABEL_PAD + 8 + CURSOR_BAR_LABEL_PAD;

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

// 计算柱体布局（7 天留间隙，30 天均分）
static void cursorBarLayout(const int x, const int w, const int days, const int gap, const int i,
                            int& bar_x, int& bar_w) {
  if (gap > 0 && days > 1) {
    const int total_gap = gap * (days - 1);
    bar_w = (w - total_gap) / days;
    bar_x = x + i * (bar_w + gap);
    return;
  }
  bar_x = x + (i * w) / days;
  const int next_x = x + ((i + 1) * w) / days;
  bar_w = next_x - bar_x;
}

// 绘制每日 bar 图；daily_cents 为 null 时仅画空框
static void drawDailyBars(const int x, const int y, const int w, const int bar_h, const int days,
                          const float* daily_cents, const int gap, const bool draw_labels) {
  if (days <= 0 || bar_h <= 0 || w <= 0) {
    return;
  }

  float max_val = 0.01f;
  if (daily_cents != nullptr) {
    for (int i = 0; i < days; i++) {
      if (daily_cents[i] > max_val) {
        max_val = daily_cents[i];
      }
    }
  }

  for (int i = 0; i < days; i++) {
    int bar_x = 0;
    int bar_w = 0;
    cursorBarLayout(x, w, days, gap, i, bar_x, bar_w);

    if (daily_cents != nullptr) {
      const int fill_h = static_cast<int>(bar_h * (daily_cents[i] / max_val));
      const int by = y + bar_h - fill_h;
      const uint16_t color = (i == days - 1) ? APP_COLOR_OK : CYAN;
      if (fill_h > 0) {
        M5Cardputer.Display.fillRect(bar_x, by, bar_w, fill_h, color);
      }
    }
    M5Cardputer.Display.drawRect(bar_x, y, bar_w, bar_h, DARKGREY);
  }

  if (!draw_labels) {
    return;
  }

  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setTextColor(WHITE, BLACK);
  const int label_y = y + bar_h + CURSOR_BAR_LABEL_PAD;
  for (int i = 0; i < days; i++) {
    int bar_x = 0;
    int bar_w = 0;
    cursorBarLayout(x, w, days, gap, i, bar_x, bar_w);

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
    M5Cardputer.Display.setCursor(tx, label_y);
    M5Cardputer.Display.print(lbl);
  }
}

// 柱状图区域居中 loading 叠层
static void drawChartLoadingOverlay(const int x, const int y, const int w, const int h) {
  M5Cardputer.Display.setTextSize(2);
  M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
  const char* text = "loading...";
  const int tw = M5Cardputer.Display.textWidth(text);
  M5Cardputer.Display.setCursor(x + (w - tw) / 2, y + (h - 16) / 2);
  M5Cardputer.Display.print(text);
}

static void formatMoney(const int cents, char* buf, const size_t buf_size) {
  const int dollars = cents / 100;
  const int frac = abs(cents % 100);
  snprintf(buf, buf_size, "$%d.%02d", dollars, frac);
}

static void drawCursorHints() {
  const int hint_y = M5Cardputer.Display.height() - 12;
  M5Cardputer.Display.fillRect(APP_CONTENT_X, hint_y, 236, 12, BLACK);
  const char* page_text = "usage";
  if (g_page == CursorPage::CHART_7) {
    page_text = "7d";
  } else if (g_page == CursorPage::CHART_30) {
    page_text = "30d";
  }
  const KeyHintItem items[] = {
      {'[', "prev"},
      {']', "next"},
      {'r', "refresh"},
  };
  drawKeyHintsRow(APP_CONTENT_X, hint_y, items, 3, 1, APP_COLOR_HINT);
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
  M5Cardputer.Display.setCursor(APP_CONTENT_X + 120, hint_y);
  M5Cardputer.Display.print(page_text);
}

static void drawCursorSummaryPage(const int y) {
  const int screen_h = M5Cardputer.Display.height();
  const int screen_w = M5Cardputer.Display.width();
  const int content_w = screen_w - APP_CONTENT_X * 2;
  const int hint_h = 12;
  const int area_bottom = screen_h - hint_h;
  const int area_h = area_bottom - y;
  constexpr int text_sz = 2;
  constexpr int bar_h = 16;
  char buf[24];

  M5Cardputer.Display.setTextSize(text_sz);
  const int label_gap = M5Cardputer.Display.textWidth(" ");
  const int auto_w = M5Cardputer.Display.textWidth("Auto");
  const int api_w = M5Cardputer.Display.textWidth("API");
  const int label_w = (auto_w > api_w ? auto_w : api_w) + label_gap;
  const int pct_reserve = M5Cardputer.Display.textWidth("100%") + label_gap;

  const bool has_reset = g_usage.reset_date[0] != '\0';
  const int row_count = g_usage.limit_cents > 0 ? (has_reset ? 4 : 3) : 2;
  const int row_h = area_h / row_count;

  auto drawBarRow = [&](const int row, const char* label, const float pct, const uint16_t color) {
    const int bar_y = y + row * row_h + (row_h - bar_h) / 2;
    M5Cardputer.Display.setTextSize(text_sz);
    M5Cardputer.Display.setTextColor(INFO_LABEL_COLOR, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, bar_y);
    M5Cardputer.Display.print(label);
    snprintf(buf, sizeof(buf), "%.0f%%", pct);
    drawPctBar(APP_CONTENT_X + label_w, bar_y, content_w - label_w - pct_reserve, bar_h, pct, color);
    M5Cardputer.Display.setTextColor(INFO_VALUE_COLOR, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X + content_w - pct_reserve + label_gap, bar_y);
    M5Cardputer.Display.print(buf);
  };

  drawBarRow(0, "Auto", 100.0f - g_usage.auto_pct, APP_COLOR_OK);
  drawBarRow(1, "API", 100.0f - g_usage.api_pct, ORANGE);

  if (g_usage.limit_cents > 0) {
    char used_s[16];
    char left_s[16];
    formatMoney(g_usage.used_cents, used_s, sizeof(used_s));
    formatMoney(g_usage.remaining_cents, left_s, sizeof(left_s));

    const int text_y = y + row_h * 2 + (row_h - INFO_LINE_H_2X) / 2;
    M5Cardputer.Display.setTextSize(text_sz);
    M5Cardputer.Display.setTextColor(INFO_LABEL_COLOR, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, text_y);
    M5Cardputer.Display.print("used: ");
    M5Cardputer.Display.setTextColor(INFO_VALUE_COLOR, BLACK);
    M5Cardputer.Display.print(used_s);
    M5Cardputer.Display.print("/");
    M5Cardputer.Display.print(left_s);

    if (has_reset) {
      const int reset_y = y + row_h * 3 + (row_h - INFO_LINE_H_2X) / 2;
      M5Cardputer.Display.setTextColor(INFO_LABEL_COLOR, BLACK);
      M5Cardputer.Display.setCursor(APP_CONTENT_X, reset_y);
      M5Cardputer.Display.print("reset: ");
      M5Cardputer.Display.setTextColor(ORANGE, BLACK);
      M5Cardputer.Display.print(g_usage.reset_date);
    }
  } else {
    const int text_y = y + row_h * 2 + (row_h - INFO_LINE_H_2X) / 2;
    snprintf(buf, sizeof(buf), "%.0f%%", 100.0f - g_usage.api_pct);
    drawInfoLineAt(APP_CONTENT_X, text_y, "api left", buf, text_sz);
  }
}

static void drawCursorChartPage(const int days, const float* daily_cents, const bool ready) {
  const int y = APP_CONTENT_Y;
  const int screen_w = M5Cardputer.Display.width();
  const int chart_x = CURSOR_BAR_MARGIN_X;
  const int chart_w = screen_w - CURSOR_BAR_MARGIN_X * 2;
  const int bottom_hint_h = 12;
  const int chart_h =
      M5Cardputer.Display.height() - y - bottom_hint_h - CURSOR_BAR_LABEL_H;
  const int gap = (days == 7) ? 3 : 0;

  if (chart_h >= 20) {
    drawDailyBars(chart_x, y, chart_w, chart_h, days, ready ? daily_cents : nullptr, gap, ready);
    if (!ready) {
      drawChartLoadingOverlay(chart_x, y, chart_w, chart_h);
    }
  }
  drawCursorHints();
}

void drawCursorApp() {
  redrawCursorContent();

  int y = APP_CONTENT_Y;

  // 静默刷新时不打断当前界面
  if (!g_silent_fetch &&
      (g_phase == CursorPhase::WIFI || g_phase == CursorPhase::FETCHING)) {
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
    const KeyHintItem items[] = {{'r', "retry"}};
    drawKeyHintsRow(APP_CONTENT_X, y, items, 1, 2, APP_COLOR_HINT);
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

  if (g_page == CursorPage::SUMMARY) {
    drawCursorSummaryPage(y);
    drawCursorHints();
    return;
  }

  if (g_page == CursorPage::CHART_7) {
    drawCursorChartPage(7, g_chart_7_cents, g_chart_7_ready);
    return;
  }

  drawCursorChartPage(30, g_chart_30_cents, g_chart_30_ready);
}

static void beginCursorFetch(const CursorFetchMode mode) {
  g_fetch_mode = mode;
  g_silent_fetch = (mode != CursorFetchMode::FULL);
  g_fetch_pending = true;
  g_fetch_step = 0;
  g_error_msg[0] = '\0';
  g_status_msg[0] = '\0';

  if (mode == CursorFetchMode::FULL) {
    g_phase = CursorPhase::WIFI;
    g_usage.valid = false;
    g_chart_7_ready = false;
    g_chart_30_ready = false;
    drawCursorApp();
    return;
  }

  if (mode == CursorFetchMode::PERIOD) {
    return;
  }

  if (mode == CursorFetchMode::CHART) {
    g_chart_fetch_days = (g_page == CursorPage::CHART_30) ? 30 : 7;
  }
}

static void beginCursorChartFetch(const int days, const bool silent) {
  g_chart_fetch_days = days;
  if (!silent) {
    if (days == 7) {
      g_chart_7_ready = false;
    } else {
      g_chart_30_ready = false;
    }
    g_fetch_mode = CursorFetchMode::CHART;
    g_silent_fetch = false;
    g_fetch_pending = true;
    g_fetch_step = 0;
    strncpy(g_status_msg, "chart...", sizeof(g_status_msg));
    drawCursorApp();
    return;
  }
  beginCursorFetch(CursorFetchMode::CHART);
}

static void cursorPageNav(const int delta) {
  if (delta == 0) {
    return;
  }
  const int next = static_cast<int>(g_page) + delta;
  if (next < 0 || next > 2) {
    return;
  }
  g_page = static_cast<CursorPage>(next);

  if (g_page == CursorPage::CHART_7 && !g_chart_7_ready && g_phase == CursorPhase::READY &&
      g_user_id > 0) {
    beginCursorChartFetch(7, false);
    return;
  }
  if (g_page == CursorPage::CHART_30 && !g_chart_30_ready && g_phase == CursorPhase::READY &&
      g_user_id > 0) {
    beginCursorChartFetch(30, false);
    return;
  }
  drawCursorApp();
}

void enterCursorApp() {
  g_screen_ready = false;
  g_page = CursorPage::SUMMARY;
  g_periodic_refresh_active = false;
  g_last_activity_ms = millis();
  beginCursorFetch(CursorFetchMode::FULL);
}

void updateCursorApp() {
  if (!g_fetch_pending) {
    if (shouldScheduleCursorRefresh()) {
      triggerCursorIdleRefresh();
    }
    return;
  }

  if (g_fetch_step == 0) {
    if (!ensureConfigWifi(CURSOR_WIFI_TIMEOUT_MS)) {
      g_phase = CursorPhase::ERROR;
      strncpy(g_error_msg, "wifi fail", sizeof(g_error_msg));
      g_fetch_pending = false;
      g_silent_fetch = false;
      finishCursorWifi();
      drawCursorApp();
      return;
    }
    quickSyncTime();
    if (!g_silent_fetch) {
      g_phase = CursorPhase::FETCHING;
      drawCursorApp();
    }
    // 已有 user_id 的图表刷新可跳过鉴权
    if (g_fetch_mode == CursorFetchMode::CHART && g_user_id > 0) {
      g_fetch_step = 2;
    } else {
      g_fetch_step = 1;
    }
    return;
  }

  if (g_fetch_step == 1) {
    if (!g_silent_fetch) {
      g_phase = CursorPhase::FETCHING;
      drawCursorApp();
    }
    if (fetchCursorAuthPeriod()) {
      g_phase = CursorPhase::READY;
      g_error_msg[0] = '\0';
      if (g_fetch_mode == CursorFetchMode::PERIOD) {
        g_fetch_pending = false;
        g_silent_fetch = false;
        finishCursorWifi();
        drawCursorApp();
        return;
      }
      if (g_fetch_mode == CursorFetchMode::FULL) {
        g_fetch_pending = false;
        g_silent_fetch = false;
        finishCursorWifi();
        drawCursorApp();
        return;
      }
      if (g_fetch_mode == CursorFetchMode::CHART) {
        g_fetch_step = 2;
        return;
      }
    } else if (!g_silent_fetch) {
      g_phase = CursorPhase::ERROR;
      g_fetch_pending = false;
      g_silent_fetch = false;
      finishCursorWifi();
      drawCursorApp();
      return;
    } else {
      g_fetch_pending = false;
      g_silent_fetch = false;
      finishCursorWifi();
      return;
    }
  }

  if (g_fetch_step == 2) {
    float* target = (g_chart_fetch_days == 30) ? g_chart_30_cents : g_chart_7_cents;
    if (fetchDailyUsage(g_chart_fetch_days, target)) {
      if (g_chart_fetch_days == 30) {
        g_chart_30_ready = true;
        g_last_chart_30_fetch_ms = millis();
      } else {
        g_chart_7_ready = true;
        g_last_chart_7_fetch_ms = millis();
      }
      g_error_msg[0] = '\0';
    } else if (!g_silent_fetch) {
      strncpy(g_error_msg, "chart fail", sizeof(g_error_msg));
    }
    g_status_msg[0] = '\0';
    g_fetch_pending = false;
    g_silent_fetch = false;
    finishCursorWifi();
    if (g_phase != CursorPhase::ERROR) {
      g_phase = CursorPhase::READY;
    }
    drawCursorApp();
  }
}

void handleCursorApp(const String& key) {
  noteCursorActivity();
  if (key == "r") {
    beginCursorFetch(CursorFetchMode::FULL);
    return;
  }
  if (key == "[") {
    cursorPageNav(-1);
    return;
  }
  if (key == "]") {
    cursorPageNav(1);
  }
}
