#pragma once

#include "M5Cardputer.h"
#include <WString.h>

void drawI2cScanApp(m5::I2C_Class& bus, const char* title, bool internal_bus);
void handleI2cScanApp(const String& key, m5::I2C_Class& bus, const char* title, bool internal_bus);
bool isI2cScanHelpVisible();
bool closeI2cScanHelp(m5::I2C_Class& bus, const char* title, bool internal_bus);
void resetI2cScanHelp();
// 截图功能名：main / help
void getI2cScanShotFeature(char* out, size_t out_len);
