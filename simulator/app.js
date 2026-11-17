const canvas = document.querySelector("#screen");
const ctx = canvas.getContext("2d");

const ui = {
  stateSelect: document.querySelector("#stateSelect"),
  playButton: document.querySelector("#playButton"),
  playIcon: document.querySelector("#playIcon"),
  playLabel: document.querySelector("#playLabel"),
  nextButton: document.querySelector("#nextButton"),
  gpsToggle: document.querySelector("#gpsToggle"),
  bleToggle: document.querySelector("#bleToggle"),
  distance: document.querySelector("#distanceReadout"),
  route: document.querySelector("#routeReadout"),
  state: document.querySelector("#stateReadout"),
  eventTime: document.querySelector("#eventTime"),
  eventText: document.querySelector("#eventText"),
  frameStatus: document.querySelector("#frameStatus"),
};

const palette = {
  paper: "#d9dec6",
  ink: "#10150f",
  muted: "#5f6759",
};

const rideEvents = [
  { distance: 120, routeLeft: 3.8, street: "Weteringschans", turn: "right", message: "Route started — heading toward Museumplein" },
  { distance: 85, routeLeft: 3.7, street: "Weteringschans", turn: "right", message: "Next maneuver: RIGHT" },
  { distance: 30, routeLeft: 3.6, street: "Weteringschans", turn: "right", message: "Approaching turn" },
  { distance: 8, routeLeft: 3.5, street: "Weteringschans", turn: "right", message: "Turn now" },
  { distance: 260, routeLeft: 3.4, street: "Stadhouderskade", turn: "straight", message: "Maneuver passed" },
  { distance: 95, routeLeft: 2.9, street: "Hobbemastraat", turn: "left", message: "Continue toward Museumplein" },
  { distance: 40, routeLeft: 2.7, street: "Hobbemastraat", turn: "left", message: "Next maneuver: LEFT" },
  { distance: 0, routeLeft: 0, street: "Museumplein", turn: "arrive", message: "Destination reached" },
];

const stateCopy = {
  startup: ["BIKEOS", "STARTING", "Loading navigation system"],
  "waiting-gps": ["GPS", "SEARCHING", "Move outdoors for a clear view"],
  "waiting-route": ["READY", "NO ROUTE", "Choose a destination on iPhone"],
  "off-route": ["OFF ROUTE", "38 m away", "Return toward the route"],
  rerouting: ["REROUTING", "REQUEST SENT", "Continue safely while waiting"],
  "phone-disconnected": ["NO PHONE", "ROUTE SAVED", "Navigation continues offline"],
  "gps-unavailable": ["NO GPS", "POSITION STALE", "Stop if guidance is unclear"],
  arrived: ["ARRIVED", "MUSEUMPLEIN", "Ride complete"],
  error: ["ERROR", "ROUTE INVALID", "Open the BikeOS app"],
};

const sim = {
  state: "navigating",
  eventIndex: 0,
  playing: true,
  gps: true,
  ble: true,
  elapsed: 0,
  frame: 0,
  lastTick: performance.now(),
};

function setupContext() {
  ctx.imageSmoothingEnabled = false;
  ctx.textBaseline = "alphabetic";
}

function clearScreen() {
  ctx.fillStyle = palette.paper;
  ctx.fillRect(0, 0, 400, 240);
  ctx.fillStyle = palette.ink;
}

function setFont(size, weight = 700, family = "Arial, sans-serif") {
  ctx.font = `${weight} ${size}px ${family}`;
}

function fitText(text, maxWidth, initialSize, minSize = 10, weight = 700) {
  let size = initialSize;
  while (size > minSize) {
    setFont(size, weight);
    if (ctx.measureText(text).width <= maxWidth) break;
    size -= 1;
  }
  return size;
}

function drawStatusBar() {
  ctx.fillStyle = palette.ink;
  ctx.fillRect(0, 0, 400, 28);
  ctx.fillStyle = palette.paper;
  setFont(11, 800, "monospace");
  ctx.fillText("BIKEOS", 10, 18);

  drawGpsIcon(294, 7, sim.gps);
  drawBleIcon(330, 6, sim.ble);

  ctx.strokeStyle = palette.paper;
  ctx.lineWidth = 2;
  ctx.strokeRect(366, 8, 23, 11);
  ctx.fillRect(389, 11, 3, 5);
  ctx.fillRect(369, 11, 15, 5);
}

function drawGpsIcon(x, y, active) {
  ctx.strokeStyle = palette.paper;
  ctx.fillStyle = palette.paper;
  ctx.lineWidth = 2;
  ctx.beginPath();
  ctx.arc(x + 8, y + 7, 3, 0, Math.PI * 2);
  active ? ctx.fill() : ctx.stroke();
  ctx.beginPath();
  ctx.arc(x + 8, y + 7, 7, Math.PI * 1.08, Math.PI * 1.92);
  ctx.stroke();
  ctx.beginPath();
  ctx.arc(x + 8, y + 7, 11, Math.PI * 1.12, Math.PI * 1.88);
  ctx.stroke();
  if (!active) {
    ctx.beginPath();
    ctx.moveTo(x, y + 1);
    ctx.lineTo(x + 17, y + 17);
    ctx.stroke();
  }
}

function drawBleIcon(x, y, active) {
  ctx.strokeStyle = palette.paper;
  ctx.lineWidth = 2;
  ctx.beginPath();
  ctx.moveTo(x + 7, y);
  ctx.lineTo(x + 14, y + 6);
  ctx.lineTo(x + 4, y + 14);
  ctx.lineTo(x + 14, y + 21);
  ctx.lineTo(x + 7, y + 27);
  ctx.closePath();
  ctx.moveTo(x + 7, y);
  ctx.lineTo(x + 7, y + 27);
  ctx.stroke();
  if (!active) {
    ctx.beginPath();
    ctx.moveTo(x, y + 3);
    ctx.lineTo(x + 18, y + 23);
    ctx.stroke();
  }
}

function drawTurnArrow(type) {
  ctx.save();
  ctx.translate(78, 116);
  ctx.strokeStyle = palette.ink;
  ctx.fillStyle = palette.ink;
  ctx.lineWidth = 17;
  ctx.lineCap = "square";
  ctx.lineJoin = "miter";

  if (type === "left" || type === "right") {
    const dir = type === "right" ? 1 : -1;
    ctx.beginPath();
    ctx.moveTo(0, 67);
    ctx.lineTo(0, -16);
    ctx.quadraticCurveTo(0, -42, 26 * dir, -42);
    ctx.lineTo(48 * dir, -42);
    ctx.stroke();
    ctx.beginPath();
    ctx.moveTo(48 * dir, -67);
    ctx.lineTo(79 * dir, -42);
    ctx.lineTo(48 * dir, -17);
    ctx.closePath();
    ctx.fill();
  } else if (type === "straight") {
    ctx.beginPath();
    ctx.moveTo(0, 68);
    ctx.lineTo(0, -40);
    ctx.stroke();
    ctx.beginPath();
    ctx.moveTo(-27, -32);
    ctx.lineTo(0, -67);
    ctx.lineTo(27, -32);
    ctx.closePath();
    ctx.fill();
  } else {
    ctx.beginPath();
    ctx.arc(0, 0, 46, 0, Math.PI * 2);
    ctx.fill();
    ctx.fillStyle = palette.paper;
    ctx.beginPath();
    ctx.arc(0, 0, 20, 0, Math.PI * 2);
    ctx.fill();
  }
  ctx.restore();
}

function drawNavigation() {
  const event = rideEvents[sim.eventIndex];
  drawStatusBar();
  drawTurnArrow(event.turn);

  ctx.fillStyle = palette.ink;
  ctx.fillRect(170, 42, 2, 143);
  setFont(12, 800, "monospace");
  ctx.fillText(event.distance <= 10 && event.distance > 0 ? "TURN NOW" : "NEXT TURN", 190, 55);

  const distanceText = event.distance === 0 ? "HERE" : `${event.distance} m`;
  const size = fitText(distanceText, 195, 48, 32, 800);
  setFont(size, 800);
  ctx.fillText(distanceText, 188, 111);

  ctx.fillRect(188, 124, 190, 2);
  const streetSize = fitText(event.street.toUpperCase(), 190, 19, 12, 800);
  setFont(streetSize, 800);
  ctx.fillText(event.street.toUpperCase(), 188, 154);

  setFont(12, 700, "monospace");
  ctx.fillText(`${event.routeLeft.toFixed(1)} KM LEFT`, 188, 177);

  ctx.fillRect(0, 196, 400, 44);
  ctx.fillStyle = palette.paper;
  setFont(12, 800, "monospace");
  const routeState = sim.ble ? "ON ROUTE" : "ROUTE SAVED";
  ctx.fillText(routeState, 12, 223);
  ctx.textAlign = "right";
  ctx.fillText(sim.gps ? "GPS  3 m" : "GPS  --", 388, 223);
  ctx.textAlign = "left";
}

function drawCenteredState(state) {
  const [title, headline, detail] = stateCopy[state];
  drawStatusBar();
  ctx.fillStyle = palette.ink;
  ctx.textAlign = "center";
  setFont(12, 800, "monospace");
  ctx.fillText(title, 200, 66);
  ctx.fillRect(156, 78, 88, 3);
  const headlineSize = fitText(headline, 350, 38, 24, 800);
  setFont(headlineSize, 800);
  ctx.fillText(headline, 200, 133);
  const detailSize = fitText(detail, 355, 17, 12, 600);
  setFont(detailSize, 600);
  ctx.fillText(detail, 200, 165);

  if (["waiting-gps", "rerouting", "startup"].includes(state)) {
    const phase = Math.floor(sim.elapsed * 2) % 4;
    for (let i = 0; i < 4; i += 1) {
      ctx.fillStyle = i <= phase ? palette.ink : palette.muted;
      ctx.fillRect(169 + i * 18, 190, 10, 10);
    }
  } else {
    ctx.strokeStyle = palette.ink;
    ctx.lineWidth = 2;
    ctx.strokeRect(110, 187, 180, 24);
    ctx.fillStyle = palette.ink;
    setFont(11, 800, "monospace");
    ctx.fillText(state === "arrived" ? "RIDE COMPLETE" : "STATUS", 200, 203);
  }
  ctx.textAlign = "left";
}

function drawScreen() {
  clearScreen();
  if (sim.state === "navigating") {
    drawNavigation();
  } else {
    drawCenteredState(sim.state);
  }
  sim.frame += 1;
  ui.frameStatus.textContent = `FRAME ${String(sim.frame).padStart(4, "0")}`;
}

function setToggle(button, on) {
  button.classList.toggle("is-on", on);
  button.setAttribute("aria-pressed", String(on));
}

function updateReadouts(message) {
  const event = rideEvents[sim.eventIndex];
  ui.distance.textContent = event.distance === 0 ? "Arrived" : `${event.distance} m`;
  ui.route.textContent = `${event.routeLeft.toFixed(1)} km`;
  ui.state.textContent = sim.state === "navigating" ? "ON ROUTE" : sim.state.replaceAll("-", " ").toUpperCase();
  ui.eventTime.textContent = new Date(sim.elapsed * 1000).toISOString().slice(14, 19);
  if (message) ui.eventText.textContent = message;
}

function chooseState(state, message) {
  sim.state = state;
  ui.stateSelect.value = state;
  updateReadouts(message || `Display changed to ${state.replaceAll("-", " ")}`);
  drawScreen();
}

function nextEvent() {
  sim.eventIndex = (sim.eventIndex + 1) % rideEvents.length;
  const event = rideEvents[sim.eventIndex];
  chooseState(event.turn === "arrive" ? "arrived" : "navigating", event.message);
}

function togglePlay() {
  sim.playing = !sim.playing;
  ui.playIcon.textContent = sim.playing ? "Ⅱ" : "▶";
  ui.playLabel.textContent = sim.playing ? "Pause ride" : "Resume ride";
}

ui.stateSelect.addEventListener("change", (event) => chooseState(event.target.value));
ui.playButton.addEventListener("click", togglePlay);
ui.nextButton.addEventListener("click", nextEvent);

ui.gpsToggle.addEventListener("click", () => {
  sim.gps = !sim.gps;
  setToggle(ui.gpsToggle, sim.gps);
  chooseState(sim.gps ? "navigating" : "gps-unavailable", sim.gps ? "GPS fix restored" : "GPS signal lost");
});

ui.bleToggle.addEventListener("click", () => {
  sim.ble = !sim.ble;
  setToggle(ui.bleToggle, sim.ble);
  chooseState(sim.ble ? "navigating" : "phone-disconnected", sim.ble ? "Phone reconnected" : "Phone disconnected — saved route remains active");
});

function tick(now) {
  const delta = Math.min((now - sim.lastTick) / 1000, 0.25);
  sim.lastTick = now;
  if (sim.playing) {
    sim.elapsed += delta;
    if (sim.state === "navigating" && Math.floor(sim.elapsed) > 0 && Math.floor(sim.elapsed) % 6 === 0) {
      const currentSecond = Math.floor(sim.elapsed);
      if (tick.lastEventSecond !== currentSecond) {
        tick.lastEventSecond = currentSecond;
        nextEvent();
      }
    }
  }
  if (["waiting-gps", "rerouting", "startup"].includes(sim.state)) drawScreen();
  updateReadouts();
  requestAnimationFrame(tick);
}

setupContext();
updateReadouts(rideEvents[0].message);
drawScreen();
requestAnimationFrame(tick);
