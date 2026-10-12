"""The launcher's START row with the campaign beside it, at Quest font size.

The main page is laid out to its last pixel (gevrLauncherBeginBody reserves
gevrLauncherFooterHeight below the options), so the campaign choice must sit
on START's own row. This renders the production row and the line under it
below a full options panel on the launcher's 1280x960 canvas and checks the
page does not overflow; then adds the old CAMPAIGN block's five lines (title,
three radios, a note) to show the check catches the scrolling it caused. It also opens the dropdown.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "port/vr/vr_launcher.cpp").read_text(encoding="utf-8")
start = source.index("enum {\n    GEVR_START_ROW_NONE,")
end = source.index("static void gevrModsPage(", start)
row = source[start:end]

fixture = r'''
#include "gevr_launcher_ui.h"
#include "imgui/imgui_internal.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

/* ROW */

static int campaign = 0;
static ImRect startRect;
static float pageBottom, windowBottom;

static void frame(int extraLines, bool openCombo)
{
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("##launcher", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
    ImGui::TextUnformatted("GoldenEye VR");           // the header line above the panel
    gevrLauncherBeginBody("main-options");
    ImGui::Dummy(ImGui::GetContentRegionAvail());     // a full options page
    ImGui::EndChild();
    ImGui::Separator();
    for (int i = 0; i < extraLines; i++) ImGui::TextUnformatted("CAMPAIGN");
    if (openCombo) ImGui::OpenPopupEx(ImHashStr("##ComboPopup", 0, ImGui::GetID("##campaign")));  /* BeginCombo's own id */
    gevrLauncherStartRow(&campaign, true, false, true, false);
    startRect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    gevrLauncherStatus("Goldfinger 64 did not load, so GoldenEye started instead. A long note is cut to one line.");
    pageBottom = ImGui::GetItemRectMax().y;
    windowBottom = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y - ImGui::GetStyle().WindowPadding.y;
    ImGui::End();
    ImGui::Render();
}

int main()
{
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1280, 960);
    io.DeltaTime = 1.0f / 72.0f;
    unsigned char *px; int w, h;
    io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    io.FontGlobalScale = 2.2f;
    ImGui::GetStyle().ScaleAllSizes(2.2f);

    frame(0, false);
    frame(0, false);
    ImGuiWindow *win = ImGui::FindWindowByName("##launcher");
    std::printf("footer: page bottom %.1f, window bottom %.1f, START x %.0f-%.0f\n",
                pageBottom, windowBottom, startRect.Min.x, startRect.Max.x);
    std::fflush(stdout);
    assert(pageBottom <= windowBottom + 0.5f);          // the row and its note fit the reserved footer
    assert(win->ScrollMax.y <= 0.5f);                    // nothing to scroll
    assert(startRect.Min.x > 300.0f);                    // the dropdown sits left of START, on its row
    assert(startRect.GetHeight() >= ImGui::GetFrameHeight() * 1.5f);   // START keeps its size

    /* the old CAMPAIGN block (title, three radio rows, a note) overflows: the check is live */
    frame(5, false);
    frame(5, false);
    assert(pageBottom > windowBottom + 0.5f || win->ScrollMax.y > 0.5f);

    /* the dropdown opens on START's row */
    frame(0, true);
    frame(0, true);
    ImGuiWindow *popup = ImGui::FindWindowByName("##Combo_00");
    assert(popup && popup->Active);

    ImGui::DestroyContext();
    std::puts("PASS: campaign dropdown beside START; the main page keeps its height at Quest font size");
    return 0;
}
'''.replace("/* ROW */", row)

with tempfile.TemporaryDirectory(prefix="gevr-start-row-") as temp:
    cpp, exe = Path(temp) / "row.cpp", Path(temp) / "row.exe"
    cpp.write_text("#define IMGUI_DEFINE_MATH_OPERATORS\n" + fixture, encoding="utf-8")
    subprocess.run([shutil.which("g++") or "g++", "-std=c++17", "-O1", "-I" + str(ROOT / "port/vr"),
                    "-I" + str(ROOT / "port/include"), str(cpp),
                    *[str(ROOT / "port/vr/imgui" / f) for f in ("imgui.cpp", "imgui_draw.cpp", "imgui_tables.cpp", "imgui_widgets.cpp")],
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe)], cwd=temp, check=True)
