#pragma once

#include "daisy_seed.h"
#include <cstdio>
#include <cstdarg>

// NOTE: This namespace must not be called `gamma`: newlib's <math.h> (pulled in
// by daisy_seed.h) declares a global `gamma()` function, and the clash is a hard
// compile error ("'namespace gamma' redeclared as different kind of entity").
namespace gamma_usb
{

// Static storage for UsbComm. Templated so the static members can be defined
// in this header without C++17 inline variables (libDaisy builds with
// -std=gnu++14) and without ODR violations if included from several .cpp files.
template <typename Dummy = void>
struct UsbCommState
{
    using CommandHandler = void (*)(char c);
    using BootloaderHook = void (*)();

    static constexpr size_t kNumTxBufs   = 4;
    static constexpr size_t kTxBufSize   = 256;
    static constexpr size_t kRxQueueSize = 128;

    static daisy::DaisySeed* s_hw;
    static CommandHandler    s_cmd_handler;
    static BootloaderHook    s_bootloader_hook;

    static char     s_tx_bufs[kNumTxBufs][kTxBufSize];
    static size_t   s_cur_tx_buf;
    static uint32_t s_last_timeout_ms;

    static volatile char   s_rx_queue[kRxQueueSize];
    static volatile size_t s_rx_head;
    static volatile size_t s_rx_tail;
};

template <typename D>
daisy::DaisySeed* UsbCommState<D>::s_hw = nullptr;
template <typename D>
typename UsbCommState<D>::CommandHandler UsbCommState<D>::s_cmd_handler = nullptr;
template <typename D>
typename UsbCommState<D>::BootloaderHook UsbCommState<D>::s_bootloader_hook = nullptr;
template <typename D>
char UsbCommState<D>::s_tx_bufs[kNumTxBufs][kTxBufSize] = {};
template <typename D>
size_t UsbCommState<D>::s_cur_tx_buf = 0;
template <typename D>
uint32_t UsbCommState<D>::s_last_timeout_ms = 0;
template <typename D>
volatile char UsbCommState<D>::s_rx_queue[kRxQueueSize] = {};
template <typename D>
volatile size_t UsbCommState<D>::s_rx_head = 0;
template <typename D>
volatile size_t UsbCommState<D>::s_rx_tail = 0;

// ========================================================================
// Non-blocking, Deadlock-Immune USB CDC Communication Driver
// ========================================================================
// On Daisy Seed 2 DFM (STM32H750), the default libDaisy Logger<LOGGER_EXTERNAL>
// uses TransmitSync(), which enters an infinite spinloop if the host closes the
// port or stops polling the USB IN endpoint.
//
// GammaUsb provides:
// 1. A 4-buffer non-blocking pool with short timeout (500 us) & backoff bypass.
// 2. An interrupt-safe lock-free RX ring buffer.
// 3. Command dispatcher for interactive shell / control commands.
// 4. Safe DFU bootloader reboot trigger.
// ========================================================================
class UsbComm : private UsbCommState<>
{
public:
    using UsbCommState<>::CommandHandler;
    using UsbCommState<>::BootloaderHook;

    static void Init(daisy::DaisySeed& hw, CommandHandler cmd_handler = nullptr)
    {
        s_hw          = &hw;
        s_cmd_handler = cmd_handler;

        // Initialize USB Full-Speed External peripheral (USB CDC Virtual COM Port)
        s_hw->usb_handle.Init(daisy::UsbHandle::FS_EXTERNAL);
        s_hw->usb_handle.SetReceiveCallback(RxCallbackInternal, daisy::UsbHandle::FS_EXTERNAL);
    }

    // Non-blocking printf
    static void Print(const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        LogInternal(false, format, args);
        va_end(args);
    }

    // Non-blocking println
    static void PrintLine(const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        LogInternal(true, format, args);
        va_end(args);
    }

    // Must be called in the main while(1) loop to process queued USB commands
    static void Process()
    {
        while(s_rx_tail != s_rx_head)
        {
            char c = s_rx_queue[s_rx_tail];
            __DSB();
            s_rx_tail = (s_rx_tail + 1) % kRxQueueSize;

            if(c == 'b' || c == 'B')
            {
                RebootToBootloader();
            }
            else if(s_cmd_handler)
            {
                s_cmd_handler(c);
            }
        }
    }

    // Reboot into the Daisy bootloader so new firmware can be flashed to QSPI
    // (0x90040000). Runs the optional pre-reboot hook first (e.g. mute the
    // speaker amp, stop audio, draw a "DFU MODE" screen on the OLED).
    // Call from main-loop context only (never from an ISR).
    //
    // NOTE: System::ResetToBootloader() with no argument defaults to
    // BootloaderMode::STM (the STM32 ROM bootloader). That also enumerates as
    // 0483:df11 but can only program internal flash, so flashing a BOOT_SRAM
    // app to 0x90040000 fails. Always pass a DAISY_* mode on the Gamma.
    // DAISY_INFINITE_TIMEOUT keeps the bootloader waiting until a flash arrives
    // instead of booting the old app after the ~2.5 s window.
    static void RebootToBootloader()
    {
        PrintLine("\n*** Rebooting into Daisy DFU Bootloader... ***\n");
        if(s_bootloader_hook)
            s_bootloader_hook();
        daisy::System::Delay(100);
        daisy::System::ResetToBootloader(daisy::System::DAISY_INFINITE_TIMEOUT);
    }

    // Set or replace command handler
    static void SetCommandHandler(CommandHandler handler)
    {
        s_cmd_handler = handler;
    }

    // Optional callback run (in main-loop context) just before rebooting into the bootloader
    static void SetBootloaderHook(BootloaderHook hook)
    {
        s_bootloader_hook = hook;
    }

private:
    // Interrupt service callback executed in USB ISR context
    // CRITICAL: Do NOT print or call TransmitExternal from within this ISR!
    static void RxCallbackInternal(uint8_t* buff, uint32_t* length)
    {
        if(!buff || !length)
            return;

        for(uint32_t i = 0; i < *length; i++)
        {
            size_t next = (s_rx_head + 1) % kRxQueueSize;
            if(next == s_rx_tail)
                break; // Buffer overrun: safely drop overflow bytes
            s_rx_queue[s_rx_head] = static_cast<char>(buff[i]);
            __DSB();
            s_rx_head = next;
        }
    }

    static void LogInternal(bool newline, const char* format, va_list args)
    {
        if(!s_hw)
            return;

        char* buf = s_tx_bufs[s_cur_tx_buf];
        int   len = vsnprintf(buf, kTxBufSize - 3, format, args);
        if(len <= 0)
            return;
        if(len > static_cast<int>(kTxBufSize - 3))
            len = kTxBufSize - 3;

        if(newline)
        {
            buf[len++] = '\r';
            buf[len++] = '\n';
            buf[len]   = '\0';
        }

        uint32_t now_ms = daisy::System::GetNow();
        // Fast-fail bypass if recent transmission timed out within 200 ms
        if(s_last_timeout_ms > 0 && (now_ms - s_last_timeout_ms < 200))
        {
            if(s_hw->usb_handle.TransmitExternal((uint8_t*)buf, len) == daisy::UsbHandle::Result::OK)
            {
                s_last_timeout_ms = 0;
                s_cur_tx_buf      = (s_cur_tx_buf + 1) % kNumTxBufs;
            }
            return;
        }

        // Host connected or backoff expired: allow up to 500 us for transmission
        uint32_t start_us = daisy::System::GetUs();
        while(s_hw->usb_handle.TransmitExternal((uint8_t*)buf, len) != daisy::UsbHandle::Result::OK)
        {
            if(daisy::System::GetUs() - start_us >= 500)
            {
                s_last_timeout_ms = daisy::System::GetNow();
                return; // Drop packet to prevent starvation of audio or UI
            }
        }

        s_last_timeout_ms = 0;
        s_cur_tx_buf      = (s_cur_tx_buf + 1) % kNumTxBufs;
    }
};

} // namespace gamma_usb
