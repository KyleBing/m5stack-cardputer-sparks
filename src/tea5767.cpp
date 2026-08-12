#include "tea5767.h"
#include <Arduino.h>

namespace {

static constexpr uint32_t QUARTZ = 32768;
static constexpr uint32_t FILTER = 225000;

static constexpr uint8_t REG_1 = 0;
static constexpr uint8_t REG_3 = 2;
static constexpr uint8_t REG_4 = 3;

static constexpr uint8_t REG_1_MUTE = 0x80;
static constexpr uint8_t REG_3_MS = 0x08;
static constexpr uint8_t REG_4_XTAL = 0x10;
static constexpr uint8_t REG_4_SMUTE = 0x08;

static constexpr uint8_t STAT_3 = 2;
static constexpr uint8_t STAT_3_STEREO = 0x80;
static constexpr uint8_t STAT_4 = 3;
static constexpr uint8_t STAT_4_ADC = 0xF0;

} // namespace

bool Tea5767::begin(m5::I2C_Class& bus) {
    bus_ = &bus;
    if (!bus_->isEnabled()) {
        return false;
    }

    regs_[0] = 0x00;
    regs_[1] = 0x00;
    regs_[2] = 0xB0;
    regs_[3] = REG_4_XTAL | REG_4_SMUTE;
    regs_[4] = 0x00; // 欧洲 50 ms 去加重
    muted_ = false;
    mono_ = false;
    return probe();
}

bool Tea5767::probe() const {
    if (!bus_ || !bus_->isEnabled()) {
        return false;
    }
    bool found[120]{};
    bus_->scanID(found);
    return found[I2C_ADDR];
}

bool Tea5767::writeRegs() {
    if (!bus_ || !bus_->isEnabled()) {
        return false;
    }
    if (!bus_->start(I2C_ADDR, false, 100000)) {
        return false;
    }
    if (bus_->write(regs_, sizeof(regs_)) != sizeof(regs_)) {
        bus_->stop();
        return false;
    }
    bus_->stop();
    return true;
}

bool Tea5767::readStatus(uint8_t out[5]) {
    if (!bus_ || !bus_->isEnabled()) {
        return false;
    }
    if (!bus_->start(I2C_ADDR, true, 100000)) {
        return false;
    }
    if (bus_->read(out, 5) != 5) {
        bus_->stop();
        return false;
    }
    bus_->stop();
    return true;
}

void Tea5767::applyFrequencyWord(const uint16_t freq_centi) {
    const uint32_t pll = 4UL * (static_cast<uint32_t>(freq_centi) * 10000UL + FILTER) / QUARTZ;
    regs_[REG_1] = static_cast<uint8_t>((regs_[REG_1] & REG_1_MUTE) | ((pll >> 8) & 0x3F));
    regs_[1] = static_cast<uint8_t>(pll & 0xFF);
}

void Tea5767::setFrequency(const uint16_t freq_centi) {
    uint16_t clamped = freq_centi;
    if (clamped < FREQ_MIN) {
        clamped = FREQ_MIN;
    }
    if (clamped > FREQ_MAX) {
        clamped = FREQ_MAX;
    }
    applyFrequencyWord(clamped);
    writeRegs();
    delay(60);
}

uint16_t Tea5767::getFrequency() {
    uint8_t status[5]{};
    if (!readStatus(status)) {
        return 0;
    }
    uint32_t pll = ((status[REG_1] & 0x3F) << 8) | status[1];
    pll = ((pll * QUARTZ / 4) - FILTER) / 10000;
    return static_cast<uint16_t>(pll);
}

void Tea5767::setMute(const bool on) {
    muted_ = on;
    if (on) {
        regs_[REG_1] |= REG_1_MUTE;
    } else {
        regs_[REG_1] &= static_cast<uint8_t>(~REG_1_MUTE);
    }
    writeRegs();
}

void Tea5767::setMono(const bool on) {
    mono_ = on;
    if (on) {
        regs_[REG_3] |= REG_3_MS;
    } else {
        regs_[REG_3] &= static_cast<uint8_t>(~REG_3_MS);
    }
    writeRegs();
}

uint8_t Tea5767::getRssi() {
    uint8_t status[5]{};
    if (!readStatus(status)) {
        return 0;
    }
    return (status[STAT_4] & STAT_4_ADC) >> 4;
}

bool Tea5767::isStereo() {
    uint8_t status[5]{};
    if (!readStatus(status)) {
        return false;
    }
    return (status[STAT_3] & STAT_3_STEREO) != 0;
}

bool Tea5767::seek(const bool up, const uint8_t rssi_threshold) {
    uint16_t freq = getFrequency();
    if (freq < FREQ_MIN) {
        freq = 9850;
    }

    for (int step = 0; step < 220; ++step) {
        if (up) {
            freq = static_cast<uint16_t>(freq + FREQ_STEP);
            if (freq > FREQ_MAX) {
                freq = FREQ_MIN;
            }
        } else {
            if (freq <= FREQ_MIN) {
                freq = FREQ_MAX;
            } else {
                freq = static_cast<uint16_t>(freq - FREQ_STEP);
            }
        }
        setFrequency(freq);
        if (getRssi() >= rssi_threshold) {
            return true;
        }
    }
    return false;
}
