# The XBLA release in Xenia: Bean's reference

Bean draws the XBLA remaster's meshes, so the remaster itself is the
reference for a Bean report: does the release look the same? Rendering the
release's mesh ourselves is not proof. #250's seam came out of a raw-mesh
software render, but the release in Xenia shows none (2026-11-18). Set up
2026-11-18 on Windows (RTX 3080 Ti, Direct3D 12). The game runs at full
speed. Nothing here ships, and no game data is in the repository.

## Setup

- **Xenia Canary** `02d2cb5` (2026-03-24), fetched and checked against its
  SHA-256 by `tools/emu/xenia/xrun.ps1` into `%LOCALAPPDATA%\gevr-tools`.
  It runs portable: its config, profile and caches sit beside the exe.
- **The game:** the player's XBLA copy is an STFS package,
  `584108A9/000D0000/<40 hex>`. That is the same file the Bean import reads
  (`gevr_bean_import.c`). Unzip that one file and start it with
  `xrun.ps1 -Package <file>`. Its title name is "Bean" ("Closed Beta" on the
  logo).
- **Profile:** the menu ignores A without a signed-in profile. Use Profile >
  Show Profile Menu > Create Profile, then put its XUID in
  `xenia-canary.config.toml` `[Profiles] logged_profile_slot_0_xuid` to sign
  it in at boot. The local one here is `GEVRTest`.
- **Keyboard pad:** Xenia's `[HID.WinKey]` defaults use `;` for A and `'` for
  B. Letters are easier to type and to script: `keybind_a = "0xBA J"`,
  `keybind_b = "0xDE N"`, d-pad `T F G H` (`keybind_dpad_up = "^W T"` and so
  on), `keybind_left_thumb = "V"`. Start is X, WASD the left stick, the
  arrow keys the right stick, E fires, Q aims. A real Xbox pad works
  (XInput).

## Scripts (`tools/emu/xenia`, PowerShell)

- `xshot.ps1 out.png`: captures the Xenia window (`PrintWindow` with
  `PW_RENDERFULLCONTENT`, so the D3D picture is included). It works in the
  background.
- `xclick.ps1 X Y [-Text s]`: clicks the window's ImGui dialogs (the
  profile menu) at a point of an `xshot` capture. It types with `WM_CHAR`.
  It works in the background.
- `xkey.ps1 -Keys X`: posts keys to the window without focusing it. Only
  Start lands reliably. The keyboard pad reads other keys only while Xenia
  has focus.
- `xpress.ps1 -Keys W -Ms 2000` / `-Seq "X:150,wait:1000,J:150"`: brings
  Xenia to the front, sends real input and gives focus back. Windows refuses
  the focus switch while someone is using another window (the foreground
  lock), so for now a person drives the game.
- `xtomenu.ps1`: presses Start through the attract loop until the main menu
  is up.

## Captures

Captures are kept beside the game files, not in the repository:
`GoldenEye-007-Bean\xenia-captures`.

- **#250, Dam:** the dam wall's walkway by the far tower, looking downstream
  over the railing. The release shows the mountains as one slope from the
  peaks down, with no seam (`250-dam-railing-xbla.png`).
