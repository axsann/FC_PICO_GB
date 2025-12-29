/*
    rp_gbapu.h

    Game Boy APU to NES APU mapping module
    Receives GB APU register writes and converts them to NES APU commands
*/

#ifndef rp_gbapu_h
#define rp_gbapu_h

#include <stdint.h>

// Debug logging flags (set to 1 to enable)
#define APU_DEBUG_TRIGGER  0  // Log channel triggers
#define APU_DEBUG_WAVE     0  // Log wave type changes

// GB APU register range: 0xFF10 - 0xFF3F
#define GB_APU_REG_START  0xFF10
#define GB_APU_REG_END    0xFF3F
#define GB_APU_REG_SIZE   (GB_APU_REG_END - GB_APU_REG_START + 1)

// GB APU register offsets (from 0xFF10)
// Channel 1 (Pulse with sweep)
#define NR10  0x00  // Sweep
#define NR11  0x01  // Length/Duty
#define NR12  0x02  // Volume envelope
#define NR13  0x03  // Frequency low
#define NR14  0x04  // Frequency high / Trigger

// Channel 2 (Pulse)
#define NR21  0x06  // Length/Duty
#define NR22  0x07  // Volume envelope
#define NR23  0x08  // Frequency low
#define NR24  0x09  // Frequency high / Trigger

// Channel 3 (Wave)
#define NR30  0x0A  // DAC enable
#define NR31  0x0B  // Length
#define NR32  0x0C  // Volume
#define NR33  0x0D  // Frequency low
#define NR34  0x0E  // Frequency high / Trigger

// Channel 4 (Noise)
#define NR41  0x10  // Length
#define NR42  0x11  // Volume envelope
#define NR43  0x12  // Frequency / Randomness
#define NR44  0x13  // Trigger

// Control
#define NR50  0x14  // Master volume
#define NR51  0x15  // Panning
#define NR52  0x16  // APU enable

// Wave RAM: 0xFF30-0xFF3F (offsets 0x20-0x2F)
#define WAVE_RAM_START  0x20

// Wave type classification for dynamic channel mapping
enum WaveType {
    WAVE_TYPE_UNKNOWN = 0,   // Unknown/complex waveform
    WAVE_TYPE_TRIANGLE,      // Triangle wave
    WAVE_TYPE_PULSE_12,      // 12.5% duty pulse
    WAVE_TYPE_PULSE_25,      // 25% duty pulse
    WAVE_TYPE_PULSE_50,      // 50% duty pulse
    WAVE_TYPE_PULSE_75,      // 75% duty pulse
    WAVE_TYPE_SAWTOOTH,      // Sawtooth wave
    WAVE_TYPE_NOISE_LIKE     // Noise-like waveform
};

// NES channel ownership tracking
enum NesChannelOwner {
    NES_OWNER_NONE = 0,
    NES_OWNER_GB_PULSE1,
    NES_OWNER_GB_PULSE2,
    NES_OWNER_GB_WAVE,
    NES_OWNER_GB_NOISE
};

// Channel state
struct gb_channel {
    bool active;
    bool triggered;          // Trigger occurred this frame (for phase reset)
    bool stop_requested;     // Stop was requested this frame (for proper note separation)
    uint16_t freq;           // GB frequency register (11-bit)
    uint8_t volume;          // Current volume (0-15)
    uint8_t duty;            // Duty cycle (0-3)
    uint16_t last_nes_period; // For phase reset avoidance
    WaveType detected_wave_type;  // Detected wave type (for wave channel)
    uint8_t nes_channel_used;     // NES channel used (0=Pulse1, 1=Pulse2, 2=Triangle, 0xFF=none)

    // Envelope tracking (for Pulse1, Pulse2, Noise)
    uint8_t env_initial;     // Initial volume (0-15)
    uint8_t env_direction;   // 0=decrease, 1=increase
    uint8_t env_period;      // Envelope period (0=disabled, 1-7)
    uint16_t env_counter;    // Fixed-point (8.8) counter for accurate 64Hz timing

    // Sweep tracking (for Pulse1 only)
    uint8_t sweep_period;    // Sweep period (0=disabled, 1-7)
    uint8_t sweep_direction; // 0=increase, 1=decrease
    uint8_t sweep_shift;     // Sweep shift (0-7)
    uint16_t sweep_counter;  // Fixed-point (8.8) counter for accurate 128Hz timing
};

class rp_gbapu {
public:
    rp_gbapu();

    void init();

    // Peanut-GB callbacks
    uint8_t read(uint16_t addr);
    void write(uint16_t addr, uint8_t val);

    // Called every frame (60Hz) to update NES APU
    void update();

    // Called at 240Hz with rotating tick (0-3)
    // tick=0: Pulse1, tick=1: Pulse2, tick=2: Wave, tick=3: Noise
    // Each channel updates at 60Hz (240/4 = 60)
    void updateTick(uint8_t tick);

    // Update all 4 channels in one frame (60Hz)
    // Uses differential sending to minimize buffer usage
    void updateAllChannels();

    // Update 2 channels per frame (30Hz per channel)
    // tick: 0 = Pulse1+Pulse2, 1 = Wave+Noise
    void updateChannelPair(uint8_t tick);

    // Pulse優先更新 (メロディ重視)
    // Pulse1+Pulse2: 60Hz, Wave+Noise: 20Hz
    void updatePulsePriority(uint8_t tick);

private:
    // GB APU registers (0xFF10-0xFF3F)
    uint8_t m_regs[GB_APU_REG_SIZE];

    // Channel states
    gb_channel m_ch[4];

    // Master control
    bool m_enabled;      // NR52 bit 7

    // NES channel ownership (0=Pulse1, 1=Pulse2, 2=Triangle, 3=Noise)
    NesChannelOwner m_nesChannelOwner[4];

    // Wave analysis cache
    WaveType m_lastWaveType;
    uint8_t m_waveRamHash;

    // NES APU レジスタ書き込み
    void queueApuWrite(uint8_t reg, uint8_t value);

    // Channel update functions
    void updatePulse1();
    void updatePulse2();
    void preAllocateWaveChannel();
    void updateWave();
    void updateNoise();

    // Frequency conversion
    uint16_t gbToNesPulsePeriod(uint16_t gbFreqReg);
    uint16_t gbToNesTrianglePeriod(uint16_t gbFreqReg);
    uint8_t gbNoiseToNesPeriod(uint8_t gbNR43);

    // Helper to stop a channel
    void stopChannel(uint8_t ch);

    // Envelope and sweep
    void updateEnvelope(uint8_t ch);
    void updateSweep();

    // Waveform analysis
    WaveType analyzeWaveform();
    bool isTrianglePattern(const uint8_t* samples);
    bool isSawtoothPattern(const uint8_t* samples);
    uint8_t computeWaveRamHash();

    // Channel allocation
    uint8_t allocateNesChannel(WaveType waveType);
    void releaseNesChannel(uint8_t nesChannel);

    // Wave channel output
    void updateWaveAsTriangle();
    void stopWaveChannel();
    void forceWaveToTriangle();
};

extern rp_gbapu gbapu;

// Peanut-GB callback wrappers (must be defined before peanut_gb.h include)
uint8_t audio_read(uint16_t addr);
void audio_write(uint16_t addr, uint8_t val);

#endif
