/*
 * Launcher > Play > Sync: Google Drive sync (port/pc/gevr_sync_drive.cpp,
 * docs/cloud-sync.md). The player's own Drive carries settings, saves, ROMs
 * and (opted in) texture packs between every device; the sign-in is Google's
 * device code, for headsets and TVs alike. On an Apple build signed for
 * iCloud (port/ios/ios_icloud.mm) the page first asks which sync this device
 * uses: Off, iCloud or Google Drive, one at a time (owner, 2026-11-26: two
 * syncs on the same files would each take the other's changes for this
 * device's own). Header only: the launcher's frame test draws it.
 */
#pragma once

#include <cstdio>
#include <cstring>

#include "imgui/imgui.h"
/* the game's PR/os.h defines BLOCKSIZE, an ImGui template parameter (as gevr_friends_ui.h) */
#pragma push_macro("BLOCKSIZE")
#undef BLOCKSIZE
#include "imgui/imgui_internal.h"
#pragma pop_macro("BLOCKSIZE")
#include "gevr_sync.h"
#include "gevr_tooltip.h"

/* google.com/device as a QR code (version 3, 29 modules a side, made once with
 * segno: the address Google's device sign-in always gives). A phone's camera
 * opens it; the code is still typed. */
static const unsigned kGevrDeviceQr[29] = {
    0x1fc1177f, 0x10539441, 0x175ca65d, 0x1747ef5d, 0x17487a5d,
    0x104d9b41, 0x1fd5557f, 0x0006de00, 0x0ec2de06, 0x199c9fd9,
    0x1354154e, 0x01892139, 0x184249a7, 0x162f4a6d, 0x02e0b7bb,
    0x118d335a, 0x0e7a18bb, 0x01bad124, 0x125c7964, 0x07114cde,
    0x0ee1dffe, 0x0016171b, 0x1fc01b56, 0x1055a112, 0x1747c5fc,
    0x17559216, 0x1752bd25, 0x1058111a, 0x1fc06722,
};

inline void gevrSyncQr(float module)
{
    const int n = 29, quiet = 3;
    const float side = (n + 2 * quiet) * module;
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(at, ImVec2(at.x + side, at.y + side), IM_COL32(255, 255, 255, 255));
    for (int y = 0; y < n; y++) {
        for (int x = 0; x < n; x++) {
            if (!(kGevrDeviceQr[y] >> (n - 1 - x) & 1)) continue;
            const ImVec2 a(at.x + (quiet + x) * module, at.y + (quiet + y) * module);
            dl->AddRectFilled(a, ImVec2(a.x + module, a.y + module), IM_COL32(0, 0, 0, 255));
        }
    }
    ImGui::Dummy(ImVec2(side, side));
}

/* "eeprom.bin.before-sync-20261125-201400" -> "eeprom.bin, set aside 2026-11-25 20:14" */
inline void gevrSyncAsideLabel(const char *name, char *out, size_t size)
{
    const char *at = strstr(name, ".before-sync-");
    if (!at || strlen(at) < 13 + 15) {
        snprintf(out, size, "%s", name);
        return;
    }
    const char *d = at + 13;   // YYYYmmdd-HHMMSS
    snprintf(out, size, "%.*s, set aside %.4s-%.2s-%.2s %.2s:%.2s", (int)(at - name), name, d, d + 4, d + 6, d + 9, d + 11);
}

/* iCloud's switches, for a build that can use it (the launcher's ini values) */
struct GevrSyncICloud {
    int *sync;              // VrICloudSync
    int *packs;             // VrICloudPacks
    void (*changed)(void);  // tell iCloud and save goldeneye-vr.ini
    const char *status;     // iCloud's line ("iCloud: up to date"), may be ""
};

inline void gevrSyncPage(const ImVec4 &gold, const ImVec4 &bad, const GevrSyncICloud *icloud = nullptr)
{
    auto note = [](const char *text) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", text);
        ImGui::PopStyleColor();
    };
    GevrSyncDriveInfo drive;
    const int ds = gevrSyncDrive(&drive);
    if (icloud) {
        // which sync this device uses: one at a time
        enum { USE_OFF, USE_ICLOUD, USE_DRIVE };
        static bool s_wantDrive = false;   // Google Drive chosen, not linked yet
        const bool driveOn = ds == GEVR_SYNC_DRIVE_STARTING || ds == GEVR_SYNC_DRIVE_WAITING || ds == GEVR_SYNC_DRIVE_LINKED;
        if (driveOn && *icloud->sync) {   // linked some other way: Drive is the one
            *icloud->sync = 0;
            icloud->changed();
        }
        int use = driveOn || s_wantDrive ? USE_DRIVE : *icloud->sync ? USE_ICLOUD : USE_OFF;
        static int s_pending = -1;   // a choice that unlinks Google Drive, once confirmed
        ImGui::TextColored(gold, "SYNC");
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 3.0f);
        int pick = use;
        ImGui::RadioButton("Off", &pick, USE_OFF);
        ImGui::SameLine();
        ImGui::RadioButton("iCloud", &pick, USE_ICLOUD);
        if (ImGui::IsItemHovered()) gevrTooltip("Your iPhone, iPad, Apple TV and Vision Pro, through your iCloud.");
        if (ds != GEVR_SYNC_DRIVE_NONE) {
            ImGui::SameLine();
            ImGui::RadioButton("Google Drive", &pick, USE_DRIVE);
            if (ImGui::IsItemHovered())
                gevrTooltip("Every device: Quest, PC, Steam Frame and Apple, through your own Google Drive.");
        }
        if (pick != use) {
            if (use == USE_DRIVE && ds == GEVR_SYNC_DRIVE_LINKED) {
                s_pending = pick;   // asks first: it unlinks this device
                ImGui::OpenPopup("Stop using Google Drive?");
            } else {
                if (use == USE_DRIVE && driveOn) gevrSyncDriveCancel();
                s_wantDrive = pick == USE_DRIVE;
                *icloud->sync = pick == USE_ICLOUD;
                icloud->changed();
                use = pick;
            }
        }
        if (ImGui::BeginPopupModal("Stop using Google Drive?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(s_pending == USE_ICLOUD ? "This device unlinks Google Drive and syncs with iCloud."
                                                           : "This device unlinks Google Drive and stops syncing.");
            ImGui::TextDisabled("Its settings and saves stay here; your other devices and Drive keep theirs.");
            if (ImGui::Button("Cancel", ImVec2(220, 52))) ImGui::CloseCurrentPopup();
            ImGui::SameLine();
            if (ImGui::Button(s_pending == USE_ICLOUD ? "Use iCloud" : "Stop syncing", ImVec2(220, 52))) {
                gevrSyncDriveUnlink();
                s_wantDrive = false;
                *icloud->sync = s_pending == USE_ICLOUD;
                icloud->changed();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        // while choosing (Drive linked, the page is full: the rule is in the radios' tooltips)
        if (ds != GEVR_SYNC_DRIVE_LINKED) {
            note("Devices sync with each other when they use the same one. With a Quest or a PC too, use Google "
                 "Drive on every device.");
            ImGui::Spacing();
        }
        if (use == USE_OFF) {
            note("Nothing leaves this device.");
            return;
        }
        if (use == USE_ICLOUD) {
            ImGui::TextWrapped("Your settings, saves and ROMs follow you between your Apple devices, from the next "
                               "start.");
            bool packs = *icloud->packs != 0;
            if (ImGui::Checkbox("Big packs too##icloud", &packs)) {
                *icloud->packs = packs ? 1 : 0;
                icloud->changed();
            }
            if (ImGui::IsItemHovered())
                gevrTooltip("The HD, AI and Bean texture packs and other mods travel too. They take hundreds of MB or "
                            "more of your iCloud storage and move only on Wi-Fi.");
            if (icloud->status && icloud->status[0]) note(icloud->status);
            return;
        }
    } else {
        ImGui::TextColored(gold, "SYNC WITH GOOGLE DRIVE");
    }
    if (ds == GEVR_SYNC_DRIVE_NONE) {
        note("This build has no Google Drive sync.");
        return;
    }
    if (ds == GEVR_SYNC_DRIVE_UNLINKED || ds == GEVR_SYNC_DRIVE_FAILED) {
        ImGui::TextWrapped("Your settings, saves and ROMs follow you between your devices (Quest, PC, phones, "
                           "Apple TV) through your own Google Drive.");
        note("The game sees only its own GoldenEye VR folder there. Each device keeps its own display settings "
             "and split-screen seats.");
        if (ds == GEVR_SYNC_DRIVE_FAILED) {
            ImGui::PushStyleColor(ImGuiCol_Text, bad);
            ImGui::TextWrapped("%s", drive.note);
            ImGui::PopStyleColor();
        }
        ImGui::Spacing();
        if (ImGui::Button("Link Google Drive")) gevrSyncDriveLink();
        if (ImGui::IsItemHovered()) gevrTooltip("Shows a code to enter at google.com/device on your phone or computer.");
        return;
    }
    if (ds == GEVR_SYNC_DRIVE_STARTING) {
        note("Asking Google for a code...");
        if (ImGui::Button("Cancel##drive")) gevrSyncDriveCancel();
        return;
    }
    if (ds == GEVR_SYNC_DRIVE_WAITING) {
        const bool qr = strcmp(drive.url, "https://www.google.com/device") == 0;
        if (qr) {
            gevrSyncQr(ImGui::GetFontSize() * 0.22f);
            ImGui::SameLine();
        }
        ImGui::BeginGroup();
        ImGui::TextUnformatted(qr ? "Scan it, or on your phone or computer go to" : "On your phone or computer, go to");
        ImGui::TextColored(gold, "%s", drive.url);
        ImGui::TextUnformatted("and enter this code:");
        const float scale = ImGui::GetCurrentWindow()->FontWindowScale;
        ImGui::SetWindowFontScale(scale * 1.8f);
        ImGui::TextColored(gold, "%s", drive.code);
        ImGui::SetWindowFontScale(scale);
        note("Then allow GoldenEye VR. This page moves on by itself.");
#if !defined(GEVR_TVOS)
        if (ImGui::Button("Open in the browser")) gevrSyncOpenUrl(drive.url);
        if (ImGui::IsItemHovered()) gevrTooltip("Opens the page on this device; sign in to Google there and enter the code.");
        ImGui::SameLine();
#endif
        if (ImGui::Button("Cancel##drive")) gevrSyncDriveCancel();
        ImGui::EndGroup();
        return;
    }

    // linked
    ImGui::TextWrapped("Linked: %s", drive.account[0] ? drive.account : "Google Drive");
    note(drive.syncing ? (drive.status[0] ? drive.status : "Syncing.")
                       : "Your settings, saves and ROMs sync from the next start.");

    // this device's name: the others' notes say which device ("the save from Den")
    {
        static char name[48];
        static bool loaded = false;
        if (!loaded) {
            gevrSyncDeviceName(name, sizeof(name));
            loaded = true;
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("This device:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
        ImGui::InputTextWithHint("##syncname", "Living room", name, sizeof(name));
        if (ImGui::IsItemDeactivatedAfterEdit()) gevrSyncSetDeviceName(name);
        if (ImGui::IsItemHovered()) gevrTooltip("How your other devices name this one in sync notes.");
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 3.0f);
    }

    bool packs = gevrSyncPacks() != 0;
    if (ImGui::Checkbox("Big packs too", &packs)) gevrSyncSetPacks(packs ? 1 : 0);
    if (ImGui::IsItemHovered())
        gevrTooltip("The HD, AI and Bean texture packs and other mods travel too. They take hundreds of MB or more "
                    "of your Drive's space; the first upload or download takes a while.");
    float done = 0;
    char line[192];
    if (gevrSyncProgress(&done, line, sizeof(line))) {
        ImGui::ProgressBar(done < 0 ? -1.0f * (float)ImGui::GetTime() : done, ImVec2(-1, ImGui::GetTextLineHeight()), line);
    }

    // saves the sync set aside: one can be used again
    char aside[3][96];
    const int n = gevrSyncSavesAside(aside, 3);   // the newest three of the five kept
    if (n > 0) {
        ImGui::Spacing();
        ImGui::TextColored(gold, "SAVES SET ASIDE");
        if (ImGui::IsItemHovered())
            gevrTooltip("A save replaced by a newer one from another device is kept here. The one you choose comes "
                        "back at the next start and goes to your other devices.");
        char chosen[96];
        const bool waiting = gevrSyncSaveChosen(chosen, sizeof(chosen)) != 0;
        for (int i = 0; i < n; i++) {
            char label[160];
            gevrSyncAsideLabel(aside[i], label, sizeof(label));
            ImGui::PushID(i);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::SameLine();
            if (waiting && strcmp(chosen, aside[i]) == 0) {
                ImGui::TextDisabled("(used from the next start)");
            } else if (ImGui::SmallButton("Use this one")) {
                gevrSyncUseSaveAside(aside[i]);
            }
            ImGui::PopID();
        }
    }

    ImGui::Spacing();
    if (ImGui::Button("Unlink Google Drive")) ImGui::OpenPopup("Unlink Google Drive?");
    if (ImGui::BeginPopupModal("Unlink Google Drive?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("This device stops syncing.");
        ImGui::TextDisabled("Its settings and saves stay here; your other devices and Drive keep theirs.");
        ImGui::TextDisabled("To take GoldenEye VR's access away everywhere: your Google Account, Security.");
        if (ImGui::Button("Cancel", ImVec2(220, 52))) ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if (ImGui::Button("Unlink", ImVec2(220, 52))) {
            gevrSyncDriveUnlink();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
