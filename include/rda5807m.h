#pragma once

#include <M5Unified.h>
#include <cstddef>
#include <cstdint>

// RDA5807M FM/RDS 驱动；所有频率均以 0.01 MHz 表示。
class Rda5807m {
  public:
    static constexpr uint8_t RANDOM_ADDRESS = 0x11;
    static constexpr uint8_t SEQUENTIAL_ADDRESS = 0x10;
    static constexpr uint16_t DEFAULT_FREQUENCY = 9850;

    enum class Band : uint8_t {
        UsEurope,  // 87-108 MHz
        Japan,     // 76-91 MHz
        Worldwide, // 76-108 MHz
        EastEurope,// 65-76 MHz
        Low,       // 50-76 MHz
    };

    enum class Spacing : uint16_t {
        Khz25 = 25,
        Khz50 = 50,
        Khz100 = 100,
        Khz200 = 200,
    };

    enum class Clock : uint8_t {
        Khz32_768 = 0,
        Mhz12 = 1,
        Mhz13 = 2,
        Mhz19_2 = 3,
        Mhz24 = 5,
        Mhz26 = 6,
        Mhz38_4 = 7,
    };

    enum class LnaPort : uint8_t {
        None = 0,
        Negative = 1,
        Positive = 2,
        Dual = 3,
    };

    enum class LnaCurrent : uint8_t {
        Ma1_8 = 0,
        Ma2_1 = 1,
        Ma2_5 = 2,
        Ma3_0 = 3,
    };

    enum class GpioMode : uint8_t {
        HighZ = 0,
        Special = 1,
        Low = 2,
        High = 3,
    };

    enum class I2sSampleRate : uint8_t {
        Khz8 = 0,
        Khz11_025 = 1,
        Khz12 = 2,
        Khz16 = 3,
        Khz22_05 = 4,
        Khz24 = 5,
        Khz32 = 6,
        Khz44_1 = 7,
        Khz48 = 8,
    };

    struct I2sConfig {
        bool slave = false;
        bool left_when_ws_low = false;
        bool invert_input_clock = false;
        bool signed_data = true;
        bool invert_input_ws = false;
        I2sSampleRate sample_rate = I2sSampleRate::Khz48;
        bool invert_output_ws = false;
        bool invert_output_clock = false;
        bool left_delay = false;
        bool right_delay = false;
    };

    struct Status {
        uint16_t freq_centi = 0;
        uint16_t readchan = 0;
        uint8_t rssi = 0; // 0-127
        bool stereo = false;
        bool stc = false;
        bool seek_failed = false;
        bool rds_ready = false;
        bool rds_synced = false;
        bool fm_true = false;
        bool fm_ready = false;
    };

    struct RdsGroup {
        uint16_t a = 0;
        uint16_t b = 0;
        uint16_t c = 0;
        uint16_t d = 0;
        uint8_t block_a_errors = 0;
        uint8_t block_b_errors = 0;
        bool block_e = false;
    };

    struct RdsClock {
        bool valid = false;
        uint32_t mjd = 0;
        int16_t year = 0;
        uint8_t month = 0;
        uint8_t day = 0;
        uint8_t hour = 0;   // UTC
        uint8_t minute = 0; // UTC
        int16_t local_offset_minutes = 0;
    };

    bool begin(m5::I2C_Class& bus, bool reset = true);
    bool probe(uint16_t* chip_id = nullptr);
    bool initialize();
    bool softReset();
    // I2C 扫描写探测可能误开射频；确认芯片后清 ENABLE，音频高阻
    bool silence(m5::I2C_Class& bus);
    bool setStandby(bool standby);
    bool isStandby() const { return standby_; }

    bool setBand(Band band);
    bool setSpacing(Spacing spacing);
    Band band() const { return band_; }
    Spacing spacing() const { return spacing_; }
    uint16_t frequencyMin() const;
    uint16_t frequencyMax() const;

    // wait=false 只启动调谐；随后可用 pollTune() 或 waitTune() 完成。
    bool setFrequency(uint16_t freq_centi, bool wait = true, uint32_t timeout_ms = 500);
    bool startTune(uint16_t freq_centi);
    bool pollTune(Status* status = nullptr);
    bool waitTune(uint32_t timeout_ms = 500, Status* status = nullptr);
    uint16_t frequency() const { return frequency_; }

    bool startSeek(bool up);
    bool abortSeek();
    bool waitSeek(uint32_t timeout_ms = 5000, Status* status = nullptr);
    bool isSeeking() const { return seeking_; }

    bool setMute(bool mute);
    bool setMono(bool mono);
    bool setHighZ(bool high_z);
    bool setVolume(uint8_t volume);
    bool setBass(bool enabled);
    bool setDeemphasis50(bool use_50us);
    bool setSoftMute(bool enabled);
    bool setSoftBlend(bool enabled);
    bool setSoftBlendThreshold(uint8_t threshold);
    bool setAfc(bool enabled);
    bool setNewMethod(bool enabled);
    bool setSeekThreshold(uint8_t threshold);
    bool setSeekMode(uint8_t mode);
    bool setOldSeekThreshold(uint8_t threshold);
    bool setSeekWrap(bool wrap);
    bool setInterruptMode(bool latched);

    bool setClock(Clock clock, bool direct_input = false, bool non_calibrated = false);
    bool setLna(LnaPort port, LnaCurrent current);
    bool setGpio(uint8_t gpio, GpioMode mode);
    bool setTuneInterrupt(bool enabled, bool latched = true);

    bool setRds(bool enabled);
    bool setRbds(bool enabled);
    bool setRdsFifo(bool enabled);
    bool clearRdsFifo();
    bool setI2s(bool enabled);
    bool configureI2s(const I2sConfig& config);

    // 直接频率模式使用数据手册定义的基准频率加 kHz 偏移。
    bool setDirectFrequency(uint16_t freq_centi, bool wait = true, uint32_t timeout_ms = 500);
    bool setDirectFrequencyOffset(uint16_t offset_khz, bool enabled);

    bool readStatus(Status& status);
    bool readRdsGroup(RdsGroup& group);
    bool pollRds(RdsGroup* group = nullptr);
    void clearRds();

    uint16_t programId() const { return pi_; }
    uint8_t programType() const { return pty_; }
    bool trafficProgram() const { return tp_; }
    bool trafficAnnouncement() const { return ta_; }
    const char* programService() const { return ps_; }
    const char* radioText() const { return rt_; }
    const RdsClock& clockTime() const { return clock_time_; }
    const uint16_t* alternativeFrequencies() const { return af_; }
    size_t alternativeFrequencyCount() const { return af_count_; }

    // 诊断 API 仅允许公开寄存器；写操作限制在公开可写的 0x02-0x08。
    bool readRegister(uint8_t reg, uint16_t& value);
    bool writeRegister(uint8_t reg, uint16_t value);
    bool readRegisters(uint8_t first_reg, uint16_t* values, size_t count);
    uint16_t shadowRegister(uint8_t reg) const;

  private:
    static constexpr uint8_t FIRST_SHADOW_REG = 0x02;
    static constexpr uint8_t LAST_SHADOW_REG = 0x08;
    static constexpr size_t SHADOW_COUNT = LAST_SHADOW_REG - FIRST_SHADOW_REG + 1;

    m5::I2C_Class* bus_ = nullptr;
    uint16_t shadow_[SHADOW_COUNT]{};
    Band band_ = Band::UsEurope;
    Spacing spacing_ = Spacing::Khz100;
    uint16_t frequency_ = DEFAULT_FREQUENCY;
    bool standby_ = true;
    bool tuning_ = false;
    bool seeking_ = false;

    uint16_t pi_ = 0;
    uint8_t pty_ = 0;
    bool tp_ = false;
    bool ta_ = false;
    char ps_[9]{};
    char ps_stage_[8]{};
    char ps_seen_[8]{};
    uint8_t ps_mask_ = 0;
    char rt_[65]{};
    char rt_stage_[64]{};
    char rt_seen_[64]{};
    uint16_t rt_mask_ = 0;
    uint8_t rt_ab_ = 0xFF;
    bool rt_is_2b_ = false;
    RdsClock clock_time_{};
    uint32_t ct_candidate_mjd_ = 0;
    uint16_t af_[25]{};
    size_t af_count_ = 0;

    bool writeShadow(uint8_t reg);
    bool writeAllShadows();
    bool modifyShadow(uint8_t reg, uint16_t mask, uint16_t value);
    bool writeRandom(uint8_t reg, uint16_t value);
    bool readRandom(uint8_t reg, uint16_t* values, size_t count);
    bool readSequential(uint16_t* values, size_t count);
    void setDefaults();
    void syncBandBits();
    uint16_t clampFrequency(uint16_t freq_centi) const;
    uint16_t channelFromFrequency(uint16_t freq_centi) const;
    uint16_t frequencyFromChannel(uint16_t channel) const;
    void decodeRds(const RdsGroup& group);
    void decodePs(uint16_t block_b, uint16_t block_d);
    void decodeRt(uint16_t block_b, uint16_t block_c, uint16_t block_d);
    void decodeCt(uint16_t block_b, uint16_t block_c, uint16_t block_d);
    void decodeAf(uint16_t block_c);
    void addAlternativeFrequency(uint8_t code);
};
