# GoldenEye VR Menu Studio

A local browser workspace for designing multiplayer menus before implementing
them in the native Quest UI. It edits mockups and simulates interactions; it
does not connect to the production lobby service or change gameplay.

## Open it

The portable build is `build/menu-studio.html`. Open that file in a modern
desktop browser. It contains the editor, existing project branding, the native
launcher font, and the asset reference catalog. It needs no server, npm
dependencies, account, or internet connection.

To rebuild the portable file:

```bash
python3 tools/menu-studio/build.py
```

For development, from the repository root:

```bash
python3 tools/menu-studio/build.py
python3 tools/menu-studio/serve.py --port 8800
```

Open port 8800 on your local machine. The server serves only the editor files
and explicitly selected branding/font resources. It has no upload endpoint
and does not expose the repository, ROM files, or local configuration.

## Test on a phone

Open the hosted editor URL in Safari or Chrome. On phones the canvas uses
the full screen width. **Screens**, **Add**, **Layers**, **Edit** and **More**
open bottom panels. Tap an element, drag to move it, and pull its gold corner
to resize. Properties stay open while editing. Use **Select many** to tap
several elements for grouping or alignment. **+ / −** and the slider zoom;
**Pan** lets you scroll the enlarged canvas, and **Fit** restores the overview.
Landscape is useful for detailed layouts. **More** includes imports, assets,
theme and notes. **Export project** saves the JSON to send back.

iOS Files previews do not reliably execute a downloaded HTML app. A hosted
URL is the recommended phone entry point. A cloud workspace's `localhost`
URL is only accessible inside that workspace.

To prepare static hosting:

```bash
python3 tools/menu-studio/build.py --site
```

This creates `build/menu-studio-site/index.html` and
`build/menu-studio-site.zip`. The ZIP has just the self-contained editor,
security headers and the bundled font's license. It contains no ROM, secrets,
backend configuration or user designs. Imported assets and saved designs stay
in the current device's browser unless the user exports or shares them.

For Cloudflare Pages, create a **separate Pages project** using **Direct
Upload**, select this ZIP, and open the resulting `pages.dev` URL. A connected
repository deployment can use build command
`python3 tools/menu-studio/build.py --site` and output directory
`build/menu-studio-site`. No dependencies or server are needed. The existing
production lobby worker is not part of the editor deployment.

Use a stable hosting URL to retain autosaved projects between visits. Export
before moving to a different URL, device or browser.

### GitHub Pages

The repository includes `.github/workflows/menu-studio-pages.yml`. In GitHub,
open **Settings → Pages → Build and deployment → Source** and select
**GitHub Actions**. Then run **Actions → Deploy Menu Studio to GitHub Pages →
Run workflow**. Later changes to the studio on `main` redeploy automatically.

The standard repository URL is `https://mrsco.github.io/goldeneye-vr/`.
Use the deployment URL shown by GitHub if the repository has a custom domain.
The self-contained build works under the repository path without absolute
asset paths or external packages. The workflow uploads only
`build/menu-studio-site`, so the game repository and lobby backend are not
served. GitHub Pages ignores the Cloudflare `_headers` file; it supplies its
own hosting headers.

## Shape the experience

The starter project has four views of the shared pause window: Match, Rules,
Player and Audio. Deathmatch and co-op use mode-specific content in those views. The map IDs, player limits, scenarios and weapon
sets come from `port/src/net/net_match.c`.

- **Screens:** add templates or a blank canvas, duplicate/remove screens.
- **Add:** reusable text, buttons, panels, images, models, player rosters,
  game lists, player cards, ballots, scoreboards, loadouts, comms, countdowns,
  tabs, toggles, sliders, selectors, fields, and progress bars.
- **Canvas:** drag, resize, Shift-click to multi-select, group, align,
  distribute, and snap to an eight-pixel grid. Alt-drag bypasses snapping.
  Alt-click selects an individual member of a group.
- **Layers:** change draw order, lock geometry, or hide elements.
- **Properties:** change copy, dimensions, colors, fonts, visual state,
  assets, native references, bindings, visibility rules, actions and screen
  destinations. Project Properties holds the shared palette, notes, screen
  size and sample multiplayer data.
- **Play flow:** navigate linked screens, toggle local-player readiness,
  cast/change a map vote, and try controls. Host/client and phase selectors
  exercise visibility rules. Preview changes do not modify the design.

The local player is identified by `sample.localPlayer`. Edit sample data to
test an empty or full roster, different scores, teams, pings and readiness.
Dynamic copy supports `{{map}}`, `{{weaponSet}}`, `{{playerCount}}`,
`{{readyCount}}`, `{{waitingPlayers}}` and `{{phase}}`.

The starter canvas uses the native window’s 1280 × 960 logical pixels. Coordinates use a top-left
origin. They describe design intent; actual VR panel size, angular readability,
controller navigation, focus and pointer hit testing still need native
implementation and headset testing.

## Reuse existing assets

The studio reuses the project's icon/banner, gold/charcoal palette, and
ProggyClean font extracted without alteration from the vendored ImGui
default font. Its MIT notice is in `assets/LICENSE-font.txt`.

Most original textures, models and game fonts are loaded from the player's
ROM at runtime. The searchable catalog contains identifiers and source paths
from `assets/images.def`, the ROM file table and font documentation; it does
not contain those game visuals.

Import your own previews using **Assets** or by dropping files onto the
canvas:

- PNG, JPEG, WebP or GIF screenshots, portraits, map art and textures.
- TTF, OTF, WOFF or WOFF2 fonts.
- OBJ geometry, including negative indices and triangulated polygons.
- Model JSON from the existing `tools/gevr_model_export.py`. Its world-space
  positions are used directly; local positions use the supplied matrices.

Model previews are a flat-shaded geometry view with editable yaw/pitch.
They do not reproduce the game's materials, textures, skeleton animation or
renderer. For an exact visual, use a captured image and keep the model's
runtime reference.

Select an element, then **Use** an asset or choose it in Properties. Dragging
an asset onto the canvas creates an element. **Reference** records the
native identifier that implementation should use. An imported font can be
assigned to text or other components.

Imports stay in browser storage and exported project files. Preview files are
limited to 8 MB each, 100 assets and roughly 64 MB total. Projects with
original game previews should stay under ignored `build/` directories or
outside the checkout; do not commit ROM-derived assets.

## Hand a design back

1. Set project/screen notes describing the purpose and direction.
2. Give controls destinations, actions, native bindings and visibility rules.
3. Try the flow and use **Review layout** to catch bounds, links, missing
   assets, small text and small pointer targets.
4. **Export project** creates a `.gevr-menu.json` file containing every
   screen, its ordered elements, shared theme, sample data, asset references,
   embedded imported previews, interactions, and notes.
5. Send that JSON back for native implementation. PNG/SVG exports help
   visual review; Markdown exports provide the implementation brief.

The schema is versioned with `format: "gevr-menu-studio"` and `version: 1`.
It preserves explicit geometry and semantic references rather than inferring
them from a screenshot. Group membership is flat: grouped elements retain
absolute canvas coordinates. Draw order is the array order, back to front.
An action is design intent; the implementation must respect the native
voting rules, eight-player protocol, authority and host migration.

Autosave uses IndexedDB in the current browser/origin. Export before clearing
browser data, changing browser, moving machines or making a separate design.
When browser storage is unavailable, the editor shows **Export to save**.

## Verify changes

No npm installation is needed for the model tests:

```bash
node tools/menu-studio/model.test.mjs
```

Browser acceptance tests additionally require Playwright and Chromium. Start
the local server, build the standalone file, then run:

```bash
node tools/menu-studio/browser.test.cjs
node tools/menu-studio/mobile.test.cjs
```

The tests exercise content editing, real drag/resize/undo, lock/hide, ready
and vote simulation, navigation, model import, palette changes, persistent
autosave, portable JSON round trips, PNG output, and the standalone bundle
with networking disabled. Set `MENU_STUDIO_TEST_URL`,
`MENU_STUDIO_CHROMIUM` or `MENU_STUDIO_TEST_OUTPUT` for another environment.
The container's managed Chromium blocks `file://` navigation, so the offline
test loads the exact built HTML into a page with networking disabled.
The touch suite uses iPhone and Pixel profiles in Chromium to exercise
portrait/landscape layouts, actual touch drag/resize, panels that survive
property changes, component insertion, voting, undo and project downloads.
These are browser simulations; real iOS Safari still needs device testing.

## Shared native pause window

The starter matches `port/vr/gevr_pause_widgets.h`: an opaque 1280×960
launcher window with the same ProggyClean font, icon, gold values, white
labels and blue square controls. Four views are tabs of one window:
**Match**, **Rules**, **Player** and **Audio**. No separate exit or kill/loss
pages are needed. Match shows eight deathmatch players and independent map
and weapon ballots. The **4-player co-op** mode replaces those with party
status and mission objectives. Switch modes in the canvas toolbar; use Play
flow's Host/Client selector to check authority. Sample fields remain local.

Older saved projects are preserved. **Launcher starter** opens a confirmation
to replace the current design; export the old design first if you want both.
The source icon is converted from the launcher's `launcher_icon.rgba` without
changing its artwork. ROM visuals remain references or user-imported captures.

Native Quest implementation is in `port/vr/vr_pause_menu.cpp`, with a C bridge
in `src/game/mpmenu.c`. It uses existing configuration setters, votes,
visibility and host restrictions. Raw controller rays feed the same pointer
ownership logic as the launcher. A trigger interacts; either stick and A/X
provide a fallback. B/Y or Resume closes the panel. Opening with a held
trigger requires release before clicking; closing with a held trigger requires
release before firing. Both grips move/resize the surface. Co-op opens this
panel without pausing teammates; the host can confirm ending the mission.
Leave requires confirmation and disconnects this headset to the launcher.

JSON exports contain layout, bindings, dropdown choices, tab destinations,
authority and mode visibility. Send an export back to rearrange the native
widgets. The game does **not** load these JSON files automatically.

Run `python3 port/tests/test_pause_ui.py` for production ImGui widget and
trigger-gate checks. An Android build verifies compilation and linkage;
headset checks still require a Quest and your own ROM. Check opening/closing,
either-hand rays, held triggers, grip movement, host/client permissions,
eight-player scores, independent votes and four-player co-op objectives.
