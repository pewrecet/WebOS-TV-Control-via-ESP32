#pragma once

// ============================================================================
// User configuration
// ============================================================================

// --- Wi-Fi ---
#define WIFI_SSID       "AltForceMini"
#define WIFI_PASSWORD   "V6788007a"

// --- TV ---
// Reserve this IP by MAC in the router so it does not change.
#define TV_IP           "192.168.1.85"
#define TV_PORT         3001        // 3000 = ws, 3001 = wss
#define TV_USE_TLS      true

// If empty, the TV will ask for pairing and the key will be saved to NVS.
#define TV_CLIENT_KEY   ""

// --- App ---
#define APP_ID_KION         "com.huawei.app.webos.mts.tv.production"
#define APP_ID_KION_ALT     "com.mts.kion"

// Temporary debug mode: print app list after registration instead of launching KION.
#define DEBUG_LIST_APPS_ON_READY 0
#define DEBUG_LIST_LAUNCH_POINTS_ON_READY 0
#define DEBUG_WATCH_FOREGROUND_APP_ON_READY 0

// --- TV power detection ---
// Some LG models answer ICMP ping even in standby, so TCP probe is the safer default.
// Keep detection on 3000 even when the actual control channel uses secure 3001.
#define PING_INTERVAL_MS        3000
#define PING_TIMEOUT_MS         1000
#define PING_ATTEMPTS           2
#define DETECT_METHOD_PING      0
#define DETECT_TCP_PORT         3000

// Delay between OFF -> ON detection and SSAP connection.
#define TV_BOOT_DELAY_MS        8000

// --- Remote sequence after launching KION ---
struct ButtonStep {
    const char* button;
    uint32_t    delayAfterMs;
};

static const ButtonStep REMOTE_SEQUENCE[] = {
    {nullptr, 45000},
    {"RIGHT", 10000},
    {"ENTER", 20000},
    {"DOWN", 15000},
    {"DOWN", 15000},
    {"DOWN", 15000},
    {"ENTER", 0},
};
static const size_t REMOTE_SEQUENCE_LEN = sizeof(REMOTE_SEQUENCE) / sizeof(REMOTE_SEQUENCE[0]);

// Delay between app launch and the first remote action.
#define APP_LAUNCH_DELAY_MS     5000

// --- Misc ---
#define SERIAL_BAUD             115200
#define HOSTNAME                "lg-kion-auto"
