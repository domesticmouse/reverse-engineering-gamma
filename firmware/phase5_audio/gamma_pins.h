#pragma once

#include "daisy_seed.h"

namespace gamma_pins
{

// ============================================================================
// Display (1.3" SSD1306 128x64 OLED)
// ============================================================================
namespace display
{
    constexpr daisy::Pin pin_scl      = daisy::seed::D11; // PB8 (I2C1_SCL)
    constexpr daisy::Pin pin_sda      = daisy::seed::D12; // PB9 (I2C1_SDA)
    constexpr uint8_t    i2c_address  = 0x3D;
    constexpr uint32_t   screen_width = 128;
    constexpr uint32_t   screen_height= 64;
}

// ============================================================================
// Potentiometers (4 Rotary Knobs across top panel, left to right)
// Wipers sweep 3.3V (full CCW) to 0V (full CW); invert in software: (1.0 - raw)
// ============================================================================
namespace knobs
{
    enum Index
    {
        CHORD_VOL = 0,
        CHORD_FILTER,
        NOTES_VOL,
        NOTES_FILTER,
        COUNT
    };

    constexpr daisy::Pin pins[COUNT] = {
        daisy::seed::D18, // 0: Chord Vol    (PA7 / ADC1_INP7  / A3)
        daisy::seed::D17, // 1: Chord Filter (PB1 / ADC1_INP5  / A2)
        daisy::seed::D19, // 2: Notes Vol    (PA6 / ADC1_INP3  / A4)
        daisy::seed::D20  // 3: Notes Filter (PC1 / ADC1_INP11 / A5)
    };

    constexpr bool invert[COUNT] = {true, true, true, true};
    constexpr float iir_coefficient = 0.002f; // Heavy smoothing
}

// ============================================================================
// Joysticks / Thumbsticks (2 Dual-Axis Sticks: 4 Axes)
// ============================================================================
namespace joysticks
{
    enum Axis
    {
        LEFT_X = 0,
        LEFT_Y,
        RIGHT_X,
        RIGHT_Y,
        COUNT
    };

    constexpr daisy::Pin pins[COUNT] = {
        daisy::seed::D22, // 0: Left Stick X  (PA5 / ADC1_INP19 / A7) [Left=0%, Right=100%]
        daisy::seed::D21, // 1: Left Stick Y  (PC4 / ADC1_INP4  / A6) [Down=0%, Up=100%]
        daisy::seed::D24, // 2: Right Stick X (PA1 / ADC1_INP17 / A9) [Left=0%, Right=100%]
        daisy::seed::D23  // 3: Right Stick Y (PA4 / ADC1_INP18 / A8) [Down=0%, Up=100%]
    };

    // Note: Right Stick X is electrically inverted on the PCB layout
    constexpr bool invert[COUNT] = {
        false, // Left X: direct
        true,  // Left Y: inverted (1.0 - raw)
        true,  // Right X: inverted (1.0 - raw)
        true   // Right Y: inverted (1.0 - raw)
    };

    constexpr float iir_coefficient = 0.05f; // Fast, responsive smoothing
}

// ============================================================================
// Auxiliary Analog Input (Internal / Battery sensor)
// ============================================================================
namespace aux_adc
{
    constexpr daisy::Pin pin = daisy::seed::D31; // PC2 / ADC1_INP12
}

// ============================================================================
// Left Keypad (7 Note Keys: Physical Keys on the Left, Active-Low w/ Pull-up)
// ============================================================================
namespace note_keys
{
    enum Key
    {
        N1 = 0, // Top Row, col 1
        N2,     // Top Row, col 2
        N3,     // Top Row, col 3
        N4,     // Top Row, col 4
        N5,     // Bottom Row, col 1
        N6,     // Bottom Row, col 2
        N7,     // Bottom Row, col 3
        COUNT
    };

    constexpr daisy::Pin pins[COUNT] = {
        daisy::seed::D1, // N1: PC11
        daisy::seed::D2, // N2: PC10
        daisy::seed::D3, // N3: PC9
        daisy::seed::D4, // N4: PC8
        daisy::seed::D5, // N5: PD2
        daisy::seed::D6, // N6: PC12
        daisy::seed::D7  // N7: PG10
    };
}

// ============================================================================
// Right Keypad (7 Chord Keys: Physical Keys on the Right, Active-Low w/ Pull-up)
// ============================================================================
namespace chord_keys
{
    enum Key
    {
        C1 = 0, // Top Row, col 1
        C2,     // Top Row, col 2
        C3,     // Top Row, col 3
        C4,     // Top Row, col 4
        C5,     // Bottom Row, col 1
        C6,     // Bottom Row, col 2
        C7,     // Bottom Row, col 3
        COUNT
    };

    constexpr daisy::Pin pins[COUNT] = {
        daisy::seed::D8,  // C1: PG11
        daisy::seed::D9,  // C2: PB4
        daisy::seed::D10, // C3: PB5
        daisy::seed::D13, // C4: PB6
        daisy::seed::D14, // C5: PB7
        daisy::seed::D26, // C6: PD11
        daisy::seed::D27  // C7: PG9
    };
}

// ============================================================================
// Rotary Encoder & Push Switch
// Requires sampling at >= 1 kHz (timer or audio callback) to prevent missed pulses.
// ============================================================================
namespace encoder
{
    constexpr daisy::Pin pin_a     = daisy::seed::D15; // PC0 (Input Pull-up)
    constexpr daisy::Pin pin_b     = daisy::seed::D16; // PA3 (Input Pull-up)
    constexpr daisy::Pin pin_click = daisy::seed::D28; // PA2 (Active-Low Input Pull-up)
}

// ============================================================================
// Board Rail & Auxiliary GPIOs
// ============================================================================
namespace system_pins
{
    constexpr daisy::Pin pin_rail_low = daisy::Pin(daisy::PORTC, 3); // PC3: Driven LOW (0)
    constexpr daisy::Pin pin_aux_in   = daisy::seed::D0;            // PB12: Input Pull-up
}

} // namespace gamma_pins
