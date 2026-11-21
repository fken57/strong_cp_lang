/* Optional browser E2E. Set FKEN_PLAYWRIGHT to an installed playwright module. */
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const {spawn} = require("node:child_process");
const {chromium} = require(process.env.FKEN_PLAYWRIGHT || "playwright");

async function main() {
  let server, browser, address = process.env.FKEN_TEST_URL;
  try {
    if (!address) {
      server = spawn(process.env.PYTHON || "python3", [path.join(__dirname, "fken_cpl"), "--web", "--port", "0"]);
      address = await new Promise((resolve, reject) => {
        const timer = setTimeout(() => reject(new Error("Web server startup timed out")), 15000);
        server.stdout.on("data", (chunk) => {
          const match = chunk.toString().match(/http:\/\/127\.0\.0\.1:[0-9]+/);
          if (match) { clearTimeout(timer); resolve(match[0]); }
        });
        server.stderr.on("data", (chunk) => process.stderr.write(chunk));
        server.on("exit", (code) => { clearTimeout(timer); reject(new Error("Web server exited: " + code)); });
      });
    }
    browser = await chromium.launch({headless: true});
    const context = await browser.newContext({viewport: {width: 1440, height: 1040},
                                             permissions: ["clipboard-read", "clipboard-write"]});
    const page = await context.newPage();
    const errors = [];
    page.on("pageerror", (error) => errors.push(error.message));
    await page.goto(address);
    await page.waitForFunction(() => document.querySelector("#output-state").textContent === "変換済み");
    const source = page.locator("#source");
    await page.locator("#example").selectOption("graph");
    await page.locator("#stdin").fill("3 2 1 2 10 1 3 9223372036854775808");
    await page.locator("#run").click();
    await page.waitForFunction(() => document.querySelector("#stdout").textContent === "2 11\n3 9223372036854775809\n");
    await source.fill(`int[] a = [3,1,3]; a.sort();
pair<int,int>[] runs = a.run_length();
cout << a.at_least(3) << ' ' << a.more_than(3) << ' ' << a.at_most(3) << ' ' << a.less_than(3) << '\\n';
for (pair<int,int> run : runs) { cout << run.first << ' ' << run.second << '\\n'; }`);
    await page.locator("#run").click();
    await page.waitForFunction(() => document.querySelector("#stdout").textContent === "1 3 2 0\n1 1\n3 2\n");
    await source.fill("graph G(3); G.add(0,1,5); G.add(1,2,8); auto cnt = G.diameter; auto [d,a,b] = G.diameter; print(cnt.dist); print(d); print(a); print(b);");
    await page.locator("#run").click();
    await page.waitForFunction(() => document.querySelector("#stdout").textContent === "13\n13\n2\n0\n");
    await source.fill("graph G(3); G.add(0,1,5); G.add(1,2,8); auto cnt = G.diameter;");
    await source.press("Control+Space");
    const expansion = page.locator(".completion-item").filter({hasText: "分解して展開"});
    await expansion.waitFor({state: "visible"});
    await source.press("Enter");
    assert.match(await source.inputValue(), /auto \[diameter_dist, diameter_one, diameter_two\] = G\.diameter;$/);
    await source.press("End");
    await page.keyboard.insertText(" print(diameter_dist);");
    await page.locator("#run").click();
    await page.waitForFunction(() => document.querySelector("#stdout").textContent === "13\n");
    await source.fill(`char[][] a = ["かな", "ab"];
a[0].push_back('!'); a[1][1] = 'x';
char c; string s; cin >> c >> s;
cout << a[0] << ' ' << a[1] << ' ' << a[0].size() << ' ' << c << s << '\\n';`);
    await page.locator("#stdin").fill("word");
    await page.locator("#run").click();
    await page.waitForFunction(() => document.querySelector("#stdout").textContent === "かな! ax 7 word\n");
    await source.fill("");
    await source.pressSequentially("([])");
    assert.equal(await source.inputValue(), "([])");
    await source.pressSequentially("{");
    assert.equal(await source.inputValue(), "([]){}");
    await source.press("Backspace");
    assert.equal(await source.inputValue(), "([])");
    await source.fill("while (1) {}");
    await page.locator("#run").click();
    await page.waitForFunction(() => document.querySelector("#run-status").textContent.includes("3 秒"));
    await source.fill("int x; x += read_int(); print(x + 1);");
    await page.locator("#stdin").fill("9223372036854775807");
    await page.locator("#run").click();
    await page.waitForFunction(() => document.querySelector("#stdout").textContent === "9223372036854775808\n");
    await source.fill("let count=1;\nprint(missing);");
    await page.waitForFunction(() => document.querySelector("#diagnostic-list").textContent.includes("undefined variable"));
    assert.equal(await source.getAttribute("aria-invalid"), "true");
    await page.locator(".diagnostic-item").click();
    assert.equal(await source.evaluate((el) => el.value.slice(el.selectionStart, el.selectionEnd)), "missing");

    await source.fill("let count=1;\nprint(count);");
    await page.waitForFunction(() => document.querySelector("#diagnostic-count").textContent === "0" &&
                                    document.querySelector("#analysis-state").textContent === "チェック完了");
    assert.equal(await page.locator("#copy").isDisabled(), true);
    await page.locator("#convert").click();
    await page.waitForFunction(() => !document.querySelector("#copy").disabled);
    await page.locator("#copy").click();
    const copied = await page.evaluate(() => navigator.clipboard.readText());
    assert.ok(copied.includes("#ifndef FKEN_RUNTIME_HPP") && copied.includes("int main()"));
    const downloadEvent = page.waitForEvent("download");
    await page.locator("#download").click();
    const download = await downloadEvent;
    assert.equal(download.suggestedFilename(), "main.cpp");
    assert.equal(fs.readFileSync(await download.path(), "utf8"), copied);

    await source.fill("rea");
    await source.press("Control+Space");
    await page.locator("#completion-popup").waitFor({state: "visible"});
    assert.ok((await page.locator("#completions").textContent()).includes("read_vec"));
    await source.press("ArrowUp");
    await source.press("Tab");
    assert.equal(await source.inputValue(), "read_vec(n)");
    assert.equal(await source.evaluate((el) => el.value.slice(el.selectionStart, el.selectionEnd)), "n");
    await page.keyboard.insertText("3");
    await source.press("Tab");
    assert.equal(await source.evaluate((el) => el.selectionStart), "read_vec(3)".length);

    await source.fill("rep");
    await source.press("Control+Space");
    await page.locator("#completion-popup").waitFor({state: "visible"});
    await source.press("Enter");
    await page.keyboard.insertText("j");
    await source.press("Tab"); await page.keyboard.insertText("1");
    await source.press("Tab"); await page.keyboard.insertText("4");
    await source.press("Tab"); await page.keyboard.insertText("print(j);");
    assert.match(await source.inputValue(), /rep\(j, 1, 4\)/);
    await page.waitForFunction(() => document.querySelector("#analysis-state").textContent === "チェック完了");

    await source.fill("fn twice(n) { return n * 2; }\ntw");
    await source.press("Control+Space");
    await page.locator("#completion-popup").waitFor({state: "visible"});
    assert.ok((await page.locator("#completions").textContent()).includes("twice"));
    await source.press("Enter"); await page.keyboard.insertText("6"); await source.press("Tab");
    await page.keyboard.insertText(";");
    await page.waitForFunction(() => document.querySelector("#analysis-state").textContent === "チェック完了");
    assert.match(await source.inputValue(), /twice\(6\);$/);

    await source.fill("int[][] grid(2,3);\ngrid[0].");
    await source.press("End");
    await page.locator("#completion-popup").waitFor({state: "visible"});
    assert.ok((await page.locator("#completions").textContent()).includes("push"));
    await page.locator(".completion-item").filter({hasText: "push"}).click();
    await page.keyboard.insertText("7"); await source.press("Tab");
    await page.keyboard.insertText(";");
    await page.waitForFunction(() => document.querySelector("#analysis-state").textContent === "チェック完了");
    assert.match(await source.inputValue(), /grid\[0\]\.push\(7\);$/);
    await source.fill("while(1){break;}");
    await page.waitForFunction(() => document.querySelector("#analysis-state").textContent === "チェック完了");
    await source.fill("break;");
    await page.waitForFunction(() => document.querySelector("#diagnostic-list").textContent.includes("break is only allowed"));

    // A result received after another edit must not become the current downloadable artifact.
    await source.fill("print(1);");
    await page.route("**/api/compile", async (route) => {
      await new Promise((resolve) => setTimeout(resolve, 250));
      await route.continue();
    });
    const converted = page.waitForResponse((response) => response.url().endsWith("/api/compile"));
    await page.locator("#convert").click();
    await source.fill("print(2);");
    await converted;
    await page.waitForFunction(() => !document.querySelector("#convert").disabled);
    assert.equal(await page.locator("#copy").isDisabled(), true);
    assert.equal(await page.locator("#output-state").textContent(), "未変換");
    await page.unroute("**/api/compile");

    await source.fill("print(42);");
    await page.reload();
    await page.waitForFunction(() => document.querySelector("#output-state").textContent === "変換済み");
    assert.equal(await source.inputValue(), "print(42);");
    await page.locator("#example").selectOption("bigint");
    await page.locator("#restore-draft").waitFor({state: "visible"});
    await page.locator("#restore-draft").click();
    assert.equal(await source.inputValue(), "print(42);");

    await source.fill('<img src=x onerror="window.fkenInjected=true">');
    await page.waitForFunction(() => document.querySelector("#diagnostic-count").textContent === "1");
    assert.equal(await page.locator("#highlight img").count(), 0);
    assert.equal(await page.evaluate(() => window.fkenInjected), undefined);

    await source.fill("print(42);");
    await source.press("Control+Space");
    await page.locator("#completion-popup").waitFor({state: "visible"});
    await source.press("Escape"); await source.press("Tab");
    assert.equal(await source.evaluate((el) => document.activeElement === el), false);

    await page.locator("#example").selectOption("functions");
    await page.waitForFunction(() => document.querySelector("#output-state").textContent === "変換済み");
    await page.screenshot({path: path.join(__dirname, ".build/playground.png"), fullPage: true});
    await page.setViewportSize({width: 390, height: 844});
    assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
    await page.screenshot({path: path.join(__dirname, ".build/playground-mobile.png"), fullPage: true});
    assert.deepEqual(errors, []);
    console.log("Web E2E passed: diagnostics, completion/snippets, copy/download, stale results, drafts, keyboard and mobile.");
  } finally {
    if (browser) await browser.close();
    if (server) server.kill("SIGINT");
  }
}
main().catch((error) => { console.error(error); process.exitCode = 1; });
