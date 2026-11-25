/*
 * Which settings travel between a player's devices, for every sync: iCloud
 * (port/ios/ios_icloud.mm) and the cross-platform sync (port/pc/gevr_sync_core.cpp,
 * docs/cloud-sync.md). C++, header only (the iCloud test includes ios_icloud.mm
 * whole).
 *
 *  - shared: the player's (controls, look speed, gameplay, cheats, gun fits...)
 *  - kind: one kind of device's (its display, frame rate, touch screen, room
 *    scale): synced between devices of the same kind only
 *  - device: this device's alone, never synced: what belongs to the room it
 *    is in (which pad is which player, the split-screen guest seats) and to its
 *    hardware (audio output, window), and the sync switches themselves
 */
#ifndef GEVR_SYNC_KEYS_H
#define GEVR_SYNC_KEYS_H

#include <cstring>
#include <string>

namespace gevrsync {

enum KeyScope { KEY_SHARED, KEY_KIND, KEY_DEVICE };

inline bool keyHasPrefix(const std::string &k, const char *p)
{
    return k.compare(0, strlen(p), p) == 0;
}

inline bool keyHasSuffix(const std::string &k, const char *s)
{
    const size_t n = strlen(s);
    return k.size() >= n && k.compare(k.size() - n, n, s) == 0;
}

/* goldeneye-vr.ini */
inline KeyScope iniKeyScope(const std::string &k)
{
    static const char *const device[] = {
        "ICloudSync", "ICloudPacks",   // each device chooses
    };
    for (const char *d : device) {
        if (k == d) return KEY_DEVICE;
    }
    // the split-screen guest seats (Guest2Name, Guest3Chr, Guest4Loadout1...):
    // the people on that couch; two Apple TVs in two rooms keep their own
    if (keyHasPrefix(k, "Guest")) return KEY_DEVICE;
    if (keyHasPrefix(k, "CloudSync")) return KEY_DEVICE;   // the cross-platform sync's rows
    static const char *const kind[] = {
        "FrameRate", "DisplayHz", "SpaceWarp", "EyeMsaa", "CpuLevel", "ShowStats",
        "ScreenDistance", "ScreenFov", "ScreenCurved", "ScreenPassthrough", "ScreenShape", "ScreenHeight",
        "TouchControls", "TouchLayout", "TouchHaptics", "PlayMode", "SeatedMode", "PlayerHeight",
        "MatchCharacterHeight", "WorldScale", "HudDistance", "StereoCrosshair", "PadDefaults",
        "LookUprightFolders", "MicMuted", "LiveSplit", "LiveSplitHost",
        "MasterVolume",   // a phone's speaker and a living room's TV want their own
    };
    for (const char *d : kind) {
        if (k == d) return KEY_KIND;
    }
    return KEY_SHARED;
}

/* goldeneye.ini ("Section.key") */
inline KeyScope cfgKeyScope(const std::string &k)
{
    // which pad is which player: the pads in this room
    if (keyHasPrefix(k, "Input.Player") && keyHasSuffix(k, ".ControllerIndex")) return KEY_DEVICE;
    // its audio output and its window
    if (keyHasPrefix(k, "Audio.") || keyHasPrefix(k, "Video.Default") || k == "Video.CenterWindow" ||
        k == "Video.ExclusiveFullscreen") {
        return KEY_DEVICE;
    }
    // the kind's display and input backends
    static const char *const kind[] = {
        "Video.", "Input.Use", "Input.FakeGamepads", "Input.FirstGamepadNum",
        "Input.Mouse", "Game.MemorySize", "Game.ExtraSleep",
    };
    for (const char *p : kind) {
        if (keyHasPrefix(k, p)) return KEY_KIND;
    }
    return KEY_SHARED;
}

}   // namespace gevrsync

#endif
