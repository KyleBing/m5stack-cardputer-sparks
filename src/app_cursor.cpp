#include "app_cursor.h"
#include "app_colors.h"
#include "app_common.h"
#include "app_config.h"
#include "app_connectivity.h"
#include "app_header.h"
#include "M5Cardputer.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mbedtls/base64.h>
#include <time.h>

static constexpr const char* CURSOR_HOST = "https://cursor.com";
static constexpr int CURSOR_DAYS_MAX = 31;
static constexpr int CURSOR_HOURS = 24;
static constexpr int CURSOR_CHART_PAGE_SIZE = 200; // 每页条数；配合流式解析避免 OOM
static constexpr uint32_t CURSOR_IDLE_TRIGGER_MS = 60000;
static constexpr uint32_t CURSOR_REFRESH_INTERVAL_MS = 300000;
static constexpr uint32_t CURSOR_SLOW_LOOP_IDLE_MS = 300000; // 无操作 5 分钟后主循环 1s 一拍
static constexpr uint32_t CURSOR_WIFI_TIMEOUT_MS = 5000;
static constexpr int CURSOR_BAR_MARGIN_X = 5;
static constexpr int CURSOR_SUMMARY_PAD_X = 10; // 摘要页进度条左右 padding
static constexpr int CURSOR_CHART_ETA_24_SEC = 3;   // 当天 24h 预计加载时间
static constexpr int CURSOR_CHART_ETA_7_SEC = 10;   // 图表 7 天预计加载时间
static constexpr int CURSOR_CHART_ETA_30_SEC = 25;  // 图表 30 天预计加载时间
// 柱体外框：比 DARKGREY 更暗，避免边框抢眼
static constexpr uint16_t CURSOR_BAR_BORDER = 0x3186;

enum class CursorPhase {
    IDLE,
    WIFI,
    FETCHING,
    READY,
    ERROR,
};

enum class CursorPage {
    SUMMARY = 0,
    LAST = 1,      // 上次请求
    CHART_24 = 2,
    CHART_7 = 3,
    CHART_30 = 4,
};
static constexpr int CURSOR_PAGE_COUNT = 5;

enum class CursorFetchMode {
    FULL,
    PERIOD,
    CHART,
    LAST, // 仅刷新最近请求列表
};

struct CursorUsageData {
    bool valid;
    float auto_pct;
    float api_pct;
    // On-Demand（按量）：来自 spendLimitUsage / individualUsage.onDemand
    bool ond_valid;
    int ond_used_cents;
    int ond_limit_cents; // 0 = 无上限 / null
    char reset_date[12];
    time_t reset_epoch; // billingCycleEnd，用于算剩余天数
    float daily_cents[CURSOR_DAYS_MAX];
    int day_count;
};

// 最近 usage event：时间 / 模型 / token 量
struct CursorLastRequest {
    bool included;
    char time_str[20]; // 2026-07-17 11:56:46
    char model[40];
    char tokens_str[12];
};

static constexpr int CURSOR_LAST_MAX = 5;
static constexpr int CURSOR_LAST_PER_PAGE = 1; // usage 每页一条，方便阅读
static constexpr int CURSOR_LAST_STATUS_H = 12;  // header 下分页/状态行（含 [ ] tip）

static CursorPhase g_phase = CursorPhase::IDLE;
static CursorPage g_page = CursorPage::SUMMARY;
static CursorFetchMode g_fetch_mode = CursorFetchMode::FULL;
static bool g_screen_ready = false;
static bool g_fetch_pending = false;
static bool g_silent_fetch = false;
static char g_error_msg[48] = "";
static char g_status_msg[32] = "";
static char g_cookie[CURSOR_TOKEN_MAX + 64] = "";
static int g_user_id = 0;
static CursorUsageData g_usage{};
static CursorLastRequest g_last_reqs[CURSOR_LAST_MAX]{};
static int g_last_req_count = 0;
static int g_last_page = 0;            // usage 分页（0-based）
static bool g_last_refreshing = false; // r 软刷新中（保留旧列表）
static bool g_want_last_fetch = false; // 切到 last 时若尚未就绪则排队拉取
static uint32_t g_last_period_fetch_ms = 0;
static bool g_was_slow_loop = false; // 1s 慢循环态，用于右上角蓝点
static int64_t g_last_clock_minute = -1; // last 状态栏时钟的最近显示分钟
static uint32_t g_last_chart_24_fetch_ms = 0;
static uint32_t g_last_chart_7_fetch_ms = 0;
static uint32_t g_last_chart_30_fetch_ms = 0;
static float g_chart_24_cents[CURSOR_HOURS]{};
static float g_chart_7_cents[7]{};
static float g_chart_30_cents[30]{};
static int g_chart_24_highlight_hour = -1; // 24h 图中当前高亮的本地小时
static bool g_chart_24_ready = false;
static bool g_chart_7_ready = false;
static bool g_chart_30_ready = false;
static char g_chart_24_error[32] = "";
static char g_chart_7_error[32] = "";
static char g_chart_30_error[32] = "";
static int g_chart_fetch_days = 7;
static uint32_t g_chart_fetch_start_ms = 0;
static int g_chart_fetch_eta_sec = CURSOR_CHART_ETA_7_SEC;
static int g_last_countdown_sec = -1;
// 图表分页拉取状态（后台 task 内分页，主循环可随时取消）
static int g_chart_http_page = 1;
static int g_chart_http_fetched = 0;
static int g_chart_http_total = 0;
static float* g_chart_http_target = nullptr;
static int64_t g_chart_http_start_ms = 0;
static int64_t g_chart_http_end_ms = 0;
static bool g_chart_http_active = false;
static uint32_t g_last_activity_ms = 0;
static uint32_t g_last_scheduled_refresh_ms = 0;
static bool g_periodic_refresh_active = false;
// 熄屏：关背光/面板，后台仍按 5 分钟刷新
static bool g_display_blanked = false;
static uint8_t g_saved_brightness = 30;
// Help 页
static bool g_help_visible = false;
// FreeRTOS：网络请求与主循环键扫分离
static constexpr uint32_t CURSOR_FETCH_STACK = 8192; // FreeRTOS 单位：字（≈32KB）
static volatile bool g_task_running = false;
static volatile uint32_t g_fetch_gen = 0;
static volatile bool g_need_redraw = false;
static volatile bool g_queue_other_chart = false;
static TaskHandle_t g_fetch_task = nullptr;

static void beginCursorFetch(const CursorFetchMode mode);
static void beginCursorChartFetch(const int days, const bool silent);
static void beginCursorLastRefresh();
static void startChartFetchTimer(const int days);
static int chartFetchCountdownSec();
static void refreshChartLoadingFrame();
static char* chartErrorBuf(const int days);
static void blankCursorDisplay();
static void wakeCursorDisplay(bool redraw);
static void drawCursorHelpPage();
static void scheduleCursorFetchTask();
static void cursorFetchTaskFn(void* arg);
static void waitCursorFetchTaskDone(uint32_t timeout_ms);
static bool ensureCursorWifi(uint32_t timeout_ms, uint32_t gen);

// 记录用户操作，重置空闲刷新计时
static void noteCursorActivity() {
  g_last_activity_ms = millis();
  g_periodic_refresh_active = false;
}

// 拉取结束断开 WiFi 省电；下次请求再 ensureConfigWifi。
// 后台 task 不调 Display 回调，仅关射频；header 由主循环 redraw 刷新。
static void finishCursorWifi() {
  g_chart_http_active = false;
  g_chart_http_target = nullptr;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  g_need_redraw = true;
}

static void releaseCursorWifi() {
  g_chart_http_active = false;
  g_chart_http_target = nullptr;
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
  } else if (g_page == CursorPage::LAST) {
    // last 页空闲刷新只重拉列表
    beginCursorLastRefresh();
  } else if (g_page == CursorPage::CHART_24 && g_chart_24_ready) {
    beginCursorChartFetch(24, true);
  } else if (g_page == CursorPage::CHART_7 && g_chart_7_ready) {
    beginCursorChartFetch(7, true);
  } else if (g_page == CursorPage::CHART_30 && g_chart_30_ready) {
    beginCursorChartFetch(30, true);
  }
}

// 仅刷新内容区；last / 图表页 header 带副标题
static void redrawCursorContent() {
    const char* accent = nullptr;
    if (g_page == CursorPage::LAST) {
        accent = "last";
    } else if (g_page == CursorPage::CHART_24) {
        accent = "24h";
    } else if (g_page == CursorPage::CHART_7) {
        accent = "7d";
    } else if (g_page == CursorPage::CHART_30) {
        accent = "30d";
    }

    if (!g_screen_ready) {
        if (accent != nullptr) {
            beginAppScreenAccentWithBattery("Cursor ", accent, APP_COLOR_LABEL);
        } else {
            beginAppScreenWithBattery("Cursor");
        }
        g_screen_ready = true;
        return;
    }

    clearAppContentArea();
    // 翻页时同步 header 副标题
    if (accent != nullptr) {
        drawAppScreenHeaderAccentWithBattery("Cursor ", accent, APP_COLOR_LABEL);
    } else {
        drawAppScreenHeaderWithBattery("Cursor");
    }
}

// BtnA：灭屏（后台刷新继续）
static void blankCursorDisplay() {
  if (g_display_blanked) {
    return;
  }
  g_saved_brightness = M5Cardputer.Display.getBrightness();
  if (g_saved_brightness == 0) {
    g_saved_brightness = 30;
  }
  M5Cardputer.Display.sleep();
  M5Cardputer.Display.waitDisplay();
  M5Cardputer.Display.setBrightness(0);
  g_display_blanked = true;
}

// 任意键 / BtnA：亮屏；redraw 时用内存最新数据重画
static void wakeCursorDisplay(const bool redraw) {
  if (!g_display_blanked) {
    return;
  }
  M5Cardputer.Display.wakeup();
  M5Cardputer.Display.setBrightness(g_saved_brightness);
  g_display_blanked = false;
  if (redraw) {
    g_screen_ready = false;
    drawCursorApp();
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
static bool buildCursorCookie(const char* token, char* cookie, const size_t cookie_size) {
    if (token == nullptr || token[0] == '\0' || cookie == nullptr || cookie_size < 32) {
        return false;
    }

    if (strstr(token, "::") != nullptr || strstr(token, "%3A%3A") != nullptr) {
        snprintf(cookie, cookie_size, "WorkosCursorSessionToken=%s", token);
        String encoded(cookie);
        encoded.replace("::", "%3A%3A");
        strncpy(cookie, encoded.c_str(), cookie_size - 1);
        cookie[cookie_size - 1] = '\0';
        return true;
    }

    char sub[96];
    if (!extractJwtSub(token, sub, sizeof(sub))) {
        return false;
    }

    snprintf(cookie, cookie_size, "WorkosCursorSessionToken=%s%%3A%%3A%s", sub, token);
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
    configTzTime(getAppTimezone(), "ntp.aliyun.com", "pool.ntp.org");
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

// 解析周期用量（含 On-Demand）
static void parsePeriodUsage(const JsonDocument& doc, CursorUsageData& out) {
    out.ond_valid = false;
    out.ond_used_cents = 0;
    out.ond_limit_cents = 0;

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
    }

    // On-Demand：优先 spendLimitUsage（get-current-period-usage）
    JsonVariantConst slu = doc["spendLimitUsage"];
    if (!slu.isNull()) {
        out.ond_valid = true;
        // 个人优先，否则团队池
        const int ind_used = slu["individualUsed"] | -1;
        const int pool_used = slu["pooledUsed"] | -1;
        if (ind_used >= 0) {
            out.ond_used_cents = ind_used;
            out.ond_limit_cents = slu["individualLimit"] | 0;
        } else if (pool_used >= 0) {
            out.ond_used_cents = pool_used;
            out.ond_limit_cents = slu["pooledLimit"] | 0;
        } else {
            out.ond_used_cents = slu["totalSpend"] | 0;
            out.ond_limit_cents = slu["individualLimit"] | slu["pooledLimit"] | 0;
        }
    }

    // usage-summary：individualUsage.onDemand
    JsonVariantConst individual = doc["individualUsage"];
    if (!individual.isNull()) {
        JsonVariantConst ond = individual["onDemand"];
        if (!ond.isNull()) {
            const bool en = ond["enabled"] | false;
            const int used = ond["used"] | 0;
            if (en || used > 0 || out.ond_valid) {
                out.ond_valid = true;
                out.ond_used_cents = used;
                if (!ond["limit"].isNull() && ond["limit"].is<int>()) {
                    out.ond_limit_cents = ond["limit"] | 0;
                } else if (!ond["limit"].isNull() && ond["limit"].is<long>()) {
                    out.ond_limit_cents = static_cast<int>(ond["limit"].as<long>());
                }
                // limit 为 null 时保持 0 = 无上限
            }
        }
    }

    out.reset_epoch = 0;
    out.reset_date[0] = '\0';
    const char* reset_raw = doc["billingCycleEnd"];
    if (reset_raw != nullptr) {
        if (isdigit(static_cast<unsigned char>(reset_raw[0]))) {
            const int64_t ms = strtoll(reset_raw, nullptr, 10);
            if (ms > 0) {
                const time_t t = static_cast<time_t>(ms / 1000);
                out.reset_epoch = t;
                struct tm utc_tm;
                if (gmtime_r(&t, &utc_tm) != nullptr) {
                    strftime(out.reset_date, sizeof(out.reset_date), "%m-%d", &utc_tm);
                }
            }
        } else {
            strncpy(out.reset_date, reset_raw, sizeof(out.reset_date) - 1);
            out.reset_date[10] = '\0';
            // ISO 日期：尽量解析出 epoch，供剩余天数
            struct tm parsed {};
            if (sscanf(reset_raw, "%d-%d-%d", &parsed.tm_year, &parsed.tm_mon, &parsed.tm_mday) == 3) {
                parsed.tm_year -= 1900;
                parsed.tm_mon -= 1;
                parsed.tm_isdst = 0;
                const time_t t = mktime(&parsed);
                if (t > 0) {
                    out.reset_epoch = t;
                }
            }
            if (strlen(out.reset_date) >= 10) {
                out.reset_date[5] = out.reset_date[8];
                out.reset_date[6] = out.reset_date[9];
                out.reset_date[7] = '\0';
            }
        }
    }
}

// 距 billing 重置还剩几天（向上取整天；未知返回 -1）
static int cursorResetDaysLeft() {
  if (g_usage.reset_epoch <= 0) {
    return -1;
  }
  const time_t now = time(nullptr);
  if (now <= 1600000000) {
    return -1;
  }
  const int64_t diff = static_cast<int64_t>(g_usage.reset_epoch) - static_cast<int64_t>(now);
  if (diff <= 0) {
    return 0;
  }
  return static_cast<int>((diff + 86399) / 86400);
}

// token 量格式：374.7K / 1.2M
static void formatTokenCount(const uint64_t n, char* buf, const size_t buf_size) {
  if (buf == nullptr || buf_size == 0) {
    return;
  }
  if (n >= 1000000ULL) {
    const float m = static_cast<float>(n) / 1000000.0f;
    snprintf(buf, buf_size, "%.1fM", static_cast<double>(m));
  } else if (n >= 1000ULL) {
    const float k = static_cast<float>(n) / 1000.0f;
    snprintf(buf, buf_size, "%.1fK", static_cast<double>(k));
  } else {
    snprintf(buf, buf_size, "%llu", static_cast<unsigned long long>(n));
  }
}

// 解析单条 usage event → CursorLastRequest
static void parseCursorLastEvent(JsonObject ev, CursorLastRequest& out) {
  out = {};
  const char* model = ev["model"] | "";
  strncpy(out.model, model, sizeof(out.model) - 1);

  const char* kind = ev["kind"] | "";
  out.included = (strstr(kind, "INCLUDED") != nullptr) || (strstr(kind, "Included") != nullptr);

  JsonObject tu = ev["tokenUsage"];
  uint64_t tokens = 0;
  if (!tu.isNull()) {
    tokens += static_cast<uint64_t>(tu["inputTokens"] | 0);
    tokens += static_cast<uint64_t>(tu["outputTokens"] | 0);
    tokens += static_cast<uint64_t>(tu["cacheWriteTokens"] | 0);
    tokens += static_cast<uint64_t>(tu["cacheReadTokens"] | 0);
  }
  formatTokenCount(tokens, out.tokens_str, sizeof(out.tokens_str));

  int64_t ts_ms = 0;
  if (ev["timestamp"].is<const char*>()) {
    ts_ms = strtoll(ev["timestamp"].as<const char*>(), nullptr, 10);
  } else {
    ts_ms = ev["timestamp"].as<int64_t>();
  }
  if (ts_ms > 0) {
    const time_t t = static_cast<time_t>(ts_ms / 1000);
    struct tm local_tm;
    if (localtime_r(&t, &local_tm) != nullptr) {
      strftime(out.time_str, sizeof(out.time_str), "%Y-%m-%d %H:%M:%S", &local_tm);
    }
  }
  if (out.time_str[0] == '\0') {
    strncpy(out.time_str, "-", sizeof(out.time_str) - 1);
  }
}

// 拉取最近 N 条请求，供 usage(last) 页展示
static bool fetchCursorLastRequest() {
  if (g_user_id <= 0 || g_cookie[0] == '\0') {
    return false;
  }
  const time_t now = time(nullptr);
  if (now <= 1600000000) {
    return false;
  }
  const int64_t end_ms = static_cast<int64_t>(now) * 1000LL;
  const int64_t start_ms = end_ms - 7LL * 86400LL * 1000LL;

  char body[200];
  snprintf(body, sizeof(body),
           "{\"teamId\":0,\"startDate\":\"%lld\",\"endDate\":\"%lld\",\"userId\":%d,"
           "\"page\":1,\"pageSize\":%d}",
           static_cast<long long>(start_ms), static_cast<long long>(end_ms), g_user_id,
           CURSOR_LAST_MAX);

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(15000);

  HTTPClient http;
  const String url = String(CURSOR_HOST) + "/api/dashboard/get-filtered-usage-events";
  if (!http.begin(client, url)) {
    return false;
  }
  http.setTimeout(15000);
  http.addHeader("Cookie", g_cookie);
  http.addHeader("Accept", "application/json");
  http.addHeader("User-Agent", "Cardputer/1.0");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Origin", "https://cursor.com");

  const int code = http.POST(body);
  if (code != 200) {
    http.end();
    Serial.printf("[cursor] last-req code=%d\n", code);
    return false;
  }

  JsonDocument filter;
  filter["usageEventsDisplay"][0]["timestamp"] = true;
  filter["usageEventsDisplay"][0]["model"] = true;
  filter["usageEventsDisplay"][0]["kind"] = true;
  filter["usageEventsDisplay"][0]["tokenUsage"]["inputTokens"] = true;
  filter["usageEventsDisplay"][0]["tokenUsage"]["outputTokens"] = true;
  filter["usageEventsDisplay"][0]["tokenUsage"]["cacheWriteTokens"] = true;
  filter["usageEventsDisplay"][0]["tokenUsage"]["cacheReadTokens"] = true;

  JsonDocument doc;
  const DeserializationError jerr =
      deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (jerr) {
    Serial.printf("[cursor] last-req json=%s\n", jerr.c_str());
    return false;
  }

  JsonArray events = doc["usageEventsDisplay"].as<JsonArray>();
  int n = 0;
  if (!events.isNull()) {
    for (JsonObject ev : events) {
      if (n >= CURSOR_LAST_MAX) {
        break;
      }
      parseCursorLastEvent(ev, g_last_reqs[n]);
      n++;
    }
  }
  g_last_req_count = n;
  const int pages =
      g_last_req_count > 0 ? (g_last_req_count + CURSOR_LAST_PER_PAGE - 1) / CURSOR_LAST_PER_PAGE : 1;
  if (g_last_page >= pages) {
    g_last_page = pages - 1;
  }
  if (g_last_page < 0) {
    g_last_page = 0;
  }
  return true;
}

// 将事件累计到每日数组（index 0 = 最早一天）
static void accumulateEventDay(const int64_t ts_ms, const float cents, const int range_days,
                               float* daily_out) {
  if (ts_ms <= 0 || range_days <= 0 || range_days > CURSOR_DAYS_MAX || daily_out == nullptr) {
    return;
  }

  const time_t event_sec = static_cast<time_t>(ts_ms / 1000);
  const time_t range_start = dayStartEpoch(range_days - 1);
  if (range_start <= 0 || event_sec < range_start) {
    return;
  }
  const int day_index = static_cast<int>((event_sec - range_start) / 86400);
  if (day_index >= 0 && day_index < range_days) {
    daily_out[day_index] += cents;
  }
}

// 将事件累计到当天 24 小时桶（index = 本地小时 0..23）
static void accumulateEventHour(const int64_t ts_ms, const float cents, float* hourly_out) {
  if (ts_ms <= 0 || hourly_out == nullptr) {
    return;
  }
  const time_t today_start = dayStartEpoch(0);
  if (today_start <= 0) {
    return;
  }
  const time_t event_sec = static_cast<time_t>(ts_ms / 1000);
  if (event_sec < today_start || event_sec >= today_start + 86400) {
    return;
  }
  const int hour = static_cast<int>((event_sec - today_start) / 3600);
  if (hour >= 0 && hour < CURSOR_HOURS) {
    hourly_out[hour] += cents;
  }
}

// 拉取每日用量：启动分页状态（不阻塞）
static char* chartErrorBuf(const int days) {
  if (days == 30) {
    return g_chart_30_error;
  }
  if (days == 24) {
    return g_chart_24_error;
  }
  return g_chart_7_error;
}

static bool beginChartPagedFetch(const int range_days, float* daily_out) {
  if (daily_out == nullptr) {
    return false;
  }
  const int64_t end_ms = static_cast<int64_t>(time(nullptr)) * 1000LL;
  // 24h：只请求当天；7/30：从 range 起点到现在
  const time_t start_sec =
      (range_days == 24) ? dayStartEpoch(0) : dayStartEpoch(range_days - 1);
  if (start_sec <= 0) {
    strncpy(chartErrorBuf(range_days), "time sync", 32);
    chartErrorBuf(range_days)[31] = '\0';
    return false;
  }

  const int bucket_count = (range_days == 24) ? CURSOR_HOURS : range_days;
  memset(daily_out, 0, sizeof(float) * static_cast<size_t>(bucket_count));
  chartErrorBuf(range_days)[0] = '\0';

  g_chart_http_target = daily_out;
  g_chart_http_page = 1;
  g_chart_http_fetched = 0;
  g_chart_http_total = 0;
  g_chart_http_start_ms = static_cast<int64_t>(start_sec) * 1000LL;
  g_chart_http_end_ms = end_ms;
  g_chart_http_active = true;
  return true;
}

// 写入图表错误（截断到 31 字符）
static void setChartError(const int days, const char* msg) {
  char* buf = chartErrorBuf(days);
  strncpy(buf, msg, 31);
  buf[31] = '\0';
}

// 每 tick 拉取一页；返回 1=完成 0=继续 -1=失败
// 流式解析：pageSize=200 时整包 getString 易空响应 → json EmptyInput
static int stepChartPagedFetch() {
  if (!g_chart_http_active || g_chart_http_target == nullptr) {
    return -1;
  }
  if (g_chart_http_page > 20) {
    g_chart_http_active = false;
    return 1;
  }

  // 分页请求前确认 WiFi 仍在（避免上一阶段过早 disconnect）
  if (WiFi.status() != WL_CONNECTED) {
    if (!ensureCursorWifi(CURSOR_WIFI_TIMEOUT_MS, g_fetch_gen)) {
      g_chart_http_active = false;
      setChartError(g_chart_fetch_days, "wifi lost");
      return -1;
    }
  }

  char body[180];
  snprintf(body, sizeof(body),
           "{\"teamId\":0,\"startDate\":\"%lld\",\"endDate\":\"%lld\",\"userId\":%d,"
           "\"page\":%d,\"pageSize\":%d}",
           static_cast<long long>(g_chart_http_start_ms),
           static_cast<long long>(g_chart_http_end_ms), g_user_id, g_chart_http_page,
           CURSOR_CHART_PAGE_SIZE);

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(20000);

  HTTPClient http;
  const String url = String(CURSOR_HOST) + "/api/dashboard/get-filtered-usage-events";
  if (!http.begin(client, url)) {
    g_chart_http_active = false;
    if (g_chart_http_page == 1) {
      setChartError(g_chart_fetch_days, "http begin");
    }
    return g_chart_http_page == 1 ? -1 : 1;
  }

  http.setTimeout(20000);
  http.addHeader("Cookie", g_cookie);
  http.addHeader("Accept", "application/json");
  http.addHeader("User-Agent", "Cardputer/1.0");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Origin", "https://cursor.com");

  const int code = http.POST(body);
  Serial.printf("[cursor] chart p%d days=%d code=%d heap=%u\n", g_chart_http_page,
                g_chart_fetch_days, code, ESP.getFreeHeap());

  if (code != 200) {
    http.end();
    g_chart_http_active = false;
    if (g_chart_http_page == 1) {
      if (WiFi.status() != WL_CONNECTED) {
        setChartError(g_chart_fetch_days, "wifi lost");
      } else if (code > 0) {
        char msg[32];
        snprintf(msg, sizeof(msg), "http %d", code);
        setChartError(g_chart_fetch_days, msg);
      } else {
        char msg[32];
        snprintf(msg, sizeof(msg), "net %d", code);
        setChartError(g_chart_fetch_days, msg);
      }
      return -1;
    }
    return 1; // 后续页失败则用已有数据
  }

  JsonDocument filter;
  filter["totalUsageEventsCount"] = true;
  filter["usageEventsDisplay"][0]["timestamp"] = true;
  filter["usageEventsDisplay"][0]["tokenUsage"]["totalCents"] = true;
  filter["usageEventsDisplay"][0]["chargedCents"] = true;

  JsonDocument doc;
  const DeserializationError jerr =
      deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();

  if (jerr) {
    Serial.printf("[cursor] chart json=%s heap=%u\n", jerr.c_str(), ESP.getFreeHeap());
    g_chart_http_active = false;
    if (g_chart_http_page == 1) {
      char msg[32];
      snprintf(msg, sizeof(msg), "json %s", jerr.c_str());
      setChartError(g_chart_fetch_days, msg);
      return -1;
    }
    return 1;
  }

  g_chart_http_total = doc["totalUsageEventsCount"] | 0;
  JsonArray events = doc["usageEventsDisplay"].as<JsonArray>();
  if (events.isNull() || events.size() == 0) {
    g_chart_http_active = false;
    return 1;
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
    if (g_chart_fetch_days == 24) {
      accumulateEventHour(ts, cents, g_chart_http_target);
    } else {
      accumulateEventDay(ts, cents, g_chart_fetch_days, g_chart_http_target);
    }
    g_chart_http_fetched++;
  }

  if (g_chart_http_fetched >= g_chart_http_total ||
      static_cast<int>(events.size()) < CURSOR_CHART_PAGE_SIZE) {
    g_chart_http_active = false;
    return 1;
  }
  g_chart_http_page++;
  return 0;
}

// 是否有图表拉取进行中（后台 task，可跨页）
static bool isChartFetchInProgress() {
  return g_chart_http_active ||
         ((g_fetch_pending || g_task_running) && g_fetch_mode == CursorFetchMode::CHART);
}

static bool isChartFetchInProgressFor(const int days) {
  return isChartFetchInProgress() && g_chart_fetch_days == days;
}

// 递增代数：使正在跑的 fetch task 丢弃结果
static void invalidateCursorFetch() {
  g_fetch_gen++;
  g_chart_http_active = false;
  g_chart_http_target = nullptr;
}

// 等待后台 task 退出（离开 App / 断网前）
static void waitCursorFetchTaskDone(const uint32_t timeout_ms) {
  const uint32_t deadline = millis() + timeout_ms;
  while (g_task_running && static_cast<int32_t>(millis() - deadline) < 0) {
    delay(20);
  }
}

// 可取消的 WiFi 连接：leave 递增 gen 后尽快返回，避免卡死 5s 并污染下次连网
static bool ensureCursorWifi(const uint32_t timeout_ms, const uint32_t gen) {
  const AppConfig& cfg = getAppConfig();
  if (!cfg.loaded || cfg.wifi_ssid[0] == '\0') {
    return false;
  }
  if (WiFi.status() == WL_CONNECTED && WiFi.SSID() == cfg.wifi_ssid) {
    return true;
  }

  // 先彻底关射频再开，避免上次 ESC 中途断连后 begin 立刻失败
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(80);
  if (gen != g_fetch_gen) {
    return false;
  }

  WiFi.mode(WIFI_STA);
  applyWifiRadioSleepPolicy();
  WiFi.begin(cfg.wifi_ssid, cfg.wifi_password);

  const uint32_t deadline = millis() + timeout_ms;
  while (WiFi.status() != WL_CONNECTED && static_cast<int32_t>(millis() - deadline) < 0) {
    if (gen != g_fetch_gen) {
      return false;
    }
    delay(50);
  }
  return WiFi.status() == WL_CONNECTED;
}

// 拉取认证与周期用量（较快，先展示摘要）
static bool fetchCursorAuthPeriod() {
  const AppConfig& cfg = getAppConfig();
  if (!cfg.loaded || cfg.cursor_token[0] == '\0') {
    strncpy(g_error_msg, "no token in cfg", sizeof(g_error_msg));
    return false;
  }
  if (!buildCursorCookie(cfg.cursor_token, g_cookie, sizeof(g_cookie))) {
    strncpy(g_error_msg, "bad session token", sizeof(g_error_msg));
    return false;
  }

  String response;
  int code = 0;

  strncpy(g_status_msg, "auth...", sizeof(g_status_msg));
  g_need_redraw = true;
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
  g_need_redraw = true;
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
  // last 列表改到切页时再拉，进入 App 不请求
  return true;
}

// 绘制横向进度条（仅边框，空白不填底色）
static void drawPctBar(const int x, const int y, const int w, const int h, const float pct,
                       const uint16_t color) {
  if (w <= 0 || h <= 0) {
    return;
  }
  float clamped = pct;
  if (clamped < 0.0f) {
    clamped = 0.0f;
  } else if (clamped > 100.0f) {
    clamped = 100.0f;
  }
  const int fill_w = static_cast<int>(w * (clamped / 100.0f));
  if (fill_w > 0) {
    M5Cardputer.Display.fillRect(x, y, fill_w, h, color);
  }
  M5Cardputer.Display.drawRect(x, y, w, h, DARKGREY);
}

static constexpr int CURSOR_BAR_LABEL_PAD = 3;
static constexpr int CURSOR_BAR_LABEL_H = CURSOR_BAR_LABEL_PAD + 8 + CURSOR_BAR_LABEL_PAD;

// 空间不足时稀疏显示日期（如 1 5 10 15 20 25 30）；24h 每 3 小时一个 label，末列标 24
static bool shouldShowBarDayLabel(const int i, const int days, const int bar_w) {
  if (days == 24) {
    return (i % 3) == 0 || i == days - 1;
  }
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

// 获取当前本地小时，系统时间尚未同步时不高亮。
static int currentLocalHour() {
  const time_t now = time(nullptr);
  if (now <= 1600000000) {
    return -1;
  }
  struct tm local_tm;
  return localtime_r(&now, &local_tm) != nullptr ? local_tm.tm_hour : -1;
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

  const int current_hour = days == 24 ? currentLocalHour() : -1;
  if (days == 24) {
    g_chart_24_highlight_hour = current_hour;
  }

  for (int i = 0; i < days; i++) {
    int bar_x = 0;
    int bar_w = 0;
    cursorBarLayout(x, w, days, gap, i, bar_x, bar_w);
    const bool is_current_hour = days == 24 && i == current_hour;

    if (daily_cents != nullptr) {
      const int fill_h = static_cast<int>(bar_h * (daily_cents[i] / max_val));
      const int by = y + bar_h - fill_h;
      const uint16_t color =
          (is_current_hour || (days != 24 && i == days - 1)) ? APP_COLOR_OK : CYAN;
      if (fill_h > 0) {
        M5Cardputer.Display.fillRect(bar_x, by, bar_w, fill_h, color);
      }
    }
    M5Cardputer.Display.drawRect(bar_x, y, bar_w, bar_h, CURSOR_BAR_BORDER);
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

    char lbl[4];
    bool show = shouldShowBarDayLabel(i, days, bar_w);
    if (days == 24) {
      // 末列标 24（一天结束）
      snprintf(lbl, sizeof(lbl), "%d", (i == days - 1) ? 24 : i);
    } else {
      const time_t day_ts = dayStartEpoch(days - 1 - i);
      int mday = i + 1;
      int month = 0;
      struct tm local_tm;
      if (day_ts > 0 && localtime_r(&day_ts, &local_tm) != nullptr) {
        mday = local_tm.tm_mday;
        month = local_tm.tm_mon + 1;
      }
      // 30 天图：每月 1 号改标月份，便于跨月辨认
      if (days == 30 && mday == 1 && month > 0) {
        show = true;
        snprintf(lbl, sizeof(lbl), "%d", month);
      } else {
        snprintf(lbl, sizeof(lbl), "%d", mday);
      }
    }
    if (!show) {
      continue;
    }
    const int tw = M5Cardputer.Display.textWidth(lbl);
    const int tx = bar_x + (bar_w - tw) / 2;
    M5Cardputer.Display.setCursor(tx, label_y);
    M5Cardputer.Display.print(lbl);
  }
}

// 按天数取预计秒数
static int chartEtaSecForDays(const int days) {
  if (days == 30) {
    return CURSOR_CHART_ETA_30_SEC;
  }
  if (days == 24) {
    return CURSOR_CHART_ETA_24_SEC;
  }
  return CURSOR_CHART_ETA_7_SEC;
}

// 发起拉取前先写入本页 ETA，避免仍显示上一页的秒数
static void prepareChartFetchEta(const int days) {
  g_chart_fetch_eta_sec = chartEtaSecForDays(days);
  g_chart_fetch_start_ms = 0;
  g_last_countdown_sec = -1;
}

// 记录图表拉取倒计时起点
static void startChartFetchTimer(const int days) {
  g_chart_fetch_start_ms = millis();
  g_chart_fetch_eta_sec = chartEtaSecForDays(days);
  g_last_countdown_sec = -1;
}

// 剩余秒数（不低于 0）
static int chartFetchCountdownSec() {
  if (g_chart_fetch_start_ms == 0) {
    return g_chart_fetch_eta_sec;
  }
  const uint32_t elapsed = (millis() - g_chart_fetch_start_ms) / 1000;
  int left = g_chart_fetch_eta_sec - static_cast<int>(elapsed);
  return left < 0 ? 0 : left;
}

// 柱状图区域居中 loading / 错误叠层（days：当前页对应天数）
static void drawChartLoadingOverlay(const int x, const int y, const int w, const int h,
                                    const int days, const char* err) {
  const bool fetching_this = isChartFetchInProgressFor(days);

  // 有错误且本页未在拉：显示错误
  if (err != nullptr && err[0] != '\0' && !fetching_this) {
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(APP_COLOR_ERROR, BLACK);
    const int tw = M5Cardputer.Display.textWidth(err);
    M5Cardputer.Display.setCursor(x + (w - tw) / 2, y + (h - 16) / 2);
    M5Cardputer.Display.print(err);
    return;
  }

  // 其它天数在后台拉：提示稍候
  if (!fetching_this && isChartFetchInProgress()) {
    const char* wait = "bg fetch...";
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    const int tw = M5Cardputer.Display.textWidth(wait);
    M5Cardputer.Display.setCursor(x + (w - tw) / 2, y + (h - 16) / 2);
    M5Cardputer.Display.print(wait);
    return;
  }

  const char* text = "loading...";
  if (g_phase == CursorPhase::WIFI) {
    text = "connecting...";
  } else if (g_status_msg[0] != '\0') {
    text = g_status_msg;
  }

  char eta_buf[16];
  const int left = chartFetchCountdownSec();
  if (left >= g_chart_fetch_eta_sec) {
    snprintf(eta_buf, sizeof(eta_buf), "approx %ds", g_chart_fetch_eta_sec);
  } else {
    snprintf(eta_buf, sizeof(eta_buf), "%ds", left);
  }

  M5Cardputer.Display.setTextSize(2);
  M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
  const int tw = M5Cardputer.Display.textWidth(text);
  M5Cardputer.Display.setCursor(x + (w - tw) / 2, y + (h - 28) / 2);
  M5Cardputer.Display.print(text);

  M5Cardputer.Display.setTextSize(1);
  const int eta_w = M5Cardputer.Display.textWidth(eta_buf);
  M5Cardputer.Display.setCursor(x + (w - eta_w) / 2, y + (h - 28) / 2 + 20);
  M5Cardputer.Display.print(eta_buf);
}

// 仅刷新当前可见图表页的 loading 倒计时
static void refreshChartLoadingFrame() {
  if (g_display_blanked || g_silent_fetch || g_help_visible) {
    return; // Help 打开时不盖 loading，避免冲掉帮助页
  }
  // 仅当正在看「正在拉取」的那一页时更新 ETA
  int page_days = 0;
  if (g_page == CursorPage::CHART_24) {
    page_days = 24;
  } else if (g_page == CursorPage::CHART_7) {
    page_days = 7;
  } else if (g_page == CursorPage::CHART_30) {
    page_days = 30;
  } else {
    return;
  }
  if (!isChartFetchInProgressFor(page_days)) {
    return;
  }

  const int y = APP_CONTENT_Y;
  const int screen_w = M5Cardputer.Display.width();
  const int chart_x = CURSOR_BAR_MARGIN_X;
  const int chart_w = screen_w - CURSOR_BAR_MARGIN_X * 2;
  const int bottom_hint_h = 12;
  const int chart_h =
      M5Cardputer.Display.height() - y - bottom_hint_h - CURSOR_BAR_LABEL_H;
  if (chart_h < 20) {
    return;
  }

  const int left = chartFetchCountdownSec();
  if (left == g_last_countdown_sec) {
    return;
  }
  g_last_countdown_sec = left;

  M5Cardputer.Display.fillRect(chart_x + 1, y + 1, chart_w - 2, chart_h - 2, BLACK);
  drawChartLoadingOverlay(chart_x, y, chart_w, chart_h, page_days, chartErrorBuf(page_days));
  updateAppHeaderStatus();
}

static void formatMoney(const int cents, char* buf, const size_t buf_size) {
  const int dollars = cents / 100;
  const int frac = abs(cents % 100);
  snprintf(buf, buf_size, "$%d.%02d", dollars, frac);
}

static void drawCursorHints() {
  const int hint_y = M5Cardputer.Display.height() - 12;
  M5Cardputer.Display.fillRect(APP_CONTENT_X, hint_y, 236, 12, BLACK);
  int cx = APP_CONTENT_X;
  // 方向键切界面
  cx += drawArrowBadge(cx, hint_y, 1);
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
  M5Cardputer.Display.setCursor(cx, hint_y);
  M5Cardputer.Display.print("pg ");
  cx += M5Cardputer.Display.textWidth("pg ");
  // last 列表翻页 tip 已放在状态栏页码旁，底栏不再重复
  cx += drawKeyBadge(cx, hint_y, 'r', 1);
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setTextColor(APP_COLOR_OK, BLACK);
  M5Cardputer.Display.setCursor(cx, hint_y);
  M5Cardputer.Display.print("rf ");
  cx += M5Cardputer.Display.textWidth("rf ");
  drawHelpHintRight("help");
}

// Help 分栏标题
static int drawCursorHelpColHeader(const int x, const int y, const int w, const char* title) {
  M5Cardputer.Display.fillRect(x, y, w, 11, APP_COLOR_LABEL);
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setTextColor(BLACK, APP_COLOR_LABEL);
  M5Cardputer.Display.setCursor(x + 2, y + 1);
  M5Cardputer.Display.print(title);
  return y + 13;
}

// Help 功能说明
static int drawCursorHelpText(const int x, const int y, const char* text) {
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
  M5Cardputer.Display.setCursor(x, y);
  M5Cardputer.Display.print(text);
  return y + 11;
}

// Help：按键徽章 + 说明（徽章后恢复 hint 色）
static int drawCursorHelpKey(const int x, const int y, const char key, const char* text) {
  const int cx = x + drawKeyBadge(x, y, key, 1);
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
  M5Cardputer.Display.setCursor(cx, y);
  M5Cardputer.Display.print(text);
  return y + 11;
}

// Help：文本徽章 + 说明
static int drawCursorHelpBadge(const int x, const int y, const char* badge, const char* text) {
  const int cx = x + drawTextBadge(x, y, badge, 1);
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
  M5Cardputer.Display.setCursor(cx, y);
  M5Cardputer.Display.print(text);
  return y + 11;
}

// Help：箭头徽章 + 说明
static int drawCursorHelpArrows(const int x, const int y, const char* text) {
  const int cx = x + drawArrowBadge(x, y, 1);
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
  M5Cardputer.Display.setCursor(cx, y);
  M5Cardputer.Display.print(text);
  return y + 11;
}

static void drawCursorHelpPage() {
  beginAppScreen("Help");
  constexpr int col_gap = 4;
  const int screen_w = M5Cardputer.Display.width();
  const int col_w = (screen_w - col_gap) / 2;
  const int manual_x = col_w + col_gap;
  const int col_y = APP_CONTENT_Y_NO_TAP_TO_HEADER;
  M5Cardputer.Display.drawFastVLine(col_w + col_gap / 2, col_y,
                                   M5Cardputer.Display.height() - col_y, DARKGREY);

  int y = drawCursorHelpColHeader(0, col_y, col_w, "keymap");
  y = drawCursorHelpArrows(2, y, "switch views");
  y = drawCursorHelpKey(2, y, 's', "summary");
  y = drawCursorHelpKey(2, y, 'u', "latest usage");
  y = drawCursorHelpBadge(2, y, "d/w/m", "charts");
  y = drawCursorHelpBadge(2, y, "[ ]", "records");
  y = drawCursorHelpKey(2, y, 'r', "refresh");
  y = drawCursorHelpBadge(2, y, "BtnA", "screen off");

  y = drawCursorHelpColHeader(manual_x, col_y, screen_w - manual_x, "manual");
  y = drawCursorHelpText(manual_x + 2, y, "Cursor usage viewer");
  y = drawCursorHelpText(manual_x + 2, y, "Auto/API remaining");
  y = drawCursorHelpText(manual_x + 2, y, "On-Demand spend");
  y = drawCursorHelpText(manual_x + 2, y, "latest requests");
  y = drawCursorHelpText(manual_x + 2, y, "24h/7d/30d charts");
  y = drawCursorHelpText(manual_x + 2, y, "auto refresh idle");
  y = drawCursorHelpText(manual_x + 2, y, "WiFi only on fetch");

  drawHelpHintRight("close");
  updateAppHeaderStatus();
}

// 摘要：标签色=条色；百分比右对齐同一起点；On-Demand / reset 数值用界面蓝色
static void drawCursorSummaryPage(const int y) {
  const int screen_h = M5Cardputer.Display.height();
  const int screen_w = M5Cardputer.Display.width();
  const int pad_x = CURSOR_SUMMARY_PAD_X;
  const int content_w = screen_w - pad_x * 2;
  const int hint_h = 12;
  const int area_bottom = screen_h - hint_h;
  constexpr int text_sz = 2;
  constexpr int bar_h = 14;
  constexpr int label_bar_gap = 1; // 标签与进度条间距（较原先各 -1）
  constexpr int block_gap = 4;
  constexpr int footer_h = 14; // ond/reset 一排
  char buf[24];

  // 标签与百分比均用 2 倍字；按最长标签对齐百分比列
  M5Cardputer.Display.setTextSize(text_sz);
  const int label_col_w = M5Cardputer.Display.textWidth("Auto");
  const int value_gap = M5Cardputer.Display.textWidth("  ");
  const int value_x = pad_x + label_col_w + value_gap;

  auto drawUsageBlock = [&](int& cy, const char* label, const float pct, const uint16_t color) {
    // 标签：与进度条同色，2 倍字
    M5Cardputer.Display.setTextSize(text_sz);
    M5Cardputer.Display.setTextColor(color, BLACK);
    M5Cardputer.Display.setCursor(pad_x, cy);
    M5Cardputer.Display.print(label);

    snprintf(buf, sizeof(buf), "%.2f%%", pct);
    // 百分比：白色大字
    M5Cardputer.Display.setTextSize(text_sz);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
    M5Cardputer.Display.setCursor(value_x, cy);
    M5Cardputer.Display.print(buf);

    cy += INFO_LINE_H_2X + label_bar_gap;
    drawPctBar(pad_x, cy, content_w > 0 ? content_w : 0, bar_h, pct, color);
    cy += bar_h + block_gap;
  };

  // 从上往下紧凑排布，多出的垂直空间留给底栏
  int cy = y;
  drawUsageBlock(cy, "Auto", 100.0f - g_usage.auto_pct, APP_COLOR_OK);
  drawUsageBlock(cy, "API", 100.0f - g_usage.api_pct, ORANGE);

  // 底栏：On-Demand / reset
  const int footer_y = area_bottom - footer_h + 2;
  M5Cardputer.Display.setTextSize(1);

  if (g_usage.ond_valid) {
    char used_s[16];
    formatMoney(g_usage.ond_used_cents, used_s, sizeof(used_s));
    if (g_usage.ond_limit_cents > 0) {
      char lim_s[16];
      formatMoney(g_usage.ond_limit_cents, lim_s, sizeof(lim_s));
      snprintf(buf, sizeof(buf), "%s/%s", used_s, lim_s);
    } else {
      snprintf(buf, sizeof(buf), "%s", used_s);
    }
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(pad_x, footer_y);
    M5Cardputer.Display.print("ond ");
    M5Cardputer.Display.setTextColor(APP_COLOR_LABEL, BLACK);
    M5Cardputer.Display.print(buf);
  } else {
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(pad_x, footer_y);
    M5Cardputer.Display.print("ond ");
    M5Cardputer.Display.setTextColor(APP_COLOR_MUTED, BLACK);
    M5Cardputer.Display.print("-");
  }

  // 右侧：剩余天数 + 日期，如 24d | 08-09
  const int days_left = cursorResetDaysLeft();
  char reset_val[24] = "";
  if (days_left >= 0 && g_usage.reset_date[0] != '\0') {
    snprintf(reset_val, sizeof(reset_val), "%dd | %s", days_left, g_usage.reset_date);
  } else if (days_left >= 0) {
    snprintf(reset_val, sizeof(reset_val), "%dd", days_left);
  } else if (g_usage.reset_date[0] != '\0') {
    strncpy(reset_val, g_usage.reset_date, sizeof(reset_val) - 1);
  }
  if (reset_val[0] != '\0') {
    const char* reset_label = "reset ";
    const int rw =
        M5Cardputer.Display.textWidth(reset_label) + M5Cardputer.Display.textWidth(reset_val);
    M5Cardputer.Display.setCursor(pad_x + content_w - rw, footer_y);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.print(reset_label);
    M5Cardputer.Display.setTextColor(APP_COLOR_LABEL, BLACK);
    M5Cardputer.Display.print(reset_val);
  }
}

// usage 页 header 下方状态行：分页、载入状态和当前时间（小字）
static void drawCursorLastStatusBar(const int y) {
  const int pad_x = APP_CONTENT_X;
  const int screen_w = M5Cardputer.Display.width();
  M5Cardputer.Display.fillRect(0, y, screen_w, CURSOR_LAST_STATUS_H, BLACK);
  M5Cardputer.Display.setTextSize(1);

  const int pages =
      g_last_req_count > 0
          ? (g_last_req_count + CURSOR_LAST_PER_PAGE - 1) / CURSOR_LAST_PER_PAGE
          : 0;
  char current_page_s[6] = "";
  char total_pages_s[8] = "";
  if (pages > 0) {
    snprintf(current_page_s, sizeof(current_page_s), "%d", g_last_page + 1);
    snprintf(total_pages_s, sizeof(total_pages_s), "/%d", pages);
  }

  // 左：当前页橙色，总页数白色；其后跟 [ ] 翻页 tip
  int left_x = pad_x;
  if (current_page_s[0] != '\0') {
    M5Cardputer.Display.setTextColor(APP_COLOR_WARN, BLACK);
    M5Cardputer.Display.setCursor(left_x, y + 1);
    M5Cardputer.Display.print(current_page_s);
    left_x += M5Cardputer.Display.textWidth(current_page_s);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
    M5Cardputer.Display.print(total_pages_s);
    left_x += M5Cardputer.Display.textWidth(total_pages_s);
    // 多页时：翻页 tip 紧挨页码右侧
    if (pages > 1) {
      left_x += 4;
      left_x += drawKeyBadge(left_x, y, '[', 1);
      left_x += drawKeyBadge(left_x, y, ']', 1);
    }
  }

  // 右：当前本地时间，仅显示时分
  char clock_s[6] = "--:--";
  const time_t now = time(nullptr);
  if (now > 1600000000) {
    struct tm local_tm;
    if (localtime_r(&now, &local_tm) != nullptr) {
      strftime(clock_s, sizeof(clock_s), "%H:%M", &local_tm);
      g_last_clock_minute = static_cast<int64_t>(now) / 60;
    }
  }
  M5Cardputer.Display.setTextColor(WHITE, BLACK);
  const int clock_w = M5Cardputer.Display.textWidth(clock_s);
  M5Cardputer.Display.setCursor(screen_w - pad_x - clock_w, y + 1);
  M5Cardputer.Display.print(clock_s);

  // 中间保留载入/空列表状态，避免覆盖右侧时间与左侧 tip
  const char* st = nullptr;
  if (g_last_refreshing) {
    st = "loading...";
  } else if (g_last_req_count <= 0) {
    st = "no request";
  }
  if (st != nullptr) {
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    const int sw = M5Cardputer.Display.textWidth(st);
    const int sx = screen_w - pad_x - clock_w - 6 - sw;
    if (sx > left_x + 4) {
      M5Cardputer.Display.setCursor(sx, y + 1);
      M5Cardputer.Display.print(st);
    }
  }
}

// 截断到可用宽度（末尾加 ~）
static void cursorFitText(char* buf, const size_t buf_size, const int max_w) {
  if (buf == nullptr || buf_size == 0 || max_w <= 0) {
    return;
  }
  M5Cardputer.Display.setTextSize(1);
  if (M5Cardputer.Display.textWidth(buf) <= max_w) {
    return;
  }
  while (strlen(buf) > 1 && M5Cardputer.Display.textWidth(buf) > max_w - 6) {
    buf[strlen(buf) - 1] = '\0';
  }
  const size_t n = strlen(buf);
  if (n + 1 < buf_size) {
    buf[n] = '~';
    buf[n + 1] = '\0';
  }
}

// usage 页：每页 1 条；日期/模型分层，token 突出；分页在 header 下
static void drawCursorLastPage(const int y) {
  const int pad_x = APP_CONTENT_X + 2;
  const int screen_w = M5Cardputer.Display.width();
  const int content_w = screen_w - pad_x * 2;
  const int hint_h = 12;
  const int area_bottom = M5Cardputer.Display.height() - hint_h;

  drawCursorLastStatusBar(y);
  const int body_top = y + CURSOR_LAST_STATUS_H + 2;
  const int body_h = area_bottom - body_top;
  if (body_h < 20) {
    return;
  }

  if (g_last_req_count <= 0) {
    return;
  }

  const int pages = (g_last_req_count + CURSOR_LAST_PER_PAGE - 1) / CURSOR_LAST_PER_PAGE;
  if (g_last_page >= pages) {
    g_last_page = pages - 1;
  }
  if (g_last_page < 0) {
    g_last_page = 0;
  }

  const CursorLastRequest& req = g_last_reqs[g_last_page];

  // 拆分日期 / 时间，便于扫读
  char date_s[12] = "";
  char time_s[12] = "";
  {
    const char* sp = strchr(req.time_str, ' ');
    if (sp != nullptr) {
      const size_t dl = static_cast<size_t>(sp - req.time_str);
      if (dl < sizeof(date_s)) {
        memcpy(date_s, req.time_str, dl);
        date_s[dl] = '\0';
      }
      strncpy(time_s, sp + 1, sizeof(time_s) - 1);
    } else {
      strncpy(date_s, req.time_str, sizeof(date_s) - 1);
    }
  }

  char model_line[40];
  strncpy(model_line, req.model[0] != '\0' ? req.model : "-", sizeof(model_line) - 1);
  model_line[sizeof(model_line) - 1] = '\0';
  cursorFitText(model_line, sizeof(model_line), content_w);

  const char* tok = req.tokens_str[0] != '\0' ? req.tokens_str : "-";

  // 块高：日期和时间间隔 2px，再显示模型与 token 状态
  constexpr int date_h = 8;
  constexpr int date_time_gap = 2;
  constexpr int time_h = INFO_LINE_H_2X;
  constexpr int model_h = 12;
  constexpr int token_h = INFO_LINE_H_2X;
  constexpr int gap1 = 4;
  constexpr int gap2 = 6;
  const int block_h =
      date_h + date_time_gap + time_h + gap1 + model_h + gap2 + token_h;
  int cy = body_top + (body_h > block_h ? (body_h - block_h) / 2 : 0);

  // 日期（次要）
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
  M5Cardputer.Display.setCursor(pad_x, cy);
  M5Cardputer.Display.print(date_s[0] != '\0' ? date_s : "-");
  cy += date_h + date_time_gap;

  // 时间（主时间，x2）
  M5Cardputer.Display.setTextSize(2);
  M5Cardputer.Display.setTextColor(WHITE, BLACK);
  M5Cardputer.Display.setCursor(pad_x, cy);
  M5Cardputer.Display.print(time_s[0] != '\0' ? time_s : "--:--:--");
  cy += time_h + gap1;

  // 模型
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setTextColor(APP_COLOR_LABEL, BLACK);
  M5Cardputer.Display.setCursor(pad_x, cy);
  M5Cardputer.Display.print(model_line);
  cy += model_h + gap2;

  // token 大字 + Included 完整状态标签
  M5Cardputer.Display.setTextSize(2);
  M5Cardputer.Display.setTextColor(WHITE, BLACK);
  M5Cardputer.Display.setCursor(pad_x, cy);
  M5Cardputer.Display.print(tok);
  if (req.included) {
    const int tw = M5Cardputer.Display.textWidth(tok);
    // 绿底小徽章，比纯文字更醒目
    const char* badge = "Included";
    M5Cardputer.Display.setTextSize(1);
    const int bw = M5Cardputer.Display.textWidth(badge) + 6;
    const int bh = 10;
    const int bx = pad_x + tw + 8;
    const int by = cy + 3;
    M5Cardputer.Display.fillRoundRect(bx, by, bw, bh, 2, APP_COLOR_OK);
    M5Cardputer.Display.setTextColor(BLACK, APP_COLOR_OK);
    M5Cardputer.Display.setCursor(bx + 3, by + 1);
    M5Cardputer.Display.print(badge);
  }
}

// 1s 慢循环：内容区右上角内缩 (3,3) 画 3x3 蓝点
static void drawCursorSlowLoopDot() {
  if (g_display_blanked) {
    return;
  }
  if ((millis() - g_last_activity_ms) < CURSOR_SLOW_LOOP_IDLE_MS) {
    return;
  }
  const int screen_w = M5Cardputer.Display.width();
  constexpr int dot = 3;
  constexpr int inset = 3;
  M5Cardputer.Display.fillRect(screen_w - inset - dot, APP_CONTENT_Y + inset, dot, dot,
                               APP_COLOR_LABEL);
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
  const char* err = chartErrorBuf(days);

  if (chart_h >= 20) {
    if (ready) {
      drawDailyBars(chart_x, y, chart_w, chart_h, days, daily_cents, gap, true);
    } else {
      // 加载中 / 失败：不画空柱，避免盖住 header WiFi 图标
      M5Cardputer.Display.drawRect(chart_x, y, chart_w, chart_h, DARKGREY);
      drawChartLoadingOverlay(chart_x, y, chart_w, chart_h, days, err);
    }
  }
  drawCursorHints();
}

void drawCursorApp() {
  // 熄屏时只更新内存数据，亮屏后再画
  if (g_display_blanked) {
    return;
  }
  // Help 打开时保持帮助页（后台静默刷新不打断）
  if (g_help_visible) {
    drawCursorHelpPage();
    return;
  }
  redrawCursorContent();

  int y = APP_CONTENT_Y;

  // 静默 / 图表后台拉取时不打断摘要页；图表页在柱状图区域内显示 loading
  if (!g_silent_fetch && g_page == CursorPage::SUMMARY &&
      g_fetch_mode != CursorFetchMode::CHART &&
      (g_phase == CursorPhase::WIFI || g_phase == CursorPhase::FETCHING)) {
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
    if (g_phase == CursorPhase::WIFI) {
      M5Cardputer.Display.println("connecting...");
    } else {
      M5Cardputer.Display.println(g_status_msg[0] != '\0' ? g_status_msg : "loading...");
    }
    updateAppHeaderStatus();
    return;
  }

  if (g_phase == CursorPhase::ERROR) {
    drawInfoLineAt(APP_CONTENT_X, y, "err", g_error_msg, 2);
    y += INFO_LINE_H_2X;
    drawInfoLineAt(APP_CONTENT_X, y, "hint", "u web cfg", 2);
    y += INFO_LINE_H_2X;
    const KeyHintItem items[] = {{'r', "retry"}};
    drawKeyHintsRow(APP_CONTENT_X, y, items, 1, 2, APP_COLOR_HINT);
    updateAppHeaderStatus();
    return;
  }

  const AppConfig& cfg = getAppConfig();
  if (!cfg.loaded || cfg.cursor_token[0] == '\0') {
    drawInfoLineAt(APP_CONTENT_X, y, "cfg", "no token", 2);
    y += INFO_LINE_H_2X;
    drawInfoLineAt(APP_CONTENT_X, y, "hint", "u web setup", 2);
    updateAppHeaderStatus();
    return;
  }

  // usage 页可在 period 未就绪时单独展示/刷新
  if (g_page == CursorPage::LAST) {
    drawCursorLastPage(y);
    drawCursorHints();
    drawCursorSlowLoopDot();
    updateAppHeaderStatus();
    return;
  }

  if (!g_usage.valid) {
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
    M5Cardputer.Display.println("no data");
    updateAppHeaderStatus();
    return;
  }

  if (g_page == CursorPage::SUMMARY) {
    drawCursorSummaryPage(y);
    drawCursorHints();
    drawCursorSlowLoopDot();
    updateAppHeaderStatus();
    return;
  }

  if (g_page == CursorPage::CHART_24) {
    drawCursorChartPage(24, g_chart_24_cents, g_chart_24_ready);
    drawCursorSlowLoopDot();
    updateAppHeaderStatus();
    return;
  }

  if (g_page == CursorPage::CHART_7) {
    drawCursorChartPage(7, g_chart_7_cents, g_chart_7_ready);
    drawCursorSlowLoopDot();
    updateAppHeaderStatus();
    return;
  }

  drawCursorChartPage(30, g_chart_30_cents, g_chart_30_ready);
  drawCursorSlowLoopDot();
  updateAppHeaderStatus();
}

static void beginCursorFetch(const CursorFetchMode mode) {
  // 作废进行中的 task，由 updateCursorApp 再调度新任务
  invalidateCursorFetch();
  g_fetch_mode = mode;
  g_silent_fetch = (mode != CursorFetchMode::FULL);
  g_fetch_pending = true;
  g_queue_other_chart = false;
  g_error_msg[0] = '\0';
  g_status_msg[0] = '\0';

  if (mode == CursorFetchMode::FULL) {
    g_phase = CursorPhase::WIFI;
    g_usage.valid = false;
    g_last_req_count = 0;
    g_last_page = 0;
    g_last_refreshing = false;
    g_chart_24_ready = false;
    g_chart_7_ready = false;
    g_chart_30_ready = false;
    g_chart_24_error[0] = '\0';
    g_chart_7_error[0] = '\0';
    g_chart_30_error[0] = '\0';
    drawCursorApp();
    return;
  }

  if (mode == CursorFetchMode::PERIOD || mode == CursorFetchMode::LAST) {
    return;
  }

  if (mode == CursorFetchMode::CHART) {
    if (g_page == CursorPage::CHART_30) {
      g_chart_fetch_days = 30;
    } else if (g_page == CursorPage::CHART_24) {
      g_chart_fetch_days = 24;
    } else {
      g_chart_fetch_days = 7;
    }
  }
}

static void beginCursorChartFetch(const int days, const bool silent) {
  // 已有缓存则直接展示
  if ((days == 7 && g_chart_7_ready) || (days == 30 && g_chart_30_ready) ||
      (days == 24 && g_chart_24_ready)) {
    drawCursorApp();
    return;
  }
  // 同天数已在后台拉：不重启，重绘显示当前 ETA
  if (isChartFetchInProgressFor(days)) {
    drawCursorApp();
    return;
  }
  // 其它天数在拉：不取消，等用户稍后再进或显示 bg fetch
  if (isChartFetchInProgress()) {
    drawCursorApp();
    return;
  }

  g_chart_fetch_days = days;
  chartErrorBuf(days)[0] = '\0';
  if (!silent) {
    if (days == 7) {
      g_chart_7_ready = false;
    } else if (days == 30) {
      g_chart_30_ready = false;
    } else {
      g_chart_24_ready = false;
    }
    invalidateCursorFetch();
    g_fetch_mode = CursorFetchMode::CHART;
    g_silent_fetch = false;
    g_fetch_pending = true;
    g_queue_other_chart = false;
    strncpy(g_status_msg, "chart...", sizeof(g_status_msg));
    // 在 task 真正 startChartFetchTimer 之前就写好本页 ETA，避免闪上一页秒数
    prepareChartFetchEta(days);
    drawCursorApp();
    return;
  }
  beginCursorFetch(CursorFetchMode::CHART);
}

// last 页拉取：软刷新保留旧列表；忙碌时排队，等会话就绪后再拉
static void beginCursorLastRefresh() {
  g_last_refreshing = true;
  if (g_task_running || g_fetch_pending || g_user_id <= 0 || g_cookie[0] == '\0') {
    g_want_last_fetch = true;
    drawCursorApp();
    return;
  }
  g_want_last_fetch = false;
  invalidateCursorFetch();
  g_fetch_mode = CursorFetchMode::LAST;
  g_silent_fetch = true;
  g_fetch_pending = true;
  g_queue_other_chart = false;
  g_error_msg[0] = '\0';
  drawCursorApp();
}

// 跳到指定页；图表页无缓存则发起拉取
static void cursorGotoPage(const CursorPage page) {
  g_page = page;

  // last：切到该页再请求（进入 App 不预拉）
  if (g_page == CursorPage::LAST) {
    beginCursorLastRefresh();
    return;
  }

  // 翻页不取消后台拉取；有缓存直接画，进行中显示 ETA，否则才发起
  if (g_page == CursorPage::CHART_24) {
    if (g_chart_24_ready || isChartFetchInProgressFor(24) || isChartFetchInProgress()) {
      drawCursorApp();
      return;
    }
    if (g_user_id > 0 && (g_phase == CursorPhase::READY || g_usage.valid)) {
      beginCursorChartFetch(24, false);
      return;
    }
  }
  if (g_page == CursorPage::CHART_7) {
    if (g_chart_7_ready || isChartFetchInProgressFor(7) || isChartFetchInProgress()) {
      drawCursorApp();
      return;
    }
    if (g_user_id > 0 && (g_phase == CursorPhase::READY || g_usage.valid)) {
      beginCursorChartFetch(7, false);
      return;
    }
  }
  if (g_page == CursorPage::CHART_30) {
    if (g_chart_30_ready || isChartFetchInProgressFor(30) || isChartFetchInProgress()) {
      drawCursorApp();
      return;
    }
    if (g_user_id > 0 && (g_phase == CursorPhase::READY || g_usage.valid)) {
      beginCursorChartFetch(30, false);
      return;
    }
  }
  drawCursorApp();
}

// 方向键切界面（summary / last / 24h / 7d / 30d）
static void cursorPageNav(const int delta) {
  if (delta == 0) {
    return;
  }
  int next = static_cast<int>(g_page) + delta;
  if (next >= CURSOR_PAGE_COUNT) {
    next = 0;
  }
  if (next < 0) {
    return;
  }
  cursorGotoPage(static_cast<CursorPage>(next));
}

// last 列表内翻记录
static void cursorLastRecNav(const int delta) {
  if (delta == 0 || g_last_req_count <= 0) {
    return;
  }
  const int pages =
      (g_last_req_count + CURSOR_LAST_PER_PAGE - 1) / CURSOR_LAST_PER_PAGE;
  g_last_page += delta;
  if (g_last_page < 0) {
    g_last_page = 0;
  } else if (g_last_page >= pages) {
    g_last_page = pages - 1;
  }
  drawCursorApp();
}

void enterCursorApp() {
  g_screen_ready = false;
  g_page = CursorPage::SUMMARY;
  g_periodic_refresh_active = false;
  g_last_activity_ms = millis();
  g_was_slow_loop = false;
  g_last_page = 0;
  g_last_refreshing = false;
  g_want_last_fetch = false;
  g_display_blanked = false;
  g_help_visible = false;
  beginCursorFetch(CursorFetchMode::FULL);
}

// 离开 Cursor：中止后台拉取并释放 WiFi
void leaveCursorApp() {
  g_help_visible = false;
  g_fetch_pending = false;
  g_silent_fetch = false;
  g_queue_other_chart = false;
  g_chart_fetch_start_ms = 0;
  g_last_countdown_sec = -1;
  g_status_msg[0] = '\0';
  invalidateCursorFetch();
  // 先断网加速卡在 HTTPS / WiFi begin 上的 task 退出
  WiFi.disconnect(true);
  // 等够 WiFi 超时 + 余量，避免僵尸 task 干扰下次进 App
  waitCursorFetchTaskDone(CURSOR_WIFI_TIMEOUT_MS + 2000);
  wakeCursorDisplay(false);
  releaseCursorWifi();
  // 射频冷却，降低紧接着再 enter 时 begin 立刻失败概率
  delay(80);
}

bool isCursorDisplayBlanked() {
  return g_display_blanked;
}

// 无操作满 5 分钟后主循环可降到 1s 一拍
bool isCursorIdleSlowLoop() {
  return (millis() - g_last_activity_ms) >= CURSOR_SLOW_LOOP_IDLE_MS;
}

// BtnA：亮屏时灭屏，灭屏时亮屏（wasPressed 仅单帧有效）
void pollCursorBtnA() {
  if (!M5Cardputer.BtnA.wasPressed()) {
    return;
  }
  noteCursorActivity();
  if (g_display_blanked) {
    wakeCursorDisplay(true);
  } else {
    blankCursorDisplay();
  }
}

// 启动拉取 task（仅主循环调用；失败则回落错误态）
static void scheduleCursorFetchTask() {
  if (g_task_running || !g_fetch_pending) {
    return;
  }
  g_task_running = true;
  if (xTaskCreate(cursorFetchTaskFn, "cursor_fetch", CURSOR_FETCH_STACK, nullptr, 1,
                  &g_fetch_task) != pdPASS) {
    g_task_running = false;
    g_fetch_task = nullptr;
    g_fetch_pending = false;
    g_silent_fetch = false;
    g_phase = CursorPhase::ERROR;
    strncpy(g_error_msg, "task fail", sizeof(g_error_msg));
    g_need_redraw = true;
  }
}

// 后台 task 退出前清理运行标志
static void endCursorFetchTask() {
  g_fetch_task = nullptr;
  g_task_running = false;
  vTaskDelete(nullptr);
}

// 后台 task：WiFi / HTTPS / 图表分页；主循环只扫键与重绘
static void cursorFetchTaskFn(void* /*arg*/) {
  const uint32_t gen = g_fetch_gen;
  const CursorFetchMode mode = g_fetch_mode;
  const bool silent = g_silent_fetch;
  const int chart_days = g_chart_fetch_days;

  auto aborted = [gen]() -> bool { return gen != g_fetch_gen; };

  auto finish_ok = [aborted]() {
    if (aborted()) {
      return;
    }
    g_fetch_pending = false;
    g_silent_fetch = false;
    g_last_refreshing = false;
    if (g_phase != CursorPhase::ERROR) {
      g_phase = CursorPhase::READY;
    }
    // 再次确认，避免 leave 后误关新 task 的 WiFi
    if (aborted()) {
      return;
    }
    finishCursorWifi();
    g_need_redraw = true;
  };

  auto finish_fail = [aborted, silent](const char* err) {
    if (aborted()) {
      return;
    }
    g_fetch_pending = false;
    g_silent_fetch = false;
    g_last_refreshing = false;
    if (!silent) {
      g_phase = CursorPhase::ERROR;
      strncpy(g_error_msg, err, sizeof(g_error_msg) - 1);
      g_error_msg[sizeof(g_error_msg) - 1] = '\0';
      g_need_redraw = true;
    } else {
      g_need_redraw = true; // 软刷新失败也清 rf... 提示
    }
    if (aborted()) {
      return;
    }
    finishCursorWifi();
  };

  // --- WiFi + NTP ---
  if (!silent) {
    g_phase = CursorPhase::WIFI;
    g_need_redraw = true;
  }
  if (!ensureCursorWifi(CURSOR_WIFI_TIMEOUT_MS, gen)) {
    // 取消退出不报 wifi fail；真正连不上才 fail
    if (!aborted()) {
      finish_fail("wifi fail");
    }
    endCursorFetchTask();
    return;
  }
  if (aborted()) {
    endCursorFetchTask();
    return;
  }
  quickSyncTime();
  if (aborted()) {
    endCursorFetchTask();
    return;
  }

  // --- Auth + period（图表/last 且已有 user_id 可跳过）---
  const bool need_auth =
      !((mode == CursorFetchMode::CHART || mode == CursorFetchMode::LAST) && g_user_id > 0);
  if (need_auth) {
    if (!silent) {
      g_phase = CursorPhase::FETCHING;
      strncpy(g_status_msg, "auth...", sizeof(g_status_msg));
      g_need_redraw = true;
    }
    if (!fetchCursorAuthPeriod()) {
      if (!silent) {
        finish_fail(g_error_msg[0] != '\0' ? g_error_msg : "auth fail");
      } else {
        if (!aborted()) {
          g_fetch_pending = false;
          g_silent_fetch = false;
          g_last_refreshing = false;
          finishCursorWifi();
          g_need_redraw = true;
        }
      }
      endCursorFetchTask();
      return;
    }
    if (aborted()) {
      endCursorFetchTask();
      return;
    }
    if (mode == CursorFetchMode::PERIOD || mode == CursorFetchMode::FULL ||
        mode == CursorFetchMode::LAST) {
      g_error_msg[0] = '\0';
      finish_ok();
      endCursorFetchTask();
      return;
    }
  }

  // --- 仅刷新最近请求列表（软刷新，不碰 period）---
  if (mode == CursorFetchMode::LAST) {
    if (!fetchCursorLastRequest()) {
      if (!aborted()) {
        finish_fail("last fail");
      }
      endCursorFetchTask();
      return;
    }
    if (!aborted()) {
      g_error_msg[0] = '\0';
      finish_ok();
    }
    endCursorFetchTask();
    return;
  }

  // --- Chart 分页（整段在 task 内跑完，页间检查取消）---
  if (mode == CursorFetchMode::CHART) {
    float* target = g_chart_7_cents;
    if (chart_days == 30) {
      target = g_chart_30_cents;
    } else if (chart_days == 24) {
      target = g_chart_24_cents;
    }
    if (!silent) {
      startChartFetchTimer(chart_days);
      strncpy(g_status_msg, "chart...", sizeof(g_status_msg));
      g_need_redraw = true;
    }
    if (!beginChartPagedFetch(chart_days, target)) {
      g_chart_fetch_start_ms = 0;
      g_last_countdown_sec = -1;
      if (!aborted()) {
        g_fetch_pending = false;
        g_silent_fetch = false;
        finishCursorWifi();
        if (g_phase != CursorPhase::ERROR) {
          g_phase = CursorPhase::READY;
        }
        g_need_redraw = true;
        // 启动失败只排队「另一段」；同页失败不自动重试，避免 14s 循环
        g_queue_other_chart = true;
      }
      endCursorFetchTask();
      return;
    }

    int step_rc = 0;
    while (!aborted()) {
      step_rc = stepChartPagedFetch();
      if (step_rc != 0) {
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(1)); // 让出 CPU，保证键扫及时
    }

    if (aborted()) {
      g_chart_http_active = false;
      g_chart_http_target = nullptr;
      endCursorFetchTask();
      return;
    }

    if (step_rc > 0) {
      if (chart_days == 30) {
        g_chart_30_ready = true;
        g_chart_30_error[0] = '\0';
        g_last_chart_30_fetch_ms = millis();
      } else if (chart_days == 24) {
        g_chart_24_ready = true;
        g_chart_24_error[0] = '\0';
        g_last_chart_24_fetch_ms = millis();
      } else {
        g_chart_7_ready = true;
        g_chart_7_error[0] = '\0';
        g_last_chart_7_fetch_ms = millis();
      }
    }
    // step_rc < 0：错误已写入 chartErrorBuf，停留错误态等用户按 r

    g_chart_http_target = nullptr;
    g_status_msg[0] = '\0';
    g_chart_fetch_start_ms = 0;
    g_last_countdown_sec = -1;
    if (!aborted()) {
      g_fetch_pending = false;
      g_silent_fetch = false;
      finishCursorWifi();
      if (g_phase != CursorPhase::ERROR) {
        g_phase = CursorPhase::READY;
      }
      g_need_redraw = true;
      // 成功或失败都可排队另一段；同页失败由 update 里 want!=finished 过滤
      g_queue_other_chart = true;
    }
  }

  endCursorFetchTask();
}

void updateCursorApp() {
  // 主循环只做 UI：倒计时 / 任务回调重绘 / 调度后台 task
  if (isChartFetchInProgress()) {
    refreshChartLoadingFrame();
  }

  // last 页右上角时钟跨分钟后刷新。
  const time_t now = time(nullptr);
  if (g_page == CursorPage::LAST && !g_display_blanked && !g_help_visible &&
      now > 1600000000 && static_cast<int64_t>(now) / 60 != g_last_clock_minute) {
    g_need_redraw = true;
  }
  if (g_page == CursorPage::CHART_24 && g_chart_24_ready && !g_display_blanked &&
      !g_help_visible && currentLocalHour() != g_chart_24_highlight_hour) {
    g_need_redraw = true;
  }

  if (g_need_redraw) {
    g_need_redraw = false;
    drawCursorApp();
  }

  // 上一段图表结束后：仅当当前页是「另一段」未就绪时再拉；同页失败不自动重试
  if (g_queue_other_chart && !g_task_running && !g_fetch_pending) {
    g_queue_other_chart = false;
    const int finished_days = g_chart_fetch_days;
    int want_days = 0;
    if (g_page == CursorPage::CHART_24 && !g_chart_24_ready) {
      want_days = 24;
    } else if (g_page == CursorPage::CHART_7 && !g_chart_7_ready) {
      want_days = 7;
    } else if (g_page == CursorPage::CHART_30 && !g_chart_30_ready) {
      want_days = 30;
    }
    if (want_days != 0 && want_days != finished_days) {
      beginCursorChartFetch(want_days, false);
    }
  }

  if (g_fetch_pending && !g_task_running) {
    scheduleCursorFetchTask();
  }

  // 切到 last 时若 period/auth 尚未完成，就绪后再拉列表
  if (g_want_last_fetch && g_page == CursorPage::LAST && g_user_id > 0 &&
      g_cookie[0] != '\0' && !g_task_running && !g_fetch_pending) {
    beginCursorLastRefresh();
  }

  if (!g_fetch_pending && !g_task_running && shouldScheduleCursorRefresh()) {
    triggerCursorIdleRefresh();
  }

  // 进入/退出 1s 慢循环时更新右上角蓝点
  const bool slow = isCursorIdleSlowLoop() && !g_display_blanked;
  if (slow != g_was_slow_loop) {
    g_was_slow_loop = slow;
    if (slow) {
      drawCursorSlowLoopDot();
    } else {
      g_need_redraw = true;
    }
  }
}

void handleCursorApp(const Keyboard_Class::KeysState& status) {
  // 任意键亮屏，本帧不执行翻页/刷新
  if (g_display_blanked) {
    wakeCursorDisplay(true);
    return;
  }

  const String key = getPressedKey();
  if (g_help_visible) {
    noteCursorActivity();
    if (key == "h") {
      g_help_visible = false;
      g_screen_ready = false;
      drawCursorApp();
    }
    return;
  }

  noteCursorActivity();
  if (key == "h") {
    g_help_visible = true;
    drawCursorHelpPage();
    return;
  }
  // 字母快捷跳转（与方向键并存）
  if (key == "s") {
    cursorGotoPage(CursorPage::SUMMARY);
    return;
  }
  if (key == "u") {
    cursorGotoPage(CursorPage::LAST);
    return;
  }
  if (key == "d") {
    cursorGotoPage(CursorPage::CHART_24);
    return;
  }
  if (key == "w") {
    cursorGotoPage(CursorPage::CHART_7);
    return;
  }
  if (key == "m") {
    cursorGotoPage(CursorPage::CHART_30);
    return;
  }
  // last 列表：[] 翻记录
  if (g_page == CursorPage::LAST) {
    if (key == "[") {
      cursorLastRecNav(-1);
      return;
    }
    if (key == "]") {
      cursorLastRecNav(1);
      return;
    }
  }
  // Tab：下一界面
  for (const uint8_t hid : status.hid_keys) {
    if (hid == 0x2B) {
      cursorPageNav(1);
      return;
    }
  }
  for (const char c : status.word) {
    if (c == '\t') {
      cursorPageNav(1);
      return;
    }
  }
  // 方向键切界面
  const int delta = getMenuNavDelta(status);
  if (delta != 0) {
    cursorPageNav(delta);
    return;
  }
  if (key == "r") {
    // 图表页：只重拉当前段；usage：软刷新；摘要：整页刷新
    if (g_page == CursorPage::CHART_24) {
      g_chart_24_ready = false;
      g_chart_24_error[0] = '\0';
      beginCursorChartFetch(24, false);
      return;
    }
    if (g_page == CursorPage::CHART_7) {
      g_chart_7_ready = false;
      g_chart_7_error[0] = '\0';
      beginCursorChartFetch(7, false);
      return;
    }
    if (g_page == CursorPage::CHART_30) {
      g_chart_30_ready = false;
      g_chart_30_error[0] = '\0';
      beginCursorChartFetch(30, false);
      return;
    }
    if (g_page == CursorPage::LAST) {
      beginCursorLastRefresh();
      return;
    }
    beginCursorFetch(CursorFetchMode::FULL);
  }
}
