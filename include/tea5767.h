#pragma once

#include <M5Unified.h>
#include <cstdint>

// TEA5767 FM 芯片（优先 Ex_I2C Grove G1/G2，其次 In_I2C G8/G9）
// 频率单位 0.01 MHz（9850 = 98.50 MHz）
class Tea5767 {
  public:
    static constexpr uint8_t I2C_ADDR = 0x60;
    static constexpr uint16_t FREQ_EU_MIN = 8750;  // 87.50 MHz
    static constexpr uint16_t FREQ_EU_MAX = 10800; // 108.00 MHz
    static constexpr uint16_t FREQ_JP_MIN = 7600;  // 76.00 MHz
    static constexpr uint16_t FREQ_JP_MAX = 9100;  // 91.00 MHz
    static constexpr uint16_t FREQ_MIN = FREQ_EU_MIN;
    static constexpr uint16_t FREQ_MAX = FREQ_EU_MAX;
    static constexpr uint16_t FREQ_STEP = 10; // 0.10 MHz

    enum class SeekStop : uint8_t {
        Low = 1,  // SSL=01, ADC≈5
        Mid = 2,  // SSL=10, ADC≈7
        High = 3, // SSL=11, ADC≈10
    };

    enum class ChannelMute : uint8_t {
        Off = 0,
        Left = 1,
        Right = 2,
    };

    struct Status {
        uint16_t freq_centi = 0;
        uint8_t rssi = 0;       // 0-15
        uint8_t if_counter = 0; // 调谐质量，约 0x36 最佳
        uint8_t chip_id = 0;    // TEA5767 = 0
        bool stereo = false;
        bool ready = false;      // RF：搜台结束或 PLL 就绪
        bool band_limit = false; // BLF：扫到频段边界
    };

    bool begin(m5::I2C_Class& bus);
    bool probe() const;
    // I2C 扫描写探测可能误开音频；确认在线后 mute + standby，不调谐
    bool silence(m5::I2C_Class& bus);

    uint16_t freqMin() const { return japan_ ? FREQ_JP_MIN : FREQ_EU_MIN; }
    uint16_t freqMax() const { return japan_ ? FREQ_JP_MAX : FREQ_EU_MAX; }

    // wait_settle=false 时立刻返回，由 App 用 millis 等 PLL 稳定
    void setFrequency(uint16_t freq_centi, bool wait_settle = true);
    uint16_t getFrequency();

    void setMute(bool on);
    void setMono(bool on);
    void setSoftMute(bool on);
    void setHighCut(bool on);             // HCC：弱台削高音降噪
    void setStereoNoiseCancel(bool on);   // SNC
    void setJapanBand(bool on);           // BL：日带 76-91 / 欧美 87.5-108
    void setDeemphasis75(bool on);        // DTC：75µs(美) / 50µs(欧日)
    void setHighSideInjection(bool on);   // HLSI
    void setSeekStop(SeekStop level);     // 硬件搜台停台门限
    void setChannelMute(ChannelMute ch);  // ML / MR
    void setStandby(bool on);
    void setPort1(bool high); // SWP1，模块未接线则无效
    void setPort2(bool high); // SWP2

    bool isMuted() const { return muted_; }
    bool isMono() const { return mono_; }
    bool isSoftMute() const { return soft_mute_; }
    bool isHighCut() const { return hcc_; }
    bool isStereoNoiseCancel() const { return snc_; }
    bool isJapanBand() const { return japan_; }
    bool isDeemphasis75() const { return deemph75_; }
    bool isHighSideInjection() const { return hlsi_high_; }
    bool isStandby() const { return standby_; }
    SeekStop seekStop() const { return ssl_; }
    ChannelMute channelMute() const { return ch_mute_; }

    uint8_t getRssi();
    bool isStereo();
    bool readStatus(Status& out);
    // 同频再写一遍：复位 RF，启动新一轮电平 ADC（只读会拿到上次调谐的冻结值）
    void kickAdc();

    // 芯片硬件搜台（SM/SUD/SSL）；结束后须 abortSearch 清 SM
    void startSearch(bool up);
    void abortSearch();
    bool isSearching() const { return searching_; }

  private:
    m5::I2C_Class* bus_ = nullptr;
    uint8_t regs_[5]{};
    uint16_t freq_ = 9850;
    bool muted_ = false;
    bool mono_ = false;
    bool soft_mute_ = true;
    bool hcc_ = true;
    bool snc_ = true;
    bool japan_ = false;
    bool deemph75_ = false;
    bool hlsi_high_ = true;
    bool standby_ = false;
    bool port1_ = false;
    bool port2_ = false;
    bool searching_ = false;
    bool search_up_ = true;
    bool search_ind_ = false; // SI：搜台时 SWPORT1 作就绪指示
    SeekStop ssl_ = SeekStop::Mid;
    ChannelMute ch_mute_ = ChannelMute::Off;

    bool writeRegs();
    bool readRaw(uint8_t out[5]);
    void packRegs();
    uint32_t pllFromFreq(uint16_t freq_centi) const;
    uint16_t freqFromPll(uint32_t pll) const;
    uint16_t clampFreq(uint16_t freq_centi) const;
};
