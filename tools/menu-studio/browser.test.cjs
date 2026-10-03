// Browser acceptance tests. Requires Playwright and a Chromium executable.
const assert = require("node:assert/strict");
const fs = require("node:fs/promises");
const path = require("node:path");
const { chromium } = require("playwright");
const ROOT = path.resolve(__dirname, "../..");
const OUT = process.env.MENU_STUDIO_TEST_OUTPUT || "/tmp/gevr-menu-studio-tests";
const URL = process.env.MENU_STUDIO_TEST_URL || "http://127.0.0.1:8800";
const chromiumPath = process.env.MENU_STUDIO_CHROMIUM || "/usr/bin/chromium";

async function main() {
  await fs.mkdir(OUT, { recursive: true });
  const browser = await chromium.launch({ executablePath: chromiumPath, headless: true, args: ["--no-sandbox"] });
  try {
    const page = await browser.newPage({ viewport:{ width:1600,height:1000 }, acceptDownloads:true });
    const errors=[]; page.on("pageerror",e => errors.push(e.message));
    await page.goto(URL); await page.waitForFunction(() => !!window.MenuStudio);
    const initial = await page.evaluate(() => MenuStudio.getProject());
    assert.equal(initial.screens.length,4);
    const rows=await page.locator(".type-scoreboard tbody tr").all();
    assert.equal(rows.length,8);
    const rosterBox=await page.locator(".type-scoreboard").boundingBox(), lastRow=await rows.at(-1).boundingBox();
    assert.ok(lastRow.y+lastRow.height <= rosterBox.y+rosterBox.height+1,"Full eight-player scoreboard fits");
    await page.screenshot({path:path.join(OUT,"starter.png")});
    const title=initial.screens[0].nodes.find(n => n.name === "Screen title");
    await page.locator('[data-id="' + title.id + '"]').click();
    await page.locator('[data-prop="text"]').fill("ASSEMBLE THE SQUAD.");
    await page.locator('[data-prop="text"]').press("Tab");
    assert.equal((await page.evaluate(() => MenuStudio.getProject())).screens[0].nodes.find(n=>n.id===title.id).text,"ASSEMBLE THE SQUAD.");
    console.log("PASS: select and edit real canvas content");

    const geometry=await page.locator('[data-id="' + title.id + '"]').boundingBox();
    await page.mouse.move(geometry.x+50,geometry.y+15);
    await page.mouse.down(); await page.mouse.move(geometry.x+82,geometry.y+31,{steps:5}); await page.mouse.up();
    const moved=(await page.evaluate(() => MenuStudio.getProject())).screens[0].nodes.find(n=>n.id===title.id);
    assert.ok(moved.x>title.x && moved.y>title.y);
    const handle=await page.locator("[data-resize]").boundingBox();
    await page.mouse.move(handle.x+5,handle.y+5); await page.mouse.down(); await page.mouse.move(handle.x+61,handle.y+29,{steps:5}); await page.mouse.up();
    const resized=(await page.evaluate(() => MenuStudio.getProject())).screens[0].nodes.find(n=>n.id===title.id);
    assert.ok(resized.w>moved.w && resized.h>moved.h);
    await page.getByRole("button",{name:"↶",exact:true}).click();
    assert.equal((await page.evaluate(() => MenuStudio.getProject())).screens[0].nodes.find(n=>n.id===title.id).w,moved.w);
    console.log("PASS: drag, resize and undo");

    await page.locator('[data-dock="library"]').click();
    await page.locator('[data-add="button"]').click();
    const added=(await page.evaluate(() => MenuStudio.getProject())).screens[0].nodes.at(-1);
    assert.equal(added.type,"button");
    await page.locator('[data-prop="locked"]').check();
    await page.locator('[data-prop="x"]').fill("400"); await page.locator('[data-prop="x"]').press("Tab");
    assert.equal((await page.evaluate(() => MenuStudio.getProject())).screens[0].nodes.at(-1).x,added.x);
    await page.locator('[data-prop="locked"]').uncheck();
    await page.locator('[data-prop="hidden"]').check();
    assert.ok((await page.evaluate(() => MenuStudio.getProject())).screens[0].nodes.at(-1).hidden);
    await page.locator('[data-prop="hidden"]').uncheck();
    console.log("PASS: reusable components, locking and visibility");

    await page.locator('[data-do="preview"]').click();
    const choice=text=>page.locator('.type-select').filter({has:page.locator('.c-choice>span',{hasText:text})}).locator('select');
    await choice('NEXT MAP').selectOption('Complex');
    await choice('NEXT WEAPONS').selectOption('Pistols');
    assert.equal(await choice('MAP').first().inputValue(),'Facility');
    assert.equal(await choice('NEXT MAP').inputValue(),'Complex');
    assert.equal(await choice('NEXT WEAPONS').inputValue(),'Pistols');
    await page.locator('#preview-role').selectOption('client');
    assert.equal(await choice('MAP').first().isDisabled(),true);
    assert.equal(await choice('NEXT MAP').isEnabled(),true);
    await page.getByRole('button',{name:'Audio',exact:true}).click();
    assert.equal((await page.evaluate(()=>MenuStudio.getScreen())).kind,'audio');
    await page.getByRole('button',{name:'Match',exact:true}).click();
    await page.locator('#sample-mode').selectOption('coop');
    assert.equal(await page.locator('.type-roster tbody tr').count(),4);
    assert.equal(await page.locator('.type-scoreboard').count(),0);
    assert.equal(await page.locator('.type-objectives').count(),1);
    assert.equal(await page.locator('.type-select').count(),0);
    await page.screenshot({path:path.join(OUT,'coop.png')});
    await page.locator('[data-do="preview"]').click();
    assert.equal((await page.evaluate(()=>MenuStudio.getScreen())).kind,'match');
    console.log('PASS: direct tabs, independent votes, authority and four-player co-op objectives');

    const fixture=path.join(OUT,"preview.obj");
    await fs.writeFile(fixture,"v -1 0 0\nv 1 0 0\nv 0 2 0\nf 1 2 3\n");
    await page.locator("#asset-files").setInputFiles(fixture);
    await page.waitForFunction(() => MenuStudio.getProject().assets.some(a=>a.kind==="model"));
    const imported=(await page.evaluate(() => MenuStudio.getProject())).assets.find(a=>a.kind==="model");
    await page.locator('[data-use-asset="' + imported.id + '"]').click();
    assert.equal((await page.evaluate(() => MenuStudio.getScreen())).nodes.at(-1).type,"model");
    assert.ok(await page.locator('.type-model polygon').count());
    console.log("PASS: native-compatible OBJ preview import");

    await page.locator('[data-do="inspector-tab"]').click();
    await page.locator('[data-theme="accent"]').fill("#aabbcc");
    await page.locator('[data-theme="accent"]').dispatchEvent("change");
    assert.equal((await page.evaluate(() => MenuStudio.getProject())).theme.accent,"#aabbcc");
    await page.waitForFunction(() => document.querySelector("#saved-status")?.textContent.includes("Saved"));
    const beforeReload=await page.evaluate(() => MenuStudio.getProject());
    await page.reload(); await page.waitForFunction(() => !!window.MenuStudio);
    assert.deepEqual(await page.evaluate(() => MenuStudio.getProject()),beforeReload);
    console.log("PASS: shared palette and IndexedDB autosave round trip");

    const projectDownloadPromise=page.waitForEvent("download");
    await page.locator("#export-button").click();
    const projectDownload=await projectDownloadPromise, projectFile=path.join(OUT,"roundtrip.gevr-menu.json");
    await projectDownload.saveAs(projectFile);
    const exported=JSON.parse(await fs.readFile(projectFile,"utf8"));
    assert.ok(exported.assets.filter(a=>a.kind==="image").every(a=>a.data.startsWith("data:image/")));
    assert.ok(exported.assets.find(a=>a.id==="native-font").data.startsWith("data:font/"));
    await page.locator("#project-file").setInputFiles(projectFile);
    await page.waitForFunction(name => MenuStudio.getProject().name===name,exported.name);
    assert.deepEqual(await page.evaluate(() => MenuStudio.getProject()),exported);
    console.log("PASS: portable project export/import with embedded branding and models");

    const pngPromise=page.waitForEvent("download");
    await page.evaluate(() => MenuStudio.exportVisual(true));
    const png=await pngPromise; await png.saveAs(path.join(OUT,"menu.png"));
    const data=await fs.readFile(path.join(OUT,"menu.png"));
    assert.equal(data.subarray(1,4).toString(),"PNG"); assert.ok(data.length>10000);
    console.log("PASS: full-size PNG export");
    await page.screenshot({path:path.join(OUT,"editor.png")});
    assert.deepEqual(errors,[]);
    console.log("PASS: no browser exceptions");

    const offlineContext=await browser.newContext({offline:true});
    const standalone=await offlineContext.newPage();
    const offlineErrors=[]; standalone.on("pageerror",e=>offlineErrors.push(e.message));
    // The managed cloud Chromium blocks file:// URLs. Load the exact built
    // document with networking disabled to exercise its standalone bundle.
    await standalone.setContent(await fs.readFile(path.join(ROOT,"build/menu-studio.html"),"utf8"));
    await standalone.waitForFunction(() => !!window.MenuStudio);
    assert.equal((await standalone.evaluate(() => MenuStudio.getProject())).screens.length,4);
    const standaloneDownloadPromise=standalone.waitForEvent("download");
    await standalone.locator("#export-button").click();
    const offlineDownload=await standaloneDownloadPromise;
    await offlineDownload.saveAs(path.join(OUT,"offline.gevr-menu.json"));
    assert.equal(JSON.parse(await fs.readFile(path.join(OUT,"offline.gevr-menu.json"),"utf8")).screens.length,4);
    assert.deepEqual(offlineErrors,[]);
    console.log("PASS: standalone HTML runs and exports with networking disabled");
  } finally { await browser.close(); }
}
main().catch(error=>{console.error(error);process.exitCode=1;});
