// AtmoForge settings panel.
//
// X-Plane registers each native function as window.xplane.<name>(payload).
// The payload reaches C++ as a JSON string and the answer comes back parsed.
// Native code never pushes into the page, so the state line is polled;
// everything else is request/response.
//
// Settings, presets and quick sets all live in C++ (src/settings.cpp,
// src/presets.cpp). This file renders them and sends changes back; nothing
// about the weather is decided here.

(() => {
  "use strict";

  const $ = (id) => document.getElementById(id);

  const state = {
    mode: "live",
    schema: [],
    values: {},
    quick: {},
    activePreset: null,
    lastError: "",
    missedPolls: 0,
  };

  // Which page each section of the settings table is rendered into.
  const HOSTS = {
    general: "fields-wx",
    wx: "fields-wx",
    clouds: "fields-clouds",
    atmo: "fields-atmo",
    water: "fields-water",
  };

  // A few settings sit in a section that is not where a pilot looks for them.
  // The crossfade is a general setting but it is a cloud control.
  const KEY_HOSTS = { "general.transition": "fields-clouds" };

  // ── bridge ───────────────────────────────────────────────────────
  function bridge(name, payload = {}) {
    if (!window.xplane || typeof window.xplane[name] !== "function")
      return Promise.reject(new Error("X-Plane connection unavailable"));
    return Promise.resolve(window.xplane[name](payload)).then((r) =>
      typeof r === "string" ? JSON.parse(r) : r
    );
  }

  function fail(error) {
    state.lastError = (error && error.message) || String(error);
    $("statusText").textContent = state.lastError;
  }

  // Binds only if the element is there. One stale id must not take the whole
  // panel down: wire() throwing before the controls are built leaves every
  // column empty with no clue why.
  function on(id, event, handler) {
    const node = $(id);
    if (node) node.addEventListener(event, handler);
    else console.warn(`AtmoForge: no #${id} to bind ${event}`);
  }

  function flash(node) {
    if (!node) return;
    node.classList.add("flash");
    setTimeout(() => node.classList.remove("flash"), 300);
  }

  // ── settings ─────────────────────────────────────────────────────
  const pending = {};
  function push(key, value) {
    state.values[key] = value;
    clearTimeout(pending[key]);
    pending[key] = setTimeout(() => bridge("setSetting", { key, value }).catch(fail), 110);
  }

  function decimalsFor(entry) {
    const whole = (n) => Math.abs(n - Math.round(n)) < 1e-6;
    const span = Math.abs(entry.max - entry.min);
    if (whole(entry.min) && whole(entry.max) && whole(entry.value) && span >= 10) return 0;
    if (span <= 0.5) return 3;
    if (span > 4) return 1;
    return 2;
  }

  function format(entry, value) {
    return value.toFixed(decimalsFor(entry));
  }

  function paint(input, entry, value) {
    const span = entry.max - entry.min || 1;
    input.style.setProperty("--at", `${((value - entry.min) / span) * 100}%`);
  }

  function makeCheckbox(entry, value) {
    const row = document.createElement("button");
    row.className = "chk";
    row.type = "button";
    row.dataset.key = entry.key;
    row.setAttribute("aria-pressed", value >= 0.5 ? "true" : "false");
    row.innerHTML = `<i class="box"></i><span></span>`;
    row.querySelector("span").textContent = entry.label;
    row.addEventListener("click", () => {
      const next = row.getAttribute("aria-pressed") === "true" ? 0 : 1;
      row.setAttribute("aria-pressed", next ? "true" : "false");
      push(entry.key, next);
    });
    return row;
  }

  function makeSlider(entry, value) {
    const row = document.createElement("div");
    row.className = "ctl";
    row.dataset.key = entry.key;
    row.innerHTML = `<div class="ctl-head"><label></label><output></output></div><input type="range">`;

    const output = row.querySelector("output");
    const input = row.querySelector("input");
    row.querySelector("label").textContent = entry.label;
    input.min = entry.min;
    input.max = entry.max;
    input.step = decimalsFor(entry) === 0 ? 1 : (entry.max - entry.min) / 200;
    input.value = value;
    output.textContent = format(entry, value);
    paint(input, entry, value);

    input.addEventListener("input", () => {
      const next = parseFloat(input.value);
      output.textContent = format(entry, next);
      paint(input, entry, next);
      push(entry.key, next);
    });
    return row;
  }

  function reflect(key, value) {
    const entry = state.schema.find((e) => e.key === key);
    if (!entry) return;
    const changed = Math.abs((state.values[key] ?? value) - value) > 1e-6;
    state.values[key] = value;

    const box = document.querySelector(`.chk[data-key="${key}"]`);
    if (box) { box.setAttribute("aria-pressed", value >= 0.5 ? "true" : "false"); return; }

    const row = document.querySelector(`.ctl[data-key="${key}"]`);
    if (!row) return;
    const input = row.querySelector("input");
    input.value = value;
    row.querySelector("output").textContent = format(entry, value);
    paint(input, entry, value);

    // Mark what moved without the user touching it, then let it settle.
    if (changed) {
      row.classList.add("changed");
      clearTimeout(row.dataset.timer);
      row.dataset.timer = setTimeout(() => row.classList.remove("changed"), 900);
    }
  }

  function adoptSettings(values) {
    if (!values) return;
    state.values = { ...values };
    Object.entries(values).forEach(([key, value]) => reflect(key, value));
  }

  // ── build ────────────────────────────────────────────────────────
  async function buildSettings() {
    const [schemaReply, values] = await Promise.all([bridge("getSchema"), bridge("getSettings")]);
    state.schema = schemaReply.schema || [];
    state.values = { ...values };

    // Switches above, sliders below. Mixed into one grid they interleave by
    // accident and every row sits at a different height.
    const bins = {};
    Object.values(HOSTS).forEach((id) => {
      const host = $(id);
      if (!host || bins[id]) return;
      host.innerHTML = "";
      const switches = document.createElement("div");
      switches.className = "switches";
      const grid = document.createElement("div");
      grid.className = "grid";
      host.append(switches, grid);
      bins[id] = { switches, grid };
    });

    state.schema.forEach((entry) => {
      // Advanced entries are saved and applied like any other; they just do
      // not belong on a panel read mid-flight. Settings.json has them.
      if (entry.advanced) return;
      const bin = bins[KEY_HOSTS[entry.key] || HOSTS[entry.section]];
      if (!bin) return;
      const current = entry.key in state.values ? state.values[entry.key] : entry.value;
      if (entry.toggle) bin.switches.appendChild(makeCheckbox(entry, current));
      else bin.grid.appendChild(makeSlider(entry, current));
    });

    Object.values(bins).forEach(({ switches }) => {
      if (!switches.childElementCount) switches.remove();
    });
  }

  async function buildPresets() {
    const reply = await bridge("getPresets");
    state.quick = reply.quick || {};
    const host = $("presets");
    host.innerHTML = "";

    // Twenty-one in one flat list is a wall; grouped it is a menu.
    let current = null;
    (reply.presets || []).forEach((preset) => {
      if (preset.category && preset.category !== current) {
        current = preset.category;
        const head = document.createElement("div");
        head.className = "preset-cat";
        head.textContent = current;
        host.appendChild(head);
      }
      const button = document.createElement("button");
      button.className = "preset";
      button.type = "button";
      button.setAttribute("aria-pressed", "false");
      button.innerHTML = "<b></b><i></i>";
      button.querySelector("b").textContent = preset.name;
      button.querySelector("i").textContent = preset.description || "";
      button.addEventListener("click", () => {
        bridge("applyPreset", { id: preset.id })
          .then((answer) => {
            state.activePreset = preset.id;
            adoptSettings(answer && answer.settings);
            host.querySelectorAll(".preset").forEach((other) =>
              other.setAttribute("aria-pressed", other === button ? "true" : "false"));
          })
          .catch(fail);
      });
      host.appendChild(button);
    });
  }

  // ── pages and modes ──────────────────────────────────────────────
  function selectMode(mode, tellNative = true) {
    state.mode = mode;
    document.querySelectorAll(".seg-b").forEach((b) =>
      b.setAttribute("aria-selected", b.dataset.mode === mode ? "true" : "false"));
    document.querySelectorAll(".pane").forEach((p) => { p.hidden = p.dataset.pane !== mode; });
    if (tellNative) bridge("setMode", { mode }).catch(fail);
  }

  // ── sheet ────────────────────────────────────────────────────────
  function openSheet(title, withIcao = false) {
    $("sheetTitle").textContent = title;
    $("icaoWrap").hidden = !withIcao;
    $("overlay").hidden = false;
    const body = $("sheetBody");
    body.innerHTML = "<pre>…</pre>";
    return body;
  }
  const closeSheet = () => { $("overlay").hidden = true; };

  function showMetar() {
    const body = openSheet("METAR", true);
    bridge("getMetar", { icao: $("metarIcao").value.trim() })
      .then((r) => {
        body.innerHTML = "";
        const add = (label, value) => {
          const head = document.createElement("p");
          head.className = "label-mini";
          head.textContent = label;
          const pre = document.createElement("pre");
          pre.textContent = value || "—";
          body.append(head, pre);
        };
        add("AtmoForge depiction", r.injected);
        add("X-Plane's own METAR", r.sim || "(X-Plane returned nothing for this station)");
        if (r.station && !$("metarIcao").value.trim()) $("metarIcao").placeholder = r.station;
      })
      .catch(fail);
  }

  function showAudit() {
    const body = openSheet("Datarefs in this X-Plane");
    bridge("getDiagnostics")
      .then((r) => {
        const audit = r.audit || {};
        body.innerHTML = "";
        const line = (txt, alert) => {
          const node = document.createElement("div");
          node.className = alert ? "line alert" : "line";
          node.textContent = txt;
          body.appendChild(node);
        };
        const okReq = audit.requiredMissing === 0;
        line(okReq ? `All ${audit.requiredTotal} weather datarefs are present.`
                   : `${audit.requiredMissing} of ${audit.requiredTotal} weather datarefs are missing.`, !okReq);
        const found = audit.privateTotal - audit.privateMissing;
        line(`${found} of ${audit.privateTotal} rendering datarefs resolved.`, audit.privateMissing > 0);
        if ((audit.missing || []).length) {
          const head = document.createElement("p");
          head.className = "label-mini";
          head.textContent = "Not found";
          const pre = document.createElement("pre");
          pre.textContent = audit.missing.join("\n");
          body.append(head, pre);
        }
        line("Missing rendering datarefs are skipped, not forced. "
           + "Plugins > AtmoForge > Probe for rendering datarefs searches for the real names.");
      })
      .catch(fail);
  }

  // ── state line ───────────────────────────────────────────────────
  // Two things can make the panel look dead while it is working perfectly:
  // another weather engine overwriting the same datarefs, and X-Plane not
  // handing over its rendering controls. Say so on screen rather than
  // leaving it to Log.txt.
  function renderAlarm(s) {
    const node = $("alarm");
    if (!node) return;
    const notes = [];
    // Measured first, guessed second. "overridden" means the writes were
    // checked and did not survive; "rival" only means a known plugin is
    // loaded, which it may be without interfering.
    if (s.overridden)
      notes.push("<b>AtmoForge's weather is being overwritten.</b> "
               + (s.rival ? `${s.rival} is running and ` : "Another plugin is ")
               + "writing the same datarefs, so nothing here reaches the sim. Disable one of them.");
    else if (s.rival)
      notes.push(`<b>${s.rival}</b> is also loaded. It writes the same weather datarefs — `
               + "if the sky stops following this panel, that is why.");
    if (s.visualsTotal > 0 && s.visualsMissing >= s.visualsTotal)
      notes.push("<b>X-Plane has not exposed its rendering controls.</b> "
               + "Clouds, light and water do nothing until it does — this clears "
               + "a few seconds after a flight loads.");
    else if (s.visualsMissing > 0)
      notes.push(`${s.visualsMissing} of ${s.visualsTotal} rendering controls are absent on this build; `
               + "those sliders are skipped.");

    node.hidden = notes.length === 0;
    node.innerHTML = notes.join("<br>");
  }

  function renderState(s) {
    $("statusText").textContent = s.status || "";
    renderAlarm(s);
    $("btnRefresh").textContent = s.fetching ? "…" : "Refresh";
    if (s.mode && s.mode !== state.mode) selectMode(s.mode, false);

    if (s.historicalDate && document.activeElement !== $("histDate")) $("histDate").value = s.historicalDate;
    if (document.activeElement !== $("histHour")) $("histHour").value = s.historicalHour;

    if (s.mode === "live" && state.activePreset) {
      state.activePreset = null;
      document.querySelectorAll(".preset").forEach((p) => p.setAttribute("aria-pressed", "false"));
    }
  }

  let pollTimer = null;
  function poll() {
    bridge("getState")
      .then((s) => { state.missedPolls = 0; renderState(s); })
      .catch((error) => {
        // One missed poll is not worth a red status line; a run of them is.
        if (++state.missedPolls > 3 && !state.lastError) fail(error);
      });
  }

  // ── wiring ───────────────────────────────────────────────────────
  function wire() {
    document.querySelectorAll(".seg-b").forEach((b) =>
      b.addEventListener("click", () => selectMode(b.dataset.mode)));

    document.querySelectorAll("[data-quick]").forEach((b) =>
      b.addEventListener("click", () => {
        flash(b);
        const name = b.dataset.quick;
        // Reflect at once so the panel does not appear to lag, then let the
        // answer from C++ settle the exact values.
        const local = state.quick[name];
        if (local) Object.entries(local).forEach(([k, v]) => reflect(k, v));
        bridge("applyQuick", { name })
          .then((answer) => adoptSettings(answer && answer.settings))
          .catch(fail);
      }));

    on("btnRefresh", "click", () => bridge("refresh").catch(fail));
    on("btnMetar", "click", showMetar);
    on("btnAudit", "click", showAudit);
    on("btnBriefing", "click", () => {
      const body = openSheet("Weather briefing");
      bridge("getBriefing")
        .then((r) => {
          body.innerHTML = "";
          const pre = document.createElement("pre");
          pre.textContent = r.text || "—";
          body.appendChild(pre);
        })
        .catch(fail);
    });

    on("metarIcao", "keydown", (e) => { if (e.key === "Enter") showMetar(); });
    on("sheetClose", "click", closeSheet);
    on("overlay", "click", (e) => { if (e.target === $("overlay")) closeSheet(); });
    document.addEventListener("keydown", (e) => {
      if (e.key === "Escape" && !$("overlay").hidden) closeSheet();
    });

    on("btnHistApply", "click", () => {
      const date = $("histDate").value;
      const hour = parseInt($("histHour").value, 10);
      if (!date) return fail(new Error("Pick a date first"));
      bridge("setHistorical", { date, hour: isNaN(hour) ? 12 : hour })
        .then((r) => { if (!r.ok) fail(new Error("That date was not accepted")); })
        .catch(fail);
    });

    on("btnDone", "click", () => bridge("hideWindow").catch(fail));
  }

  function start() {
    try { wire(); } catch (error) { console.error("AtmoForge: wiring failed", error); }
    selectMode("live", false);
    Promise.all([buildSettings(), buildPresets()])
      .then(() => { poll(); pollTimer = setInterval(poll, 250); })
      .catch(fail);
  }

  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", start);
  else start();

  window.addEventListener("unload", () => clearInterval(pollTimer));
})();
