// BikeOS screen test bench front end. Frames are raw RGB565 from the engine.
"use strict";

const $ = (id) => document.getElementById(id);
const screen = $("screen");
const screenCtx = screen.getContext("2d");
const image = screenCtx.createImageData(320, 240);

let status = null;
let framePeriod = 1000 / 7;
let dragging = false;
let lastRouteId = null;
const frameTimes = [];

// ---------- server calls ----------
async function post(path, body) {
  const response = await fetch(path, { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) });
  const json = await response.json();
  if (!response.ok || json.error) throw new Error(json.error || response.statusText);
  return json;
}
const setting = (key, value) => post("/api/set", { key, value: String(value) }).catch(showRideError);

// ---------- frame loop: same cadence as the device (1 Hz steps, ~7 fps frames) ----------
function drawPixels(buffer) {
  const src = new Uint16Array(buffer);
  const dst = image.data;
  for (let i = 0, j = 0; i < src.length; i++, j += 4) {
    const p = src[i];
    const r = (p >> 11) & 0x1f, g = (p >> 5) & 0x3f, b = p & 0x1f;
    dst[j] = (r << 3) | (r >> 2);
    dst[j + 1] = (g << 2) | (g >> 4);
    dst[j + 2] = (b << 3) | (b >> 2);
    dst[j + 3] = 255;
  }
  screenCtx.putImageData(image, 0, 0);
}

async function frameLoop() {
  const started = performance.now();
  try {
    const response = await fetch(`/api/frame?now=${Math.round(started)}`);
    const next = JSON.parse(response.headers.get("X-Status"));
    drawPixels(await response.arrayBuffer());
    frameTimes.push(started);
    while (frameTimes.length && started - frameTimes[0] > 2000) frameTimes.shift();
    updateStatus(next);
  } catch (error) {
    $("r-state").textContent = "Engine not responding";
  }
  const elapsed = performance.now() - started;
  setTimeout(frameLoop, Math.max(0, framePeriod - elapsed));
}

// ---------- status → controls ----------
const metres = (m) => (m >= 1000 ? `${(m / 1000).toFixed(2)} km` : `${Math.round(m)} m`);
function updateStatus(next) {
  status = next;
  const fps = frameTimes.length > 1 ? ((frameTimes.length - 1) * 1000) / (frameTimes.at(-1) - frameTimes[0]) : 0;
  $("r-state").textContent = next.state;
  $("r-run").textContent = `ride: ${next.run_state.replace("_", " ")}`;
  $("r-fps").textContent = `${fps.toFixed(1)} fps`;
  $("r-render").textContent = `render ${next.render_ms.toFixed(1)} ms (host)`;
  $("r-zoom").textContent = `${next.zoom.toFixed(2)} m/px`;
  $("r-map").textContent = next.map_source === "city tiles"
    ? `city tiles: ${next.tiles_loaded} loaded, ${next.map_roads} roads, last load ${next.tile_load_ms.toFixed(1)} ms`
    : `${next.map_source}: ${next.map_roads} roads`;

  const slider = $("progress");
  slider.max = Math.round(next.total);
  if (!dragging) slider.value = Math.round(next.progress);
  $("progress-text").textContent = `${metres(next.progress)} of ${metres(next.total)} · engine ${metres(next.engine_progress)} · next turn ${metres(next.to_maneuver)}`;
  const auto = $("auto");
  auto.textContent = next.auto ? "❚❚ Stop following" : "▶ Auto-follow route";
  auto.classList.toggle("on", next.auto);
  $("phone").checked = next.phone;

  $("conn-dot").classList.toggle("off", !next.phone);
  $("conn-state").textContent = next.phone ? "Connected · ready for transfer" : "Disconnected";
  $("connect").disabled = next.phone;
  $("disconnect").disabled = !next.phone;
  $("app-status").textContent = `${next.state} · ${next.run_state.replace("_", " ")} · GPS ${next.gps_valid ? "fix" : "no fix"} · ${metres(next.remaining)} left`;
  const paused = next.run_state === "paused";
  $("cmd-pause").textContent = paused ? "▶ Resume" : "❚❚ Pause";

  if (next.route_id !== lastRouteId) {
    lastRouteId = next.route_id;
    buildTicks(next);
  }
  drawMinimap();
}

function buildTicks(next) {
  const ticks = $("ticks");
  const jumps = $("jumps");
  ticks.innerHTML = "";
  jumps.innerHTML = "";
  next.maneuvers.forEach((raw) => {
    const m = { ...raw, type: raw.type.toLowerCase() };
    const tick = document.createElement("div");
    tick.className = "tick";
    tick.style.left = `${(m.at / next.total) * 100}%`;
    tick.textContent = m.type === "destination" ? "🏁" : m.type.replace("_", " ");
    ticks.appendChild(tick);
    const button = document.createElement("button");
    const before = Math.max(0, m.at - 40);
    button.textContent = m.type === "destination" ? `40 m before arrival` : `40 m before ${m.type.replace("_", " ")} · ${m.street}`;
    button.onclick = () => seek(before);
    jumps.appendChild(button);
  });
  const start = document.createElement("button");
  start.textContent = "Route start";
  start.onclick = () => seek(0);
  jumps.prepend(start);
}

// ---------- bench controls ----------
let seekTimer = null;
function seek(m) {
  post("/api/seek", { metres: m }).catch(showRideError);
}
$("progress").addEventListener("input", (event) => {
  dragging = true;
  clearTimeout(seekTimer);
  seekTimer = setTimeout(() => seek(Number(event.target.value)), 40);
});
$("progress").addEventListener("change", (event) => {
  seek(Number(event.target.value));
  setTimeout(() => (dragging = false), 300);
});
$("auto").onclick = () => setting("auto", status && status.auto ? 0 : 1);
$("speed").onchange = (e) => setting("speed", e.target.value);
for (const [id, key] of [["gps", "gps"], ["phone", "phone"], ["offroute", "offroute"], ["noise", "noise"], ["smooth", "smooth"]]) {
  $(id).onchange = (e) => setting(key, e.target.checked ? 1 : 0);
}
$("theme").onchange = (e) => setting("theme", e.target.value);

function amsterdamEpochAt(hour) {
  // Today's date in Amsterdam at hour:00 local time, as a UTC epoch.
  const now = new Date();
  const offsetText = new Intl.DateTimeFormat("en-US", { timeZone: "Europe/Amsterdam", timeZoneName: "shortOffset" })
    .formatToParts(now).find((p) => p.type === "timeZoneName").value; // "GMT+1"
  const offset = Number((offsetText.match(/[+-]\d+/) || ["+1"])[0]);
  const parts = new Intl.DateTimeFormat("en-CA", { timeZone: "Europe/Amsterdam", year: "numeric", month: "2-digit", day: "2-digit" }).format(now);
  const [y, m, d] = parts.split("-").map(Number);
  return Date.UTC(y, m - 1, d, hour - offset, 0, 0);
}
$("clock").onchange = (e) => {
  const v = e.target.value;
  const value = v === "live" || v === "none" ? v : amsterdamEpochAt({ morning: 8, noon: 13, evening: 20 }[v]);
  setting("clock", value);
};
$("fps").onchange = (e) => (framePeriod = 1000 / Number(e.target.value));
function applyScale() {
  const v = $("scale").value;
  if (v === "actual") {
    // 2.8" diagonal 4:3 panel: 57.6 mm x 43.2 mm active area (CSS mm, so only exact on a calibrated display).
    screen.style.width = "57.6mm";
  } else {
    screen.style.width = `${320 * Number(v)}px`;
  }
}
$("scale").onchange = applyScale;
$("shot").onclick = () => {
  screen.toBlob((blob) => {
    const link = document.createElement("a");
    link.href = URL.createObjectURL(blob);
    link.download = `bikeos-screen-${new Date().toISOString().replace(/[:.]/g, "-")}.png`;
    link.click();
  });
};

// ---------- phone app ----------
let places = [];
let currentLocation = null;
let chooseStart = false;
let planStart = null;
let planDest = null;
let plan = null;
let nextTapIsStart = true;

function showPlanError(error) { const el = $("plan-error"); el.hidden = !error; el.textContent = error ? String(error.message || error) : ""; }
function showRideError(error) { const el = $("ride-error"); el.hidden = !error; el.textContent = error ? String(error.message || error) : ""; if (error) setTimeout(() => (el.hidden = true), 4000); }

function findPlace(query) {
  const q = query.trim().toLowerCase();
  if (!q) return null;
  return places.find((p) => p.name.toLowerCase() === q) || places.find((p) => p.name.toLowerCase().includes(q)) || null;
}
function refreshPlanButton() {
  const start = chooseStart ? planStart : currentLocation;
  $("calc").disabled = !(start && planDest);
}
$("dest-search").onclick = () => {
  const place = findPlace($("dest").value);
  showPlanError(place ? null : `No match for “${$("dest").value}” in the bundled Amsterdam area`);
  if (place) { planDest = [place.lat, place.lon]; $("dest").value = place.name; plan = null; $("route-summary").hidden = true; }
  refreshPlanButton(); drawMinimap();
};
$("start-search").onclick = () => {
  const place = findPlace($("start").value);
  showPlanError(place ? null : `No match for “${$("start").value}”`);
  if (place) { planStart = [place.lat, place.lon]; $("start").value = place.name; }
  refreshPlanButton(); drawMinimap();
};
$("dest").addEventListener("keydown", (e) => e.key === "Enter" && $("dest-search").click());
$("start").addEventListener("keydown", (e) => e.key === "Enter" && $("start-search").click());
$("seg-current").onclick = () => { chooseStart = false; $("seg-current").classList.add("on"); $("seg-choose").classList.remove("on"); $("start-row").hidden = true; refreshPlanButton(); drawMinimap(); };
$("seg-choose").onclick = () => { chooseStart = true; nextTapIsStart = !planStart; $("seg-choose").classList.add("on"); $("seg-current").classList.remove("on"); $("start-row").hidden = false; refreshPlanButton(); drawMinimap(); };

$("calc").onclick = async () => {
  showPlanError(null);
  $("calc").disabled = true;
  $("calc").textContent = "Calculating…";
  try {
    const start = chooseStart ? planStart : currentLocation;
    plan = await post("/api/route", { start, destination: planDest });
    const destName = $("dest").value || "Dropped pin";
    $("route-name").textContent = `To ${destName}`;
    $("route-meta").textContent = `${(plan.distance_m / 1000).toFixed(1)} km · ${plan.duration_min} min · OSM bike graph`;
    $("route-turns").innerHTML = plan.maneuvers.map((m) => `<li>${m.type} · ${m.street}</li>`).join("");
    $("route-summary").hidden = false;
  } catch (error) {
    showPlanError(error);
  }
  $("calc").textContent = "🚲 Calculate bike route";
  refreshPlanButton();
  drawMinimap();
};
$("send").onclick = async () => {
  try {
    await post("/api/send", { token: plan.token });
    $("conn-foot").textContent = `Sent route ${plan.token} (route only; map from the ESP's flash)`;
    plan = null; $("route-summary").hidden = true;
  } catch (error) { showPlanError(error); }
};
$("demo").onclick = async () => {
  try {
    await post("/api/send", { token: "demo" });
    $("conn-foot").textContent = "Demo route on the ESP32";
    plan = null; $("route-summary").hidden = true;
  } catch (error) { showPlanError(error); }
};
$("connect").onclick = () => setting("phone", 1);
$("disconnect").onclick = () => setting("phone", 0);
const command = (name) => post("/api/command", { name }).then(() => showRideError(null)).catch(showRideError);
$("cmd-start").onclick = () => command("start");
$("cmd-pause").onclick = () => command(status && status.run_state === "paused" ? "resume" : "pause");
$("cmd-stop").onclick = () => command("stop");
$("cmd-restart").onclick = () => command("restart");
$("cmd-offroute").onclick = () => command("offroute");

setInterval(() => {
  $("phone-time").textContent = new Date().toLocaleTimeString([], { hour: "numeric", minute: "2-digit" }).replace(/\s?[AP]M/, "");
}, 1000);

// ---------- route preview map ----------
let mapData = null;
const minimap = $("minimap");
const mctx = minimap.getContext("2d");
let project = null;
let unproject = null;

function setupProjection() {
  const [[minLat, minLon], [maxLat, maxLon]] = mapData.bounds;
  const k = Math.cos(((minLat + maxLat) / 2) * Math.PI / 180);
  const w = (maxLon - minLon) * k, h = maxLat - minLat;
  const pad = 10;
  const scale = Math.min((minimap.width - 2 * pad) / w, (minimap.height - 2 * pad) / h);
  const ox = (minimap.width - w * scale) / 2, oy = (minimap.height - h * scale) / 2;
  project = ([lat, lon]) => [ox + (lon - minLon) * k * scale, oy + (maxLat - lat) * scale];
  unproject = (x, y) => [maxLat - (y - oy) / scale, minLon + (x - ox) / (k * scale)];
}

function pin(point, color, label) {
  const [x, y] = project(point);
  mctx.fillStyle = color;
  mctx.beginPath(); mctx.arc(x, y - 18, 11, 0, Math.PI * 2); mctx.fill();
  mctx.beginPath(); mctx.moveTo(x - 6, y - 12); mctx.lineTo(x + 6, y - 12); mctx.lineTo(x, y); mctx.fill();
  mctx.fillStyle = "#fff"; mctx.beginPath(); mctx.arc(x, y - 18, 4, 0, Math.PI * 2); mctx.fill();
  mctx.font = "600 20px -apple-system, sans-serif"; mctx.fillStyle = "#000"; mctx.textAlign = "center";
  mctx.fillText(label, x, y + 20);
}

function polyline(points, color, width, dash) {
  if (!points || points.length < 2) return;
  mctx.strokeStyle = color; mctx.lineWidth = width; mctx.lineJoin = mctx.lineCap = "round";
  mctx.setLineDash(dash || []);
  mctx.beginPath();
  points.forEach((p, i) => { const [x, y] = project(p); i ? mctx.lineTo(x, y) : mctx.moveTo(x, y); });
  mctx.stroke();
  mctx.setLineDash([]);
}

function drawMinimap() {
  if (!mapData || !project) return;
  mctx.fillStyle = "#f1efe9"; mctx.fillRect(0, 0, minimap.width, minimap.height);
  for (const area of mapData.areas) {
    mctx.fillStyle = area.c === 0 ? "#a9d3f5" : "#c9e8b0";
    mctx.beginPath();
    area.p.forEach((p, i) => { const [x, y] = project(p); i ? mctx.lineTo(x, y) : mctx.moveTo(x, y); });
    mctx.fill();
  }
  for (const road of mapData.roads) {
    const major = road.c <= 2, bike = road.c === 5;
    polyline(road.p, bike ? "#7cc7bd" : major ? "#fbd38d" : "#ffffff", bike ? 1.5 : major ? 6 : 3);
  }
  if (status && status.route) polyline(status.route, "#0a84ff", 7);
  if (plan) polyline(plan.points, "#0a84ff", 6, [10, 8]);
  const start = chooseStart ? planStart : currentLocation;
  if (start) pin(start, "#34c759", chooseStart ? "Start" : "You");
  if (planDest) pin(planDest, "#ff3b30", "Destination");
  if (status && status.rider) {
    const [x, y] = project(status.rider);
    mctx.fillStyle = "#fff"; mctx.beginPath(); mctx.arc(x, y, 10, 0, Math.PI * 2); mctx.fill();
    mctx.fillStyle = "#0a84ff"; mctx.beginPath(); mctx.arc(x, y, 7, 0, Math.PI * 2); mctx.fill();
  }
}

minimap.addEventListener("click", (event) => {
  if (!unproject) return;
  const rect = minimap.getBoundingClientRect();
  const point = unproject((event.clientX - rect.left) * (minimap.width / rect.width), (event.clientY - rect.top) * (minimap.height / rect.height));
  if (chooseStart && nextTapIsStart) {
    planStart = point; $("start").value = "Dropped pin";
  } else {
    planDest = point; $("dest").value = "Dropped pin";
  }
  if (chooseStart) nextTapIsStart = !nextTapIsStart;
  plan = null; $("route-summary").hidden = true;
  refreshPlanButton(); drawMinimap();
});

// ---------- boot ----------
(async function boot() {
  applyScale();
  const placeData = await (await fetch("/api/places")).json();
  places = placeData.places;
  currentLocation = [placeData.current.lat, placeData.current.lon];
  $("places").innerHTML = places.map((p) => `<option value="${p.name}">`).join("");
  mapData = await (await fetch("/api/map")).json();
  setupProjection();
  refreshPlanButton();
  frameLoop();
})();
