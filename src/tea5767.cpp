#include "tea5767.h"
#include <Arduino.h>

namespace {

static constexpr uint32_t QUARTZ = 32768;
static constexpr uint32_t FILTER = 225000;

static constexpr uint8_t REG_1_MUTE = 0x80;
static constexpr uint8_t REG_1_SM = 0x40;

static constexpr uint8_t REG_3_SUD = 0x80;
static constexpr uint8_t REG_3_HLSI = 0x10;
static constexpr uint8_t REG_3_MS = 0x08;
static constexpr uint8_t REG_3_MR = 0x04;
static constexpr uint8_t REG_3_ML = 0x02;
static constexpr uint8_t REG_3_SWP1 = 0x01;

static constexpr uint8_t REG_4_SWP2 = 0x80;
static constexpr uint8_t REG_4_STBY = 0x40;
static constexpr uint8_t REG_4_BL = 0x20;
static constexpr uint8_t REG_4_XTAL = 0x10;
static constexpr uint8_t REG_4_SMUTE = 0x08;
static constexpr uint8_t REG_4_HCC = 0x04;
static constexpr uint8_t REG_4_SNC = 0x02;
static constexpr uint8_t REG_4_SI = 0x01;

static constexpr uint8_t REG_5_DTC = 0x40; // 1=75µs，0=50µs；PLLREF=0（32.768 晶振）

static constexpr uint8_t STAT_RF = 0x80;
static constexpr uint8_t STAT_BLF = 0x40;
static constexpr uint8_t STAT_STEREO = 0x80;
static constexpr uint8_t STAT_ADC = 0xF0;
static constexpr uint8_t STAT_CHIP = 0x0F;

} // namespace

uint16_t Tea5767::clampFreq(const uint16_t freq_centi) const {
    const uint16_t lo = freqMin();
    const uint16_t hi = freqMax();
    if (freq_centi < lo) {
        return lo;
    }
    if (freq_centi > hi) {
        return hi;
    }
    return freq_centi;
}

uint32_t Tea5767::pllFromFreq(const uint16_t freq_centi) const {
    const uint32_t hz = static_cast<uint32_t>(freq_centi) * 10000UL;
    if (hlsi_high_) {
        return 4UL * (hz + FILTER) / QUARTZ;
    }
    return 4UL * (hz - FILTER) / QUARTZ;
}

uint16_t Tea5767::freqFromPll(const uint32_t pll) const {
    uint32_t hz;
    if (hlsi_high_) {
        hz = (pll * QUARTZ / 4) - FILTER;
    } else {
        hz = (pll * QUARTZ / 4) + FILTER;
    }
    return static_cast<uint16_t>(hz / 10000UL);
}

void Tea5767::packRegs() {
    const uint32_t pll = pllFromFreq(freq_);
    regs_[0] = static_cast<uint8_t>((muted_ ? REG_1_MUTE : 0) | (searching_ ? REG_1_SM : 0) |
                                    ((pll >> 8) & 0x3F));
    regs_[1] = static_cast<uint8_t>(pll & 0xFF);

    const uint8_t ssl_bits = static_cast<uint8_t>(static_cast<uint8_t>(ssl_) & 0x03) << 5;
    regs_[2] = static_cast<uint8_t>(
        (search_up_ ? REG_3_SUD : 0) | ssl_bits | (hlsi_high_ ? REG_3_HLSI : 0) |
        (mono_ ? REG_3_MS : 0) | (ch_mute_ == ChannelMute::Right ? REG_3_MR : 0) |
        (ch_mute_ == ChannelMute::Left ? REG_3_ML : 0) | (port1_ ? REG_3_SWP1 : 0));

    regs_[3] = static_cast<uint8_t>(
        (port2_ ? REG_4_SWP2 : 0) | (standby_ ? REG_4_STBY : 0) | (japan_ ? REG_4_BL : 0) |
        REG_4_XTAL | (soft_mute_ ? REG_4_SMUTE : 0) | (hcc_ ? REG_4_HCC : 0) |
        (snc_ ? REG_4_SNC : 0) | (search_ind_ ? REG_4_SI : 0));

    regs_[4] = static_cast<uint8_t>(deemph75_ ? REG_5_DTC : 0);
}

bool Tea5767::writeRegs() {
    if (!bus_ || !bus_->isEnabled()) {
        return false;
    }
    packRegs();
    if (!bus_->start(I2C_ADDR, false, 100000)) {
        return false;
    }
    // M5Unified write() 返回 bool，不是字节数
    const bool ok = bus_->write(regs_, sizeof(regs_));
    bus_->stop();
    return ok;
}

bool Tea5767::readRaw(uint8_t out[5]) {
    if (!bus_ || !bus_->isEnabled()) {
        return false;
    }
    if (!bus_->start(I2C_ADDR, true, 100000)) {
        return false;
    }
    const bool ok = bus_->read(out, 5);
    bus_->stop();
    return ok;
}

bool Tea5767::begin(m5::I2C_Class& bus) {
    bus_ = &bus;
    if (!bus_->isEnabled()) {
        return false;
    }
    searching_ = false;
    search_ind_ = false;
    standby_ = false;
    if (!probe()) {
        return false;
    }
    freq_ = clampFreq(freq_);
    writeRegs(); // 探测到即视为就绪；写失败仍可稍后重试
    return true;
}

bool Tea5767::probe() const {
    if (!bus_ || !bus_->isEnabled()) {
        return false;
    }
    return bus_->scanID(I2C_ADDR);
}

bool Tea5767::silence(m5::I2C_Class& bus) {
    bus_ = &bus;
    if (!probe()) {
        bus_ = nullptr;
        return false;
    }
    muted_ = true;
    standby_ = true;
    searching_ = false;
    search_ind_ = false;
    const bool ok = writeRegs();
    bus_ = nullptr;
    return ok;
}

void Tea5767::setFrequency(const uint16_t freq_centi, const bool wait_settle) {
    searching_ = false;
    search_ind_ = false;
    freq_ = clampFreq(freq_centi);
    writeRegs();
    if (wait_settle) {
        delay(60); // 手动调台：等 PLL 锁住再读 RSSI
    }
}

uint16_t Tea5767::getFrequency() {
    Status st{};
    if (!readStatus(st) || st.freq_centi == 0) {
        return freq_;
    }
    freq_ = st.freq_centi;
    return freq_;
}

void Tea5767::setMute(const bool on) {
    muted_ = on;
    writeRegs();
}

void Tea5767::setMono(const bool on) {
    mono_ = on;
    writeRegs();
}

void Tea5767::setSoftMute(const bool on) {
    soft_mute_ = on;
    writeRegs();
}

void Tea5767::setHighCut(const bool on) {
    hcc_ = on;
    writeRegs();
}

void Tea5767::setStereoNoiseCancel(const bool on) {
    snc_ = on;
    writeRegs();
}

void Tea5767::setJapanBand(const bool on) {
    japan_ = on;
    freq_ = clampFreq(freq_);
    writeRegs();
}

void Tea5767::setDeemphasis75(const bool on) {
    deemph75_ = on;
    writeRegs();
}

void Tea5767::setHighSideInjection(const bool on) {
    hlsi_high_ = on;
    writeRegs();
}

void Tea5767::setSeekStop(const SeekStop level) {
    ssl_ = level;
    writeRegs();
}

void Tea5767::setChannelMute(const ChannelMute ch) {
    ch_mute_ = ch;
    writeRegs();
}

void Tea5767::setStandby(const bool on) {
    standby_ = on;
    searching_ = false;
    search_ind_ = false;
    writeRegs();
}

void Tea5767::setPort1(const bool high) {
    port1_ = high;
    writeRegs();
}

void Tea5767::setPort2(const bool high) {
    port2_ = high;
    writeRegs();
}

bool Tea5767::readStatus(Status& out) {
    uint8_t raw[5]{};
    if (!readRaw(raw)) {
        return false;
    }
    const uint32_t pll = (static_cast<uint32_t>(raw[0] & 0x3F) << 8) | raw[1];
    out.freq_centi = freqFromPll(pll);
    out.ready = (raw[0] & STAT_RF) != 0;
    out.band_limit = (raw[0] & STAT_BLF) != 0;
    out.stereo = (raw[2] & STAT_STEREO) != 0;
    out.if_counter = static_cast<uint8_t>(raw[2] & 0x7F);
    out.rssi = static_cast<uint8_t>((raw[3] & STAT_ADC) >> 4);
    out.chip_id = static_cast<uint8_t>(raw[3] & STAT_CHIP);
    return true;
}

uint8_t Tea5767::getRssi() {
    Status st{};
    if (!readStatus(st)) {
        return 0;
    }
    return st.rssi;
}

void Tea5767::kickAdc() {
    if (searching_ || standby_) {
        return;
    }
    writeRegs();
}

bool Tea5767::isStereo() {
    Status st{};
    if (!readStatus(st)) {
        return false;
    }
    return st.stereo;
}

void Tea5767::startSearch(const bool up) {
    searching_ = true;
    search_up_ = up;
    search_ind_ = true; // 搜台期间 SWPORT1 输出就绪（模块接了灯才会亮）
    writeRegs();
}

void Tea5767::abortSearch() {
    searching_ = false;
    search_ind_ = false;
    writeRegs();
}
