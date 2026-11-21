"use strict";
const $ = (id) => document.getElementById(id);
const source = $("source");
const state = {version: 0, diagnostics: [], cpp: "", compiledVersion: -1, items: [], selected: 0,
  composing: false, converting: false, tabFocus: false, previousDraft: null, oldText: "", stops: [], stop: 0, examples: [], completing: null, analyzing: null};
let analyzeTimer, completionTimer, toastTimer;

function escapeHtml(text) {
  return text.replace(/[&<>"']/g, (char) => ({"&":"&amp;", "<":"&lt;", ">":"&gt;", '"':"&quot;", "'":"&#39;"}[char]));
}
function syntax(text) {
  const pattern = /("(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])*'|\/\/[^\n]*|\b(?:struct|graph|digraph|edge|pair|cin|cout|fn|let|auto|if|else|while|for|rep|return|break|int|char|string|void)\b|\b(?:print|read_char|read_string|read_int|read_vec|array|len|push_back|push|pop|back|size|resize|clear|add|edge_count|sort|at_least|more_than|at_most|less_than|run_length)\b|\b[0-9]+\b)/g;
  let result = "", end = 0;
  for (const match of text.matchAll(pattern)) {
    const token = match[0];
    const kind = token.startsWith("//") ? "comment" : /^["']/.test(token) ? "string" : /^[0-9]/.test(token) ? "number" :
      /^(print|read_char|read_string|read_int|read_vec|array|len|push_back|push|pop|back|size|resize|clear|add|edge_count|sort|at_least|more_than|at_most|less_than|run_length)$/.test(token) ? "builtin" : "keyword";
    result += escapeHtml(text.slice(end, match.index)) + '<span class="token-' + kind + '">' + escapeHtml(token) + "</span>";
    end = match.index + token.length;
  }
  return result + escapeHtml(text.slice(end));
}
function positionAt(offset) {
  const lines = source.value.slice(0, offset).split("\n");
  return {line: lines.length - 1, character: lines[lines.length - 1].length};
}
function offsetAt(position) {
  const lines = source.value.split("\n");
  let offset = 0;
  for (let i = 0; i < position.line && i < lines.length; i++) offset += lines[i].length + 1;
  return Math.min(source.value.length, offset + position.character);
}
function syncScroll() {
  $("highlight").style.transform = "translate(" + -source.scrollLeft + "px," + -source.scrollTop + "px)";
  $("line-numbers").style.transform = "translateY(" + -source.scrollTop + "px)";
}
function renderSource() {
  let result = "", end = 0;
  for (const item of state.diagnostics) {
    const start = offsetAt(item.range.start), finish = offsetAt(item.range.end);
    result += syntax(source.value.slice(end, start));
    result += '<mark class="code-error" title="' + escapeHtml(item.message) + '">' +
      syntax(source.value.slice(start, finish) || " ") + "</mark>";
    end = finish;
  }
  $("highlight").innerHTML = result + syntax(source.value.slice(end)) + "\n";
  const badLines = new Set(state.diagnostics.map((item) => item.range.start.line));
  $("line-numbers").innerHTML = source.value.split("\n").map((_, index) =>
    badLines.has(index) ? '<span class="bad-line">' + (index + 1) + "</span>" : String(index + 1)).join("\n");
  syncScroll();
  updateCursor();
}
function updateCursor() {
  const position = positionAt(source.selectionStart);
  $("cursor-position").textContent = "Ln " + (position.line + 1) + ", Col " + (position.character + 1);
}
function setAnalysis(label, kind) {
  $("analysis-state").className = kind || "";
  $("analysis-state").replaceChildren(document.createElement("i"), document.createTextNode(label));
}
function showToast(text) {
  $("toast").textContent = text;
  $("toast").hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { $("toast").hidden = true; }, 2400);
}
function setConnection(connected) {
  $("connection").className = "connection" + (connected ? "" : " offline");
  $("connection").replaceChildren(document.createElement("i"), document.createTextNode(connected ? "接続済み" : "接続できません"));
}
async function request(path, body, signal) {
  const response = await fetch("/api/" + path, {method: "POST", headers: {"Content-Type": "application/json"},
    body: JSON.stringify(body), signal: signal});
  const data = await response.json();
  if (!response.ok) throw new Error(data.error || "リクエストを完了できませんでした。");
  setConnection(true);
  return data;
}
function renderDiagnostics(items) {
  state.diagnostics = items;
  source.setAttribute("aria-invalid", String(items.length > 0));
  if (items.length) source.setAttribute("aria-errormessage", "diagnostic-list");
  else source.removeAttribute("aria-errormessage");
  $("diagnostic-count").textContent = String(items.length);
  $("diagnostic-count").className = items.length ? "errors" : "";
  const list = $("diagnostic-list");
  list.replaceChildren();
  if (!items.length) {
    const empty = document.createElement("div");
    empty.className = "diagnostic-empty";
    empty.innerHTML = '<span class="check-circle">✓</span>エラーは見つかりませんでした。';
    list.append(empty);
    setAnalysis("チェック完了", "");
  } else {
    setAnalysis(items.length + " 件のエラー", "errors");
    items.forEach((item) => {
      const button = document.createElement("button");
      button.className = "diagnostic-item";
      const icon = document.createElement("span"), message = document.createElement("span"), location = document.createElement("span");
      icon.className = "error-symbol"; icon.textContent = "⊗";
      message.textContent = item.message;
      location.className = "location";
      location.textContent = "main.fken:" + (item.range.start.line + 1) + ":" + (item.range.start.character + 1);
      button.append(icon, message, location);
      button.addEventListener("click", () => {
        const start = offsetAt(item.range.start), end = offsetAt(item.range.end);
        source.focus(); source.setSelectionRange(start, end);
        source.scrollTop = Math.max(0, item.range.start.line * parseFloat(getComputedStyle(source).lineHeight) - 80);
        syncScroll(); updateCursor();
      });
      list.append(button);
    });
  }
  renderSource();
}
async function analyze() {
  if (state.composing) return;
  if (state.analyzing) state.analyzing.abort();
  const controller = new AbortController();
  state.analyzing = controller;
  const version = state.version;
  setAnalysis("解析中…", "pending");
  try {
    const data = await request("analyze", {source: source.value, version: version}, controller.signal);
    if (version === state.version) renderDiagnostics(data.diagnostics);
  } catch (error) {
    if (error.name === "AbortError" || version !== state.version) return;
    setConnection(false); setAnalysis("解析に接続できません。もう一度編集して再試行できます。", "errors");
  }
}
function hideCompletion() {
  $("completion-popup").hidden = true;
  source.setAttribute("aria-expanded", "false");
  source.removeAttribute("aria-activedescendant");
  state.items = [];
}
function renderCompletion() {
  const list = $("completions");
  list.replaceChildren();
  state.items.forEach((item, index) => {
    const row = document.createElement("div");
    row.id = "completion-" + index;
    row.className = "completion-item";
    row.setAttribute("role", "option");
    row.setAttribute("aria-selected", String(index === state.selected));
    const kind = document.createElement("span"), name = document.createElement("span"), type = document.createElement("span");
    kind.className = "completion-kind"; kind.textContent = item.kind === 6 ? "v" : item.kind === 15 ? "◇" : item.kind === 14 ? "k" : "ƒ";
    name.textContent = item.label; type.className = "completion-type";
    type.textContent = item.kind === 6 ? "変数" : item.kind === 15 ? "テンプレート" : item.kind === 14 ? "キーワード" : "関数";
    row.append(kind, name, type);
    row.addEventListener("pointerdown", (event) => { event.preventDefault(); state.selected = index; acceptCompletion(); });
    list.append(row);
  });
  $("completion-detail").textContent = state.items[state.selected].detail || "";
  source.setAttribute("aria-activedescendant", "completion-" + state.selected);
  const row = $("completion-" + state.selected);
  if (row) row.scrollIntoView({block: "nearest"});
}
function placeCompletion() {
  const style = getComputedStyle(source), height = parseFloat(style.lineHeight);
  const before = source.value.slice(0, source.selectionStart).split("\n");
  const canvas = document.createElement("canvas"), context = canvas.getContext("2d");
  context.font = style.font;
  const width = context.measureText(before[before.length - 1].replace(/\t/g, "    ")).width;
  const popup = $("completion-popup"), box = $("codebox");
  popup.style.left = Math.max(4, Math.min(box.clientWidth - popup.offsetWidth - 4, 16 + width - source.scrollLeft)) + "px";
  popup.style.top = Math.max(4, Math.min(box.clientHeight - popup.offsetHeight - 4,
    16 + before.length * height - source.scrollTop)) + "px";
}
async function complete(manual) {
  if (state.composing) return;
  if (manual) clearTimeout(completionTimer);
  const offset = source.selectionStart, version = state.version;
  const prefix = source.value.slice(0, offset).match(/[A-Za-z_][A-Za-z0-9_]*$/);
  if (!manual && !prefix && !/\.\s*$/.test(source.value.slice(0, offset))) { hideCompletion(); return; }
  if (state.completing) state.completing.abort();
  const controller = new AbortController();
  state.completing = controller;
  try {
    const data = await request("complete", {source: source.value, position: positionAt(offset), version: version}, controller.signal);
    if (version !== state.version || offset !== source.selectionStart) return;
    state.items = data.items.slice(0, 35); state.selected = 0;
    if (!state.items.length) { hideCompletion(); return; }
    $("completion-popup").hidden = false;
    source.setAttribute("aria-expanded", "true");
    renderCompletion(); placeCompletion();
  } catch (error) {
    if (error.name !== "AbortError" && manual) showToast("補完候補を取得できませんでした。");
  }
}
function expandSnippet(text) {
  const pattern = /\$\{([0-9]+)(?::([^}]*))?\}|\$([0-9]+)/g;
  let result = "", end = 0;
  const stops = [];
  for (const match of text.matchAll(pattern)) {
    result += text.slice(end, match.index);
    const start = result.length;
    result += match[2] || "";
    stops.push({number: Number(match[1] || match[3]), start: start, end: result.length});
    end = match.index + match[0].length;
  }
  result += text.slice(end);
  if (stops.length && !stops.some((stop) => stop.number === 0)) stops.push({number: 0, start: result.length, end: result.length});
  return {text: result, stops: stops.sort((a, b) => (a.number || 999) - (b.number || 999))};
}
function replaceRange(text, start, end) {
  source.setRangeText(text, start, end, "end");
  source.dispatchEvent(new Event("input", {bubbles: true}));
}
function acceptCompletion() {
  const item = state.items[state.selected];
  if (!item) return;
  const start = offsetAt(item.textEdit.range.start), end = offsetAt(item.textEdit.range.end);
  const expanded = expandSnippet(item.textEdit.newText);
  state.stops = [];
  replaceRange(expanded.text, start, end);
  hideCompletion();
  state.stops = expanded.stops.map((stop) => ({...stop, start: stop.start + start, end: stop.end + start}));
  state.stop = 0;
  source.focus();
  if (state.stops.length) selectStop(0);
}
function selectStop(index) {
  const stop = state.stops[index];
  if (!stop) { state.stops = []; return; }
  state.stop = index;
  source.setSelectionRange(stop.start, stop.end);
  if (!stop.number) state.stops = [];
  updateCursor();
}
function adjustStops(previous, next) {
  if (!state.stops.length) return;
  let start = 0, oldEnd = previous.length, newEnd = next.length;
  while (start < oldEnd && start < newEnd && previous[start] === next[start]) start++;
  while (oldEnd > start && newEnd > start && previous[oldEnd - 1] === next[newEnd - 1]) { oldEnd--; newEnd--; }
  const active = state.stops[state.stop], delta = newEnd - oldEnd;
  if (!active || start < active.start || oldEnd > active.end) { state.stops = []; return; }
  active.end += delta;
  state.stops.forEach((stop, index) => { if (index !== state.stop && stop.start >= oldEnd) { stop.start += delta; stop.end += delta; } });
}
source.addEventListener("input", () => {
  adjustStops(state.oldText, source.value);
  state.oldText = source.value;
  state.version++;
  state.diagnostics = [];
  state.compiledVersion = -1;
  $("copy").disabled = $("download").disabled = true;
  $("output-state").textContent = "未変換";
  $("output-state").className = "tab-state";
  renderSource(); hideCompletion();
  try { localStorage.setItem("fken_cpl.draft", source.value); $("save-state").textContent = "ブラウザに保存済み"; } catch (_) { $("save-state").textContent = ""; }
  clearTimeout(analyzeTimer); clearTimeout(completionTimer);
  setAnalysis("変更をチェックします…", "pending");
  if (!state.composing) { analyzeTimer = setTimeout(analyze, 250); completionTimer = setTimeout(() => complete(false), 150); }
});
const pairs = new Map([["(", ")"], ["[", "]"], ["{", "}"]]);
source.addEventListener("beforeinput", event => {
  if (!event.cancelable || event.isComposing || state.composing) return;
  const start = source.selectionStart, end = source.selectionEnd;
  const left = source.value[start - 1], right = source.value[end];
  if (event.inputType === "deleteContentBackward" && start === end && pairs.has(left) && pairs.get(left) === right) {
    event.preventDefault();
    replaceRange("", start - 1, end + 1);
  } else if (event.inputType === "insertText" && pairs.has(event.data)) {
    event.preventDefault();
    replaceRange(event.data + source.value.slice(start, end) + pairs.get(event.data), start, end);
    source.setSelectionRange(start + 1, end + 1);
  } else if (event.inputType === "insertText" && start === end && right === event.data && [")", "]", "}"].includes(right)) {
    event.preventDefault();
    source.setSelectionRange(end + 1, end + 1);
  }
  updateCursor();
});
source.addEventListener("compositionstart", () => { state.composing = true; hideCompletion(); clearTimeout(analyzeTimer); });
source.addEventListener("compositionend", () => { state.composing = false; analyze(); });
source.addEventListener("scroll", () => { syncScroll(); hideCompletion(); });
source.addEventListener("click", () => { updateCursor(); hideCompletion(); });
source.addEventListener("keyup", updateCursor);
source.addEventListener("keydown", (event) => {
  if (event.isComposing || state.composing) return;
  if ((event.ctrlKey || event.metaKey) && event.key === "Enter") { event.preventDefault(); convert(); return; }
  if (event.ctrlKey && event.code === "Space") { event.preventDefault(); complete(true); return; }
  if (!$("completion-popup").hidden) {
    if (event.key === "ArrowDown" || event.key === "ArrowUp") {
      event.preventDefault();
      state.selected = (state.selected + (event.key === "ArrowDown" ? 1 : -1) + state.items.length) % state.items.length;
      renderCompletion(); return;
    }
    if (event.key === "Tab" || event.key === "Enter") { event.preventDefault(); acceptCompletion(); return; }
    if (event.key === "Escape") { event.preventDefault(); hideCompletion(); state.stops = []; state.tabFocus = true; return; }
    if (event.key === "ArrowLeft" || event.key === "ArrowRight") hideCompletion();
  }
  if (event.key === "Tab") {
    if (state.tabFocus) { state.tabFocus = false; return; }
    event.preventDefault();
    if (state.stops.length) { selectStop(state.stop + (event.shiftKey ? -1 : 1)); return; }
    replaceRange("    ", source.selectionStart, source.selectionEnd);
  } else if (event.key === "Enter") {
    event.preventDefault();
    const start = source.selectionStart, end = source.selectionEnd;
    const line = source.value.slice(0, start).split("\n").pop();
    const indent = line.match(/^[ \t]*/)[0], extra = line.trimEnd().endsWith("{") ? "    " : "";
    const closing = extra && source.value[end] === "}" ? "\n" + indent : "";
    replaceRange("\n" + indent + extra + closing, start, end);
    if (closing) source.setSelectionRange(start + 1 + indent.length + extra.length, start + 1 + indent.length + extra.length);
  } else if (event.key === "Escape") { state.stops = []; state.tabFocus = true; }
});
async function convert() {
  if (state.converting) return;
  state.converting = true;
  const version = state.version;
  $("convert").disabled = true;
  hideCompletion();
  try {
    const data = await request("compile", {source: source.value, version: version});
    if (version !== state.version) return;
    renderDiagnostics(data.diagnostics);
    if (data.diagnostics.length) { showToast("エラーを修正すると C++ に変換できます。"); return; }
    state.cpp = data.cpp; state.compiledVersion = version;
    $("output-empty").hidden = true; $("output-content").hidden = false;
    const marker = data.cpp.indexOf("#endif\n");
    $("runtime-code").textContent = marker >= 0 ? data.cpp.slice(0, marker + 7) : "";
    $("runtime").hidden = marker < 0;
    $("output-code").textContent = marker >= 0 ? data.cpp.slice(marker + 7).trimStart() : data.cpp;
    $("output-scroll").scrollTop = 0;
    $("output-state").textContent = "変換済み"; $("output-state").className = "tab-state ready";
    $("output-size").textContent = new TextEncoder().encode(data.cpp).length.toLocaleString() + " bytes";
    $("copy").disabled = $("download").disabled = false;
  } catch (error) { setConnection(false); showToast("変換できませんでした。サーバーへの接続を確認してください。"); }
  finally { state.converting = false; $("convert").disabled = false; }
}
$("convert").addEventListener("click", convert);
$("complete-button").addEventListener("click", () => { source.focus(); complete(true); });
$("copy").addEventListener("click", async () => {
  if (state.compiledVersion !== state.version) return;
  try { await navigator.clipboard.writeText(state.cpp); showToast("C++ コードをコピーしました。"); }
  catch (_) { showToast("コピーできませんでした。「保存」をお使いください。"); }
});
$("download").addEventListener("click", () => {
  if (state.compiledVersion !== state.version) return;
  const url = URL.createObjectURL(new Blob([state.cpp], {type: "text/plain;charset=utf-8"}));
  const link = document.createElement("a"); link.href = url; link.download = "main.cpp"; link.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
});
$("guide-button").addEventListener("click", () => $("guide").showModal());
$("guide-close").addEventListener("click", () => $("guide").close());
$("guide").addEventListener("click", (event) => { if (event.target === $("guide")) $("guide").close(); });
$("example").addEventListener("change", () => {
  const example = state.examples.find((item) => item.id === $("example").value);
  if (!example) return;
  state.previousDraft = source.value; $("restore-draft").hidden = false;
  state.stops = []; source.value = example.source; source.scrollTop = source.scrollLeft = 0;
  source.dispatchEvent(new Event("input", {bubbles: true})); convert();
});
$("restore-draft").addEventListener("click", () => {
  if (state.previousDraft === null) return;
  source.value = state.previousDraft; state.previousDraft = null;
  $("restore-draft").hidden = true; state.stops = [];
  source.dispatchEvent(new Event("input", {bubbles: true}));
});
(async function initialize() {
  renderSource();
  try {
    const [health, examples] = await Promise.all([fetch("/api/health").then((r) => r.json()), fetch("/api/examples").then((r) => r.json())]);
    setConnection(true);
    $("version").textContent = "v" + health.version;
    state.examples = examples; $("example").replaceChildren();
    examples.forEach((example) => { const option = document.createElement("option"); option.value = example.id; option.textContent = example.label; $("example").append(option); });
    let draft = null;
    try { draft = localStorage.getItem("fken_cpl.draft"); } catch (_) {}
    source.value = draft === null ? examples[0].source : draft;
    state.oldText = source.value;
    renderSource();
    await convert();
  } catch (_) { setConnection(false); setAnalysis("サーバーに接続できません。再読み込みしてください。", "errors"); }
})();

let running = null;
function finishRun(message) {
  if (!running) return;
  running.controller.abort();
  running.worker?.terminate();
  clearTimeout(running.timer);
  running = null;
  $("run").textContent = "▶ 実行";
  $("run-status").textContent = message;
}
function invalidateRun() {
  if (running) finishRun("入力の変更により実行を中止しました。");
  else $("run-status").textContent = "";
  $("stdout").textContent = "";
}
source.addEventListener("input", invalidateRun);
$("stdin").addEventListener("input", invalidateRun);
$("run").addEventListener("click", async () => {
  if (running) { finishRun("停止しました。"); return; }
  const job = {controller: new AbortController()};
  running = job;
  $("run").textContent = "■ 停止";
  $("run-status").textContent = "チェック中…";
  $("stdout").textContent = "";
  try {
    const data = await request("plan", {source: source.value}, job.controller.signal);
    if (running !== job) return;
    renderDiagnostics(data.diagnostics);
    if (data.diagnostics.length) { finishRun("診断に表示されたエラーを修正してください。"); return; }
    job.worker = new Worker("/runner.js");
    job.worker.onmessage = ({data}) => {
      if (running !== job) return;
      $("stdout").textContent = data.output;
      finishRun(data.error || "実行が完了しました。");
    };
    job.worker.onerror = () => { if (running === job) finishRun("実行中にエラーが発生しました。"); };
    job.timer = setTimeout(() => { if (running === job) finishRun("実行時間の上限（3 秒）に達しました。"); }, 3000);
    $("run-status").textContent = "実行中…";
    job.worker.postMessage({plan: data.plan, stdin: $("stdin").value});
  } catch (error) { if (running === job) finishRun(error.message); }
});
