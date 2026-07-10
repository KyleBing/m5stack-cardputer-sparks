#pragma once

#include <cstdint>

struct MiioResult {
    bool ok;
    char message[96];
};

// 解析 32 位 hex token
bool miioParseTokenHex(const char* hex, uint8_t token[16]);

// 查询 power（get_prop，返回 on/off）
MiioResult miioGetPower(const char* ip, const char* token_hex, bool& on);

// 设置 power（set_power on/off，yeelink 等设备必须用此接口）
MiioResult miioSetPower(const char* ip, const char* token_hex, bool on);

// 灯：查询 power + bright（1-100）+ ct（色温 K，可选）
MiioResult miioGetLightStatus(const char* ip, const char* token_hex, bool& on, int& bright,
                              bool& bright_known, int& color_temp, bool& ct_known);

// 灯：设置亮度 1-100
MiioResult miioSetBright(const char* ip, const char* token_hex, int bright);

// 灯：设置色温（Kelvin，set_ct_abx）
MiioResult miioSetColorTemp(const char* ip, const char* token_hex, int kelvin);

// dmaker.fan.p5：查询 power/speed/roll/mode(0=normal 1=nature)
MiioResult miioFanP5GetStatus(const char* ip, const char* token_hex, bool& on, int& speed,
                              bool& roll, int& mode);

// dmaker.fan.p5：开关（s_power，bool）
MiioResult miioFanP5SetPower(const char* ip, const char* token_hex, bool on);

// dmaker.fan.p5：风速 0-100
MiioResult miioFanP5SetSpeed(const char* ip, const char* token_hex, int speed);

// dmaker.fan.p5：摇头
MiioResult miioFanP5SetRoll(const char* ip, const char* token_hex, bool on);

// dmaker.fan.p5：模式 normal / nature
MiioResult miioFanP5SetMode(const char* ip, const char* token_hex, const char* mode);

// 通用风扇：查询 power + speed_level(1-4)
MiioResult miioFanGetStatus(const char* ip, const char* token_hex, bool& on, int& speed_level);

// 通用风扇：档位 1-4
MiioResult miioFanSetSpeedLevel(const char* ip, const char* token_hex, int level);

// dmaker.airpurifier.f20：查询 power/mode/fan_level/aqi
MiioResult miioF20GetStatus(const char* ip, const char* token_hex, const char* did, bool& on,
                            int& mode, int& fan_level, int& aqi);

// dmaker.airpurifier.f20：开关
MiioResult miioF20SetPower(const char* ip, const char* token_hex, const char* did, bool on);

// dmaker.airpurifier.f20：模式 0-5
MiioResult miioF20SetMode(const char* ip, const char* token_hex, const char* did, int mode);

// dmaker.airpurifier.f20：风速档 0-5
MiioResult miioF20SetFanLevel(const char* ip, const char* token_hex, const char* did, int level);
