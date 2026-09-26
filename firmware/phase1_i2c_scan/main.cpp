#include "daisy_seed.h"
#include <cstdio>

using namespace daisy;

DaisySeed hw;

// Flag to request immediate I2C scan from main loop
static volatile bool g_request_scan = true;

// Callback for USB CDC Serial inputs
void UsbRxCallback(uint8_t* buff, uint32_t* length)
{
    if(!buff || !length)
        return;

    for(uint32_t i = 0; i < *length; i++)
    {
        char c = (char)buff[i];
        if(c == 's' || c == 'S')
        {
            g_request_scan = true;
        }
        else if(c == 'b' || c == 'B')
        {
            hw.PrintLine("\n*** Entering Daisy DFU Bootloader... ***\n");
            System::Delay(200);
            System::ResetToBootloader(System::DAISY_INFINITE_TIMEOUT);
        }
        else if(c == 'h' || c == 'H' || c == '?')
        {
            hw.PrintLine("\n--- Available Commands ---");
            hw.PrintLine("  s : Trigger I2C scan now");
            hw.PrintLine("  b : Reboot into Daisy DFU Bootloader");
            hw.PrintLine("  h : Show this help message");
            hw.PrintLine("--------------------------\n");
        }
    }
}

// Candidate I2C configuration definition
struct BusCandidate
{
    const char*                   name;
    I2CHandle::Config::Peripheral periph;
    Pin                           scl;
    Pin                           sda;
};

static const BusCandidate kCandidates[] = {
    {"I2C1 on D11 (PB8 / SCL) & D12 (PB9 / SDA)",
     I2CHandle::Config::Peripheral::I2C_1,
     seed::D11,
     seed::D12},
    {"I2C1 on D13 (PB6 / SCL) & D14 (PB7 / SDA)",
     I2CHandle::Config::Peripheral::I2C_1,
     seed::D13,
     seed::D14},
    {"I2C4 on D11 (PB8 / SCL) & D12 (PB9 / SDA)",
     I2CHandle::Config::Peripheral::I2C_4,
     seed::D11,
     seed::D12},
    {"I2C4 on D13 (PB6 / SCL) & D14 (PB7 / SDA)",
     I2CHandle::Config::Peripheral::I2C_4,
     seed::D13,
     seed::D14},
};

static void SetPinInternalPullup(Pin pin)
{
    GPIO_TypeDef* port = pin.port == PORTA ? GPIOA
                         : pin.port == PORTB ? GPIOB
                         : pin.port == PORTC ? GPIOC
                         : pin.port == PORTD ? GPIOD
                         : pin.port == PORTE ? GPIOE
                         : pin.port == PORTF ? GPIOF
                         : pin.port == PORTG ? GPIOG
                         : pin.port == PORTH ? GPIOH
                         : pin.port == PORTI ? GPIOI
                         : pin.port == PORTJ ? GPIOJ
                         : pin.port == PORTK ? GPIOK : nullptr;
    if(port)
    {
        uint32_t pin_num = pin.pin;
        port->PUPDR = (port->PUPDR & ~(3U << (pin_num * 2))) | (1U << (pin_num * 2));
    }
}

static void ScanSingleBus(const BusCandidate& bus)
{
    hw.PrintLine("\n========================================================");
    hw.PrintLine("Probing: %s", bus.name);
    hw.PrintLine("========================================================");

    I2CHandle::Config cfg;
    cfg.periph         = bus.periph;
    cfg.speed          = I2CHandle::Config::Speed::I2C_100KHZ;
    cfg.mode           = I2CHandle::Config::Mode::I2C_MASTER;
    cfg.pin_config.scl = bus.scl;
    cfg.pin_config.sda = bus.sda;

    I2CHandle i2c;
    if(i2c.Init(cfg) != I2CHandle::Result::OK)
    {
        hw.PrintLine("  [FAIL] Failed to initialize peripheral.");
        return;
    }

    // Ensure internal pull-ups are enabled if external ones are missing
    SetPinInternalPullup(bus.scl);
    SetPinInternalPullup(bus.sda);

    System::Delay(10);

    int found_count = 0;
    hw.PrintLine("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f");

    for(uint8_t row = 0; row < 128; row += 16)
    {
        char line_buf[128];
        int pos = snprintf(line_buf, sizeof(line_buf), "%02x: ", row);

        for(uint8_t col = 0; col < 16; col++)
        {
            uint8_t addr = row + col;
            if(addr < 0x08 || addr > 0x77)
            {
                pos += snprintf(line_buf + pos, sizeof(line_buf) - pos, "   ");
            }
            else
            {
                // Ping address using a 1-byte transmit (command 0x00 is NOP / Co=0 for SSD1306)
                uint8_t dummy = 0x00;
                auto res = i2c.TransmitBlocking(addr, &dummy, 1, 5);
                if(res == I2CHandle::Result::OK)
                {
                    pos += snprintf(line_buf + pos, sizeof(line_buf) - pos, "%02x ", addr);
                    found_count++;
                }
                else
                {
                    pos += snprintf(line_buf + pos, sizeof(line_buf) - pos, "-- ");
                }
            }
        }
        hw.PrintLine("%s", line_buf);
    }

    if(found_count == 0)
    {
        hw.PrintLine("  Result: No I2C devices detected on this configuration.");
    }
    else
    {
        hw.PrintLine("  Result: %d device(s) detected!", found_count);
    }
}

static void RunAllBusScans()
{
    hw.PrintLine("\n>>> STARTING I2C CANDIDATE BUS SCAN <<<");
    for(size_t i = 0; i < sizeof(kCandidates) / sizeof(kCandidates[0]); i++)
    {
        ScanSingleBus(kCandidates[i]);
        System::Delay(50);
    }
    hw.PrintLine("\n>>> SCAN COMPLETE <<<\n");
}

int main(void)
{
    // Initialize Daisy Seed 2 DFM
    hw.Init();

    // Start USB CDC Serial logging (non-blocking)
    hw.StartLog(false);

    // Register USB CDC receive callback for commands
    hw.usb_handle.SetReceiveCallback(UsbRxCallback, UsbHandle::UsbPeriph::FS_INTERNAL);

    // Initial delay for USB enumeration on host
    System::Delay(1500);

    hw.PrintLine("\n\n========================================================");
    hw.PrintLine("  Gamma Mini Synth Diagnostic Console - Phase 1");
    hw.PrintLine("  Electro-Smith Daisy Seed 2 DFM (STM32H750 + PCM3060)");
    hw.PrintLine("========================================================");
    hw.PrintLine("Send 's' for scan, 'b' for DFU bootloader, 'h' for help.\n");

    uint32_t last_scan_time  = System::GetNow();
    uint32_t last_blink_time = System::GetNow();
    bool     led_state       = false;

    while(1)
    {
        uint32_t now = System::GetNow();

        // Blink LED at 2Hz (toggle every 250ms)
        if(now - last_blink_time >= 250)
        {
            last_blink_time = now;
            led_state       = !led_state;
            hw.SetLed(led_state);
        }

        // Run scan if requested or every 10 seconds
        if(g_request_scan || (now - last_scan_time >= 10000))
        {
            g_request_scan = false;
            last_scan_time = now;
            hw.PrintLine("\n[Uptime: %lu ms]", (unsigned long)now);
            RunAllBusScans();
        }

        System::Delay(10);
    }
}
