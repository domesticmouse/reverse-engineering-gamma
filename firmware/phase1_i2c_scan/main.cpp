#include "daisy_seed.h"
#include "dev/oled_ssd130x.h"
#include "util/oled_fonts.h"
#include <cstdio>

using namespace daisy;

DaisySeed hw;

// The Gamma's USB-C connector is wired to the Seed's *external* USB pins (D29/D30 -> USB_OTG_HS
// in FS mode), not the internal PA11/PA12 port used by DaisySeed::StartLog()/PrintLine().
// Verified on hardware: OTG_FS never receives SOF frames, while the bootloader enumerates fine.
using UsbLog = Logger<LOGGER_EXTERNAL>;

// OLED driver configuration using SSD1306 128x64 on I2C1 (D11/D12)
using MyOled = OledDisplay<SSD130xI2c128x64Driver>;
static MyOled oled;
static bool   g_oled_active        = false;
static int    g_last_device_count  = 0;
static char   g_detected_str[48]   = "Scanning...";

// Flag to request immediate I2C scan from main loop
static volatile bool g_request_scan = false;

// Callback for USB CDC Serial inputs
// USB CDC receive path.
// UsbRxCallback runs inside the USB interrupt. It must NOT print: once a host terminal is
// connected the logger switches to a blocking TransmitSync(), which waits for a TX-complete
// interrupt that cannot fire while we are still inside the USB ISR -> permanent deadlock.
// Received bytes are therefore queued here and handled from the main loop.
static void HandleUsbBytes(uint8_t* buff, uint32_t* length);

static constexpr size_t kRxQueueSize = 64;
static volatile uint8_t g_rx_queue[kRxQueueSize];
static volatile size_t  g_rx_head = 0;
static volatile size_t  g_rx_tail = 0;

void UsbRxCallback(uint8_t* buff, uint32_t* length)
{
    if(!buff || !length)
        return;

    for(uint32_t i = 0; i < *length; i++)
    {
        size_t next = (g_rx_head + 1) % kRxQueueSize;
        if(next == g_rx_tail)
            break; // Queue full: drop remaining bytes
        g_rx_queue[g_rx_head] = buff[i];
        __DSB();
        g_rx_head = next;
    }
}

// Drain queued bytes and dispatch them to HandleUsbBytes() from the main loop
static void ProcessUsbCommands()
{
    uint8_t  local[kRxQueueSize];
    uint32_t n = 0;
    while(g_rx_tail != g_rx_head && n < kRxQueueSize)
    {
        local[n++] = g_rx_queue[g_rx_tail];
        __DSB();
        g_rx_tail = (g_rx_tail + 1) % kRxQueueSize;
    }
    if(n > 0)
        HandleUsbBytes(local, &n);
}

// Command handler (called from the main loop via ProcessUsbCommands, never from the ISR)
static void HandleUsbBytes(uint8_t* buff, uint32_t* length)
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
            UsbLog::PrintLine("\n*** Entering Daisy DFU Bootloader... ***\n");
            System::Delay(200);
            System::ResetToBootloader(System::DAISY_INFINITE_TIMEOUT);
        }
        else if(c == 'h' || c == 'H' || c == '?')
        {
            UsbLog::PrintLine("\n--- Available Commands ---");
            UsbLog::PrintLine("  s : Trigger I2C scan now");
            UsbLog::PrintLine("  b : Reboot into Daisy DFU Bootloader");
            UsbLog::PrintLine("  h : Show this help message");
            UsbLog::PrintLine("--------------------------\n");
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
    {"I2C1 on D11 (PB8 / SCL) & D12 (PB9 / SDA) [Verified Hardware]",
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

static GPIO_TypeDef* GetHalPort(Pin pin)
{
    switch(pin.port)
    {
        case PORTA: return GPIOA;
        case PORTB: return GPIOB;
        case PORTC: return GPIOC;
        case PORTD: return GPIOD;
        case PORTE: return GPIOE;
        case PORTF: return GPIOF;
        case PORTG: return GPIOG;
        case PORTH: return GPIOH;
        case PORTI: return GPIOI;
        case PORTJ: return GPIOJ;
        case PORTK: return GPIOK;
        default: return nullptr;
    }
}

static uint8_t GetI2CAltFn(Pin pin, I2CHandle::Config::Peripheral periph)
{
    if(periph == I2CHandle::Config::Peripheral::I2C_4)
        return GPIO_AF6_I2C4;
    return GPIO_AF4_I2C1;
}

static void ConfigureI2CPins(const BusCandidate& bus)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef gpio_init;
    gpio_init.Mode      = GPIO_MODE_AF_OD;
    gpio_init.Pull      = GPIO_PULLUP;
    gpio_init.Speed     = GPIO_SPEED_FREQ_HIGH;

    // SCL
    gpio_init.Pin       = (1U << bus.scl.pin);
    gpio_init.Alternate = GetI2CAltFn(bus.scl, bus.periph);
    HAL_GPIO_Init(GetHalPort(bus.scl), &gpio_init);

    // SDA
    gpio_init.Pin       = (1U << bus.sda.pin);
    gpio_init.Alternate = GetI2CAltFn(bus.sda, bus.periph);
    HAL_GPIO_Init(GetHalPort(bus.sda), &gpio_init);
}

static void DeinitI2CPins(const BusCandidate& bus)
{
    GPIO_TypeDef* scl_port = GetHalPort(bus.scl);
    GPIO_TypeDef* sda_port = GetHalPort(bus.sda);
    if(scl_port)
        HAL_GPIO_DeInit(scl_port, (1U << bus.scl.pin));
    if(sda_port)
        HAL_GPIO_DeInit(sda_port, (1U << bus.sda.pin));
}

static void ScanSingleBus(const BusCandidate& bus, bool is_primary)
{
    UsbLog::PrintLine("\n========================================================");
    UsbLog::PrintLine("Probing: %s", bus.name);
    UsbLog::PrintLine("========================================================");

    if(bus.periph == I2CHandle::Config::Peripheral::I2C_1)
    {
        __HAL_RCC_I2C1_FORCE_RESET();
        System::Delay(2);
        __HAL_RCC_I2C1_RELEASE_RESET();
    }
    else if(bus.periph == I2CHandle::Config::Peripheral::I2C_4)
    {
        __HAL_RCC_I2C4_FORCE_RESET();
        System::Delay(2);
        __HAL_RCC_I2C4_RELEASE_RESET();
    }

    I2CHandle::Config cfg;
    cfg.periph         = bus.periph;
    cfg.speed          = I2CHandle::Config::Speed::I2C_100KHZ;
    cfg.mode           = I2CHandle::Config::Mode::I2C_MASTER;
    cfg.pin_config.scl = bus.scl;
    cfg.pin_config.sda = bus.sda;

    I2CHandle i2c;
    if(i2c.Init(cfg) != I2CHandle::Result::OK)
    {
        UsbLog::PrintLine("  [FAIL] Failed to initialize peripheral.");
        return;
    }

    ConfigureI2CPins(bus);
    System::Delay(10);

    int  found_count   = 0;
    bool oled_3d_found = false;
    bool oled_3c_found = false;
    char found_list[48] = "";
    int  found_pos     = 0;

    UsbLog::PrintLine("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f");

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
                uint8_t dummy = 0x00;
                auto res = i2c.TransmitBlocking(addr, &dummy, 1, 5);
                if(res == I2CHandle::Result::OK)
                {
                    pos += snprintf(line_buf + pos, sizeof(line_buf) - pos, "%02x ", addr);
                    if(found_pos < (int)sizeof(found_list) - 7)
                    {
                        found_pos += snprintf(found_list + found_pos,
                                              sizeof(found_list) - found_pos,
                                              "0x%02X ",
                                              addr);
                    }
                    found_count++;
                    if(addr == 0x3D)
                        oled_3d_found = true;
                    else if(addr == 0x3C)
                        oled_3c_found = true;
                }
                else
                {
                    pos += snprintf(line_buf + pos, sizeof(line_buf) - pos, "-- ");
                }
            }
        }
        UsbLog::PrintLine("%s", line_buf);
    }

    if(found_count == 0)
    {
        UsbLog::PrintLine("  Result: No I2C devices detected on this configuration.");
    }
    else
    {
        UsbLog::PrintLine("  Result: %d device(s) detected!", found_count);
        if(oled_3d_found)
        {
            UsbLog::PrintLine("  *** [OLED MATCH] SSD1306 OLED at 0x3D (matches factory firmware!) ***");
        }
        if(oled_3c_found)
        {
            UsbLog::PrintLine("  *** [OLED MATCH] Display responded at alternate address 0x3C! ***");
        }
    }

    if(is_primary)
    {
        g_last_device_count = found_count;
        if(found_count > 0)
            snprintf(g_detected_str, sizeof(g_detected_str), "%s", found_list);
        else
            snprintf(g_detected_str, sizeof(g_detected_str), "None");
    }

    DeinitI2CPins(bus);
}

static void RunAllBusScans()
{
    UsbLog::PrintLine("\n>>> STARTING I2C CANDIDATE BUS SCAN <<<");
    for(size_t i = 0; i < sizeof(kCandidates) / sizeof(kCandidates[0]); i++)
    {
        ScanSingleBus(kCandidates[i], i == 0);
        System::Delay(50);
    }
    UsbLog::PrintLine("\n>>> SCAN COMPLETE <<<\n");
}

static void InitOled()
{
    __HAL_RCC_I2C1_FORCE_RESET();
    System::Delay(2);
    __HAL_RCC_I2C1_RELEASE_RESET();

    ConfigureI2CPins(kCandidates[0]);

    MyOled::Config disp_cfg;
    disp_cfg.driver_config.transport_config.i2c_address               = 0x3D;
    disp_cfg.driver_config.transport_config.i2c_config.periph         = I2CHandle::Config::Peripheral::I2C_1;
    disp_cfg.driver_config.transport_config.i2c_config.speed          = I2CHandle::Config::Speed::I2C_400KHZ;
    disp_cfg.driver_config.transport_config.i2c_config.mode           = I2CHandle::Config::Mode::I2C_MASTER;
    disp_cfg.driver_config.transport_config.i2c_config.pin_config.scl = seed::D11;
    disp_cfg.driver_config.transport_config.i2c_config.pin_config.sda = seed::D12;
    oled.Init(disp_cfg);
    g_oled_active = true;
}

static void UpdateOledScreen(uint32_t uptime_sec)
{
    if(!g_oled_active)
        return;

    oled.Fill(false);
    oled.SetCursor(0, 0);
    oled.WriteString("GAMMA I2C SCANNER", Font_7x10, true);

    oled.SetCursor(0, 14);
    oled.WriteString("Bus: I2C1 (D11/D12)", Font_6x8, true);

    oled.SetCursor(0, 26);
    char buf[48];
    snprintf(buf, sizeof(buf), "Devs: %s", g_detected_str);
    oled.WriteString(buf, Font_6x8, true);

    oled.SetCursor(0, 38);
    oled.WriteString("OLED @ 0x3D: ACTIVE", Font_6x8, true);

    oled.SetCursor(0, 52);
    snprintf(buf, sizeof(buf), "Uptime: %lu s", (unsigned long)uptime_sec);
    oled.WriteString(buf, Font_6x8, true);

    oled.Update();
}

int main(void)
{
    // Initialize Daisy Seed 2 DFM core
    hw.Init();

    // Start USB CDC Serial logging (non-blocking)
    UsbLog::StartLog(false);

    // Register USB CDC receive callback for commands
    hw.usb_handle.SetReceiveCallback(UsbRxCallback, UsbHandle::UsbPeriph::FS_EXTERNAL);

    UsbLog::PrintLine("\n\n========================================================");
    UsbLog::PrintLine("  Gamma Mini Synth Diagnostic Console - Phase 1");
    UsbLog::PrintLine("  Electro-Smith Daisy Seed 2 DFM (STM32H750 + PCM3060)");
    UsbLog::PrintLine("========================================================");
    UsbLog::PrintLine("Send 's' for scan, 'b' for DFU bootloader, 'h' for help.\n");

    uint32_t last_scan_time   = 0;
    uint32_t last_blink_time  = System::GetNow();
    uint32_t last_screen_time = 0;
    bool     led_state        = false;
    bool     first_run        = true;

    while(1)
    {
        uint32_t now = System::GetNow();

        // Handle USB CDC commands queued by UsbRxCallback (safe to print here)
        ProcessUsbCommands();

        // Heartbeat LED always blinks at 2Hz (toggle every 250ms)
        if(now - last_blink_time >= 250)
        {
            last_blink_time = now;
            led_state       = !led_state;
            hw.SetLed(led_state);
        }

        // Run bus scan and init OLED on first loop iteration or on demand
        if(first_run || g_request_scan || (now - last_scan_time >= 30000))
        {
            first_run      = false;
            g_request_scan = false;
            last_scan_time = now;

            UsbLog::PrintLine("\n[Uptime: %lu ms]", (unsigned long)now);
            RunAllBusScans();

            // Initialize OLED now that primary bus is probed
            InitOled();
            UpdateOledScreen(now / 1000);
            last_screen_time = now;
        }

        // Refresh OLED screen every 500ms
        if(g_oled_active && (now - last_screen_time >= 500))
        {
            last_screen_time = now;
            UpdateOledScreen(now / 1000);
        }

        System::Delay(10);
    }
}
