#pragma once

// ---------------------------------------------------------------------------
// Panel + bus definition for the current target: BIGTREETECH KNOMI V1
//   ESP32-WROVER-E driving a GC9A01 240x240 round panel over SPI.
//
// MIGRATION NOTE:
//   This is the ONLY file that should need to change to port to the final
//   target (Waveshare ESP32-S3-Touch-LCD-2.1, 480x480, RGB parallel bus).
//   Everything above this layer (display::* in display.h/.cpp and all UI
//   code) only calls into display.h, never touches LGFX_Device or pins
//   directly. When porting: write a new LGFX_KNOMI-equivalent class wired
//   to lgfx::Panel_RGB / lgfx::Bus_RGB instead of Panel_GC9A01 / Bus_SPI,
//   update display::begin() to instantiate it, and nothing else changes.
// ---------------------------------------------------------------------------

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// GPIO pin mapping, from bigtreetech/KNOMI firmware branch,
// src/pinout_knomi_v1.h (VSPI natural pins, no MISO wired to the panel)
namespace knomi_v1_pins {
    static constexpr int MOSI = 23;
    static constexpr int SCLK = 18;
    static constexpr int CS   = 5;
    static constexpr int DC   = 19;
    static constexpr int RST  = 4;
    static constexpr int BL   = 2;
}

class LGFX_KNOMI : public lgfx::LGFX_Device {
public:
    lgfx::Panel_GC9A01 _panel_instance;
    lgfx::Bus_SPI      _bus_instance;
    lgfx::Light_PWM    _light_instance;

    LGFX_KNOMI() {
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host = VSPI_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = 80000000;
            cfg.freq_read  = 5000000;
            cfg.spi_3wire  = false;  // separate MOSI/SCLK lines, MISO just unused
            cfg.use_lock   = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk = knomi_v1_pins::SCLK;
            cfg.pin_mosi = knomi_v1_pins::MOSI;
            cfg.pin_miso = -1;
            cfg.pin_dc   = knomi_v1_pins::DC;
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }
        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs   = knomi_v1_pins::CS;
            cfg.pin_rst  = knomi_v1_pins::RST;
            cfg.pin_busy = -1;

            cfg.panel_width  = 240;
            cfg.panel_height = 240;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 0;
            cfg.readable  = false;
            cfg.invert    = true;   // GC9A01 needs inverted colors
            cfg.rgb_order = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = false;
            _panel_instance.config(cfg);
        }
        {
            auto cfg = _light_instance.config();
            cfg.pin_bl = knomi_v1_pins::BL;
            cfg.invert = false;
            cfg.freq   = 44100;
            cfg.pwm_channel = 7;
            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance);
        }
        setPanel(&_panel_instance);
    }
};
