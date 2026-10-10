import assert from "node:assert/strict";
import { mkdir, writeFile } from "node:fs/promises";
import { createRequire } from "node:module";
import { resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const { chromium } = createRequire(import.meta.url)("playwright");
const root = fileURLToPath(new URL("..", import.meta.url));
const output = resolve(root, "../../build/web-consolidation");
await mkdir(output, { recursive: true });
const browser = await chromium.launch({
  headless: true,
  ...(process.env.CHROME_PATH ? { executablePath: process.env.CHROME_PATH } : {}),
});
const results = [];
try {
  for (const width of [320, 390, 1440, 1920]) {
    const page = await browser.newPage({ viewport: { width, height: width < 600 ? 844 : 1000 }, reducedMotion: "reduce" });
    const errors = [];
    page.on("pageerror", error => errors.push(error.message));
    for (const file of ["index.html", "modular.html"]) {
      await page.goto(pathToFileURL(resolve(root, file)).href);
      await page.locator("img").evaluateAll(images => images.forEach(img => { img.loading = "eager"; }));
      for (const section of await page.locator("section").all()) await section.scrollIntoViewIfNeeded();
      await page.locator("footer").scrollIntoViewIfNeeded();
      await page.waitForFunction(() => [...document.images].filter(img => img.getClientRects().length).every(img => img.complete));
      const failedImages = await page.locator("img").evaluateAll(images => images.filter(img => img.getClientRects().length && !img.naturalWidth).map(img => img.src));
      assert.deepEqual(failedImages, [], `Broken images at ${file}/${width}`);
      const overflow = await page.evaluate(() => document.documentElement.scrollWidth - innerWidth);
      if (overflow > 1) {
        console.log(await page.locator("body *").evaluateAll(elements => elements.filter(el => { const r = el.getBoundingClientRect(); return r.width > 0 && (r.right > innerWidth + 1 || r.left < -1); }).sort((a, b) => b.getBoundingClientRect().right - a.getBoundingClientRect().right).slice(0, 15).map(el => ({ tag: el.tagName, class: el.className, text: el.textContent.slice(0, 70), right: el.getBoundingClientRect().right }))));
        await page.screenshot({ path: resolve(output, `overflow-${file}-${width}.png`), fullPage: true });
      }
      assert.ok(overflow <= 1, `Horizontal overflow ${overflow}px at ${file}/${width}`);
      if (file === "index.html") {
        assert.equal(await page.locator(".suite-card").count(), 6);
        assert.equal(await page.locator("#grid .card").count(), 16);
        await page.locator('[data-cat="modular"]').click();
        assert.equal(await page.locator("#grid .card:visible").count(), 2);
        await page.locator('[data-cat="royal-chain"]').click();
        assert.equal(await page.locator("#grid .card:visible").count(), 1);
        await page.locator('[data-cat="all"]').click();
        await page.locator('[data-rack="vocal"]').click();
        assert.equal(await page.locator(".b-mod").count(), 10);
        await page.locator('[data-rack="stack"]').click();
        assert.equal(await page.locator(".b-slot").count(), 4);
      } else {
        assert.equal(await page.locator(".module-list article").count(), 22);
        await page.locator("#instrument-tab").click();
        assert.equal(await page.locator("#instrument-panel").isVisible(), true);
        await page.locator("#fx-tab").focus();
        await page.keyboard.press("ArrowRight");
        assert.equal(await page.locator("#instrument-tab").getAttribute("aria-selected"), "true");
      }
      await page.screenshot({ path: resolve(output, `${file.replace(".html", "")}-${width}-full.png`), fullPage: true });
      await page.evaluate(() => scrollTo(0, 0));
      await page.screenshot({ path: resolve(output, `${file.replace(".html", "")}-${width}.png`) });
      results.push({ file, width, overflow, failedImages });
    }
    await page.goto(pathToFileURL(resolve(root, "plugin.html")).href + "?id=vocal");
    await page.getByText("Portfolio status: legacy.", { exact: false }).waitFor();
    await page.goto(pathToFileURL(resolve(root, "plugin.html")).href + "?id=modular-instrument");
    await page.waitForURL("**/modular.html#instrument");
    assert.equal(await page.locator("#instrument-panel").isVisible(), true);
    assert.deepEqual(errors, [], `JavaScript errors at width ${width}`);
    await page.close();
  }
  await writeFile(resolve(output, "checks.json"), JSON.stringify(results, null, 2) + "\n");
  console.log(`PASS ${results.length} responsive page checks, portfolio filters, collections, legacy links and instrument tabs`);
} finally {
  await browser.close();
}
