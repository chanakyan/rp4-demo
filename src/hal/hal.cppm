// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
//
// rp4.hal — Hardware Abstraction Layer for BCM2711 (Raspberry Pi 4)
//
// Provides typed register access for:
//   - I2S / PCM audio (BCM2711 PCM/I2S peripheral)
//   - PWM audio (3.5mm jack)
//   - GPIO (pin mux for I2S/PWM)
//   - ARM Generic Timer (drives kernel tick)
//   - Mini UART (debug console)
//   - VideoCore mailbox (firmware property interface)
//   - Reboot (PSCI + tryboot for dual-boot with Linux/Kodi)
//
// Host build: register reads/writes are no-ops (simulation mode).
// Cross build: direct MMIO at BCM2711 peripheral base (0xFE000000).

export module rp4.hal;
import std;
import qnx.types;

export namespace rp4::hal {

// ─── BCM2711 peripheral base addresses ─────────────────────────────────────

inline constexpr std::uintptr_t peripheral_base = 0xFE000000;
inline constexpr std::uintptr_t gpio_base       = peripheral_base + 0x200000;
inline constexpr std::uintptr_t pcm_base        = peripheral_base + 0x203000;  // I2S
inline constexpr std::uintptr_t pwm0_base       = peripheral_base + 0x20C000;
inline constexpr std::uintptr_t pwm1_base       = peripheral_base + 0x20C800;
inline constexpr std::uintptr_t uart_base       = peripheral_base + 0x215000;  // mini UART
inline constexpr std::uintptr_t timer_base      = peripheral_base + 0x003000;  // system timer
inline constexpr std::uintptr_t gic_base        = 0xFF840000;                  // GIC-400
/** @brief VideoCore mailbox base address (property interface). */
inline constexpr std::uintptr_t mbox_base       = peripheral_base + 0x00B880;

// ─── Register name table (built at compile time, no heap) ─────────────────
// constexpr array of {address, name} pairs. Sorted by address for binary search.

struct RegEntry {
    std::uintptr_t addr;
    const char* name;
};

inline constexpr std::array reg_names = {
    // GPIO
    RegEntry{gpio_base + 0x00, "GPFSEL0"},
    RegEntry{gpio_base + 0x04, "GPFSEL1"},
    RegEntry{gpio_base + 0x08, "GPFSEL2"},
    RegEntry{gpio_base + 0x1C, "GPSET0"},
    RegEntry{gpio_base + 0x28, "GPCLR0"},
    // I2S / PCM
    RegEntry{pcm_base + 0x00, "PCM_CS"},
    RegEntry{pcm_base + 0x04, "PCM_FIFO"},
    RegEntry{pcm_base + 0x08, "PCM_MODE"},
    RegEntry{pcm_base + 0x10, "PCM_TXC"},
    RegEntry{pcm_base + 0x18, "PCM_INTEN"},
    // UART
    RegEntry{uart_base + 0x40, "AUX_MU_IO"},
    RegEntry{uart_base + 0x54, "AUX_MU_LSR"},
    // I2C (BSC1) — peripheral_base + 0x804000
    RegEntry{peripheral_base + 0x804000, "BSC1_C"},
    RegEntry{peripheral_base + 0x804004, "BSC1_S"},
    RegEntry{peripheral_base + 0x804008, "BSC1_DLEN"},
    RegEntry{peripheral_base + 0x80400C, "BSC1_A"},
    RegEntry{peripheral_base + 0x804010, "BSC1_FIFO"},
    RegEntry{peripheral_base + 0x804014, "BSC1_DIV"},
    // Clock manager
    RegEntry{peripheral_base + 0x101098, "CM_PCMCTL"},
    RegEntry{peripheral_base + 0x10109C, "CM_PCMDIV"},
    // EMMC
    RegEntry{peripheral_base + 0x340000, "EMMC_ARG2"},
    RegEntry{peripheral_base + 0x34000C, "EMMC_CMDTM"},
    RegEntry{peripheral_base + 0x340024, "EMMC_STATUS"},
    RegEntry{peripheral_base + 0x34002C, "EMMC_CONTROL1"},
    // GENET
    RegEntry{0xFD580000, "GENET_SYS_REV"},
    RegEntry{0xFD580800 + 0x008, "UMAC_CMD"},
    RegEntry{0xFD580800 + 0x00C, "UMAC_MAC0"},
    RegEntry{0xFD580800 + 0x010, "UMAC_MAC1"},
    // Timer — ARM_LOCAL at 0xFF800000
    RegEntry{0xFF800040, "TIMER_CNTRL0"},
};

/** @brief Look up register name by address. Returns "?" if unknown. */
[[nodiscard]] inline constexpr auto reg_name(std::uintptr_t addr) -> const char* {
    for (auto& r : reg_names) {
        if (r.addr == addr) return r.name;
    }
    return "?";
}

// ─── Register access (type-safe MMIO, PPS-logged) ─────────────────────────
// Every read/write is published to /pps/hal/reg for the console debug monitor.
// In IRQ context (timer tick), logging is skipped (no allocation).

inline bool hal_log_enabled = false;  // set true after PPS is ready

// Skip logging for hot-path registers (FIFO writes, UART polls)
[[nodiscard]] inline constexpr auto is_hot_path(std::uintptr_t addr) -> bool {
    return addr == (pcm_base + 0x04)    // PCM_FIFO — every sample
        || addr == (uart_base + 0x40)   // AUX_MU_IO — every char
        || addr == (uart_base + 0x54);  // AUX_MU_LSR — every poll
}

// Log to UART: "W CM_PCMCTL 0xFE101098=0x5A000016"
inline auto hal_log_write(const char* name, std::uintptr_t addr, std::uint32_t val) -> void {
    if (is_hot_path(addr)) return;
    std::array<char, 64> buf{};
    auto [end, _] = std::format_to_n(buf.data(), buf.size() - 1,
        "W {} {:08X}={:08X}\n", name, addr, val);
    *end = '\0';
    // Direct UART output — bypass uart::puts to avoid recursion
    for (auto* p = buf.data(); *p; ++p) {
        while (!(*reinterpret_cast<volatile std::uint32_t*>(uart_base + 0x54) & 0x20)) {}
        *reinterpret_cast<volatile std::uint32_t*>(uart_base + 0x40) = *p;
    }
}

inline auto hal_log_read(const char* name, std::uintptr_t addr, std::uint32_t val) -> void {
    if (is_hot_path(addr)) return;
    std::array<char, 64> buf{};
    auto [end, _] = std::format_to_n(buf.data(), buf.size() - 1,
        "R {} {:08X}={:08X}\n", name, addr, val);
    *end = '\0';
    for (auto* p = buf.data(); *p; ++p) {
        while (!(*reinterpret_cast<volatile std::uint32_t*>(uart_base + 0x54) & 0x20)) {}
        *reinterpret_cast<volatile std::uint32_t*>(uart_base + 0x40) = *p;
    }
}

/** @brief Volatile 32-bit MMIO register read. */
[[nodiscard]] inline auto mmio_read(std::uintptr_t addr) -> std::uint32_t {
#ifdef RP4_HOST_BUILD
    (void)addr;
    return 0;
#else
    auto val = *reinterpret_cast<volatile std::uint32_t*>(addr);
    if (hal_log_enabled) hal_log_read(reg_name(addr), addr, val);
    return val;
#endif
}

/** @brief Volatile 32-bit MMIO register write. */
inline auto mmio_write(std::uintptr_t addr, std::uint32_t val) -> void {
#ifdef RP4_HOST_BUILD
    (void)addr; (void)val;
#else
    *reinterpret_cast<volatile std::uint32_t*>(addr) = val;
    if (hal_log_enabled) hal_log_write(reg_name(addr), addr, val);
#endif
}

// ─── I2S / PCM audio registers ─────────────────────────────────────────────

/** @brief BCM2711 I2S control/status register offsets. */
namespace i2s {
    inline constexpr std::uintptr_t cs_a    = pcm_base + 0x00;  ///< Control/Status
    inline constexpr std::uintptr_t fifo_a  = pcm_base + 0x04;  ///< FIFO data
    inline constexpr std::uintptr_t mode_a  = pcm_base + 0x08;  ///< Mode
    inline constexpr std::uintptr_t rxc_a   = pcm_base + 0x0C;  ///< Receive config
    inline constexpr std::uintptr_t txc_a   = pcm_base + 0x10;  ///< Transmit config
    inline constexpr std::uintptr_t dreq_a  = pcm_base + 0x14;  ///< DMA request level
    inline constexpr std::uintptr_t inten_a = pcm_base + 0x18;  ///< Interrupt enables
    inline constexpr std::uintptr_t intstc_a= pcm_base + 0x1C;  ///< Interrupt status/clear
    inline constexpr std::uintptr_t gray    = pcm_base + 0x20;  ///< Gray mode

    // CS_A register bits
    inline constexpr std::uint32_t cs_en     = 1 << 0;   ///< Enable
    inline constexpr std::uint32_t cs_rxon   = 1 << 1;   ///< Receive enable
    inline constexpr std::uint32_t cs_txon   = 1 << 2;   ///< Transmit enable
    inline constexpr std::uint32_t cs_txclr  = 1 << 3;   ///< Clear TX FIFO
    inline constexpr std::uint32_t cs_rxclr  = 1 << 4;   ///< Clear RX FIFO
    inline constexpr std::uint32_t cs_txd    = 1 << 19;  ///< TX FIFO can accept data
    inline constexpr std::uint32_t cs_rxd    = 1 << 20;  ///< RX FIFO has data
    inline constexpr std::uint32_t cs_txerr  = 1 << 15;  ///< TX FIFO error
    inline constexpr std::uint32_t cs_rxerr  = 1 << 16;  ///< RX FIFO error

    // ─── PCM clock (CM_PCMCTL / CM_PCMDIV) ──────────────────────────────
    // Clock source: PLLD = 750 MHz on BCM2711.
    // BCK = PLLD / (DIVI + DIVF/4096) / (CHANLEN * CHANS)
    // CHANLEN = 32 bits, CHANS = 2 (stereo)

    inline constexpr std::uintptr_t cm_pcmctl = peripheral_base + 0x101098;
    inline constexpr std::uintptr_t cm_pcmdiv = peripheral_base + 0x10109C;
    inline constexpr std::uint32_t cm_passwd  = 0x5A000000;
    inline constexpr std::uint32_t plld_freq  = 750000000;
    inline constexpr std::uint32_t chanlen    = 32;
    inline constexpr std::uint32_t chans      = 2;

    /** @brief Set I2S bit clock for a given sample rate.
     *  Stops clock, reprograms divisor, restarts. PCM5122 PLL re-locks. */
    inline auto set_sample_rate(std::uint32_t rate) -> void {
        // Stop clock
        mmio_write(cm_pcmctl, cm_passwd | (1 << 5));  // KILL
        while (mmio_read(cm_pcmctl) & (1 << 7)) {}    // wait BUSY=0

        // Calculate divisor
        auto base = plld_freq / (chanlen * chans);
        auto divi = base / rate;
        auto rem  = base % rate;
        auto divf = (rem * 4096 + rate / 2) / rate;
        if (divf > 4095) { ++divi; divf = 0; }

        // Set divisor
        mmio_write(cm_pcmdiv, cm_passwd | (divi << 12) | divf);

        // Start clock: source = PLLD (6), MASH = 1 if fractional
        auto mash = (divf > 0) ? (1u << 9) : 0u;
        mmio_write(cm_pcmctl, cm_passwd | 6 | mash | (1 << 4));  // SRC=PLLD, ENAB
    }

    /** @brief Initialize I2S for stereo output at a given sample rate. */
    inline auto init_stereo(std::uint32_t sample_rate = 48000) -> void {
        // Set BCK for target sample rate
        set_sample_rate(sample_rate);

        // Clear FIFOs
        mmio_write(cs_a, cs_txclr | cs_rxclr);

        // Mode: master, 32 clocks per frame (16-bit * 2 channels)
        mmio_write(mode_a, (31 << 10) | 15);

        // TX config: 2 channels, 16-bit, channel 1 at position 0, channel 2 at position 16
        mmio_write(txc_a, (1 << 31) | (0 << 20) | (8 << 16) | (1 << 15) | (16 << 4) | 8);

        // Enable: PCM on, TX on
        mmio_write(cs_a, cs_en | cs_txon);
    }

    /** @brief Initialize I2S for 48kHz stereo (default). */
    inline auto init_48khz_stereo() -> void { init_stereo(48000); }

    /** @brief Write a stereo sample pair to the I2S FIFO. */
    [[nodiscard]] inline auto write_sample(std::int16_t left, std::int16_t right) -> bool {
        if (!(mmio_read(cs_a) & cs_txd)) return false;  // FIFO full
        std::uint32_t packed = static_cast<std::uint32_t>(static_cast<std::uint16_t>(left)) |
                               (static_cast<std::uint32_t>(static_cast<std::uint16_t>(right)) << 16);
        mmio_write(fifo_a, packed);
        return true;
    }
}

// ─── GPIO pin mux ──────────────────────────────────────────────────────────

namespace gpio {
    /** @brief GPIO function select values. */
    enum class Func : std::uint32_t {
        input  = 0b000,
        output = 0b001,
        alt0   = 0b100,  ///< I2S pins use ALT0
        alt1   = 0b101,
        alt2   = 0b110,
        alt3   = 0b111,
        alt4   = 0b011,
        alt5   = 0b010,
    };

    /** @brief Set the function of a GPIO pin. */
    inline auto set_function(int pin, Func func) -> void {
        auto reg = gpio_base + 4 * (pin / 10);
        auto shift = (pin % 10) * 3;
        auto val = mmio_read(reg);
        val &= ~(0b111 << shift);
        val |= static_cast<std::uint32_t>(func) << shift;
        mmio_write(reg, val);
    }

    /** @brief Configure GPIOs 18-21 for I2S (ALT0). */
    inline auto setup_i2s_pins() -> void {
        set_function(18, Func::alt0);  // PCM_CLK
        set_function(19, Func::alt0);  // PCM_FS
        set_function(20, Func::alt0);  // PCM_DIN
        set_function(21, Func::alt0);  // PCM_DOUT
    }
}

// ─── Mini UART (debug output) ───────────────────────────────────────────���──

namespace uart {
    inline constexpr std::uintptr_t io   = uart_base + 0x40;
    inline constexpr std::uintptr_t lsr  = uart_base + 0x54;

    /** @brief Send a single character over mini UART. */
    inline auto putc(char c) -> void {
        while (!(mmio_read(lsr) & 0x20)) {}  // wait for TX empty
        mmio_write(io, static_cast<std::uint32_t>(c));
    }

    /** @brief Send a null-terminated string over mini UART. */
    inline auto puts(const char* s) -> void {
        while (*s) { if (*s == '\n') putc('\r'); putc(*s++); }
    }
}

// ─── BSC / I2C (BCM2711 datasheet §3) ─────────────────────────────────────
// HiFi DAC Pro uses BSC1. GPIO 2 = SDA1, GPIO 3 = SCL1 (ALT0).

inline constexpr std::uintptr_t bsc1_base = peripheral_base + 0x804000;

namespace i2c {
    // Register offsets (Table 24)
    inline constexpr std::uintptr_t reg_c    = bsc1_base + 0x00;  ///< Control
    inline constexpr std::uintptr_t reg_s    = bsc1_base + 0x04;  ///< Status
    inline constexpr std::uintptr_t reg_dlen = bsc1_base + 0x08;  ///< Data length
    inline constexpr std::uintptr_t reg_a    = bsc1_base + 0x0C;  ///< Slave address
    inline constexpr std::uintptr_t reg_fifo = bsc1_base + 0x10;  ///< Data FIFO
    inline constexpr std::uintptr_t reg_div  = bsc1_base + 0x14;  ///< Clock divider
    inline constexpr std::uintptr_t reg_del  = bsc1_base + 0x18;  ///< Data delay
    inline constexpr std::uintptr_t reg_clkt = bsc1_base + 0x1C;  ///< Clock stretch

    // Control register bits (Table 25)
    inline constexpr std::uint32_t c_i2cen  = 1 << 15;  ///< I2C enable
    inline constexpr std::uint32_t c_st     = 1 << 7;   ///< Start transfer
    inline constexpr std::uint32_t c_clear  = 1 << 4;   ///< Clear FIFO
    inline constexpr std::uint32_t c_read   = 1 << 0;   ///< Read transfer

    // Status register bits (Table 26)
    inline constexpr std::uint32_t s_clkt   = 1 << 9;   ///< Clock stretch timeout
    inline constexpr std::uint32_t s_err    = 1 << 8;   ///< ACK error
    inline constexpr std::uint32_t s_done   = 1 << 1;   ///< Transfer done
    inline constexpr std::uint32_t s_ta     = 1 << 0;   ///< Transfer active
    inline constexpr std::uint32_t s_txd    = 1 << 4;   ///< FIFO can accept data

    /** @brief Configure GPIO 2/3 for I2C1 and enable BSC1. */
    inline auto init() -> void {
        gpio::set_function(2, gpio::Func::alt0);  // SDA1
        gpio::set_function(3, gpio::Func::alt0);  // SCL1
        // 100 kHz: core_clk (150 MHz) / CDIV. Default CDIV=0x05DC=1500 → 100 kHz.
        mmio_write(reg_div, 1500);
        mmio_write(reg_c, c_i2cen);
    }

    /** @brief Write bytes to an I2C slave. Returns true on ACK. */
    inline auto write(std::uint8_t addr, std::span<const std::uint8_t> data) -> bool {
        // Clear status flags
        mmio_write(reg_s, s_clkt | s_err | s_done);
        // Set slave address
        mmio_write(reg_a, addr);
        // Set data length
        mmio_write(reg_dlen, static_cast<std::uint32_t>(data.size()));
        // Fill FIFO
        for (auto b : data) {
            mmio_write(reg_fifo, b);
        }
        // Start write transfer
        mmio_write(reg_c, c_i2cen | c_st);
        // Wait for done
        while (!(mmio_read(reg_s) & s_done)) {
            if (mmio_read(reg_s) & s_err) return false;
        }
        mmio_write(reg_s, s_done);  // clear DONE
        return !(mmio_read(reg_s) & s_err);
    }

    /** @brief Write a single register on an I2C device. */
    inline auto write_reg(std::uint8_t addr, std::uint8_t reg, std::uint8_t val) -> bool {
        std::array<std::uint8_t, 2> buf = {reg, val};
        return write(addr, buf);
    }
}

// ─── PCM5122 DAC (HiFi DAC Pro) ──────────────────────────────────────────
// I2C address 0x4D (ADDR pin pulled high on HiFi DAC Pro).
// Datasheet: TI SLAS763C (PCM512x). Hex→symbol reverse dictionary for PPS.

namespace dac {
    inline constexpr std::uint8_t addr = 0x4D;

    // ── Register name table (constexpr, zero alloc) ──
    // DIRANA pattern: parse chip header, build reverse dictionary,
    // wire into logger. PPS sees names, not raw bytes.

    struct RegEntry {
        std::uint8_t addr;
        const char*  name;
        std::uint8_t ds_page;  // datasheet page (SLAS763C)
    };

    inline constexpr RegEntry reg_table[] = {
        {0x00, "page",       67}, {0x01, "reset",      71}, {0x02, "power",     72},
        {0x03, "mute",       72}, {0x04, "pll_en",     73}, {0x06, "spi_miso",  73},
        {0x07, "dsp",        73}, {0x08, "gpio_en",    74}, {0x09, "bck_lrck",  75},
        {0x0D, "pll_ref",    76}, {0x0E, "dac_clk_src",76},
        {0x13, "sync",       77}, {0x14, "pll_p",      77}, {0x15, "pll_j",     77},
        {0x16, "pll_d_hi",   78}, {0x17, "pll_d_lo",   78}, {0x18, "pll_r",     78},
        {0x1B, "dsp_div",    79}, {0x1C, "dac_div",    79}, {0x1D, "ncp_div",   79},
        {0x1E, "osr_div",    79}, {0x20, "bck_div",    80}, {0x21, "lrck_div",  80},
        {0x22, "fs_speed",   80}, {0x25, "err_ignore", 81},
        {0x28, "i2s_cfg",    82}, {0x29, "i2s_shift",  83}, {0x2A, "dac_path",  83},
        {0x2B, "dsp_prog",   83}, {0x2C, "clk_miss",   84},
        {0x3B, "auto_mute_time",84}, {0x3C, "vol_ctrl",85},
        {0x3D, "vol_l",      85}, {0x3E, "vol_r",      85}, {0x3F, "vol_ramp",  86},
        {0x40, "vol_emerg",  86}, {0x41, "auto_mute",  87},
        {0x50, "gpio1_sel",  87}, {0x51, "gpio2_sel",  88}, {0x52, "gpio3_sel", 89},
        {0x53, "gpio4_sel",  89}, {0x54, "gpio5_sel",  90}, {0x55, "gpio6_sel", 90},
        {0x56, "gpio_out",   91}, {0x57, "gpio_inv",   92},
        {0x5A, "dsp_ovfl",   67}, {0x5B, "det_fs",     67},
        {0x6C, "ana_mute_mon",67}, {0x77, "gpio_in",   67},
        {0x78, "auto_mute_flags",67},
    };

    inline constexpr auto reg_span() -> std::span<const RegEntry> {
        return reg_table;
    }

    /** @brief Hex→symbol lookup. constexpr, O(n) on 45 entries = nothing. */
    inline constexpr auto reg_name(std::uint8_t a) -> const char* {
        for (auto& r : reg_table)
            if (r.addr == a) return r.name;
        return "?";
    }

    /** @brief Format a register write for PPS: "vol_l=0x30" into buf.
     *  Returns chars written. */
    inline auto fmt_write(char* buf, int cap,
                          std::uint8_t reg, std::uint8_t val) -> int {
        const char* n = reg_name(reg);
        // minimal snprintf without <cstdio> — hex nibble by hand
        auto hex = [](std::uint8_t v, int nib) -> char {
            int h = (v >> (nib * 4)) & 0xF;
            return h < 10 ? '0' + h : 'a' + h - 10;
        };
        int i = 0;
        for (const char* p = n; *p && i < cap - 6; ++i, ++p) buf[i] = *p;
        if (i < cap - 5) buf[i++] = '=';
        if (i < cap - 4) buf[i++] = '0';
        if (i < cap - 3) buf[i++] = 'x';
        if (i < cap - 2) buf[i++] = hex(val, 1);
        if (i < cap - 1) buf[i++] = hex(val, 0);
        buf[i] = '\0';
        return i;
    }

    // PCM5122 register addresses
    inline constexpr std::uint8_t reg_page       = 0x00;  ///< Page select
    inline constexpr std::uint8_t reg_reset      = 0x01;  ///< Reset
    inline constexpr std::uint8_t reg_power      = 0x02;  ///< Standby/powerdown
    inline constexpr std::uint8_t reg_mute       = 0x03;  ///< Mute
    inline constexpr std::uint8_t reg_pll_en     = 0x04;  ///< PLL enable
    inline constexpr std::uint8_t reg_spi_miso   = 0x06;  ///< SPI MISO function (set to 0)
    inline constexpr std::uint8_t reg_dsp        = 0x07;  ///< DSP/de-emphasis
    inline constexpr std::uint8_t reg_gpio_en    = 0x08;  ///< GPIO output enable
    inline constexpr std::uint8_t reg_dsp_div    = 0x1B;  ///< DSP clock divider (SLAS763C Tbl73)
    inline constexpr std::uint8_t reg_dac_div    = 0x1C;  ///< DAC clock divider (SLAS763C Tbl74)
    inline constexpr std::uint8_t reg_ncp_div    = 0x1D;  ///< NCP clock divider (SLAS763C Tbl75)
    inline constexpr std::uint8_t reg_osr_div    = 0x1E;  ///< OSR clock divider (SLAS763C Tbl76)
    inline constexpr std::uint8_t reg_bck_div    = 0x20;  ///< Master BCK divider (SLAS763C Tbl77)
    inline constexpr std::uint8_t reg_mclk_src   = 0x0D;  ///< Master clock source
    inline constexpr std::uint8_t reg_pll_ref    = 0x0D;  ///< PLL reference (same as mclk_src)
    inline constexpr std::uint8_t reg_pll_p      = 0x14;  ///< PLL P divider
    inline constexpr std::uint8_t reg_pll_j      = 0x15;  ///< PLL J divider
    inline constexpr std::uint8_t reg_pll_d_hi   = 0x16;  ///< PLL D divider high
    inline constexpr std::uint8_t reg_pll_d_lo   = 0x17;  ///< PLL D divider low
    inline constexpr std::uint8_t reg_pll_r      = 0x18;  ///< PLL R divider
    inline constexpr std::uint8_t reg_i2s_cfg    = 0x28;  ///< I2S config: AFMT[5:4] + ALEN[1:0]
    inline constexpr std::uint8_t reg_i2s_shift  = 0x29;  ///< I2S shift: AOFS[7:0] (frame offset)
    inline constexpr std::uint8_t reg_vol_l      = 0x3D;  ///< Digital volume left
    inline constexpr std::uint8_t reg_vol_r      = 0x3E;  ///< Digital volume right
    inline constexpr std::uint8_t reg_auto_mute  = 0x41;  ///< Auto-mute control

    /** @brief Initialize PCM5122 for I2S slave mode, 48kHz/16-bit stereo.
     *  Call after i2c::init(). Returns false if I2C NAK.
     *  Every write logs "dac: <name>=0x<val>" to uart for PPS. */
    inline auto init() -> bool {
        auto w = [](std::uint8_t reg, std::uint8_t val) -> bool {
            bool ok = i2c::write_reg(addr, reg, val);
#ifndef RP4_HOST_BUILD
            char buf[32];
            fmt_write(buf, sizeof(buf), reg, val);
            uart::puts("dac: ");
            uart::puts(buf);
            uart::puts(ok ? " ok\n" : " FAIL\n");
#endif
            return ok;
        };

        // Page 0
        if (!w(reg_page, 0x00)) return false;

        // Reset DAC
        if (!w(reg_reset, 0x11)) return false;  // reset registers + modules
        // Brief delay — in bare metal, spin
#ifndef RP4_HOST_BUILD
        for (volatile int d = 0; d < 100000; ++d) {}
#endif
        if (!w(reg_reset, 0x00)) return false;  // release reset

        // Power: request standby off
        if (!w(reg_power, 0x00)) return false;

        // PLL: use BCK as PLL reference (HiFi DAC Pro has no separate MCLK)
        // reg 0x0D SREF[2:0] at bits 6:4. 001 = BCK → shift to b4 = 0x10
        // Datasheet SLAS763C Table 64, p76
        if (!w(reg_pll_ref, 0x10)) return false;

        // Enable PLL
        if (!w(reg_pll_en, 0x01)) return false;

        // PLL config for 48kHz from 1.536 MHz BCK (32 * 48000):
        // PLL output = BCK * R * J.D / P
        // We need 24.576 MHz DAC clock from 1.536 MHz BCK
        // 1.536 * 1 * 16.0 / 1 = 24.576 MHz
        if (!w(reg_pll_p, 0x01)) return false;   // P = 1
        if (!w(reg_pll_j, 0x10)) return false;   // J = 16
        if (!w(reg_pll_d_hi, 0x00)) return false; // D = 0 (high byte)
        if (!w(reg_pll_d_lo, 0x00)) return false; // D = 0 (low byte)
        if (!w(reg_pll_r, 0x01)) return false;   // R = 1

        // Clock dividers (SLAS763C Tables 73-77)
        // PLL out = 24.576 MHz
        if (!w(reg_dsp_div, 0x02)) return false;  // DSP = 24.576/2 = 12.288 MHz
        if (!w(reg_dac_div, 0x04)) return false;  // DAC = 24.576/4 = 6.144 MHz
        if (!w(reg_ncp_div, 0x04)) return false;  // NCP = 24.576/4 = 6.144 MHz
        if (!w(reg_osr_div, 0x01)) return false;  // OSR = 6.144/1 = 128*fs

        // I2S format + word length in one register (Pg0/Reg40, SLAS763C Table 83)
        // AFMT[5:4]=00 (I2S), ALEN[1:0]=00 (16-bit)
        // Note: ALEN default is 10 (24-bit), so we must write explicitly
        if (!w(reg_i2s_cfg, 0x00)) return false;

        // Volume: 0dB (0x30 = 0dB, 0xFF = mute)
        if (!w(reg_vol_l, 0x30)) return false;
        if (!w(reg_vol_r, 0x30)) return false;

        // Unmute
        if (!w(reg_mute, 0x00)) return false;

        return true;
    }

    /** @brief Set volume (0 = max, 255 = mute). */
    inline auto set_volume(std::uint8_t vol) -> bool {
        return i2c::write_reg(addr, reg_vol_l, vol) &&
               i2c::write_reg(addr, reg_vol_r, vol);
    }

    /** @brief Mute/unmute. */
    inline auto mute(bool on) -> bool {
        return i2c::write_reg(addr, reg_mute, on ? 0x11 : 0x00);
    }
}

// ─── ARM_LOCAL registers (BCM2711 datasheet §6.5.2) ───────────────────────
// These are NOT at peripheral_base. They're at a separate ARM-only address.

inline constexpr std::uintptr_t arm_local_base = 0xFF800000;

// ─── IRQ control ──────────────────────────────────────────────────────────

namespace irq {
    /** @brief Route CNTP (physical timer) IRQ to Core 0 IRQ line.
     *  BCM2711 §6.5.2, TIMER_CNTRL0 at ARM_LOCAL + 0x40.
     *  Bit 1 = CNT_PNS_IRQ: route non-secure physical timer to IRQ. */
    inline auto enable_timer_irq() -> void {
#ifndef RP4_HOST_BUILD
        // TIMER_CNTRL0: bit 1 = route CNTP to Core 0 IRQ
        mmio_write(arm_local_base + 0x40, 0x02);
#endif
    }

    /** @brief Unmask IRQs at the CPU (clear DAIF.I bit).
     *  AArch64 boots with all exceptions masked (SPSR_EL2 = 0x3C5).
     *  This must be called AFTER vector table is installed. */
    inline auto unmask() -> void {
#ifndef RP4_HOST_BUILD
        asm volatile("msr daifclr, #2");  // clear I bit (unmask IRQ)
#endif
    }

    /** @brief Mask IRQs at the CPU (set DAIF.I bit). */
    inline auto mask() -> void {
#ifndef RP4_HOST_BUILD
        asm volatile("msr daifset, #2");  // set I bit (mask IRQ)
#endif
    }
}

// ─── ARM Generic Timer ─────────────────────────────────────────────────────

namespace timer {
    /** @brief Read the ARM generic timer counter frequency. */
    [[nodiscard]] inline auto freq() -> std::uint64_t {
#ifdef RP4_HOST_BUILD
        return 54000000;  // BCM2711 crystal = 54 MHz
#else
        std::uint64_t f;
        asm volatile("mrs %0, cntfrq_el0" : "=r"(f));
        return f;
#endif
    }

    /** @brief Set the timer compare value for next tick (1ms default). */
    inline auto set_next_tick_ms(std::uint32_t ms = 1) -> void {
#ifndef RP4_HOST_BUILD
        std::uint64_t ticks = freq() * ms / 1000;
        asm volatile("msr cntp_tval_el0, %0" :: "r"(ticks));
        // Enable timer, unmask timer interrupt at EL1
        asm volatile("msr cntp_ctl_el0, %0" :: "r"(std::uint64_t{1}));
#else
        (void)ms;
#endif
    }

    /** @brief Acknowledge timer interrupt (clear pending, re-arm). */
    inline auto ack() -> void {
#ifndef RP4_HOST_BUILD
        set_next_tick_ms(1);  // re-arm for next tick
#endif
    }
}

// ─── VideoCore mailbox (property interface, channel 8) ────────────────────
// BCM2711 §1.3. Used for firmware calls: get/set boot partition, reboot.

namespace mbox {
    /** @brief Mailbox read register (ARM reads VC responses here). */
    inline constexpr std::uintptr_t read_reg   = mbox_base + 0x00;
    /** @brief Mailbox status register (FULL/EMPTY flags). */
    inline constexpr std::uintptr_t status_reg = mbox_base + 0x18;
    /** @brief Mailbox write register (ARM sends requests here). */
    inline constexpr std::uintptr_t write_reg  = mbox_base + 0x20;

    /** @brief Status bit: mailbox is full, cannot write. */
    inline constexpr std::uint32_t full  = 0x80000000;
    /** @brief Status bit: mailbox is empty, cannot read. */
    inline constexpr std::uint32_t empty = 0x40000000;

    /** @brief Property tag buffer (16-byte aligned, GPU-visible BSS). */
    alignas(16) inline volatile std::uint32_t buf[16];

    /** @brief Send a property tag request to the VideoCore firmware.
     *  Channel 8 = ARM→VC property interface.
     *  buf must be filled before calling. */
    inline auto call() -> bool {
#ifdef RP4_HOST_BUILD
        return true;
#else
        auto addr = reinterpret_cast<std::uintptr_t>(&buf[0]);
        auto msg = static_cast<std::uint32_t>(addr & ~0xF) | 8;  // channel 8

        // Wait until mailbox is not full
        while (mmio_read(status_reg) & full) {}
        // Write address + channel
        mmio_write(write_reg, msg);
        // Wait for response
        for (;;) {
            while (mmio_read(status_reg) & empty) {}
            if (mmio_read(read_reg) == msg) return buf[1] == 0x80000000;
        }
#endif
    }
}

// ─── Reboot (PSCI + tryboot) ──────────────────────────────────────────────
// Two modes:
//   reboot_normal()  → boots config.txt   → microkernel
//   reboot_tryboot() → boots tryboot.txt  → Linux/Kodi

namespace reboot {
    /** @brief Set the tryboot flag via VideoCore mailbox.
     *  Property tag 0x00038058 = SET_REBOOT_FLAGS, value 0x14 = tryboot. */
    inline auto set_tryboot_flag() -> bool {
        mbox::buf[0]  = 8 * 4;       // buffer size (32 bytes)
        mbox::buf[1]  = 0;           // request
        mbox::buf[2]  = 0x00038058;  // tag: SET_REBOOT_FLAGS
        mbox::buf[3]  = 4;           // value buffer size
        mbox::buf[4]  = 0;           // request indicator
        mbox::buf[5]  = 0x14;       // tryboot partition flag
        mbox::buf[6]  = 0;           // end tag
        mbox::buf[7]  = 0;
        return mbox::call();
    }

    /** @brief PSCI SYSTEM_RESET — does not return. */
    [[noreturn]] inline auto psci_reset() -> void {
#ifdef RP4_HOST_BUILD
        std::unreachable();
#else
        // PSCI 0.2: SYSTEM_RESET = function ID 0x84000009 (SMC32)
        asm volatile(
            "ldr w0, =0x84000009\n"
            "smc #0\n"
        );
        __builtin_unreachable();
#endif
    }

    /** @brief Reboot into Linux/Kodi (tryboot.txt). */
    [[noreturn]] inline auto to_kodi() -> void {
        uart::puts("reboot: setting tryboot flag\n");
        set_tryboot_flag();
        uart::puts("reboot: PSCI SYSTEM_RESET\n");
        psci_reset();
    }

    /** @brief Normal reboot (config.txt → microkernel). */
    [[noreturn]] inline auto to_microkernel() -> void {
        uart::puts("reboot: normal reset\n");
        psci_reset();
    }
}

} // namespace rp4::hal

