#include "rda5807m.h"

#include <Arduino.h>
#include <cstring>

namespace {

constexpr uint32_t I2C_FREQUENCY = 100000;

constexpr uint16_t REG02_DHIZ = 1U << 15;
constexpr uint16_t REG02_DMUTE = 1U << 14;
constexpr uint16_t REG02_MONO = 1U << 13;
constexpr uint16_t REG02_BASS = 1U << 12;
constexpr uint16_t REG02_RCLK_NON_CAL = 1U << 11;
constexpr uint16_t REG02_RCLK_DIRECT = 1U << 10;
constexpr uint16_t REG02_SEEKUP = 1U << 9;
constexpr uint16_t REG02_SEEK = 1U << 8;
constexpr uint16_t REG02_SKMODE = 1U << 7;
constexpr uint16_t REG02_CLK_MASK = 7U << 4;
constexpr uint16_t REG02_RDS_EN = 1U << 3;
constexpr uint16_t REG02_NEW_METHOD = 1U << 2;
constexpr uint16_t REG02_SOFT_RESET = 1U << 1;
constexpr uint16_t REG02_ENABLE = 1U;

constexpr uint16_t REG03_CHAN_MASK = 0xFFC0;
constexpr uint16_t REG03_TUNE = 1U << 4;
constexpr uint16_t REG03_BAND_MASK = 3U << 2;
constexpr uint16_t REG03_SPACE_MASK = 3U;

constexpr uint16_t REG04_STCIEN = 1U << 14;
constexpr uint16_t REG04_RBDS = 1U << 13;
constexpr uint16_t REG04_RDS_FIFO_EN = 1U << 12;
constexpr uint16_t REG04_DE = 1U << 11;
constexpr uint16_t REG04_RDS_FIFO_CLR = 1U << 10;
constexpr uint16_t REG04_SOFTMUTE = 1U << 9;
constexpr uint16_t REG04_AFCD = 1U << 8;
constexpr uint16_t REG04_I2S_ENABLE = 1U << 6;

constexpr uint16_t REG05_INT_MODE = 1U << 15;
constexpr uint16_t REG05_SEEK_MODE_MASK = 3U << 13;
constexpr uint16_t REG05_SEEKTH_MASK = 0x0F00;
constexpr uint16_t REG05_LNA_PORT_MASK = 3U << 6;
constexpr uint16_t REG05_LNA_CURRENT_MASK = 3U << 4;
constexpr uint16_t REG05_VOLUME_MASK = 0x000F;

constexpr uint16_t REG07_BLEND_THRESHOLD = 16U << 10;
constexpr uint16_t REG07_LOW_BAND_65 = 1U << 9;
constexpr uint16_t REG07_OLD_SEEKTH_MASK = 0x00FC;
constexpr uint16_t REG07_SOFTBLEND = 1U << 1;
constexpr uint16_t REG07_FREQ_MODE = 1U;

constexpr uint16_t REG0A_RDSR = 1U << 15;
constexpr uint16_t REG0A_STC = 1U << 14;
constexpr uint16_t REG0A_SF = 1U << 13;
constexpr uint16_t REG0A_RDSS = 1U << 12;
constexpr uint16_t REG0A_STEREO = 1U << 10;
constexpr uint16_t REG0A_READCHAN_MASK = 0x03FF;

constexpr uint16_t REG0B_RSSI_MASK = 0xFE00;
constexpr uint16_t REG0B_FM_TRUE = 1U << 8;
constexpr uint16_t REG0B_FM_READY = 1U << 7;
constexpr uint16_t REG0B_ABCD_E = 1U << 4;

bool isPublicRegister(const uint8_t reg) {
    return reg == 0x00 || (reg >= 0x02 && reg <= 0x0F);
}

uint16_t publicWriteMask(const uint8_t reg) {
    switch (reg) {
        case 0x02:
            return 0xFFFF;
        case 0x03:
            return 0xFFDF; // DIRECT_MODE 是测试位，不允许开启。
        case 0x04:
            return 0x7F7F;
        case 0x05:
            return 0xEFFF;
        case 0x06:
            return 0x1FFF; // OPEN_MODE 固定为 00，禁止隐藏寄存器写入。
        case 0x07:
            return 0x7EFF;
        case 0x08:
            return 0xFFFF;
        default:
            return 0;
    }
}

} // namespace

bool Rda5807m::begin(m5::I2C_Class& bus, const bool reset) {
    bus_ = &bus;
    if (!bus_->isEnabled() || !probe()) {
        bus_ = nullptr;
        return false;
    }
    return reset ? softReset() : initialize();
}

bool Rda5807m::probe(uint16_t* chip_id) {
    if (!bus_ || !bus_->isEnabled()) {
        return false;
    }
    uint16_t id = 0;
    if (!readRandom(0x00, &id, 1)) {
        return false;
    }
    if (chip_id) {
        *chip_id = id;
    }
    return (id & 0xFF00U) == 0x5800U;
}

void Rda5807m::setDefaults() {
    // 上电默认保持静音和高阻，交由调用方显式打开音频。
    shadow_[0] = REG02_ENABLE | REG02_NEW_METHOD;
    shadow_[1] = 0;
    shadow_[2] = REG04_SOFTMUTE;
    shadow_[3] = REG05_INT_MODE | (8U << 8) |
                 (static_cast<uint16_t>(LnaPort::Dual) << 6); // 耳机线天线多用双端 LNA
    shadow_[4] = 0; // OPEN_MODE 始终为 00。
    shadow_[5] = REG07_BLEND_THRESHOLD | REG07_SOFTBLEND;
    shadow_[6] = 0;
    band_ = Band::UsEurope;
    spacing_ = Spacing::Khz100;
    frequency_ = DEFAULT_FREQUENCY;
    standby_ = false;
    tuning_ = false;
    seeking_ = false;
    clearRds();
}

bool Rda5807m::initialize() {
    if (!bus_) {
        return false;
    }
    setDefaults();
    if (!writeAllShadows()) {
        return false;
    }
    delay(50);
    return true;
}

bool Rda5807m::softReset() {
    if (!bus_) {
        return false;
    }
    // 复位期间保持输出高阻且静音。
    if (!writeRandom(0x02, REG02_ENABLE | REG02_SOFT_RESET)) {
        return false;
    }
    delay(5);
    return initialize();
}

bool Rda5807m::setStandby(const bool standby) {
    const bool ok = modifyShadow(0x02, REG02_ENABLE, standby ? 0 : REG02_ENABLE);
    if (ok) {
        standby_ = standby;
        if (standby) {
            tuning_ = false;
            seeking_ = false;
        }
    }
    return ok;
}

uint16_t Rda5807m::frequencyMin() const {
    switch (band_) {
        case Band::UsEurope:
            return 8700;
        case Band::Japan:
        case Band::Worldwide:
            return 7600;
        case Band::EastEurope:
            return 6500;
        case Band::Low:
            return 5000;
    }
    return 8700;
}

uint16_t Rda5807m::frequencyMax() const {
    switch (band_) {
        case Band::UsEurope:
        case Band::Worldwide:
            return 10800;
        case Band::Japan:
            return 9100;
        case Band::EastEurope:
        case Band::Low:
            return 7600;
    }
    return 10800;
}

uint16_t Rda5807m::clampFrequency(const uint16_t freq_centi) const {
    if (freq_centi < frequencyMin()) {
        return frequencyMin();
    }
    if (freq_centi > frequencyMax()) {
        return frequencyMax();
    }
    return freq_centi;
}

void Rda5807m::syncBandBits() {
    uint16_t band_bits = 0;
    switch (band_) {
        case Band::UsEurope:
            band_bits = 0;
            break;
        case Band::Japan:
            band_bits = 1U << 2;
            break;
        case Band::Worldwide:
            band_bits = 2U << 2;
            break;
        case Band::EastEurope:
        case Band::Low:
            band_bits = 3U << 2;
            break;
    }
    shadow_[1] = static_cast<uint16_t>((shadow_[1] & ~REG03_BAND_MASK) | band_bits);
    if (band_ == Band::EastEurope) {
        shadow_[5] |= REG07_LOW_BAND_65;
    } else {
        shadow_[5] &= ~REG07_LOW_BAND_65;
    }
}

bool Rda5807m::setBand(const Band band) {
    band_ = band;
    syncBandBits();
    frequency_ = clampFrequency(frequency_);
    clearRds();
    return writeShadow(0x07) && writeShadow(0x03);
}

bool Rda5807m::setSpacing(const Spacing spacing) {
    uint16_t bits = 0;
    switch (spacing) {
        case Spacing::Khz100:
            bits = 0;
            break;
        case Spacing::Khz200:
            bits = 1;
            break;
        case Spacing::Khz50:
            bits = 2;
            break;
        case Spacing::Khz25:
            bits = 3;
            break;
        default:
            return false;
    }
    spacing_ = spacing;
    return modifyShadow(0x03, REG03_SPACE_MASK, bits);
}

uint16_t Rda5807m::channelFromFrequency(const uint16_t freq_centi) const {
    const uint32_t offset_khz =
        static_cast<uint32_t>(clampFrequency(freq_centi) - frequencyMin()) * 10U;
    const uint32_t step_khz = static_cast<uint16_t>(spacing_);
    const uint32_t channel = (offset_khz + step_khz / 2U) / step_khz;
    return static_cast<uint16_t>(channel > 1023U ? 1023U : channel);
}

uint16_t Rda5807m::frequencyFromChannel(const uint16_t channel) const {
    const uint32_t khz = static_cast<uint32_t>(frequencyMin()) * 10U +
                         static_cast<uint32_t>(channel) *
                             static_cast<uint16_t>(spacing_);
    return clampFrequency(static_cast<uint16_t>((khz + 5U) / 10U));
}

bool Rda5807m::setFrequency(const uint16_t freq_centi, const bool wait,
                            const uint32_t timeout_ms) {
    if (!startTune(freq_centi)) {
        return false;
    }
    return !wait || waitTune(timeout_ms);
}

bool Rda5807m::startTune(const uint16_t freq_centi) {
    if (standby_) {
        return false;
    }
    const uint16_t channel = channelFromFrequency(freq_centi);
    frequency_ = frequencyFromChannel(channel);
    shadow_[5] &= ~REG07_FREQ_MODE;
    if (!writeShadow(0x07)) {
        return false;
    }
    shadow_[1] = static_cast<uint16_t>((shadow_[1] & ~(REG03_CHAN_MASK | REG03_TUNE)) |
                                       (channel << 6) | REG03_TUNE);
    clearRds();
    tuning_ = writeShadow(0x03);
    seeking_ = false;
    return tuning_;
}

bool Rda5807m::pollTune(Status* status) {
    if (!tuning_) {
        return true;
    }
    Status current{};
    if (!readStatus(current)) {
        return false;
    }
    if (status) {
        *status = current;
    }
    if (!current.stc) {
        return false;
    }
    tuning_ = false;
    shadow_[1] &= ~REG03_TUNE; // TUNE 由芯片自动清零，仅同步本地 shadow。
    frequency_ = current.freq_centi;
    return true;
}

bool Rda5807m::waitTune(const uint32_t timeout_ms, Status* status) {
    const uint32_t started = millis();
    do {
        if (pollTune(status)) {
            return true;
        }
        delay(1);
    } while (static_cast<uint32_t>(millis() - started) < timeout_ms);
    return false;
}

bool Rda5807m::startSeek(const bool up) {
    if (standby_) {
        return false;
    }
    uint16_t value = REG02_SEEK;
    if (up) {
        value |= REG02_SEEKUP;
    }
    clearRds();
    const bool ok = modifyShadow(0x02, REG02_SEEK | REG02_SEEKUP, value);
    if (ok) {
        seeking_ = true;
        tuning_ = false;
    }
    return ok;
}

bool Rda5807m::abortSeek() {
    const bool ok = modifyShadow(0x02, REG02_SEEK, 0);
    if (ok) {
        seeking_ = false;
    }
    return ok;
}

bool Rda5807m::waitSeek(const uint32_t timeout_ms, Status* status) {
    const uint32_t started = millis();
    do {
        Status current{};
        if (!readStatus(current)) {
            return false;
        }
        if (status) {
            *status = current;
        }
        if (current.stc) {
            seeking_ = false;
            shadow_[0] &= ~REG02_SEEK; // SEEK 由芯片自动清零。
            frequency_ = current.freq_centi;
            return !current.seek_failed;
        }
        delay(1);
    } while (static_cast<uint32_t>(millis() - started) < timeout_ms);
    return false;
}

bool Rda5807m::setMute(const bool mute) {
    return modifyShadow(0x02, REG02_DMUTE, mute ? 0 : REG02_DMUTE);
}

bool Rda5807m::setMono(const bool mono) {
    return modifyShadow(0x02, REG02_MONO, mono ? REG02_MONO : 0);
}

bool Rda5807m::setHighZ(const bool high_z) {
    return modifyShadow(0x02, REG02_DHIZ, high_z ? 0 : REG02_DHIZ);
}

bool Rda5807m::setVolume(const uint8_t volume) {
    return modifyShadow(0x05, REG05_VOLUME_MASK, volume > 15 ? 15 : volume);
}

bool Rda5807m::setBass(const bool enabled) {
    return modifyShadow(0x02, REG02_BASS, enabled ? REG02_BASS : 0);
}

bool Rda5807m::setDeemphasis50(const bool use_50us) {
    return modifyShadow(0x04, REG04_DE, use_50us ? REG04_DE : 0);
}

bool Rda5807m::setSoftMute(const bool enabled) {
    return modifyShadow(0x04, REG04_SOFTMUTE, enabled ? REG04_SOFTMUTE : 0);
}

bool Rda5807m::setSoftBlend(const bool enabled) {
    return modifyShadow(0x07, REG07_SOFTBLEND, enabled ? REG07_SOFTBLEND : 0);
}

bool Rda5807m::setSoftBlendThreshold(const uint8_t threshold) {
    const uint16_t value = static_cast<uint16_t>(threshold > 31 ? 31 : threshold) << 10;
    return modifyShadow(0x07, 0x7C00, value);
}

bool Rda5807m::setAfc(const bool enabled) {
    return modifyShadow(0x04, REG04_AFCD, enabled ? 0 : REG04_AFCD);
}

bool Rda5807m::setNewMethod(const bool enabled) {
    return modifyShadow(0x02, REG02_NEW_METHOD, enabled ? REG02_NEW_METHOD : 0);
}

bool Rda5807m::setSeekThreshold(const uint8_t threshold) {
    return modifyShadow(0x05, REG05_SEEKTH_MASK,
                        static_cast<uint16_t>(threshold > 15 ? 15 : threshold) << 8);
}

bool Rda5807m::setSeekMode(const uint8_t mode) {
    if (mode > 3) {
        return false;
    }
    return modifyShadow(0x05, REG05_SEEK_MODE_MASK, static_cast<uint16_t>(mode) << 13);
}

bool Rda5807m::setOldSeekThreshold(const uint8_t threshold) {
    const uint16_t value = static_cast<uint16_t>(threshold > 63 ? 63 : threshold) << 2;
    return modifyShadow(0x07, REG07_OLD_SEEKTH_MASK, value);
}

bool Rda5807m::setSeekWrap(const bool wrap) {
    return modifyShadow(0x02, REG02_SKMODE, wrap ? 0 : REG02_SKMODE);
}

bool Rda5807m::setInterruptMode(const bool latched) {
    return modifyShadow(0x05, REG05_INT_MODE, latched ? REG05_INT_MODE : 0);
}

bool Rda5807m::setClock(const Clock clock, const bool direct_input,
                        const bool non_calibrated) {
    const uint8_t mode = static_cast<uint8_t>(clock);
    if (mode > 7 || mode == 4) {
        return false;
    }
    const uint16_t mask = REG02_CLK_MASK | REG02_RCLK_DIRECT | REG02_RCLK_NON_CAL;
    const uint16_t value = static_cast<uint16_t>(mode) << 4 |
                           (direct_input ? REG02_RCLK_DIRECT : 0) |
                           (non_calibrated ? REG02_RCLK_NON_CAL : 0);
    return modifyShadow(0x02, mask, value);
}

bool Rda5807m::setLna(const LnaPort port, const LnaCurrent current) {
    const uint16_t value = (static_cast<uint16_t>(port) << 6) |
                           (static_cast<uint16_t>(current) << 4);
    return modifyShadow(0x05, REG05_LNA_PORT_MASK | REG05_LNA_CURRENT_MASK, value);
}

bool Rda5807m::setGpio(const uint8_t gpio, const GpioMode mode) {
    if (gpio < 1 || gpio > 3) {
        return false;
    }
    const uint8_t shift = static_cast<uint8_t>((gpio - 1U) * 2U);
    const uint16_t mask = static_cast<uint16_t>(3U << shift);
    return modifyShadow(0x04, mask, static_cast<uint16_t>(mode) << shift);
}

bool Rda5807m::setTuneInterrupt(const bool enabled, const bool latched) {
    const uint16_t stc = enabled ? REG04_STCIEN : 0;
    const uint16_t int_mode = latched ? REG05_INT_MODE : 0;
    return modifyShadow(0x04, REG04_STCIEN, stc) &&
           modifyShadow(0x05, REG05_INT_MODE, int_mode);
}

bool Rda5807m::setRds(const bool enabled) {
    const bool ok = modifyShadow(0x02, REG02_RDS_EN, enabled ? REG02_RDS_EN : 0);
    if (ok) {
        clearRds();
    }
    return ok;
}

bool Rda5807m::setRbds(const bool enabled) {
    const bool ok = modifyShadow(0x04, REG04_RBDS, enabled ? REG04_RBDS : 0);
    if (ok) {
        clearRds();
    }
    return ok;
}

bool Rda5807m::setRdsFifo(const bool enabled) {
    return modifyShadow(0x04, REG04_RDS_FIFO_EN,
                        enabled ? REG04_RDS_FIFO_EN : 0);
}

bool Rda5807m::clearRdsFifo() {
    shadow_[2] |= REG04_RDS_FIFO_CLR;
    if (!writeShadow(0x04)) {
        return false;
    }
    shadow_[2] &= ~REG04_RDS_FIFO_CLR;
    clearRds();
    return writeShadow(0x04);
}

bool Rda5807m::setI2s(const bool enabled) {
    return modifyShadow(0x04, REG04_I2S_ENABLE,
                        enabled ? REG04_I2S_ENABLE : 0);
}

bool Rda5807m::configureI2s(const I2sConfig& config) {
    const uint8_t rate = static_cast<uint8_t>(config.sample_rate);
    if (rate > 8) {
        return false;
    }
    uint16_t value = 0;
    value |= config.slave ? 1U << 12 : 0;
    value |= config.left_when_ws_low ? 1U << 11 : 0;
    value |= config.invert_input_clock ? 1U << 10 : 0;
    value |= config.signed_data ? 1U << 9 : 0;
    value |= config.invert_input_ws ? 1U << 8 : 0;
    value |= static_cast<uint16_t>(rate) << 4;
    value |= config.invert_output_ws ? 1U << 3 : 0;
    value |= config.invert_output_clock ? 1U << 2 : 0;
    value |= config.left_delay ? 1U << 1 : 0;
    value |= config.right_delay ? 1U : 0;
    shadow_[4] = value; // OPEN_MODE 位不会被设置。
    return writeShadow(0x06);
}

bool Rda5807m::setDirectFrequency(const uint16_t freq_centi, const bool wait,
                                  const uint32_t timeout_ms) {
    const uint16_t base = band_ == Band::UsEurope ? 8700 : 7600;
    if (freq_centi < base || freq_centi > frequencyMax()) {
        return false;
    }
    const uint32_t offset_khz = static_cast<uint32_t>(freq_centi - base) * 10U;
    if (offset_khz > 0xFFFFU ||
        !setDirectFrequencyOffset(static_cast<uint16_t>(offset_khz), true)) {
        return false;
    }
    frequency_ = freq_centi;
    shadow_[1] |= REG03_TUNE;
    clearRds();
    tuning_ = writeShadow(0x03);
    seeking_ = false;
    return tuning_ && (!wait || waitTune(timeout_ms));
}

bool Rda5807m::setDirectFrequencyOffset(const uint16_t offset_khz,
                                        const bool enabled) {
    shadow_[6] = offset_khz;
    if (!writeShadow(0x08)) {
        return false;
    }
    return modifyShadow(0x07, REG07_FREQ_MODE,
                        enabled ? REG07_FREQ_MODE : 0);
}

bool Rda5807m::readStatus(Status& status) {
    uint16_t regs[2]{};
    if (!readSequential(regs, 2)) {
        return false;
    }
    status.readchan = regs[0] & REG0A_READCHAN_MASK;
    status.freq_centi = (shadow_[5] & REG07_FREQ_MODE)
                            ? frequency_
                            : frequencyFromChannel(status.readchan);
    status.rssi = static_cast<uint8_t>((regs[1] & REG0B_RSSI_MASK) >> 9);
    status.stereo = (regs[0] & REG0A_STEREO) != 0;
    status.stc = (regs[0] & REG0A_STC) != 0;
    status.seek_failed = (regs[0] & REG0A_SF) != 0;
    status.rds_ready = (regs[0] & REG0A_RDSR) != 0;
    status.rds_synced = (regs[0] & REG0A_RDSS) != 0;
    status.fm_true = (regs[1] & REG0B_FM_TRUE) != 0;
    status.fm_ready = (regs[1] & REG0B_FM_READY) != 0;
    return true;
}

bool Rda5807m::readRdsGroup(RdsGroup& group) {
    uint16_t regs[6]{};
    if (!readSequential(regs, 6)) {
        return false;
    }
    if ((regs[0] & REG0A_RDSR) == 0) {
        return false;
    }
    group.a = regs[2];
    group.b = regs[3];
    group.c = regs[4];
    group.d = regs[5];
    group.block_e = (regs[1] & REG0B_ABCD_E) != 0;
    group.block_a_errors = static_cast<uint8_t>((regs[1] >> 2) & 0x03);
    group.block_b_errors = static_cast<uint8_t>(regs[1] & 0x03);
    return true;
}

bool Rda5807m::pollRds(RdsGroup* group) {
    RdsGroup current{};
    if (!readRdsGroup(current)) {
        return false;
    }
    if (group) {
        *group = current;
    }
    // 无法纠正的 A/B 块或 RBDS Block E 不进入解码器。
    if (current.block_e || current.block_a_errors == 3 ||
        current.block_b_errors == 3) {
        return false;
    }
    decodeRds(current);
    return true;
}

void Rda5807m::clearRds() {
    pi_ = 0;
    pty_ = 0;
    tp_ = false;
    ta_ = false;
    std::memset(ps_, 0, sizeof(ps_));
    std::memset(ps_stage_, ' ', sizeof(ps_stage_));
    std::memset(ps_seen_, 0, sizeof(ps_seen_));
    ps_mask_ = 0;
    std::memset(rt_, 0, sizeof(rt_));
    std::memset(rt_stage_, ' ', sizeof(rt_stage_));
    std::memset(rt_seen_, 0, sizeof(rt_seen_));
    rt_mask_ = 0;
    rt_ab_ = 0xFF;
    rt_is_2b_ = false;
    clock_time_ = {};
    ct_candidate_mjd_ = 0;
    std::memset(af_, 0, sizeof(af_));
    af_count_ = 0;
}

void Rda5807m::decodeRds(const RdsGroup& group) {
    if (pi_ != 0 && pi_ != group.a) {
        clearRds(); // 换台或 PI 改变时不得混用旧台数据。
    }
    pi_ = group.a;
    tp_ = (group.b & (1U << 10)) != 0;
    pty_ = static_cast<uint8_t>((group.b >> 5) & 0x1F);

    const uint8_t type = static_cast<uint8_t>((group.b >> 12) & 0x0F);
    const bool version_b = (group.b & (1U << 11)) != 0;
    if (type == 0) {
        ta_ = (group.b & (1U << 4)) != 0;
        decodePs(group.b, group.d);
        if (!version_b) {
            decodeAf(group.c);
        }
    } else if (type == 2) {
        decodeRt(group.b, group.c, group.d);
    } else if (type == 4 && !version_b) {
        decodeCt(group.b, group.c, group.d);
    }
}

void Rda5807m::decodePs(const uint16_t block_b, const uint16_t block_d) {
    const uint8_t segment = static_cast<uint8_t>(block_b & 0x03);
    const uint8_t pos = static_cast<uint8_t>(segment * 2);
    const char first = static_cast<char>(block_d >> 8);
    const char second = static_cast<char>(block_d & 0xFF);
    if (ps_seen_[pos] == first && ps_seen_[pos + 1] == second) {
        ps_stage_[pos] = first;
        ps_stage_[pos + 1] = second;
        ps_mask_ |= static_cast<uint8_t>(1U << segment);
    } else {
        ps_seen_[pos] = first;
        ps_seen_[pos + 1] = second;
        ps_mask_ &= static_cast<uint8_t>(~(1U << segment));
    }
    if (ps_mask_ == 0x0F) {
        std::memcpy(ps_, ps_stage_, 8);
        ps_[8] = '\0';
    }
}

void Rda5807m::decodeRt(const uint16_t block_b, const uint16_t block_c,
                        const uint16_t block_d) {
    const bool version_b = (block_b & (1U << 11)) != 0;
    const uint8_t ab = static_cast<uint8_t>((block_b >> 4) & 1U);
    if (rt_ab_ != ab || rt_is_2b_ != version_b) {
        std::memset(rt_, 0, sizeof(rt_));
        std::memset(rt_stage_, ' ', sizeof(rt_stage_));
        std::memset(rt_seen_, 0, sizeof(rt_seen_));
        rt_mask_ = 0;
        rt_ab_ = ab;
        rt_is_2b_ = version_b;
    }

    const uint8_t segment = static_cast<uint8_t>(block_b & 0x0F);
    const uint8_t width = version_b ? 2 : 4;
    const uint8_t pos = static_cast<uint8_t>(segment * width);
    char chars[4] = {
        static_cast<char>(block_c >> 8),
        static_cast<char>(block_c & 0xFF),
        static_cast<char>(block_d >> 8),
        static_cast<char>(block_d & 0xFF),
    };
    if (version_b) {
        chars[0] = chars[2];
        chars[1] = chars[3];
    }

    bool repeated = true;
    for (uint8_t i = 0; i < width; ++i) {
        repeated = repeated && rt_seen_[pos + i] == chars[i];
    }
    if (repeated) {
        for (uint8_t i = 0; i < width; ++i) {
            rt_stage_[pos + i] = chars[i];
        }
        rt_mask_ |= static_cast<uint16_t>(1U << segment);
    } else {
        for (uint8_t i = 0; i < width; ++i) {
            rt_seen_[pos + i] = chars[i];
        }
        rt_mask_ &= static_cast<uint16_t>(~(1U << segment));
    }

    size_t text_length = version_b ? 32 : 64;
    uint8_t final_segment = 15;
    bool terminated = false;
    for (size_t i = 0; i < text_length; ++i) {
        if (rt_stage_[i] == '\r') {
            text_length = i;
            final_segment = static_cast<uint8_t>(i / width);
            terminated = true;
            break;
        }
    }
    const uint16_t required = final_segment == 15
                                  ? 0xFFFF
                                  : static_cast<uint16_t>((1U << (final_segment + 1U)) - 1U);
    if ((rt_mask_ & required) == required && (terminated || rt_mask_ == 0xFFFF)) {
        std::memcpy(rt_, rt_stage_, text_length);
        rt_[text_length] = '\0';
    }
}

void Rda5807m::decodeCt(const uint16_t block_b, const uint16_t block_c,
                        const uint16_t block_d) {
    const uint32_t mjd = (static_cast<uint32_t>(block_b & 0x03) << 15) |
                         (static_cast<uint32_t>(block_c) >> 1);
    if (ct_candidate_mjd_ != mjd) {
        ct_candidate_mjd_ = mjd; // 日期至少接收两次再采用。
        return;
    }

    const uint8_t hour =
        static_cast<uint8_t>(((block_c & 1U) << 4) | (block_d >> 12));
    const uint8_t minute = static_cast<uint8_t>((block_d >> 6) & 0x3F);
    if (hour > 23 || minute > 59) {
        return;
    }

    // 使用整数 JDN 算法将 MJD 转为公历日期。
    int32_t l = static_cast<int32_t>(mjd + 2400001UL) + 68569;
    const int32_t n = 4 * l / 146097;
    l -= (146097 * n + 3) / 4;
    const int32_t i = 4000 * (l + 1) / 1461001;
    l = l - 1461 * i / 4 + 31;
    const int32_t j = 80 * l / 2447;
    const int32_t day = l - 2447 * j / 80;
    l = j / 11;
    const int32_t month = j + 2 - 12 * l;
    const int32_t year = 100 * (n - 49) + i + l;

    int16_t offset = static_cast<int16_t>(block_d & 0x1F) * 30;
    if ((block_d & (1U << 5)) != 0) {
        offset = static_cast<int16_t>(-offset);
    }
    clock_time_.valid = true;
    clock_time_.mjd = mjd;
    clock_time_.year = static_cast<int16_t>(year);
    clock_time_.month = static_cast<uint8_t>(month);
    clock_time_.day = static_cast<uint8_t>(day);
    clock_time_.hour = hour;
    clock_time_.minute = minute;
    clock_time_.local_offset_minutes = offset;
}

void Rda5807m::decodeAf(const uint16_t block_c) {
    addAlternativeFrequency(static_cast<uint8_t>(block_c >> 8));
    addAlternativeFrequency(static_cast<uint8_t>(block_c & 0xFF));
}

void Rda5807m::addAlternativeFrequency(const uint8_t code) {
    if (code < 1 || code > 204) {
        return;
    }
    const uint16_t freq = static_cast<uint16_t>(8750U + code * 10U);
    for (size_t i = 0; i < af_count_; ++i) {
        if (af_[i] == freq) {
            return;
        }
    }
    if (af_count_ < sizeof(af_) / sizeof(af_[0])) {
        af_[af_count_++] = freq;
    }
}

bool Rda5807m::readRegister(const uint8_t reg, uint16_t& value) {
    if (!isPublicRegister(reg)) {
        return false;
    }
    return readRandom(reg, &value, 1);
}

bool Rda5807m::writeRegister(const uint8_t reg, const uint16_t value) {
    const uint16_t mask = publicWriteMask(reg);
    if (mask == 0 || reg < FIRST_SHADOW_REG || reg > LAST_SHADOW_REG) {
        return false;
    }
    shadow_[reg - FIRST_SHADOW_REG] = value & mask;
    return writeShadow(reg);
}

bool Rda5807m::readRegisters(const uint8_t first_reg, uint16_t* values,
                              const size_t count) {
    if (!values || count == 0 || first_reg > 0x0F ||
        count > static_cast<size_t>(0x10 - first_reg)) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        if (!isPublicRegister(static_cast<uint8_t>(first_reg + i))) {
            return false;
        }
    }
    return readRandom(first_reg, values, count);
}

uint16_t Rda5807m::shadowRegister(const uint8_t reg) const {
    if (reg < FIRST_SHADOW_REG || reg > LAST_SHADOW_REG) {
        return 0;
    }
    return shadow_[reg - FIRST_SHADOW_REG];
}

bool Rda5807m::modifyShadow(const uint8_t reg, const uint16_t mask,
                            const uint16_t value) {
    if (reg < FIRST_SHADOW_REG || reg > LAST_SHADOW_REG) {
        return false;
    }
    uint16_t& target = shadow_[reg - FIRST_SHADOW_REG];
    target = static_cast<uint16_t>((target & ~mask) | (value & mask));
    return writeShadow(reg);
}

bool Rda5807m::writeShadow(const uint8_t reg) {
    if (reg < FIRST_SHADOW_REG || reg > LAST_SHADOW_REG) {
        return false;
    }
    const uint16_t mask = publicWriteMask(reg);
    uint16_t& value = shadow_[reg - FIRST_SHADOW_REG];
    value &= mask;
    return writeRandom(reg, value);
}

bool Rda5807m::writeAllShadows() {
    if (!bus_ || !bus_->isEnabled()) {
        return false;
    }
    uint8_t bytes[SHADOW_COUNT * 2]{};
    for (size_t i = 0; i < SHADOW_COUNT; ++i) {
        shadow_[i] &= publicWriteMask(static_cast<uint8_t>(i + FIRST_SHADOW_REG));
        bytes[i * 2] = static_cast<uint8_t>(shadow_[i] >> 8);
        bytes[i * 2 + 1] = static_cast<uint8_t>(shadow_[i] & 0xFF);
    }
    if (!bus_->start(SEQUENTIAL_ADDRESS, false, I2C_FREQUENCY)) {
        return false;
    }
    const bool ok = bus_->write(bytes, sizeof(bytes));
    bus_->stop();
    return ok;
}

bool Rda5807m::writeRandom(const uint8_t reg, const uint16_t value) {
    if (!bus_ || !bus_->isEnabled()) {
        return false;
    }
    const uint8_t bytes[3] = {
        reg,
        static_cast<uint8_t>(value >> 8),
        static_cast<uint8_t>(value & 0xFF),
    };
    if (!bus_->start(RANDOM_ADDRESS, false, I2C_FREQUENCY)) {
        return false;
    }
    const bool ok = bus_->write(bytes, sizeof(bytes));
    bus_->stop();
    return ok;
}

bool Rda5807m::readRandom(const uint8_t reg, uint16_t* values,
                          const size_t count) {
    if (!bus_ || !bus_->isEnabled() || !values || count == 0) {
        return false;
    }
    if (!bus_->start(RANDOM_ADDRESS, false, I2C_FREQUENCY)) {
        return false;
    }
    const bool selected = bus_->write(&reg, 1);
    if (!selected) {
        bus_->stop();
        return false;
    }
    // 随机读必须用 repeated START，不能在寄存器地址后先发 STOP。
    if (!bus_->restart(RANDOM_ADDRESS, true, I2C_FREQUENCY)) {
        bus_->stop();
        return false;
    }
    uint8_t bytes[32]{};
    const size_t byte_count = count * 2;
    if (byte_count > sizeof(bytes)) {
        bus_->stop();
        return false;
    }
    const bool ok = bus_->read(bytes, byte_count);
    bus_->stop();
    if (!ok) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        values[i] = static_cast<uint16_t>(bytes[i * 2] << 8) | bytes[i * 2 + 1];
    }
    return true;
}

bool Rda5807m::readSequential(uint16_t* values, const size_t count) {
    if (!bus_ || !bus_->isEnabled() || !values || count == 0 || count > 6) {
        return false;
    }
    if (!bus_->start(SEQUENTIAL_ADDRESS, true, I2C_FREQUENCY)) {
        return false;
    }
    uint8_t bytes[12]{};
    const bool ok = bus_->read(bytes, count * 2);
    bus_->stop();
    if (!ok) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        values[i] = static_cast<uint16_t>(bytes[i * 2] << 8) | bytes[i * 2 + 1];
    }
    return true;
}
