# bM5Unified API 参考

> 基于 `.pio/libdeps/m5stack-cardputer/M5Unified/` 库整理。  
> 全局实例：`extern m5::M5Unified M5;`  
> Display 绘图 API 见 [Display.md](./Display.md)。

## 概述

M5Unified 是 M5Stack 设备的统一初始化库，自动检测板型并管理屏幕、按键、电源、音频、IMU、RTC 等外设。

```cpp
#include <M5Unified.h>

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
}

void loop() {
    M5.update();  // 必须在 loop 中调用，更新按键/触摸状态
}
```

---

## M5Unified 主类

### 成员对象


| 成员                | 类型              | 说明                  |
| ----------------- | --------------- | ------------------- |
| `Display` / `Lcd` | `M5GFX`         | 主显示屏（同引用）           |
| `Imu`             | `IMU_Class`     | 惯性测量单元              |
| `Log`             | `Log_Class`     | 日志输出                |
| `Power`           | `Power_Class`   | 电源管理                |
| `Rtc`             | `RTC_Class`     | 实时时钟                |
| `Touch`           | `Touch_Class`   | 触摸                  |
| `Speaker`         | `Speaker_Class` | 扬声器                 |
| `Mic`             | `Mic_Class`     | 麦克风                 |
| `Led`             | `LED_Class`     | RGB LED             |
| `BtnA`            | `Button_Class&` | 按键 A（`_buttons[0]`） |
| `BtnB`            | `Button_Class&` | 按键 B                |
| `BtnC`            | `Button_Class&` | 按键 C                |
| `BtnEXT`          | `Button_Class&` | 扩展按键（CoreInk 顶部）    |
| `BtnPWR`          | `Button_Class&` | 电源键                 |
| `In_I2C`          | `I2C_Class&`    | 内部 I2C 总线           |
| `Ex_I2C`          | `I2C_Class&`    | 外部 I2C 总线（Port.A）   |


### 初始化


| 方法                    | 说明                    |
| --------------------- | --------------------- |
| `begin()`             | 默认配置初始化               |
| `begin(config_t cfg)` | 自定义配置初始化（只能执行一次）      |
| `update()`            | 更新按键/触摸状态，需在 loop 中调用 |
| `config()`            | 返回默认 `config_t`（静态）   |
| `getBoard()`          | 获取当前板型 `board_t`      |
| `getUpdateMsec()`     | 上次 update 的时间戳（毫秒）    |


### 时间工具（静态）


| 方法                      | 说明                      |
| ----------------------- | ----------------------- |
| `M5.delay(ms)`          | 延时（FreeRTOS vTaskDelay） |
| `M5.millis()`           | 毫秒计时                    |
| `M5.micros()`           | 微秒计时                    |
| `M5.getPin(pin_name_t)` | 获取引脚编号                  |


### 多屏管理


| 方法                                      | 说明                |
| --------------------------------------- | ----------------- |
| `getDisplay(index)` / `Displays(index)` | 获取指定索引的显示屏        |
| `getDisplayCount()`                     | 已注册屏幕数量           |
| `addDisplay(M5GFX& dsp)`                | 注册外接屏幕            |
| `getDisplayIndex(board)`                | 按板型查找屏幕索引（-1=未找到） |
| `setPrimaryDisplay(index)`              | 设为主屏              |
| `setPrimaryDisplayType(board)`          | 按板型设为主屏           |
| `setLogDisplayIndex(index)`             | 设置日志输出屏幕          |
| `setLogDisplayType(board)`              | 按板型设置日志屏幕         |


### 按键


| 方法                                    | 说明          |
| ------------------------------------- | ----------- |
| `getButton(index)` / `Buttons(index)` | 获取按键对象（0~4） |


### 触摸虚拟按键


| 方法                                   | 说明         |
| ------------------------------------ | ---------- |
| `setTouchButtonHeight(pixel)`        | 设置触摸虚拟按键高度 |
| `setTouchButtonHeightByRatio(ratio)` | 按比例设置      |
| `getTouchButtonHeight()`             | 获取当前高度     |


### IO 扩展


| 方法                   | 说明                 |
| -------------------- | ------------------ |
| `getIOExpander(idx)` | 获取 IO 扩展器（idx & 1） |


---

## config_t 配置项

```cpp
auto cfg = M5.config();
cfg.serial_baudrate = 115200;   // Serial 波特率（0=禁用）
cfg.clear_display   = true;     // 启动时清屏
cfg.output_power    = true;     // 外部端口 5V 输出
cfg.pmic_button     = true;     // 使用 PMIC 电源键
cfg.internal_imu    = true;     // 内部 IMU
cfg.internal_rtc    = true;     // 内部 RTC
cfg.internal_mic    = true;     // 麦克风
cfg.internal_spk    = true;     // 扬声器
cfg.external_imu    = false;    // 外部 Unit IMU
cfg.external_rtc    = false;    // 外部 Unit RTC
cfg.disable_rtc_irq = true;     // 启动时关闭 RTC 中断
cfg.led_brightness  = 0;        // 系统 LED 亮度（0~255）
cfg.fallback_board  = ...;      // 自动检测失败时的回退板型
M5.begin(cfg);
```

### 外接屏幕 / 扬声器（位域）


| 字段                                | 说明                 |
| --------------------------------- | ------------------ |
| `external_display.module_display` | Module Display     |
| `external_display.atom_display`   | Atom Display       |
| `external_display.unit_oled`      | Unit OLED          |
| `external_display.unit_lcd`       | Unit LCD           |
| `external_speaker.module_display` | Module Display 扬声器 |
| `external_speaker.hat_spk`        | Speaker HAT        |


---

## pin_name_t 引脚名

常用枚举（完整列表见 `M5Unified.hpp`）：


| 枚举                                                          | 说明             |
| ----------------------------------------------------------- | -------------- |
| `in_i2c_scl` / `in_i2c_sda`                                 | 内部 I2C         |
| `port_a_scl` / `port_a_sda`                                 | Port A（外部 I2C） |
| `port_b_in` / `port_b_out`                                  | Port B         |
| `port_c_rxd` / `port_c_txd`                                 | Port C UART    |
| `sd_spi_sclk` / `sd_spi_mosi` / `sd_spi_miso` / `sd_spi_cs` | SD 卡 SPI       |
| `rgb_led`                                                   | RGB LED        |
| `power_hold`                                                | 电源保持           |


---

## Button_Class 按键


| 方法                                          | 说明             |
| ------------------------------------------- | -------------- |
| `isPressed()` / `isReleased()`              | 当前按下/释放        |
| `wasPressed()` / `wasReleased()`            | 边沿：刚按下/刚释放     |
| `wasClicked()`                              | 短按并释放          |
| `wasHold()`                                 | 长按             |
| `wasSingleClicked()` / `wasDoubleClicked()` | 单击/双击          |
| `wasDecideClickCount()`                     | 点击次数已确定        |
| `getClickCount()`                           | 点击次数           |
| `isHolding()`                               | 正在长按           |
| `pressedFor(ms)` / `releasedFor(ms)`        | 持续按下/释放时长      |
| `wasReleaseFor(ms)`                         | 长按后释放且超过 ms    |
| `setDebounceThresh(ms)`                     | 防抖阈值（默认 10ms）  |
| `setHoldThresh(ms)`                         | 长按阈值（默认 500ms） |
| `getState()`                                | 当前状态枚举         |


---

## Power_Class 电源


| 方法                                            | 说明                     |
| --------------------------------------------- | ---------------------- |
| `begin()`                                     | 初始化                    |
| `getBatteryLevel()`                           | 电量 0~100               |
| `getBatteryVoltage()`                         | 电池电压 [mV]              |
| `getBatteryCurrent()`                         | 电池电流 [mA]（+=充电 / -=放电） |
| `isCharging()`                                | 充电状态                   |
| `setBatteryCharge(enable)`                    | 充电开关                   |
| `setChargeCurrent(mA)`                        | 充电电流                   |
| `setChargeVoltage(mV)`                        | 充电电压                   |
| `getVBUSVoltage()`                            | VBUS 电压 [mV]           |
| `getKeyState()`                               | 电源键状态（AXP 系列）          |
| `setLed(brightness)`                          | 电源 LED                 |
| `powerOff()`                                  | 关机                     |
| `timerSleep(seconds)`                         | 定时唤醒                   |
| `timerSleep(time)` / `timerSleep(date, time)` | RTC 定时唤醒               |
| `deepSleep(us)`                               | 深度睡眠                   |
| `lightSleep(us)`                              | 轻度睡眠                   |
| `setExtOutput(enable, mask)`                  | 外部端口电源输出               |
| `getExtOutput()`                              | 获取外部端口输出状态             |
| `setUsbOutput(enable)`                        | USB 端口输出（CoreS3）       |
| `setVibration(level)`                         | 振动马达                   |
| `getType()`                                   | PMIC 类型                |


---

## Speaker_Class 扬声器


| 方法                                            | 说明           |
| --------------------------------------------- | ------------ |
| `begin()` / `end()`                           | 初始化/结束       |
| `isEnabled()` / `isRunning()`                 | 是否可用/运行中     |
| `isPlaying()` / `isPlaying(channel)`          | 是否正在播放       |
| `getPlayingChannels()`                        | 正在播放的通道数     |
| `setVolume(0~255)` / `getVolume()`            | 主音量          |
| `setChannelVolume(ch, vol)`                   | 通道音量         |
| `stop()` / `stop(channel)`                    | 停止播放         |
| `tone(freq, duration, channel, stop_current)` | 播放单音         |
| `playRaw(data, len, sample_rate, ...)`        | 播放原始 PCM     |
| `playWav(wav_data, len, repeat, channel)`     | 播放 WAV       |
| `config()` / `config(cfg)`                    | 获取/设置 I2S 配置 |


---

## Mic_Class 麦克风


| 方法                                       | 说明           |
| ---------------------------------------- | ------------ |
| `begin()` / `end()`                      | 初始化/结束       |
| `isEnabled()` / `isRunning()`            | 是否可用/运行中     |
| `isRecording()`                          | 是否正在录音       |
| `setSampleRate(hz)`                      | 采样率          |
| `record(data, len)`                      | 录音到缓冲区       |
| `record(data, len, sample_rate, stereo)` | 指定采样率录音      |
| `config()` / `config(cfg)`               | 获取/设置 I2S 配置 |


---

## RTC_Class 实时时钟


| 方法                                               | 说明             |
| ------------------------------------------------ | -------------- |
| `begin(i2c, board)`                              | 初始化            |
| `isEnabled()`                                    | 是否可用           |
| `getDateTime(date, time)`                        | 获取日期时间         |
| `getDate()` / `getTime()`                        | 获取日期/时间        |
| `setDateTime(date, time)`                        | 设置日期时间         |
| `setSystemTimeFromRtc()`                         | 同步到 ESP32 系统时间 |
| `setTimerIRQ(msec)`                              | 定时中断           |
| `setAlarmIRQ(time)` / `setAlarmIRQ(date, time)`  | 闹钟             |
| `getIRQstatus()` / `clearIRQ()` / `disableIRQ()` | 中断管理           |
| `getVoltLow()`                                   | 低电压检测          |


---

## IMU_Class 惯性传感器


| 方法                                          | 说明          |
| ------------------------------------------- | ----------- |
| `begin(i2c, board)`                         | 初始化         |
| `isEnabled()`                               | 是否可用        |
| `getType()`                                 | IMU 型号      |
| `update()`                                  | 更新传感器数据     |
| `getImuData(data)` / `getImuData()`         | 获取完整数据      |
| `getAccel(ax, ay, az)`                      | 加速度 [g]     |
| `getGyro(gx, gy, gz)`                       | 角速度 [°/s]   |
| `getMag(mx, my, mz)`                        | 磁力计 [μT]    |
| `getTemp(t)`                                | 温度          |
| `sleep()`                                   | 休眠          |
| `setAxisOrder(...)`                         | 设置轴顺序       |
| `setCalibration(accel, gyro, mag)`          | 校准强度        |
| `saveOffsetToNVS()` / `loadOffsetFromNVS()` | NVS 保存/加载偏移 |


### imu_data_t 结构

```cpp
struct imu_data_t {
    uint32_t usec;
    imu_3d_t accel;  // x, y, z
    imu_3d_t gyro;
    imu_3d_t mag;
};
```

---

## Touch_Class 触摸


| 方法                         | 说明        |
| -------------------------- | --------- |
| `begin(gfx)` / `end()`     | 绑定/解绑显示屏  |
| `update(msec)`             | 更新触摸状态    |
| `getCount()`               | 触摸点数量     |
| `getDetail(index)`         | 触摸详情（含手势） |
| `getTouchPointRaw(index)`  | 原始触摸坐标    |
| `setHoldThresh(msec)`      | 长按阈值      |
| `setFlickThresh(distance)` | 轻扫阈值      |


### touch_detail_t 手势判断


| 方法                               | 说明     |
| -------------------------------- | ------ |
| `isPressed()` / `wasPressed()`   | 按下     |
| `wasClicked()` / `wasReleased()` | 点击/释放  |
| `isHolding()` / `wasHold()`      | 长按     |
| `isDragging()` / `wasDragged()`  | 拖拽     |
| `isFlicking()` / `wasFlicked()`  | 轻扫     |
| `deltaX()` / `deltaY()`          | 相对上次位移 |
| `getClickCount()`                | 点击次数   |


---

## Log_Class 日志


| 方法/宏                                        | 说明         |
| ------------------------------------------- | ---------- |
| `M5_LOGE(fmt, ...)`                         | Error 日志   |
| `M5_LOGW(fmt, ...)`                         | Warn 日志    |
| `M5_LOGI(fmt, ...)`                         | Info 日志    |
| `M5_LOGD(fmt, ...)`                         | Debug 日志   |
| `M5_LOGV(fmt, ...)`                         | Verbose 日志 |
| `M5.Log(level, fmt, ...)`                   | 通用日志       |
| `M5.Log.printf()` / `print()` / `println()` | 直接输出（忽略级别） |
| `setLogLevel(target, level)`                | 设置日志级别     |
| `setDisplay(gfx)`                           | 日志输出到屏幕    |
| `setCallback(function)`                     | 自定义回调      |
| `dump(addr, len, level)`                    | 十六进制 dump  |


---

## LED_Class RGB LED


| 方法                                 | 说明        |
| ---------------------------------- | --------- |
| `begin()` / `display()`            | 初始化/刷新显示  |
| `getCount()`                       | LED 数量    |
| `setBrightness(0~255)`             | 亮度        |
| `setAllColor(r, g, b)`             | 全部同色      |
| `setColor(index, r, g, b)`         | 单个 LED 颜色 |
| `setColors(values, index, length)` | 批量设置      |
| `isEnabled()`                      | 是否可用      |


---

## I2C_Class I2C 总线

全局实例：`m5::In_I2C`（内部）、`m5::Ex_I2C`（外部 Port.A）


| 方法                                      | 说明            |
| --------------------------------------- | ------------- |
| `begin(port, sda, scl)`                 | 初始化           |
| `release()`                             | 释放            |
| `start(addr, read, freq)`               | 发送 START + 地址 |
| `restart(addr, read, freq)`             | 重复 START      |
| `stop()`                                | 发送 STOP       |
| `write(data)` / `write(data, len)`      | 写数据           |
| `read(result, len)`                     | 读数据           |
| `writeRegister8(addr, reg, data, freq)` | 写寄存器          |
| `readRegister8(addr, reg, freq)`        | 读寄存器          |
| `writeRegister()` / `readRegister()`    | 多字节寄存器读写      |
| `bitOn()` / `bitOff()`                  | 寄存器位操作        |
| `scanID(result, freq)`                  | I2C 扫描        |
| `getPort()` / `getSDA()` / `getSCL()`   | 获取端口/引脚       |
| `isEnabled()`                           | 是否已初始化        |


---

## 源码路径


| 文件                                        | 内容    |
| ----------------------------------------- | ----- |
| `M5Unified/src/M5Unified.hpp`             | 主类定义  |
| `M5Unified/src/M5Unified.h`               | 入口头文件 |
| `M5Unified/src/utility/Button_Class.hpp`  | 按键    |
| `M5Unified/src/utility/Power_Class.hpp`   | 电源    |
| `M5Unified/src/utility/Speaker_Class.hpp` | 扬声器   |
| `M5Unified/src/utility/Mic_Class.hpp`     | 麦克风   |
| `M5Unified/src/utility/RTC_Class.hpp`     | RTC   |
| `M5Unified/src/utility/IMU_Class.hpp`     | IMU   |
| `M5Unified/src/utility/Touch_Class.hpp`   | 触摸    |
| `M5Unified/src/utility/Log_Class.hpp`     | 日志    |
| `M5Unified/src/utility/LED_Class.hpp`     | LED   |
| `M5Unified/src/utility/I2C_Class.hpp`     | I2C   |


