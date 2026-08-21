#pragma once

#include <cstddef>
#include <FS.h>
#include <WString.h>

// 截图目录（SD 优先，否则 LittleFS）
static constexpr const char* SHOT_DIR = "/shot";

// 把当前屏存为 /shot/<slug>.png（同名则 _002/_003…；流式 zlib，体积远小于 BMP）
// slug 形如 组名_app名_功能名（exi2c_gps_live）；仍识别旧版 app_*_NNN.png
// 有 TF 卡时优先写 SD，否则写 LittleFS
// 成功时 out_name 写入文件名（不含路径）；失败时 err 有原因
bool saveScreenshotToFlash(const char* app_slug, char* out_name, size_t out_name_len, char* err,
                           size_t err_len);

// 全局热键：Fn+s 截图（任意界面）；返回 true 表示已消费按键
bool tryHandleScreenshotHotkey();

// TF 卡是否已挂载可用（新截图会优先存这里）
bool isScreenshotSdReady();

// 统计截图数量（SD + LittleFS）
int countScreenshots();
// 仅 TF / 仅 Flash
int countTfScreenshots();
int countFlashScreenshots();

// 统计截图占用字节数（SD + LittleFS）
size_t screenshotsUsedBytes();
size_t screenshotsUsedBytesTf();
size_t screenshotsUsedBytesFlash();

// LittleFS 总容量 / 已用 / 剩余（失败时写 0）
void getFlashDataSpace(size_t* total, size_t* used, size_t* free_bytes);

// SD 总容量 / 已用 / 剩余（不可用时写 0）
void getSdDataSpace(size_t* total, size_t* used, size_t* free_bytes);

// 枚举全部截图（先 SD 再 LittleFS）；storage 为 "TF" 或 "Flash"
typedef void (*ShotEnumCallback)(const char* storage, const char* basename, size_t size, void* user);
void enumScreenshots(ShotEnumCallback cb, void* user);
void enumTfScreenshots(ShotEnumCallback cb, void* user);
void enumFlashScreenshots(ShotEnumCallback cb, void* user);

// 打开截图文件供读取：SD 优先，再 LittleFS
bool openScreenshotFile(const String& uri, File& out);

// 删除全部截图（两边都清），返回删除个数
int clearAllScreenshots();
// 仅清 TF / 仅清 Flash
int clearTfScreenshots();
int clearFlashScreenshots();

// 删除单张：storage 为 "TF" 或 "Flash"；basename 如 exi2c_gps_live.png
bool deleteScreenshotFile(const char* storage, const char* basename);

// 删除 LittleFS 上最后一张截图（开机腾 Flash 用），成功返回 true
bool deleteLastScreenshot();

// 开机恢复：上次启动崩溃则删最后一张；mkdir 失败时再尝试腾空间
// 并打上 boot_pending；setup 成功结束须调 markScreenshotBootOk()
void recoverScreenshotsOnBoot();

// setup 正常跑完后调用，清除 boot_pending
void markScreenshotBootOk();

// 校验并打开截图文件路径（允许 /shot/*.png 与旧版 .bmp）
bool isSafeShotPath(const String& uri);
