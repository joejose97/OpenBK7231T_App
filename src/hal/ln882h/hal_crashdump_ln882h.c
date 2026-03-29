#ifdef PLATFORM_LN882H

#include "serial.h"
#include "../../new_common.h"
#include "../../cmnds/cmd_public.h"
#include "../../logging/logging.h"
#include <stdarg.h>
#include <stdio.h>

// Allocate 900 bytes out of the 1016 available bytes in the RETENTION region.
// This survives soft-reset, deep-sleep, and watchdog resets!
__attribute__((section(".no_init_data"))) char g_crash_buffer[900];
__attribute__((section(".no_init_data"))) int g_crash_cursor;
__attribute__((section(".no_init_data"))) uint32_t g_crash_magic;

#define CRASH_MAGIC 0x1337BEEF

void ln882h_crash_print(const char *fmt, ...) {
    // If the magic doesn't match, it means this is a cold boot or uninitialized RAM
    if (g_crash_magic != CRASH_MAGIC) {
        g_crash_cursor = 0;
        g_crash_magic = CRASH_MAGIC;
    }

    if (g_crash_cursor >= sizeof(g_crash_buffer) - 50) return; // Prevent overflow

    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(&g_crash_buffer[g_crash_cursor], sizeof(g_crash_buffer) - g_crash_cursor, fmt, args);
    if (len > 0) {
        g_crash_cursor += len;
    }
    va_end(args);
}

static commandResult_t CMD_GetCrashTrace(const void *context, const char *cmd, const char *args, int cmdFlags) {
    if (g_crash_magic == CRASH_MAGIC && g_crash_cursor > 0) {
        // Ensure null termination safely before printing
        g_crash_buffer[g_crash_cursor < sizeof(g_crash_buffer) ? g_crash_cursor : sizeof(g_crash_buffer) - 1] = '\0';
        
        ADDLOG_INFO(LOG_FEATURE_CMD, "Crash Dump Found (%d bytes):\n%s", g_crash_cursor, g_crash_buffer);
        
        // If the user specifies "clear" as an argument, erase the magic word
        if (args && *args == 'c') {
            g_crash_magic = 0;
            g_crash_cursor = 0;
            ADDLOG_INFO(LOG_FEATURE_CMD, "Crash Dump cleared from Retention RAM.", g_crash_cursor, g_crash_buffer);
        }
    } else {
        ADDLOG_INFO(LOG_FEATURE_CMD, "No crash log found in RETENTION RAM.");
    }
    
    return CMD_RES_OK;
}

static commandResult_t CMD_ForceCrash(const void *context, const char *cmd, const char *args, int cmdFlags) {
    ADDLOG_INFO(LOG_FEATURE_CMD, "Force crashing MCU to test CmBacktrace...");
    int *p = (int*)0;
    while(1) {
        *p = 0xDEADBEEF;
        p++;
    }
    return CMD_RES_OK;
}

// -------------------------------------------------------------------------------------------------
// SDK INTERCEPTION (Bypasses the need to modify the OpenLN882H submodule logic)
// -------------------------------------------------------------------------------------------------

extern int __real_log_stdio_write(char *buf, size_t size);

int __wrap_log_stdio_write(char *buf, size_t size) {
    if (buf && size > 0) {
        uint32_t ipsr;
        __asm volatile ("MRS %0, ipsr" : "=r" (ipsr) );
        
        // IPSR holds the exception number. HardFault = 3, MemManage = 4, BusFault = 5, UsageFault = 6
        if (ipsr >= 3 && ipsr <= 6) {
            char tmp[128];
            int copy_len = size < 127 ? size : 127;
            memcpy(tmp, buf, copy_len);
            tmp[copy_len] = '\0';
            ln882h_crash_print("%s", tmp);
        }
    }
    // Always forward to the real hardware UART handler
    return __real_log_stdio_write(buf, size);
}

void HAL_CrashDump_Init() {
  // Override default LN882H UART logging port to UART0 at high baud rate for
  // direct serial logging
//   #include "serial.h"
  extern Serial_t m_LogSerial;
  serial_deinit(&m_LogSerial);
  serial_init(&m_LogSerial, SER_PORT_UART0, 921600, NULL);
  // Re-verify magic on boot. If it's garbage (e.g. cold power loss), reset the
  // cursor early to prevent memory anomalies if someone calls get_crash before
  // a crash happens.
  if (g_crash_magic != CRASH_MAGIC) {
    g_crash_cursor = 0;
  }

  extern void cm_backtrace_init(const char *firmware_name,
                                const char *hardware_ver,
                                const char *software_ver);
  cm_backtrace_init("OpenBeken_LN882H", "1.0", "1.0");

  CMD_RegisterCommand("get_crash", CMD_GetCrashTrace, NULL);
  CMD_RegisterCommand("force_crash", CMD_ForceCrash, NULL);
}

#endif // PLATFORM_LN882H
