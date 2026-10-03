// Touch acceptance tests run in Chromium with iPhone and Android screen profiles.
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const { chromium, devices } = require('playwright');
const URL = process.env.MENU_STUDIO_TEST_URL || 'http://127.0.0.1:8800';
const OUT = process.env.MENU_STUDIO_TEST_OUTPUT || '/tmp/gevr-menu-studio-tests';
async function touchDrag(page, from, to) {
  const cdp = await page.context().newCDPSession(page);
  const point = (x,y) => [{x,y,id:1,radiusX:4,radiusY:4,force:1}];
  await cdp.send('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:point(from.x,from.y)});
  for (let i=1;i<=6;i++) await cdp.send('Input.dispatchTouchEvent',{type:'touchMove',touchPoints:point(from.x+(to.x-from.x)*i/6,from.y+(to.y-from.y)*i/6)});
  await cdp.send('Input.dispatchTouchEvent',{type:'touchEnd',touchPoints:[]});
  await cdp.detach();
}
async function main() {
  await fs.mkdir(OUT,{recursive:true});
  const browser=await chromium.launch({executablePath:process.env.MENU_STUDIO_CHROMIUM || '/usr/bin/chromium',headless:true,args:['--no-sandbox']});
  try {
    for (const profile of ['iPhone 13','Pixel 7']) {
      const context=await browser.newContext({...devices[profile],defaultBrowserType:undefined,acceptDownloads:true});
      const page=await context.newPage(), errors=[];
      page.on('pageerror',error=>errors.push(error.message));
      await page.goto(URL); await page.waitForFunction(()=>!!window.MenuStudio);
      const initial=await page.evaluate(()=>MenuStudio.getProject());
      const title=initial.screens[0].nodes.find(n=>n.name==='Screen title');
      assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth),page.viewportSize().width,'No horizontal page overflow');
      const viewport=await page.locator('#viewport').boundingBox();
      assert.ok(viewport.width>=page.viewportSize().width-2,'Canvas uses full phone width');
      await page.locator('[data-id="'+title.id+'"]').tap();
      await page.locator('[data-mobile-panel="properties"]').tap();
      await page.locator('[data-prop="text"]').fill('PHONE LOBBY');
      await page.locator('[data-prop="text"]').blur();
      assert.equal((await page.evaluate(()=>MenuStudio.getProject())).screens[0].nodes.find(n=>n.id===title.id).text,'PHONE LOBBY');
      assert.equal(await page.locator('#inspector').isVisible(),true,'Properties stay open after a change');
      await page.locator('#inspector [data-do="close-panel"]').tap();
      assert.equal(await page.locator('#inspector').isVisible(),false);
      const box=await page.locator('[data-id="'+title.id+'"]').boundingBox();
      await touchDrag(page,{x:box.x+box.width/2,y:box.y+box.height/2},{x:box.x+box.width/2+18,y:box.y+box.height/2+18});
      const moved=(await page.evaluate(()=>MenuStudio.getProject())).screens[0].nodes.find(n=>n.id===title.id);
      assert.ok(moved.x>title.x && moved.y>title.y,'Touch drag moves the title');
      const handle=await page.locator('[data-resize]').boundingBox();
      assert.ok(handle.width>=28,'Resize target is touch sized');
      await touchDrag(page,{x:handle.x+handle.width/2,y:handle.y+handle.height/2},{x:handle.x+handle.width/2+14,y:handle.y+handle.height/2+8});
      const resized=(await page.evaluate(()=>MenuStudio.getProject())).screens[0].nodes.find(n=>n.id===title.id);
      assert.ok(resized.w>moved.w,'Touch resize changes width');
      await page.locator('[data-do="undo"]').tap();
      assert.equal((await page.evaluate(()=>MenuStudio.getProject())).screens[0].nodes.find(n=>n.id===title.id).w,moved.w);
      await page.locator('[data-do="zoom-in"]').tap();
      await page.locator('[data-do="pan"]').tap();
      assert.equal(await page.locator('#viewport').evaluate(el=>el.classList.contains('pan-mode')),true);
      await page.locator('[data-do="pan"]').tap(); await page.locator('[data-do="fit"]').tap();
      await page.locator('[data-mobile-panel="library"]').tap();
      await page.locator('[data-add="badge"]').tap();
      assert.equal((await page.evaluate(()=>MenuStudio.getProject())).screens[0].nodes.at(-1).type,'badge');
      assert.equal(await page.locator('.dock').isVisible(),false,'Adding reveals the canvas');
      const badge=(await page.evaluate(()=>MenuStudio.getProject())).screens[0].nodes.at(-1);
      await page.locator('[data-do="multi-select"]').tap();
      await page.locator('[data-mobile-panel="layers"]').tap();
      await page.locator('[data-layer="'+title.id+'"]').tap();
      await page.locator('.dock [data-do="close-panel"]').tap();
      await page.locator('[data-do="group"]').tap();
      const grouped=(await page.evaluate(()=>MenuStudio.getProject())).screens[0].nodes;
      assert.ok(grouped.find(n=>n.id===title.id).group);
      assert.equal(grouped.find(n=>n.id===title.id).group,grouped.find(n=>n.id===badge.id).group,'Multi-selection and grouping work without a keyboard');
      await page.locator('[data-do="multi-select"]').tap();
      await page.locator('[data-mobile-panel="screens"]').tap();
      await page.locator('[data-screen="'+initial.screens[1].id+'"]').tap();
      assert.equal((await page.evaluate(()=>MenuStudio.getScreen())).kind,'rules');
      await page.locator('[data-do="preview"]').tap();
      await page.getByRole('button',{name:'Match',exact:true}).tap();
      const ballot=page.locator('.type-select').filter({hasText:'NEXT MAP'}).locator('select');
      await ballot.selectOption('Complex');
      assert.equal(await ballot.inputValue(),'Complex');
      const mode=page.locator('#sample-mode');await mode.selectOption('coop');
      assert.equal(await page.locator('.type-roster tbody tr').count(),4);
      await page.locator('[data-do="preview"]').tap();
      await page.locator('[data-mobile-panel="more"]').tap();
      await page.locator('[data-mobile-project]').tap();
      assert.equal(await page.locator('#inspector').isVisible(),true);
      await page.locator('#inspector [data-do="close-panel"]').tap();
      const download=page.waitForEvent('download');
      await page.locator('#export-button').tap();
      const exported=await download;
      await exported.saveAs(path.join(OUT,profile.replaceAll(' ','-')+'.gevr-menu.json'));
      await page.waitForFunction(()=>!document.querySelector('#toast').classList.contains('visible'));
      await page.screenshot({path:path.join(OUT,profile.replaceAll(' ','-')+'.png')});
      await page.setViewportSize({width:844,height:390});
      assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth),844,'Landscape has no page overflow');
      await page.locator('[data-mobile-panel="properties"]').tap();
      await page.locator('#inspector [data-do="close-panel"]').tap();
      assert.deepEqual(errors,[]);
      console.log('PASS: '+profile+' touch editing, drawers, resize, undo, components, votes, export and landscape');
      await context.close();
    }
  } finally { await browser.close(); }
}
main().catch(error=>{console.error(error);process.exitCode=1});
