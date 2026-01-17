/*
    rp_gbapu.cpp

    Game Boy APU to NES APU mapping implementation
*/

#include "rp_gbapu.h"
#include "rp_system.h"
#include <string.h>

#if APU_DEBUG_TRIGGER || APU_DEBUG_WAVE
#include <Arduino.h>
#endif

// Global instance
rp_gbapu gbapu;

// NES CPU clock frequency
#define NES_CPU_FREQ 1789773

// OR mask for read-only bits (when reading GB APU registers)
static const uint8_t reg_or_mask[GB_APU_REG_SIZE] = {
    0x80, 0x3F, 0x00, 0xFF, 0xBF,  // NR10-NR14
    0xFF,                          // Unused
    0x3F, 0x00, 0xFF, 0xBF,        // NR21-NR24
    0x7F, 0xFF, 0x9F, 0xFF, 0xBF,  // NR30-NR34
    0xFF,                          // Unused
    0xFF, 0x00, 0x00, 0xBF,        // NR41-NR44
    0x00, 0x00, 0x70,              // NR50-NR52
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Unused
    // Wave RAM (0xFF30-0xFF3F) - readable
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};


rp_gbapu::rp_gbapu() {
}

void rp_gbapu::init() {
    memset(m_regs, 0, sizeof(m_regs));
    memset(m_ch, 0, sizeof(m_ch));
    m_enabled = false;

    // Initialize NES channel ownership
    for (int i = 0; i < 4; i++) {
        m_nesChannelOwner[i] = NES_OWNER_NONE;
        m_ch[i].detected_wave_type = WAVE_TYPE_UNKNOWN;
        m_ch[i].nes_channel_used = 0xFF;
    }

    // Initialize wave analysis cache
    m_lastWaveType = WAVE_TYPE_UNKNOWN;
    m_waveRamHash = 0;

    // Initialize default register values
    m_regs[NR10] = 0x80;
    m_regs[NR11] = 0xBF;
    m_regs[NR12] = 0xF3;
    m_regs[NR14] = 0xBF;
    m_regs[NR21] = 0x3F;
    m_regs[NR24] = 0xBF;
    m_regs[NR30] = 0x7F;
    m_regs[NR32] = 0x9F;
    m_regs[NR34] = 0xBF;
    m_regs[NR41] = 0xFF;
    m_regs[NR44] = 0xBF;
    m_regs[NR50] = 0x77;
    m_regs[NR51] = 0xF3;
    m_regs[NR52] = 0xF1;

    // Default wave pattern
    static const uint8_t wave_init[] = {
        0xAC, 0xDD, 0xDA, 0x48,
        0x36, 0x02, 0xCF, 0x16,
        0x2C, 0x04, 0xE5, 0x2C,
        0xAC, 0xDD, 0xDA, 0x48
    };
    memcpy(&m_regs[WAVE_RAM_START], wave_init, 16);
}

uint8_t rp_gbapu::read(uint16_t addr) {
    if (addr < GB_APU_REG_START || addr > GB_APU_REG_END) {
        return 0xFF;
    }

    uint8_t offset = addr - GB_APU_REG_START;
    return m_regs[offset] | reg_or_mask[offset];
}

void rp_gbapu::write(uint16_t addr, uint8_t val) {
    if (addr < GB_APU_REG_START || addr > GB_APU_REG_END) {
        return;
    }

    uint8_t offset = addr - GB_APU_REG_START;

    // Handle NR52 (APU enable)
    if (offset == NR52) {
        m_enabled = (val & 0x80) != 0;
        if (!m_enabled) {
            // APU disabled - stop all channels
            for (int i = 0; i < 4; i++) {
                stopChannel(i);
            }
            // Clear most registers (except wave RAM)
            memset(m_regs, 0, WAVE_RAM_START);
        }
        m_regs[NR52] = val & 0x80;  // Only bit 7 is writable
        return;
    }

    // If APU is disabled, ignore writes (except wave RAM)
    if (!m_enabled && offset < WAVE_RAM_START) {
        return;
    }

    m_regs[offset] = val;

    // Handle trigger events
    switch (offset) {
        case NR14:  // Channel 1 trigger
            if (val & 0x80) {
                m_ch[0].active = true;
                m_ch[0].triggered = true;  // Mark for phase reset in updatePulse1()
                m_ch[0].duty = (m_regs[NR11] >> 6) & 0x03;
                m_ch[0].freq = m_regs[NR13] | ((m_regs[NR14] & 0x07) << 8);

                // Initialize envelope
                uint8_t nr12 = m_regs[NR12];
                m_ch[0].env_initial = (nr12 >> 4) & 0x0F;
                m_ch[0].env_direction = (nr12 >> 3) & 0x01;
                m_ch[0].env_period = nr12 & 0x07;
                m_ch[0].env_counter = 0;
                m_ch[0].volume = m_ch[0].env_initial;

                // Initialize sweep (accurate GB behavior)
                uint8_t nr10 = m_regs[NR10];
                m_ch[0].sweep_period = (nr10 >> 4) & 0x07;
                m_ch[0].sweep_direction = (nr10 >> 3) & 0x01;
                m_ch[0].sweep_shift = nr10 & 0x07;

                // Copy frequency to shadow register
                m_ch[0].sweep_shadow_freq = m_ch[0].freq;

                // Reload divider (period 0 is treated as 8)
                m_ch[0].sweep_divider = m_ch[0].sweep_period ? m_ch[0].sweep_period : 8;
                m_ch[0].sweep_timer = 0;

                // Enable sweep if period or shift is non-zero
                m_ch[0].sweep_enabled = (m_ch[0].sweep_period != 0 || m_ch[0].sweep_shift != 0);
                m_ch[0].sweep_negate_used = false;

                // If shift > 0, perform initial overflow check
                if (m_ch[0].sweep_shift > 0) {
                    uint16_t delta = m_ch[0].sweep_shadow_freq >> m_ch[0].sweep_shift;
                    uint16_t new_freq;
                    if (m_ch[0].sweep_direction) {
                        // Subtraction (negate)
                        new_freq = m_ch[0].sweep_shadow_freq - delta;
                        // Note: no overflow possible with subtraction
                    } else {
                        // Addition
                        new_freq = m_ch[0].sweep_shadow_freq + delta;
                        if (new_freq > 2047) {
                            // Overflow - disable channel immediately
                            m_ch[0].active = false;
                        }
                    }
                }

#if APU_DEBUG_TRIGGER
                Serial.printf("P1:trig,v%d,f%d,d%d,sw_p%d,sw_d%d,sw_s%d\n",
                    m_ch[0].volume, m_ch[0].freq, m_ch[0].duty,
                    m_ch[0].sweep_period, m_ch[0].sweep_direction, m_ch[0].sweep_shift);
#endif
            }
            break;

        case NR24:  // Channel 2 trigger
            if (val & 0x80) {
                m_ch[1].active = true;
                m_ch[1].triggered = true;  // Mark for phase reset in updatePulse2()
                m_ch[1].duty = (m_regs[NR21] >> 6) & 0x03;
                m_ch[1].freq = m_regs[NR23] | ((m_regs[NR24] & 0x07) << 8);

                // Initialize envelope
                uint8_t nr22 = m_regs[NR22];
                m_ch[1].env_initial = (nr22 >> 4) & 0x0F;
                m_ch[1].env_direction = (nr22 >> 3) & 0x01;
                m_ch[1].env_period = nr22 & 0x07;
                m_ch[1].env_counter = 0;
                m_ch[1].volume = m_ch[1].env_initial;

#if APU_DEBUG_TRIGGER
                Serial.printf("P2:trig,v%d,f%d,d%d\n", m_ch[1].volume, m_ch[1].freq, m_ch[1].duty);
#endif
            }
            break;

        case NR34:  // Channel 3 trigger
            if (val & 0x80) {
                m_ch[2].active = (m_regs[NR30] & 0x80) != 0;  // DAC enable
                m_ch[2].volume = (m_regs[NR32] >> 5) & 0x03;
                m_ch[2].freq = m_regs[NR33] | ((m_regs[NR34] & 0x07) << 8);
                m_ch[2].last_nes_period = 0;
#if APU_DEBUG_TRIGGER
                uint16_t nes_p3 = gbToNesTrianglePeriod(m_ch[2].freq);
                Serial.printf("WV:v%d,gbf%d,nesp%d,nch%d,wt%d\n",
                    m_ch[2].volume, m_ch[2].freq, nes_p3,
                    m_ch[2].nes_channel_used, m_ch[2].detected_wave_type);
#endif
            }
            break;

        case NR44:  // Channel 4 trigger
            if (val & 0x80) {
                m_ch[3].active = true;
                // Initialize envelope
                uint8_t nr42 = m_regs[NR42];
                m_ch[3].env_initial = (nr42 >> 4) & 0x0F;
                m_ch[3].env_direction = (nr42 >> 3) & 0x01;
                m_ch[3].env_period = nr42 & 0x07;
                m_ch[3].env_counter = 0;
                m_ch[3].volume = m_ch[3].env_initial;
                m_ch[3].last_nes_period = 0;
#if APU_DEBUG_TRIGGER
                Serial.printf("NS:v%d,nr43=%02X\n", m_ch[3].volume, m_regs[NR43]);
#endif
            }
            break;

        case NR10:  // Channel 1 sweep register
            // GB quirk: If negate was used and direction is changed to addition,
            // the channel is immediately disabled
            if (m_ch[0].sweep_negate_used && !(val & 0x08)) {
                // Negate was used previously, and now direction is addition (bit3=0)
                m_ch[0].active = false;
#if APU_DEBUG_TRIGGER
                Serial.println("P1:disabled by NR10 direction change after negate");
#endif
            }
            break;

        case NR12:  // Channel 1 volume envelope - check DAC
            if ((val & 0xF8) == 0) {
                m_ch[0].active = false;
            }
            break;

        case NR22:  // Channel 2 volume envelope - check DAC
            if ((val & 0xF8) == 0) {
                m_ch[1].active = false;
            }
            break;

        case NR30:  // Channel 3 DAC enable
            if ((val & 0x80) == 0) {
                m_ch[2].active = false;
                m_ch[2].stop_requested = true;  // フレーム内で停止を記録
            }
            break;

        case NR32:  // Channel 3 volume
            if (((val >> 5) & 0x03) == 0) {
                m_ch[2].stop_requested = true;  // volume=0 で停止を記録
            }
            break;

        case NR42:  // Channel 4 volume envelope - check DAC
            if ((val & 0xF8) == 0) {
                m_ch[3].active = false;
            }
            break;
    }
}

// Update envelope for a channel (called every frame at 60Hz)
// GB envelope runs at 64Hz - use fractional counting for accuracy
void rp_gbapu::updateEnvelope(uint8_t ch) {
    if (!m_ch[ch].active || m_ch[ch].env_period == 0) {
        return;
    }

    // Fractional timing: accumulate 64 ticks per second at 60Hz update rate
    // Each frame adds 64/60 ≈ 1.067 ticks
    // Use fixed-point: 64 * 256 / 60 = 273 per frame (8.8 fixed point)
    m_ch[ch].env_counter += 273;

    // Trigger envelope step when counter reaches threshold (env_period * 256)
    uint16_t threshold = (uint16_t)m_ch[ch].env_period * 256;
    if (m_ch[ch].env_counter >= threshold) {
        m_ch[ch].env_counter -= threshold;

        if (m_ch[ch].env_direction) {
            // Increase volume
            if (m_ch[ch].volume < 15) {
                m_ch[ch].volume++;
            }
        } else {
            // Decrease volume
            if (m_ch[ch].volume > 0) {
                m_ch[ch].volume--;
            }
        }
    }
}

// Calculate new sweep frequency and check for overflow
// Returns new frequency, or 0xFFFF if overflow (channel should be disabled)
static uint16_t calcSweepFreq(gb_channel* ch) {
    uint16_t delta = ch->sweep_shadow_freq >> ch->sweep_shift;

    if (ch->sweep_direction) {
        // Subtraction (negate) - frequency decreases
        ch->sweep_negate_used = true;
        if (delta > ch->sweep_shadow_freq) {
            return 0;  // Underflow - clamp to 0
        }
        return ch->sweep_shadow_freq - delta;
    } else {
        // Addition - frequency increases
        // If negate was previously used, addition is disabled (GB quirk)
        if (ch->sweep_negate_used) {
            return ch->sweep_shadow_freq;  // No change
        }
        uint16_t new_freq = ch->sweep_shadow_freq + delta;
        if (new_freq > 2047) {
            return 0xFFFF;  // Overflow - disable channel
        }
        return new_freq;
    }
}

// Update sweep for channel 1 (called every frame at 60Hz)
// GB sweep runs at 128Hz - we use fractional timing for accuracy
// Accurate implementation based on GB APU behavior:
// - Shadow frequency is used for calculations
// - Period 0 is treated as 8
// - Overflow check happens before applying new frequency
// - Negate usage disables subsequent addition
void rp_gbapu::updateSweep() {
    if (!m_ch[0].active) {
        return;
    }

    // Fractional timing: accumulate 128 ticks per second at 60Hz update rate
    // Use fixed-point: 128 * 256 / 60 = 546 per frame (8.8 fixed point)
    m_ch[0].sweep_timer += 546;

    // Clock sweep at 128Hz (threshold = 256 for each tick)
    while (m_ch[0].sweep_timer >= 256) {
        m_ch[0].sweep_timer -= 256;

        // Decrement divider
        if (m_ch[0].sweep_divider > 0) {
            m_ch[0].sweep_divider--;
        }

        // When divider reaches 0, clock the sweep
        if (m_ch[0].sweep_divider == 0) {
            // Reload divider (period 0 is treated as 8)
            m_ch[0].sweep_divider = m_ch[0].sweep_period ? m_ch[0].sweep_period : 8;

            // Only apply sweep if enabled and shift > 0
            if (m_ch[0].sweep_enabled && m_ch[0].sweep_shift > 0) {
                // Calculate new frequency
                uint16_t new_freq = calcSweepFreq(&m_ch[0]);

                if (new_freq == 0xFFFF) {
                    // Overflow - disable channel
                    m_ch[0].active = false;
                    return;
                }

                // Check overflow AGAIN with the new frequency (GB quirk)
                // This catches cases where the next sweep would overflow
                m_ch[0].sweep_shadow_freq = new_freq;
                uint16_t next_freq = calcSweepFreq(&m_ch[0]);
                if (next_freq == 0xFFFF) {
                    m_ch[0].active = false;
                    return;
                }

                // Apply new frequency
                m_ch[0].freq = new_freq;
            }
        }
    }
}

void rp_gbapu::update() {
    if (!m_enabled) {
        return;
    }

    // Update envelope for Pulse1, Pulse2, Noise (not Wave - uses different volume system)
    updateEnvelope(0);
    updateEnvelope(1);
    updateEnvelope(3);

    // Update sweep for Pulse1 (uses m_ch[0].freq set at trigger time)
    updateSweep();

    // Update frequency registers (Pulse2, Wave - can change without trigger)
    // Note: Pulse1 freq is set at trigger and modified by sweep, not read here
    m_ch[1].freq = m_regs[NR23] | ((m_regs[NR24] & 0x07) << 8);
    m_ch[2].freq = m_regs[NR33] | ((m_regs[NR34] & 0x07) << 8);

    // Pre-allocate Wave channel BEFORE Pulse2 so Pulse2 knows if it needs to fall back
    preAllocateWaveChannel();

    updatePulse1();
    updatePulse2();
    updateWave();
    updateNoise();
}

// 240Hz split update to avoid buffer overflow
// tick=0: Pulse1, tick=1: Pulse2, tick=2: Wave, tick=3: Noise
// Each channel updates at 60Hz (240/4 = 60)
// Envelope/sweep run on tick 0 (60Hz)
void rp_gbapu::updateTick(uint8_t tick) {
    if (!m_enabled) {
        return;
    }

    switch (tick) {
        case 0:
            // Envelope and sweep run at 60Hz
            updateEnvelope(0);
            updateEnvelope(1);
            updateEnvelope(3);
            // Pulse1 freq is set at trigger and modified by sweep
            updateSweep();
            updatePulse1();
            break;

        case 1:
            // Pre-allocate Wave channel so Pulse2 knows if it needs to fall back
            m_ch[2].freq = m_regs[NR33] | ((m_regs[NR34] & 0x07) << 8);
            preAllocateWaveChannel();
            // Update Pulse2 frequency and output
            m_ch[1].freq = m_regs[NR23] | ((m_regs[NR24] & 0x07) << 8);
            updatePulse2();
            break;

        case 2:
            // Update Wave output (allocation already done in tick 1)
            m_ch[2].freq = m_regs[NR33] | ((m_regs[NR34] & 0x07) << 8);
            updateWave();
            break;

        case 3:
            // Noise
            updateNoise();
            break;
    }
}

// Update all 4 channels in one frame (60Hz)
// With 32-byte buffer and differential sending, this should fit
void rp_gbapu::updateAllChannels() {
    if (!m_enabled) {
        return;
    }

    // Update envelope and sweep (60Hz)
    updateEnvelope(0);
    updateEnvelope(1);
    updateEnvelope(3);
    updateSweep();

    // Update frequency registers (Pulse2, Wave only)
    m_ch[1].freq = m_regs[NR23] | ((m_regs[NR24] & 0x07) << 8);
    m_ch[2].freq = m_regs[NR33] | ((m_regs[NR34] & 0x07) << 8);

    // Pre-allocate Wave channel so Pulse channels know availability
    preAllocateWaveChannel();

    // Update all 4 channels
    updatePulse1();
    updatePulse2();
    updateWave();
    updateNoise();
}

// Update 2 channels per frame (30Hz per channel)
// tick: 0 = Pulse1+Pulse2, 1 = Wave+Noise
// 16バイトバッファに収まるよう2チャンネルずつ更新
void rp_gbapu::updateChannelPair(uint8_t tick) {
    if (!m_enabled) {
        return;
    }

    // Envelope/Sweep は毎フレーム更新
    updateEnvelope(0);
    updateEnvelope(1);
    updateEnvelope(3);
    updateSweep();

    // 周波数レジスタ更新 (Pulse2, Wave only)
    m_ch[1].freq = m_regs[NR23] | ((m_regs[NR24] & 0x07) << 8);
    m_ch[2].freq = m_regs[NR33] | ((m_regs[NR34] & 0x07) << 8);

    if (tick == 0) {
        // Pulse1 + Pulse2
        preAllocateWaveChannel();
        updatePulse1();
        updatePulse2();
    } else {
        // Wave + Noise
        preAllocateWaveChannel();
        updateWave();
        updateNoise();
    }
}

// Pulse優先更新 (メロディ重視)
// 全チャンネル60Hz更新（差分送信でバッファ使用量を抑制）
void rp_gbapu::updatePulsePriority(uint8_t tick) {
    (void)tick;  // 未使用パラメータ

    if (!m_enabled) {
        return;
    }

    // Envelope/Sweep は毎フレーム更新
    updateEnvelope(0);
    updateEnvelope(1);
    updateEnvelope(3);
    updateSweep();

    // 周波数レジスタ更新 (Pulse2, Wave only)
    m_ch[1].freq = m_regs[NR23] | ((m_regs[NR24] & 0x07) << 8);
    m_ch[2].freq = m_regs[NR33] | ((m_regs[NR34] & 0x07) << 8);

    preAllocateWaveChannel();

    // 全チャンネル毎フレーム更新 (60Hz)
    // 差分送信により変更がなければ送信しないためバッファ効率良好
    updatePulse1();
    updatePulse2();
    updateWave();
    updateNoise();
}

// Convert GB frequency register to NES pulse period
// GB: freq = 131072 / (2048 - x)
// NES: freq = 1789773 / (16 * (period + 1))
// Direct conversion to avoid double integer division precision loss:
// period = (1789773 * (2048 - x)) / (16 * 131072) - 1
//        = (1789773 * divisor) / 2097152 - 1
uint16_t rp_gbapu::gbToNesPulsePeriod(uint16_t gbFreqReg) {
    if (gbFreqReg >= 2048) return 0;

    uint32_t divisor = 2048 - gbFreqReg;
    if (divisor == 0) return 2047;

    // Direct conversion with rounding for better accuracy
    uint32_t numerator = (uint32_t)1789773 * divisor;
    uint32_t period = (numerator + 1048576) / 2097152;  // +1048576 for rounding

    if (period == 0) return 8;
    period -= 1;

    // Clamp to valid range
    if (period < 8) period = 8;      // Minimum audible
    if (period > 2047) period = 2047; // 11-bit max

    return (uint16_t)period;
}

// Convert GB frequency register to NES triangle period
// GB Wave: freq = 65536 / (2048 - x) (half of pulse due to 32-sample waveform)
// NES Triangle: freq = 1789773 / (32 * (period + 1))
// Direct conversion:
// period = (1789773 * divisor) / (32 * 65536) - 1
//        = (1789773 * divisor) / 2097152 - 1 (same as pulse)
uint16_t rp_gbapu::gbToNesTrianglePeriod(uint16_t gbFreqReg) {
    if (gbFreqReg >= 2048) return 0;

    uint32_t divisor = 2048 - gbFreqReg;
    if (divisor == 0) return 2047;

    // Direct conversion with rounding (same formula as pulse)
    uint32_t numerator = (uint32_t)1789773 * divisor;
    uint32_t period = (numerator + 1048576) / 2097152;  // +1048576 for rounding

    if (period == 0) return 2;
    period -= 1;

    // GB Wave と NES Triangle は両方とも Pulse より1オクターブ低いため
    // オクターブ補正は不要（相殺される）

    // Clamp to valid range
    if (period < 2) period = 2;
    if (period > 2047) period = 2047;

    return (uint16_t)period;
}

// NES Noise Period Table (NTSC) - CPU cycles between shift register clocks
static const uint16_t NES_NOISE_PERIODS[16] = {
    4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068
};

// GB Noise divisor values for codes 0-7
static const uint8_t GB_NOISE_DIVISORS[8] = {8, 16, 32, 48, 64, 80, 96, 112};

// Convert GB noise parameters to NES noise period index
uint8_t rp_gbapu::gbNoiseToNesPeriod(uint8_t gbNR43) {
    // GB NR43: SSSS WDDD
    // S = clock shift (0-15)
    // W = width mode (0=15-bit, 1=7-bit)
    // D = divisor code (0-7)
    // GB freq = 262144 / (divisor * 2^shift)

    uint8_t shift = (gbNR43 >> 4) & 0x0F;
    uint8_t divCode = gbNR43 & 0x07;
    uint8_t divisor = GB_NOISE_DIVISORS[divCode];

    // Calculate GB noise frequency
    uint32_t divider = (uint32_t)divisor * (1 << shift);
    if (divider == 0) return 0x0F;

    uint32_t gbFreq = 262144 / divider;
    if (gbFreq == 0) return 0x0F;  // Lowest frequency

    // Find NES period that gives closest frequency
    // NES freq = 1789773 / period
    uint8_t bestIdx = 0;
    uint32_t bestDiff = 0xFFFFFFFF;

    for (uint8_t i = 0; i < 16; i++) {
        uint32_t nesFreq = 1789773 / NES_NOISE_PERIODS[i];
        uint32_t diff = (gbFreq > nesFreq) ? (gbFreq - nesFreq) : (nesFreq - gbFreq);
        if (diff < bestDiff) {
            bestDiff = diff;
            bestIdx = i;
        }
    }
    return bestIdx;
}

// NES APU レジスタ書き込み
void rp_gbapu::queueApuWrite(uint8_t reg, uint8_t value) {
    if (reg < 0x18) {
        sys.queueApuWrite(reg, value);
    }
}

void rp_gbapu::stopChannel(uint8_t ch) {
    m_ch[ch].active = false;
    m_ch[ch].triggered = false;
    m_ch[ch].last_nes_period = 0;

    switch (ch) {
        case 0:  // Pulse 1
            queueApuWrite(0x00, 0x30);  // Volume = 0, constant
            break;
        case 1:  // Pulse 2
            queueApuWrite(0x04, 0x30);  // Volume = 0, constant
            break;
        case 2:  // Triangle
            // $4015 bit2=0 で Triangle チャンネルを無効化
            queueApuWrite(0x15, 0x0B);  // Enable Pulse1, Pulse2, Noise only
            break;
        case 3:  // Noise
            queueApuWrite(0x0C, 0x30);  // Volume = 0, constant
            break;
    }
}

void rp_gbapu::updatePulse1() {
#if 0  // Debug: Channel ownership
    static uint8_t dbgCnt = 0;
    static NesChannelOwner lastOwner0 = NES_OWNER_NONE;
    static NesChannelOwner lastOwner1 = NES_OWNER_NONE;
    bool ownerChanged = (m_nesChannelOwner[0] != lastOwner0) || (m_nesChannelOwner[1] != lastOwner1);
    if (++dbgCnt >= 60 || ownerChanged) {
        dbgCnt = 0;
        // Wave 出力先を判定
        const char* waveOut = "---";
        WaveType wt = analyzeWaveform();
        if (m_nesChannelOwner[0] == NES_OWNER_GB_WAVE) waveOut = "P1+Tri";
        else if (m_nesChannelOwner[1] == NES_OWNER_GB_WAVE) waveOut = "P2+Tri";
        else if (m_ch[2].active && m_ch[2].volume > 0) waveOut = "Tri";
        const char* wtNames[] = {"?", "Tri", "P12", "P25", "P50", "P75", "Saw", "Noi"};
        const char* dutyNames[] = {"12.5%", "25%", "50%", "75%"};
        // NES duty is always 50% now
        uint8_t nesD0 = 2;  // 50%
        uint8_t nesD1 = 2;  // 50%
        Serial.printf("CH: Wout=%s wt=%s | P1:%d/v%d/d%s P2:%d/v%d/d%s W:%d/v%d\n",
            waveOut, wtNames[wt],
            m_ch[0].active, m_ch[0].volume, dutyNames[nesD0 & 0x03],
            m_ch[1].active, m_ch[1].volume, dutyNames[nesD1 & 0x03],
            m_ch[2].active, m_ch[2].volume);
        lastOwner0 = m_nesChannelOwner[0];
        lastOwner1 = m_nesChannelOwner[1];
    }
#endif
    if (!m_ch[0].active || m_ch[0].volume == 0) {
        m_ch[0].triggered = false;
        if (m_ch[0].last_nes_period != 0) {
            stopChannel(0);
        }
        return;
    }

    uint16_t period = gbToNesPulsePeriod(m_ch[0].freq);
    uint8_t volume = m_ch[0].volume;
    uint8_t duty = m_ch[0].duty;
    uint8_t originalDuty = duty;

    // Use 50% duty for all - smoothest sound on FC
    (void)originalDuty;  // unused now
    duty = 2;  // Always 50%

    // Volume adjustment: reduce to 93.75% (15/16) for better balance
    uint8_t nesVolume = (volume * 15) / 16;

    // NES $4000: DDLC VVVV
    uint8_t reg4000 = (duty << 6) | 0x30 | nesVolume;
    queueApuWrite(0x00, reg4000);

    // NES $4002: Period low (no side effects, safe to update every frame)
    queueApuWrite(0x02, period & 0xFF);

    // NES $4003: LLLL LHHH
    // IMPORTANT: Writing to $4003 restarts the sequencer (phase reset) and envelope
    // Only write on trigger to avoid click noise
    if (m_ch[0].triggered) {
        uint8_t periodHigh = (period >> 8) & 0x07;
        // NES $4001: Sweep = 0x0F (negate=1, shift=7) to avoid muting
        queueApuWrite(0x01, 0x0F);
        queueApuWrite(0x03, 0xF8 | periodHigh);
        m_ch[0].triggered = false;
    }
    // Do NOT write $4003 for periodHigh changes - causes click noise

    m_ch[0].last_nes_period = period;
}

void rp_gbapu::updatePulse2() {
    if (!m_ch[1].active || m_ch[1].volume == 0) {
        m_ch[1].triggered = false;
        if (m_ch[1].last_nes_period != 0) {
            stopChannel(1);
        }
        return;
    }

    uint16_t period = gbToNesPulsePeriod(m_ch[1].freq);
    uint8_t volume = m_ch[1].volume;
    uint8_t duty = m_ch[1].duty;
    uint8_t originalDuty = duty;

    // Use 50% duty for all - smoothest sound on FC
    (void)originalDuty;  // unused now
    duty = 2;  // Always 50%

    // Volume adjustment: reduce to 93.75% (15/16) for better balance
    uint8_t nesVolume = (volume * 15) / 16;

    // NES $4004: DDLC VVVV
    uint8_t reg4004 = (duty << 6) | 0x30 | nesVolume;
    queueApuWrite(0x04, reg4004);

    // NES $4006: Period low (no side effects, safe to update every frame)
    queueApuWrite(0x06, period & 0xFF);

    // NES $4007: LLLL LHHH
    // IMPORTANT: Writing to $4007 restarts the sequencer (phase reset) and envelope
    // Only write on trigger to avoid click noise
    if (m_ch[1].triggered) {
        uint8_t periodHigh = (period >> 8) & 0x07;
        // NES $4005: Sweep = 0x08 (negate=1, shift=0)
        queueApuWrite(0x05, 0x08);
        queueApuWrite(0x07, 0xF8 | periodHigh);
        m_ch[1].triggered = false;
    }
    // Do NOT write $4007 for periodHigh changes - causes click noise

    m_ch[1].last_nes_period = period;
}

// Pre-allocate Wave channel before Pulse2 updates
// Wave uses Pulse2 only when GB Pulse2 is inactive
void rp_gbapu::preAllocateWaveChannel() {
    // Skip if Wave is inactive or muted
    if (!m_ch[2].active || m_ch[2].volume == 0) {
        // Release channel if we had one
        if (m_ch[2].nes_channel_used != 0xFF) {
            releaseNesChannel(m_ch[2].nes_channel_used);
            m_ch[2].nes_channel_used = 0xFF;
        }
        return;
    }

    // If GB Pulse2 is active, Wave must release Pulse2
    if (m_ch[1].active && m_ch[2].nes_channel_used == 1) {
        stopWaveChannel();
        releaseNesChannel(1);
        m_ch[2].nes_channel_used = 0xFF;
        return;  // Wave is muted while GB Pulse2 is active
    }

    // Allocate channel if needed (only if GB Pulse2 is inactive)
    if (m_ch[2].nes_channel_used == 0xFF) {
        m_ch[2].nes_channel_used = allocateNesChannel(m_ch[2].detected_wave_type);
    }
}

void rp_gbapu::updateWave() {
    // Channel allocation is done by preAllocateWaveChannel() before this

    // 毎フレーム NR30/NR32 から active と volume を更新
    m_ch[2].active = (m_regs[NR30] & 0x80) != 0;
    m_ch[2].volume = (m_regs[NR32] >> 5) & 0x03;

    // Wave は Triangle で常に出力（Pulse での出力は補完）

    // フレーム内で停止リクエストがあった場合、まず停止コマンドを送信
    // これにより、停止→再開が同じフレームで起きても音の切れ目が表現される
    if (m_ch[2].stop_requested) {
        queueApuWrite(0x15, 0x0B);  // Triangle チャンネル無効化
        m_ch[2].stop_requested = false;
        m_ch[2].last_nes_period = 0;  // 再開時にフル更新を強制
    }

    // Handle inactive or muted channel
    if (!m_ch[2].active || m_ch[2].volume == 0) {
        return;
    }

    // GB Wave → NES Triangle として出力
    m_ch[2].nes_channel_used = 2;
    updateWaveAsTriangle();
}

void rp_gbapu::updateNoise() {
    if (!m_ch[3].active) {
        if (m_ch[3].last_nes_period != 0) {
            stopChannel(3);
        }
        return;
    }

    uint8_t volume = m_ch[3].volume;
    uint8_t nr43 = m_regs[NR43];

    // GB NR43: SSSS WDDD
    uint8_t nesPeriod = gbNoiseToNesPeriod(nr43);
    uint8_t nesMode = (nr43 & 0x08) ? 0x80 : 0x00;  // W bit -> NES mode bit

    // Shift noise toward lower frequency (softer sound)
    // Higher period index = lower frequency = less harsh
    if (nesPeriod < 14) nesPeriod += 2;

    // Volume adjustment: reduce to 93.75% (15/16) for better balance
    uint8_t nesVolume = (volume * 15) / 16;

    // NES $400C: --LC VVVV
    queueApuWrite(0x0C, 0x30 | nesVolume);

    // NES $400E: M--- PPPP
    queueApuWrite(0x0E, nesMode | (nesPeriod & 0x0F));

    // NES $400F: LLLL L--- (length counter load)
    if (m_ch[3].last_nes_period == 0) {
        queueApuWrite(0x0F, 0xF8);  // Max length
    }

    m_ch[3].last_nes_period = nesPeriod | (nesMode ? 0x100 : 0);
}

// Compute hash for wave RAM change detection
uint8_t rp_gbapu::computeWaveRamHash() {
    uint8_t hash = 0;
    for (int i = 0; i < 16; i++) {
        hash ^= m_regs[WAVE_RAM_START + i];
        hash = (hash << 1) | (hash >> 7);  // rotate left
    }
    return hash;
}

// Check if waveform is triangle-like pattern
bool rp_gbapu::isTrianglePattern(const uint8_t* samples) {
    // Find peak position
    int peak_idx = 0;
    uint8_t peak_val = 0;
    for (int i = 0; i < 32; i++) {
        if (samples[i] > peak_val) {
            peak_val = samples[i];
            peak_idx = i;
        }
    }

    // Check monotonic descent from peak (first half cycle)
    int falling_count = 0;
    int rising_count = 0;
    for (int i = 0; i < 16; i++) {
        int idx1 = (peak_idx + i) % 32;
        int idx2 = (peak_idx + i + 1) % 32;
        int diff = (int)samples[idx2] - (int)samples[idx1];
        if (diff > 0) rising_count++;
        else if (diff < 0) falling_count++;
    }

    // Triangle pattern: predominantly one direction
    return (falling_count >= 12 || rising_count >= 12);
}

// Check if waveform is sawtooth-like pattern
bool rp_gbapu::isSawtoothPattern(const uint8_t* samples) {
    // Sawtooth: monotonic rise then sudden drop (or vice versa)
    int rising_count = 0;
    int falling_count = 0;
    int big_jump = 0;

    for (int i = 0; i < 32; i++) {
        int next = (i + 1) % 32;
        int diff = (int)samples[next] - (int)samples[i];
        if (diff > 0) rising_count++;
        else if (diff < 0) falling_count++;

        // Check for big jump (characteristic of sawtooth)
        if (diff > 8 || diff < -8) big_jump++;
    }

    // Sawtooth: mostly one direction with 1-2 big jumps
    return (big_jump >= 1 && big_jump <= 2) &&
           ((rising_count >= 20 && falling_count <= 5) ||
            (falling_count >= 20 && rising_count <= 5));
}

// Analyze wave RAM to determine wave type
WaveType rp_gbapu::analyzeWaveform() {
    // Extract 32 4-bit samples from wave RAM
    uint8_t samples[32];
    for (int i = 0; i < 16; i++) {
        uint8_t byte = m_regs[WAVE_RAM_START + i];
        samples[i * 2]     = (byte >> 4) & 0x0F;  // High nibble first
        samples[i * 2 + 1] = byte & 0x0F;         // Low nibble second
    }

    // Calculate features
    uint8_t min_val = 15, max_val = 0;
    int transition_count = 0;
    int high_count = 0;  // Samples >= 8

    for (int i = 0; i < 32; i++) {
        if (samples[i] < min_val) min_val = samples[i];
        if (samples[i] > max_val) max_val = samples[i];
        if (samples[i] >= 8) high_count++;

        // Count zero crossings (transitions between high/low)
        int next = (i + 1) % 32;
        int curr_sign = (samples[i] >= 8) ? 1 : 0;
        int next_sign = (samples[next] >= 8) ? 1 : 0;
        if (curr_sign != next_sign) transition_count++;
    }

    uint8_t range = max_val - min_val;

    // 1. Low amplitude - treat as pulse (still audible)
    if (range < 4) {
        return WAVE_TYPE_PULSE_50;  // Default to pulse for low amplitude
    }

    // 2. Noise-like (many transitions) - check first
    if (transition_count >= 12) {
        return WAVE_TYPE_NOISE_LIKE;
    }

    // 3. Pulse wave detection (relaxed: 2-6 transitions)
    // Most game music uses pulse-like waves
    if (transition_count <= 6) {
        int duty_percent = (high_count * 100) / 32;
        if (duty_percent >= 5 && duty_percent <= 20) return WAVE_TYPE_PULSE_12;
        if (duty_percent >= 20 && duty_percent <= 35) return WAVE_TYPE_PULSE_25;
        if (duty_percent >= 35 && duty_percent <= 65) return WAVE_TYPE_PULSE_50;
        if (duty_percent >= 65 && duty_percent <= 85) return WAVE_TYPE_PULSE_75;
        // Anything else with few transitions is pulse-like
        return WAVE_TYPE_PULSE_50;
    }

    // 4. Triangle pattern detection (stricter: needs smooth gradient)
    if (transition_count >= 6 && isTrianglePattern(samples)) {
#if APU_DEBUG_WAVE
        Serial.printf("WV:tr=%d->TRI\n", transition_count);
#endif
        return WAVE_TYPE_TRIANGLE;
    }

    // 5. Sawtooth pattern detection
    if (isSawtoothPattern(samples)) {
        return WAVE_TYPE_SAWTOOTH;
    }

    // 6. Default: treat as pulse (better sound quality on NES)
#if APU_DEBUG_WAVE
    Serial.printf("WV:tr=%d,hi=%d,rng=%d->P50\n", transition_count, high_count, range);
#endif
    return WAVE_TYPE_PULSE_50;
}

// Allocate NES channel for wave output
// Wave always uses Triangle
uint8_t rp_gbapu::allocateNesChannel(WaveType waveType) {
    (void)waveType;
    m_nesChannelOwner[2] = NES_OWNER_GB_WAVE;
    return 2;  // Triangle
}

// Release NES channel
void rp_gbapu::releaseNesChannel(uint8_t nesChannel) {
    if (nesChannel < 4) {
        m_nesChannelOwner[nesChannel] = NES_OWNER_NONE;
    }
}

// Stop wave channel output
void rp_gbapu::stopWaveChannel() {
    uint8_t ch = m_ch[2].nes_channel_used;
    if (ch == 0xFF) return;

    switch (ch) {
        case 0:  // Pulse 1
            queueApuWrite(0x00, 0x30);
            break;
        case 1:  // Pulse 2
            queueApuWrite(0x04, 0x30);
            break;
        case 2:  // Triangle
            // $4015 bit2=0 で Triangle チャンネルを無効化
            queueApuWrite(0x15, 0x0B);  // Enable Pulse1, Pulse2, Noise only
            break;
    }
    m_ch[2].last_nes_period = 0;
    m_ch[2].nes_channel_used = 0xFF;  // チャンネル解放
}

// Force wave channel to use triangle (when pulse channels are needed by GB pulse)
void rp_gbapu::forceWaveToTriangle() {
    if (m_ch[2].nes_channel_used != 2 && m_ch[2].nes_channel_used != 0xFF) {
        // Currently using a pulse channel, stop it
        stopWaveChannel();
        releaseNesChannel(m_ch[2].nes_channel_used);
        // Assign triangle
        m_nesChannelOwner[2] = NES_OWNER_GB_WAVE;
        m_ch[2].nes_channel_used = 2;
    }
}

// Output wave channel as triangle
void rp_gbapu::updateWaveAsTriangle() {
    uint16_t period = gbToNesTrianglePeriod(m_ch[2].freq);

    // $4015 bit2=1 で Triangle チャンネルを有効化
    queueApuWrite(0x15, 0x0F);  // Enable all 4 channels

    // NES $4008: CRRR RRRR - linear counter (C=1 for infinite)
    queueApuWrite(0x08, 0xFF);

    // NES $400A: Period low
    queueApuWrite(0x0A, period & 0xFF);

    // NES $400B: LLLL LHHH
    uint8_t periodHigh = (period >> 8) & 0x07;
    if (m_ch[2].last_nes_period == 0 ||
        ((m_ch[2].last_nes_period >> 8) & 0x07) != periodHigh) {
        queueApuWrite(0x0B, 0xF8 | periodHigh);
    }

    m_ch[2].last_nes_period = period;
}


// Peanut-GB callback wrappers
uint8_t audio_read(uint16_t addr) {
    return gbapu.read(addr);
}

void audio_write(uint16_t addr, uint8_t val) {
    gbapu.write(addr, val);
}
