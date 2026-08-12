#pragma once

#include <M5Unified.h>
#include <cstdint>

// TEA5767 FM 芯片（Ex_I2C / Port.A），频率单位 0.01 MHz（9850 = 98.50 MHz）
class Tea5767 {
  public:
    static constexpr uint8_t I2C_ADDR = 0x60;
    static constexpr uint16_t FREQ_MIN = 8750;  // 87.50 MHz
    static constexpr uint16_t FREQ_MAX = 10800; // 108.00 MHz
    static constexpr uint16_t FREQ_STEP = 10;   // 0.10 MHz

    bool begin(m5::I2C_Class& bus);
    bool probe() const;

    void setFrequency(uint16_t freq_centi);
    uint16_t getFrequency();

    void setMute(bool on);
    void setMono(bool on);
    bool isMuted() const { return muted_; }
    bool isMono() const { return mono_; }

    uint8_t getRssi(); // 0-15
    bool isStereo();

    // 按 0.1 MHz 步进搜台，返回是否找到信号
    bool seek(bool up, uint8_t rssi_threshold = 4);

  private:
    m5::I2C_Class* bus_ = nullptr;
    uint8_t regs_[5]{};
    bool muted_ = false;
    bool mono_ = false;

    bool writeRegs();
    bool readStatus(uint8_t out[5]);
    void applyFrequencyWord(uint16_t freq_centi);
};
