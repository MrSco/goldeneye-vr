#!/usr/bin/env python3
"""Build a portable, offline HTML editor with existing project branding."""
import argparse
import base64
import hashlib
import json
import mimetypes
from pathlib import Path
import re
import zipfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
BRANDING = {
    "launcher-icon.png": HERE / "assets/launcher-icon.png",
    "icon.png": ROOT / "services/lobbies/public/favicon-64.png",
    "banner.png": ROOT / "docs/banner.png",
    "native-ui.ttf": HERE / "assets/native-ui.ttf",
}
MODULES = ["model.js", "templates.js", "render.js", "storage.js", "app.js"]


def catalog():
    entries = []
    for name, texture in re.findall(r"IMAGE\(([^,]+),\s*(0x[0-9A-Fa-f]+)", (ROOT / "assets/images.def").read_text()):
        entries.append({"name": name.strip(), "kind": "texture", "reference": "texture:" + texture,
                        "source": "assets/images.def"})
    table = (ROOT / "assets/obseg/file_resource_table.inc.c").read_text()
    for symbol, filename in re.findall(r'\{\s*([A-Z0-9_]+)\s*,\s*"([^"]+)"', table):
        kind = "model" if filename.startswith(("G", "C", "P")) else "font" if "font" in filename.lower() else "file"
        if kind != "file":
            entries.append({"name": filename, "kind": kind, "reference": "rom-file:" + filename,
                            "source": "assets/obseg/file_resource_table.inc.c", "symbol": symbol})
    entries.extend([
        {"name": "Launcher gold", "kind": "color", "reference": "#e0b040", "source": "port/vr/vr_launcher.cpp"},
        {"name": "ImGui default font", "kind": "font", "reference": "ImGui::GetFont()", "source": "port/vr/vr_launcher.cpp"},
        {"name": "Watch small glyphs", "kind": "font", "reference": "font-table:watch-small", "source": "assets/font/readme.md"},
        {"name": "Watch large glyphs", "kind": "font", "reference": "font-table:watch-large", "source": "assets/font/readme.md"},
    ])
    return entries


def build(output):
    entries = catalog()
    (HERE / "catalog.json").write_text(json.dumps(entries, indent=2) + "\n")
    css = (HERE / "studio.css").read_text()
    sources = []
    for filename in MODULES:
        text = (HERE / filename).read_text()
        text = re.sub(r"^import .*?;\n", "", text, flags=re.MULTILINE)
        text = re.sub(r"^export ", "", text, flags=re.MULTILINE)
        # Keep modules scoped: helpers with short names must not collide.
        if filename == "model.js":
            exports = "FORMAT, VERSION, uid, clone, clamp, escapeHTML, palettes, componentTypes, defaultPlayers, createNode, createScreen, bounds, moveNodes, alignNodes, distributeNodes, History, validateProject, validateMesh, parseOBJ, parseGameModel, auditProject, handoffMarkdown"
            sources.append("const StudioModel = (() => {\n" + text + "\nreturn {" + exports + "};\n})();\n")
        else:
            # Convert destructuring aliases from ES module syntax to object syntax.
            bindings = []
            for names, module in re.findall(r'^import \{ (.*?) \} from "(.*?)";', (HERE / filename).read_text(), re.MULTILINE):
                name = {"./model.js": "StudioModel", "./templates.js": "StudioTemplates", "./render.js": "StudioRender", "./storage.js": "StudioStorage"}[module]
                bindings.append("const { " + names.replace(" as ", ": ") + " } = " + name + ";")
            if filename == "app.js":
                sources.append("(async () => {\n" + "\n".join(bindings) + "\n" + text + "\n})();")
            else:
                name, exports = {
                    "templates.js": ("StudioTemplates", "stages, weapons, scenarios, screenKinds, template, starterProject"),
                    "render.js": ("StudioRender", "cssColor, fontFamily, fontCSS, interpolate, meshSVG, nodeContent, visibleNode, nodeStyle, renderScene, shownPlayers"),
                    "storage.js": ("StudioStorage", "loadSaved, saveProject, downloadFile, embedAssets, readDataURL"),
                }[filename]
                sources.append("const " + name + " = (() => {\n" + "\n".join(bindings) + "\n" + text + "\nreturn {" + exports + "};\n})();")
    bundled = "globalThis.STUDIO_CSS = " + json.dumps(css) + ";\nglobalThis.STUDIO_CATALOG = " + json.dumps(entries) + ";\n" + "\n".join(sources)
    for name, path in BRANDING.items():
        data = "data:" + (mimetypes.guess_type(path.name)[0] or "application/octet-stream") + ";base64," + base64.b64encode(path.read_bytes()).decode()
        bundled = bundled.replace("/repo-assets/" + name, data)
    html = (HERE / "index.html").read_text().replace('<link rel="stylesheet" href="studio.css">', "<style>" + css + "</style>")
    notice = (HERE / "assets/LICENSE-font.txt").read_text()
    html = html.replace("<head>", "<head>\n<!--\n" + notice + "\n-->")
    html = html.replace('<script type="module" src="app.js"></script>', "<script>\n" + bundled.replace("</script", "<\\/script") + "\n</script>")
    html = html.replace("/repo-assets/icon.png", "data:image/png;base64," + base64.b64encode(BRANDING["icon.png"].read_bytes()).decode())
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(html)
    print(f"Built {output} ({output.stat().st_size:,} bytes), {len(entries)} native asset references")


def build_site():
    """Prepare an isolated static site for Cloudflare Pages or other hosting."""
    directory = ROOT / "build/menu-studio-site"
    output = directory / "index.html"
    build(output)
    script = re.search(r"<script>([\s\S]*?)</script>", output.read_text()).group(1)
    digest = base64.b64encode(hashlib.sha256(script.encode()).digest()).decode()
    policy = ("default-src 'self'; script-src 'sha256-" + digest + "'; "
              "style-src 'self' 'unsafe-inline'; img-src 'self' data: blob:; "
              "font-src 'self' data:; connect-src 'self'; object-src 'none'; "
              "base-uri 'none'; frame-ancestors 'none'")
    (directory / "_headers").write_text(
        "/*\n  Content-Security-Policy: " + policy + "\n"
        "  X-Content-Type-Options: nosniff\n"
        "  Referrer-Policy: same-origin\n"
        "  Cache-Control: no-cache\n")
    (directory / "LICENSE-font.txt").write_bytes((HERE / "assets/LICENSE-font.txt").read_bytes())
    archive = ROOT / "build/menu-studio-site.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zipped:
        for name in ("index.html", "_headers", "LICENSE-font.txt"):
            zipped.write(directory / name, name)
    print(f"Static site: {directory}; upload bundle: {archive}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build/menu-studio.html")
    parser.add_argument("--site", action="store_true", help="Also create a static site directory and ZIP for hosting")
    args = parser.parse_args()
    build(args.output.resolve())
    if args.site:
        build_site()
