#pragma once

#include "M5Cardputer.h"
#include "rda5807m.h"
#include "tea5767.h"
#include <cstdint>

// Radio App 使用的双芯片门面；芯片专属高级 API 仍由各驱动提供。
class FmTuner {
  public:
    enum class Chip : uint8_t {
        None = 0,
        Tea5767,
        Rda5807m,
    };

    enum class SeekStop : uint8_t {
        Low = 1,
        Mid = 2,
        High = 3,
    };

    enum class ChannelMute : uint8_t {
        Off = 0,
        Left = 1,
        Right = 2,
    };

    struct Status {
        uint16_t freq_centi = 0;
        uint8_t rssi = 0; // 统一为 0-15，便于共用信号条和软件搜台
        uint8_t raw_rssi = 0;
        uint8_t quality = 0;
        uint16_t chip_id = 0;
        bool stereo = false;
        bool ready = false;
        bool band_limit = false;
        bool valid_station = false;
        bool rds_ready = false;
        bool rds_synced = false;
    };

    bool begin(m5::I2C_Class& bus);
    void detach();
    // 扫描后把总线上的 TEA5767 / RDA5807M 收回待机，避免嘶声
    static void silenceIfPresent(m5::I2C_Class& bus);

    Chip chip() const { return chip_; }
    bool isRda() const { return chip_ == Chip::Rda5807m; }
    const char* chipName() const;

    uint16_t freqMin() const;
    uint16_t freqMax() const;
    uint16_t freqStep() const;

    void setFrequency(uint16_t freq_centi, bool wait_settle = true);
    bool readStatus(Status& out);
    uint8_t getRssi();
    bool isStereo();
    void refreshSignal();

    void setMute(bool on);
    void setMono(bool on);
    void setStandby(bool on);

    void startSearch(bool up);
    void abortSearch();
    bool isSearching() const;

    // TEA5767 专属设置；RDA 模式下不产生寄存器写入。
    void setJapanBand(bool on);
    void setDeemphasis75(bool on);
    void setHighSideInjection(bool on);
    void setSoftMute(bool on);
    void setHighCut(bool on);
    void setStereoNoiseCancel(bool on);
    void setSeekStop(SeekStop level);
    void setChannelMute(ChannelMute ch);

    Tea5767& tea() { return tea_; }
    Rda5807m& rda() { return rda_; }
    const Rda5807m& rda() const { return rda_; }

  private:
    Chip chip_ = Chip::None;
    uint16_t chip_id_ = 0;
    Tea5767 tea_;
    Rda5807m rda_;
};
