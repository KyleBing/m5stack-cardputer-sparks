#include "fm_tuner.h"

bool FmTuner::begin(m5::I2C_Class& bus) {
    chip_ = Chip::None;
    chip_id_ = 0;
    if (rda_.begin(bus)) {
        (void)rda_.probe(&chip_id_);
        chip_ = Chip::Rda5807m;
        return true;
    }
    if (tea_.begin(bus)) {
        chip_ = Chip::Tea5767;
        return true;
    }
    return false;
}

void FmTuner::detach() {
    chip_ = Chip::None;
    chip_id_ = 0;
}

const char* FmTuner::chipName() const {
    if (chip_ == Chip::Rda5807m) {
        return "RDA5807M";
    }
    if (chip_ == Chip::Tea5767) {
        return "TEA5767";
    }
    return "FM";
}

uint16_t FmTuner::freqMin() const {
    return chip_ == Chip::Rda5807m ? rda_.frequencyMin() : tea_.freqMin();
}

uint16_t FmTuner::freqMax() const {
    return chip_ == Chip::Rda5807m ? rda_.frequencyMax() : tea_.freqMax();
}

uint16_t FmTuner::freqStep() const {
    if (chip_ == Chip::Rda5807m) {
        return static_cast<uint16_t>(rda_.spacing()) / 10;
    }
    return Tea5767::FREQ_STEP;
}

void FmTuner::setFrequency(const uint16_t freq_centi, const bool wait_settle) {
    if (chip_ == Chip::Rda5807m) {
        (void)rda_.setFrequency(freq_centi, wait_settle);
    } else if (chip_ == Chip::Tea5767) {
        tea_.setFrequency(freq_centi, wait_settle);
    }
}

bool FmTuner::readStatus(Status& out) {
    if (chip_ == Chip::Rda5807m) {
        Rda5807m::Status st{};
        if (!rda_.readStatus(st)) {
            return false;
        }
        out.freq_centi = st.freq_centi;
        out.raw_rssi = st.rssi;
        out.rssi = static_cast<uint8_t>((static_cast<uint16_t>(st.rssi) * 15 + 63) / 127);
        out.quality = st.rssi;
        out.chip_id = chip_id_;
        out.stereo = st.stereo;
        out.ready = st.stc;
        out.band_limit = st.seek_failed;
        out.valid_station = st.fm_true;
        out.rds_ready = st.rds_ready;
        out.rds_synced = st.rds_synced;
        return true;
    }
    if (chip_ == Chip::Tea5767) {
        Tea5767::Status st{};
        if (!tea_.readStatus(st)) {
            return false;
        }
        out.freq_centi = st.freq_centi;
        out.rssi = st.rssi;
        out.raw_rssi = st.rssi;
        out.quality = st.if_counter;
        out.chip_id = st.chip_id;
        out.stereo = st.stereo;
        out.ready = st.ready;
        out.band_limit = st.band_limit;
        out.valid_station = st.rssi >= 5;
        return true;
    }
    return false;
}

uint8_t FmTuner::getRssi() {
    Status st{};
    return readStatus(st) ? st.rssi : 0;
}

bool FmTuner::isStereo() {
    Status st{};
    return readStatus(st) && st.stereo;
}

void FmTuner::refreshSignal() {
    if (chip_ == Chip::Tea5767) {
        tea_.kickAdc();
    }
}

void FmTuner::setMute(const bool on) {
    if (chip_ == Chip::Rda5807m) {
        (void)rda_.setMute(on);
    } else if (chip_ == Chip::Tea5767) {
        tea_.setMute(on);
    }
}

void FmTuner::setMono(const bool on) {
    if (chip_ == Chip::Rda5807m) {
        (void)rda_.setMono(on);
    } else if (chip_ == Chip::Tea5767) {
        tea_.setMono(on);
    }
}

void FmTuner::setStandby(const bool on) {
    if (chip_ == Chip::Rda5807m) {
        (void)rda_.setStandby(on);
    } else if (chip_ == Chip::Tea5767) {
        tea_.setStandby(on);
    }
}

void FmTuner::startSearch(const bool up) {
    if (chip_ == Chip::Rda5807m) {
        (void)rda_.startSeek(up);
    } else if (chip_ == Chip::Tea5767) {
        tea_.startSearch(up);
    }
}

void FmTuner::abortSearch() {
    if (chip_ == Chip::Rda5807m) {
        (void)rda_.abortSeek();
    } else if (chip_ == Chip::Tea5767) {
        tea_.abortSearch();
    }
}

bool FmTuner::isSearching() const {
    if (chip_ == Chip::Rda5807m) {
        return rda_.isSeeking();
    }
    return chip_ == Chip::Tea5767 && tea_.isSearching();
}

void FmTuner::setJapanBand(const bool on) {
    if (chip_ == Chip::Tea5767) {
        tea_.setJapanBand(on);
    }
}

void FmTuner::setDeemphasis75(const bool on) {
    if (chip_ == Chip::Tea5767) {
        tea_.setDeemphasis75(on);
    }
}

void FmTuner::setHighSideInjection(const bool on) {
    if (chip_ == Chip::Tea5767) {
        tea_.setHighSideInjection(on);
    }
}

void FmTuner::setSoftMute(const bool on) {
    if (chip_ == Chip::Tea5767) {
        tea_.setSoftMute(on);
    }
}

void FmTuner::setHighCut(const bool on) {
    if (chip_ == Chip::Tea5767) {
        tea_.setHighCut(on);
    }
}

void FmTuner::setStereoNoiseCancel(const bool on) {
    if (chip_ == Chip::Tea5767) {
        tea_.setStereoNoiseCancel(on);
    }
}

void FmTuner::setSeekStop(const SeekStop level) {
    if (chip_ == Chip::Tea5767) {
        tea_.setSeekStop(static_cast<Tea5767::SeekStop>(level));
    }
}

void FmTuner::setChannelMute(const ChannelMute ch) {
    if (chip_ == Chip::Tea5767) {
        tea_.setChannelMute(static_cast<Tea5767::ChannelMute>(ch));
    }
}
