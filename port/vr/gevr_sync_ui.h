/*
 * Launcher > Play > Sync: Google Drive sync (port/pc/gevr_sync_drive.cpp,
 * docs/cloud-sync.md). The player's own Drive carries settings and saves
 * between every device; the sign-in is Google's device code, for headsets
 * and TVs alike. Header only: the launcher's frame test draws it.
 */
#pragma once

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "gevr_sync.h"
#include "gevr_tooltip.h"

inline void gevrSyncPage(const ImVec4 &gold, const ImVec4 &bad)
{
    auto note = [](const char *text) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", text);
        ImGui::PopStyleColor();
    };
    ImGui::TextColored(gold, "SYNC WITH GOOGLE DRIVE");
    GevrSyncDriveInfo drive;
    const int ds = gevrSyncDrive(&drive);
    if (ds == GEVR_SYNC_DRIVE_NONE) {
        note("This build has no Google Drive sync.");
        return;
    }
    if (ds == GEVR_SYNC_DRIVE_UNLINKED || ds == GEVR_SYNC_DRIVE_FAILED) {
        ImGui::TextWrapped("Your settings and saves follow you between your devices (Quest, PC, phones, Apple TV) "
                           "through your own Google Drive.");
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
    } else if (ds == GEVR_SYNC_DRIVE_STARTING) {
        note("Asking Google for a code...");
        if (ImGui::Button("Cancel##drive")) gevrSyncDriveCancel();
    } else if (ds == GEVR_SYNC_DRIVE_WAITING) {
        ImGui::TextWrapped("On your phone or computer, go to");
        ImGui::TextColored(gold, "%s", drive.url);
        ImGui::TextWrapped("and enter this code:");
        const float scale = ImGui::GetCurrentWindow()->FontWindowScale;
        ImGui::SetWindowFontScale(scale * 1.8f);
        ImGui::TextColored(gold, "%s", drive.code);
        ImGui::SetWindowFontScale(scale);
        note("Then allow GoldenEye VR. This page moves on by itself.");
        if (ImGui::Button("Cancel##drive")) gevrSyncDriveCancel();
    } else {   // linked
        ImGui::TextWrapped("Linked: %s", drive.account[0] ? drive.account : "Google Drive");
        note(drive.syncing ? (drive.status[0] ? drive.status : "Syncing.")
                           : "Your settings and saves sync from the next start.");
        ImGui::Spacing();
        if (ImGui::Button("Unlink Google Drive")) ImGui::OpenPopup("Unlink Google Drive?");
        if (ImGui::BeginPopupModal("Unlink Google Drive?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("This device stops syncing.");
            ImGui::TextDisabled("Its settings and saves stay here; your other devices and Drive keep theirs.");
            if (ImGui::Button("Cancel", ImVec2(220, 52))) ImGui::CloseCurrentPopup();
            ImGui::SameLine();
            if (ImGui::Button("Unlink", ImVec2(220, 52))) {
                gevrSyncDriveUnlink();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
}
