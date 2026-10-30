/**
 * System settings (SC) on the host (port/docs/HOST_BUILD.md §22).
 *
 * On the Wii, SCInit reads /shared2/sys/SYSCONF from the NAND and the SCGet*
 * functions look items up in it (src/revolution/SC). The host has no SYSCONF:
 * the values come from a small text file (PortSCLoadConfig), one setting per
 * line, `key = value`, `#` starts a comment:
 *
 *   language    = en        jp en de fr es it nl zh-s zh-t kr, or 0-9
 *   aspect      = 4:3       4:3 or 16:9
 *   progressive = 0         0 interlaced, 1 progressive (480p)
 *   eurgb60     = 1         0 50 Hz, 1 60 Hz (PAL only)
 *   sound       = stereo    mono stereo surround
 *   sensorbar   = bottom    bottom top
 *   sensitivity = 3         1-5 (pointer sensitivity, SCGetBtDpdSensibility)
 *   rumble      = 1         0 off, 1 on
 *   speaker     = 85        Wii Remote speaker volume, 0-127
 *   screensaver = 1         0 off, 1 on
 *   offset      = 0         horizontal display offset, -32..32
 *
 * The defaults are a US console: English, 4:3, interlaced, stereo, sensor bar
 * below the screen, sensitivity 3, rumble on, speaker 85, screen saver on.
 * (4:3 because no aspect-ratio choice is recorded in the port's docs; 16:9 is
 * one line in the config.)
 */

#include <revolution/SC.h>

#include <port/host.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

struct Settings {
    u8 language;
    u8 aspect;
    u8 progressive;
    u8 euRgb60;
    u8 sound;
    u8 sensorBar;
    u32 sensitivity;
    u8 motor;
    u8 speakerVolume;
    u8 screenSaver;
    s8 displayOffsetH;
};

const Settings kDefaults = {
    SC_LANG_EN, SC_ASPECT_STD, SC_INTERLACED, SC_EURGB_60_HZ, SC_SND_STEREO,
    SC_SENSOR_BAR_BOTTOM, 3, SC_MOTOR_ON, 85, 1, 0,
};

Settings sSettings = kDefaults;

const char* const kLanguages[] = {"jp", "en", "de", "fr", "es", "it", "nl", "zh-s", "zh-t", "kr"};

char* Trim(char* s) {
    while (std::isspace(static_cast<unsigned char>(*s))) {
        s++;
    }
    char* e = s + std::strlen(s);
    while (e > s && std::isspace(static_cast<unsigned char>(e[-1]))) {
        *--e = '\0';
    }
    return s;
}

bool ParseInt(const char* v, long lo, long hi, long* out) {
    char* end;
    long n = std::strtol(v, &end, 10);
    if (*v == '\0' || *end != '\0' || n < lo || n > hi) {
        return false;
    }
    *out = n;
    return true;
}

bool Set(const char* key, const char* v) {
    long n;
    if (std::strcmp(key, "language") == 0) {
        for (u8 i = 0; i < sizeof(kLanguages) / sizeof(kLanguages[0]); i++) {
            if (std::strcmp(v, kLanguages[i]) == 0) {
                sSettings.language = i;
                return true;
            }
        }
        if (!ParseInt(v, 0, 9, &n)) {
            return false;
        }
        sSettings.language = static_cast<u8>(n);
    } else if (std::strcmp(key, "aspect") == 0) {
        if (std::strcmp(v, "4:3") == 0) {
            sSettings.aspect = SC_ASPECT_STD;
        } else if (std::strcmp(v, "16:9") == 0) {
            sSettings.aspect = SC_ASPECT_WIDE;
        } else {
            return false;
        }
    } else if (std::strcmp(key, "progressive") == 0) {
        if (!ParseInt(v, 0, 1, &n)) {
            return false;
        }
        sSettings.progressive = static_cast<u8>(n);
    } else if (std::strcmp(key, "eurgb60") == 0) {
        if (!ParseInt(v, 0, 1, &n)) {
            return false;
        }
        sSettings.euRgb60 = static_cast<u8>(n);
    } else if (std::strcmp(key, "sound") == 0) {
        if (std::strcmp(v, "mono") == 0) {
            sSettings.sound = SC_SND_MONO;
        } else if (std::strcmp(v, "stereo") == 0) {
            sSettings.sound = SC_SND_STEREO;
        } else if (std::strcmp(v, "surround") == 0) {
            sSettings.sound = SC_SND_SURROUND;
        } else {
            return false;
        }
    } else if (std::strcmp(key, "sensorbar") == 0) {
        if (std::strcmp(v, "bottom") == 0) {
            sSettings.sensorBar = SC_SENSOR_BAR_BOTTOM;
        } else if (std::strcmp(v, "top") == 0) {
            sSettings.sensorBar = SC_SENSOR_BAR_TOP;
        } else {
            return false;
        }
    } else if (std::strcmp(key, "sensitivity") == 0) {
        if (!ParseInt(v, 1, 5, &n)) {
            return false;
        }
        sSettings.sensitivity = static_cast<u32>(n);
    } else if (std::strcmp(key, "rumble") == 0) {
        if (!ParseInt(v, 0, 1, &n)) {
            return false;
        }
        sSettings.motor = static_cast<u8>(n);
    } else if (std::strcmp(key, "speaker") == 0) {
        if (!ParseInt(v, 0, 127, &n)) {
            return false;
        }
        sSettings.speakerVolume = static_cast<u8>(n);
    } else if (std::strcmp(key, "screensaver") == 0) {
        if (!ParseInt(v, 0, 1, &n)) {
            return false;
        }
        sSettings.screenSaver = static_cast<u8>(n);
    } else if (std::strcmp(key, "offset") == 0) {
        if (!ParseInt(v, -32, 32, &n)) {
            return false;
        }
        sSettings.displayOffsetH = static_cast<s8>(n);
    } else {
        return false;
    }
    return true;
}

} // namespace

extern "C" {

/* Returns 0 on success, -1 if the file cannot be read, or the number of the
 * first bad line (the settings before it are kept, the rest are defaults). */
int PortSCLoadConfig(const char* path) {
    sSettings = kDefaults;
    if (path == NULL) {
        return 0;
    }
    FILE* f = std::fopen(path, "r");
    if (f == NULL) {
        return -1;
    }
    char line[256];
    int number = 0;
    int bad = 0;
    while (std::fgets(line, sizeof(line), f) != NULL) {
        number++;
        if (char* hash = std::strchr(line, '#')) {
            *hash = '\0';
        }
        char* s = Trim(line);
        if (*s == '\0') {
            continue;
        }
        char* eq = std::strchr(s, '=');
        if (eq == NULL) {
            bad = number;
            break;
        }
        *eq = '\0';
        char* key = Trim(s);
        char* value = Trim(eq + 1);
        for (char* p = value; *p; p++) {
            *p = static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
        }
        if (!Set(key, value)) {
            std::fprintf(stderr, "host_sc: %s:%d: bad setting '%s = %s'\n", path, number, key,
                         value);
            bad = number;
            break;
        }
    }
    std::fclose(f);
    return bad;
}

void SCInit(void) {}

u32 SCCheckStatus(void) {
    return SC_STATUS_OK;
}

u8 SCGetLanguage(void) {
    return sSettings.language;
}
u8 SCGetAspectRatio(void) {
    return sSettings.aspect;
}
u8 SCGetProgressiveMode(void) {
    return sSettings.progressive;
}
u8 SCGetEuRgb60Mode(void) {
    return sSettings.euRgb60;
}
u8 SCGetSoundMode(void) {
    return sSettings.sound;
}
u8 SCGetWpadSensorBarPosition(void) {
    return sSettings.sensorBar;
}
u32 SCGetBtDpdSensibility(void) {
    return sSettings.sensitivity;
}
u8 SCGetWpadMotorMode(void) {
    return sSettings.motor;
}
BOOL SCSetWpadMotorMode(u8 mode) {
    sSettings.motor = mode;
    return TRUE;
}
u8 SCGetWpadSpeakerVolume(void) {
    return sSettings.speakerVolume;
}
BOOL SCSetWpadSpeakerVolume(u8 vol) {
    sSettings.speakerVolume = vol;
    return TRUE;
}
u8 SCGetScreenSaverMode(void) {
    return sSettings.screenSaver;
}
s8 SCGetDisplayOffsetH(void) {
    return sSettings.displayOffsetH;
}
void SCGetIdleMode(SCIdleModeInfo* mode) {
    mode->wc24 = 0;
    mode->slotLight = 0;
}
u32 SCGetCounterBias(void) {
    return 0;
}

/* Settings changes are kept in memory only (the host writes no SYSCONF). */
void SCFlushAsync(SCFlushCallback callback) {
    if (callback != NULL) {
        callback(SC_STATUS_OK);
    }
}

} // extern "C"
