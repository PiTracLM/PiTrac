import * as THREE from "three";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";

const container = document.getElementById("sim-view");
const emptyEl = document.getElementById("sim-empty");
const replayBtn = document.getElementById("sim-replay");
const imagesPanel = document.getElementById("sim-images-panel");
const imagesList = document.getElementById("sim-images-list");

const scene = new THREE.Scene();
scene.background = new THREE.Color(0x0b1018);
scene.fog = new THREE.Fog(0x0b1018, 200, 600);

const CAMERA_HOME = new THREE.Vector3(0, 30, -55);
const CAMERA_TARGET = new THREE.Vector3(0, 0, 100);

const camera = new THREE.PerspectiveCamera(42, 1, 0.5, 2000);

const renderer = new THREE.WebGLRenderer({ antialias: true });
renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
container.appendChild(renderer.domElement);

const controls = new OrbitControls(camera, renderer.domElement);
controls.enableDamping = true;
controls.dampingFactor = 0.08;
controls.minDistance = 8;
controls.maxDistance = 800;
controls.maxPolarAngle = Math.PI / 2 - 0.05;

function resetCamera() {
  camera.position.copy(CAMERA_HOME);
  camera.up.set(0, 1, 0);
  controls.target.copy(CAMERA_TARGET);
  camera.lookAt(CAMERA_TARGET);
  controls.update();
}
resetCamera();
window.addEventListener("pageshow", resetCamera);

scene.add(new THREE.AmbientLight(0x99aabb, 0.5));
const sun = new THREE.DirectionalLight(0xffffff, 0.8);
sun.position.set(-80, 120, 80);
scene.add(sun);

const ground = new THREE.Mesh(
  new THREE.PlaneGeometry(200, 450),
  new THREE.MeshStandardMaterial({ color: 0x16221a, roughness: 1.0 }),
);
ground.rotation.x = -Math.PI / 2;
ground.position.z = 175;
scene.add(ground);

const minorArcMat = new THREE.LineBasicMaterial({ color: 0x2f4a3a, transparent: true, opacity: 0.40 });
const majorArcMat = new THREE.LineBasicMaterial({ color: 0x4a7358, transparent: true, opacity: 0.70 });

function makeArc(radius, isMajor) {
  const segs = 48;
  const halfAngle = Math.PI / 10;
  const pts = [];
  for (let i = 0; i <= segs; i++) {
    const a = -halfAngle + (2 * halfAngle) * (i / segs);
    pts.push(new THREE.Vector3(Math.sin(a) * radius, 0.02, Math.cos(a) * radius));
  }
  return new THREE.Line(
    new THREE.BufferGeometry().setFromPoints(pts),
    isMajor ? majorArcMat : minorArcMat,
  );
}

for (let r = 25; r <= 325; r += 25) {
  const arc = makeArc(r, r % 50 === 0);
  arc.geometry.computeBoundingSphere();
  scene.add(arc);
}

function makeYardLabel(text) {
  const canvas = document.createElement("canvas");
  canvas.width = 192; canvas.height = 96;
  const ctx = canvas.getContext("2d");
  ctx.fillStyle = "rgba(200, 220, 230, 0.85)";
  ctx.font = "700 60px -apple-system, system-ui, sans-serif";
  ctx.textAlign = "center";
  ctx.textBaseline = "middle";
  ctx.fillText(text, 96, 48);
  const tex = new THREE.CanvasTexture(canvas);
  tex.anisotropy = 4;
  const sprite = new THREE.Sprite(new THREE.SpriteMaterial({
    map: tex, transparent: true, depthWrite: false,
  }));
  sprite.scale.set(10, 5, 1);
  return sprite;
}

for (let r = 50; r <= 300; r += 50) {
  const s = makeYardLabel(String(r));
  s.position.set(0, 1.8, r);
  scene.add(s);
}

const targetLine = new THREE.Line(
  new THREE.BufferGeometry().setFromPoints([
    new THREE.Vector3(0, 0.02, 0),
    new THREE.Vector3(0, 0.02, 400),
  ]),
  new THREE.LineDashedMaterial({
    color: 0x4a6070, dashSize: 2, gapSize: 2, transparent: true, opacity: 0.4,
  }),
);
targetLine.computeLineDistances();
scene.add(targetLine);

scene.add(new THREE.Mesh(
  new THREE.CylinderGeometry(0.4, 0.4, 0.08, 24),
  new THREE.MeshStandardMaterial({ color: 0x7fdbff, emissive: 0x2a4a5a, roughness: 0.4 }),
));

const ball = new THREE.Mesh(
  new THREE.SphereGeometry(0.6, 24, 16),
  new THREE.MeshStandardMaterial({ color: 0xffffff, emissive: 0x7fdbff, emissiveIntensity: 0.4 }),
);
ball.visible = false;
scene.add(ball);

const sceneShot = {
  pathVecs: [],
  tS: [],
  apexIndex: 0,
  flightLastIndex: 0,
  totalDurS: 0,
  flightOutline: null,
  rollOutline: null,
  flightTracer: null,
  rollTracer: null,
  apexDot: null,
  apexDrop: null,
  landRing: null,
};

let speedMul = 1.0;
let animStart = 0;
let animComplete = false;

function clearSceneShot() {
  for (const key of ["flightOutline", "rollOutline", "flightTracer", "rollTracer", "shadowOutline", "shadowTracer", "apexDot", "apexDrop", "landRing"]) {
    const obj = sceneShot[key];
    if (obj) {
      scene.remove(obj);
      if (obj.geometry) obj.geometry.dispose();
      sceneShot[key] = null;
    }
  }
  ball.visible = false;
}

function projectToGround(vecs) {
  return vecs.map(v => new THREE.Vector3(v.x, 0.015, v.z));
}

function buildShot(data) {
  clearSceneShot();

  sceneShot.pathVecs = data.path_yd.map(p => new THREE.Vector3(p[0], p[2], p[1]));
  sceneShot.tS = data.t_s;
  sceneShot.apexIndex = data.apex_index;
  sceneShot.flightLastIndex = data.flight_last_index;
  sceneShot.totalDurS = data.t_s[data.t_s.length - 1];

  const flightSlice = sceneShot.pathVecs.slice(0, sceneShot.flightLastIndex + 1);
  sceneShot.flightOutline = new THREE.Line(
    new THREE.BufferGeometry().setFromPoints(flightSlice),
    new THREE.LineBasicMaterial({ color: 0x7fdbff, transparent: true, opacity: 0.15 }),
  );
  scene.add(sceneShot.flightOutline);

  sceneShot.shadowOutline = new THREE.Line(
    new THREE.BufferGeometry().setFromPoints(projectToGround(flightSlice)),
    new THREE.LineBasicMaterial({ color: 0x000000, transparent: true, opacity: 0.35 }),
  );
  scene.add(sceneShot.shadowOutline);

  if (sceneShot.pathVecs.length > sceneShot.flightLastIndex + 1) {
    const rollSlice = sceneShot.pathVecs.slice(sceneShot.flightLastIndex);
    sceneShot.rollOutline = new THREE.Line(
      new THREE.BufferGeometry().setFromPoints(rollSlice),
      new THREE.LineBasicMaterial({ color: 0x89d185, transparent: true, opacity: 0.22 }),
    );
    scene.add(sceneShot.rollOutline);
  }

  const apexVec = sceneShot.pathVecs[sceneShot.apexIndex];
  sceneShot.apexDot = new THREE.Mesh(
    new THREE.SphereGeometry(0.4, 16, 12),
    new THREE.MeshBasicMaterial({ color: 0xffe066, transparent: true, opacity: 0.7 }),
  );
  sceneShot.apexDot.position.copy(apexVec);
  scene.add(sceneShot.apexDot);

  sceneShot.apexDrop = new THREE.Line(
    new THREE.BufferGeometry().setFromPoints([
      apexVec, new THREE.Vector3(apexVec.x, 0.02, apexVec.z),
    ]),
    new THREE.LineBasicMaterial({ color: 0xffe066, transparent: true, opacity: 0.25 }),
  );
  scene.add(sceneShot.apexDrop);

  const landPt = sceneShot.pathVecs[sceneShot.pathVecs.length - 1];
  sceneShot.landRing = new THREE.Mesh(
    new THREE.RingGeometry(0.8, 1.4, 32),
    new THREE.MeshBasicMaterial({ color: 0xff6b6b, side: THREE.DoubleSide, transparent: true, opacity: 0.85 }),
  );
  sceneShot.landRing.rotation.x = -Math.PI / 2;
  sceneShot.landRing.position.set(landPt.x, 0.03, landPt.z);
  scene.add(sceneShot.landRing);

  ball.visible = true;
  replayBtn.disabled = false;
  emptyEl.classList.add("hidden");
  startAnim();
}

function buildTracer(n) {
  for (const key of ["flightTracer", "rollTracer", "shadowTracer"]) {
    if (sceneShot[key]) {
      scene.remove(sceneShot[key]);
      sceneShot[key].geometry.dispose();
      sceneShot[key] = null;
    }
  }

  const flightEnd = Math.min(n, sceneShot.flightLastIndex);
  if (flightEnd >= 1) {
    const sub = sceneShot.pathVecs.slice(0, flightEnd + 1);
    const curve = new THREE.CatmullRomCurve3(sub);
    const geo = new THREE.TubeGeometry(curve, Math.max(sub.length * 2, 4), 0.22, 8, false);
    sceneShot.flightTracer = new THREE.Mesh(geo, new THREE.MeshStandardMaterial({
      color: 0x7fdbff, emissive: 0x3a7a9a, roughness: 0.3, metalness: 0.1,
    }));
    scene.add(sceneShot.flightTracer);

    sceneShot.shadowTracer = new THREE.Line(
      new THREE.BufferGeometry().setFromPoints(projectToGround(sub)),
      new THREE.LineBasicMaterial({ color: 0x000000, transparent: true, opacity: 0.55 }),
    );
    scene.add(sceneShot.shadowTracer);
  }

  if (n > sceneShot.flightLastIndex) {
    const sub = sceneShot.pathVecs.slice(sceneShot.flightLastIndex, n + 1);
    if (sub.length >= 2) {
      sceneShot.rollTracer = new THREE.Line(
        new THREE.BufferGeometry().setFromPoints(sub),
        new THREE.LineBasicMaterial({ color: 0x9ee89a }),
      );
      scene.add(sceneShot.rollTracer);
    }
  }
}

function indexAtTime(cur) {
  const { tS } = sceneShot;
  if (!tS.length || cur <= tS[0]) return 0;
  if (cur >= sceneShot.totalDurS) return tS.length - 1;
  let i = 1;
  while (i < tS.length && tS[i] < cur) i++;
  return i - 1;
}

function startAnim() {
  animStart = performance.now();
  animComplete = false;
}

function frame(now) {
  if (sceneShot.pathVecs.length > 0) {
    const elapsed = ((now - animStart) / 1000.0) * speedMul;
    const cur = Math.min(sceneShot.totalDurS, Math.max(0, elapsed));
    const idx = indexAtTime(cur);
    const pt = sceneShot.pathVecs[idx];
    if (pt) {
      buildTracer(idx);
      ball.position.copy(pt);
      ball.material.emissiveIntensity = cur < sceneShot.totalDurS ? 0.6 : 0.15;
    }
    if (cur >= sceneShot.totalDurS && !animComplete) {
      animComplete = true;
    }
  }
  controls.update();
  renderer.render(scene, camera);
  requestAnimationFrame(frame);
}

function resize() {
  const rect = container.getBoundingClientRect();
  const w = Math.max(1, Math.floor(rect.width));
  const h = Math.max(1, Math.floor(rect.height));
  renderer.setSize(w, h, false);
  camera.aspect = w / h;
  camera.updateProjectionMatrix();
}

const ro = new ResizeObserver(resize);
ro.observe(container);

function fmtSigned(v) { return v > 0 ? "+" + v : String(v); }

function setMetrics(inputs, metrics) {
  if (inputs) {
    document.getElementById("sm-bs").textContent = inputs.ball_speed_mph?.toFixed?.(1) ?? inputs.ball_speed_mph;
    document.getElementById("sm-la").textContent = inputs.launch_angle_deg?.toFixed?.(1) ?? inputs.launch_angle_deg;
    document.getElementById("sm-ld").textContent = fmtSigned(+(inputs.launch_direction_deg ?? 0).toFixed(1));
    document.getElementById("sm-bs-spin").textContent = inputs.back_spin_rpm;
    document.getElementById("sm-ss-spin").textContent = fmtSigned(inputs.side_spin_rpm);
  }
  if (metrics) {
    document.getElementById("sm-carry").textContent = metrics.carry_yd;
    document.getElementById("sm-apex").textContent = metrics.apex_ft;
    document.getElementById("sm-land-a").textContent = metrics.landing_angle_deg;
    document.getElementById("sm-land-s").textContent = metrics.landing_speed_mph;
    document.getElementById("sm-flight-t").textContent = metrics.flight_time_s;
    document.getElementById("sm-roll").textContent = metrics.roll_yd;
    document.getElementById("sm-total").textContent = metrics.total_yd;
    document.getElementById("sm-side").textContent = fmtSigned(metrics.side_total_yd);
  }
}

function addShotImage(src, caption) {
  const wrap = document.createElement("div");
  if (src) {
    const img = document.createElement("img");
    img.src = src;
    img.className = "sim-img-thumb";
    img.onclick = () => window.open(src, "_blank");
    wrap.appendChild(img);
  }
  if (caption) {
    const cap = document.createElement("div");
    cap.className = "sim-img-caption";
    cap.textContent = caption;
    wrap.appendChild(cap);
  }
  imagesList.appendChild(wrap);
  imagesPanel.classList.add("visible");
}

function clearShotImages() {
  imagesList.innerHTML = "";
  imagesPanel.classList.remove("visible");
}

document.querySelectorAll(".sim-tab").forEach(btn => {
  btn.addEventListener("click", () => {
    document.querySelectorAll(".sim-tab").forEach(b => b.classList.remove("active"));
    document.querySelectorAll(".sim-panel").forEach(p => p.classList.remove("active"));
    btn.classList.add("active");
    document.getElementById("sim-tab-" + btn.dataset.tab).classList.add("active");
  });
});

document.querySelectorAll('input[name="sim-speed"]').forEach(el => {
  el.addEventListener("change", e => {
    speedMul = parseFloat(e.target.value);
    startAnim();
  });
});

replayBtn.addEventListener("click", startAnim);

const statusEl = document.getElementById("sim-status");
const statusTitle = document.getElementById("sim-status-title");
const statusMsg = document.getElementById("sim-status-msg");
const STATUS_CLASSES = ["initializing", "waiting", "stabilizing", "ready", "hit", "error"];

function setStatus(state, title, msg) {
  statusEl.classList.remove("hidden", ...STATUS_CLASSES);
  if (state) statusEl.classList.add(state);
  statusTitle.textContent = title;
  statusMsg.textContent = msg;
}

function updateBallStatus(resultType, message, isPiTracRunning) {
  if (isPiTracRunning === false) {
    setStatus("error", "System Stopped", "PiTrac is not running — click Start to begin");
    return;
  }
  if (!resultType) {
    setStatus(null, "System Status", message || "—");
    return;
  }
  const t = resultType.toLowerCase();
  if (t.includes("initializing")) {
    setStatus("initializing", "System Initializing", message || "Starting up PiTrac…");
  } else if (t.includes("waiting for ball")) {
    setStatus("waiting", "Waiting for Ball", message || "Please place ball on tee");
  } else if (t.includes("waiting for simulator")) {
    setStatus("waiting", "Waiting for Simulator", message || "Waiting for simulator to be ready");
  } else if (t.includes("pausing") || t.includes("stabilization")) {
    setStatus("stabilizing", "Ball Detected", message || "Waiting for ball to stabilize…");
  } else if (t.includes("ball ready") || t.includes("ready")) {
    setStatus("ready", "Ready to Hit!", message || "Ball is ready, take your shot");
  } else if (t.includes("hit")) {
    setStatus("hit", "Ball Hit!", message || "Processing shot data…");
  } else if (t.includes("multiple balls")) {
    setStatus("error", "Multiple Balls Detected", message || "Please remove extra balls");
  } else if (t.includes("error")) {
    setStatus("error", "Error", message || "An error occurred");
  } else {
    setStatus(null, "System Status", message || resultType);
  }
}

function connectWs() {
  const proto = location.protocol === "https:" ? "wss:" : "ws:";
  const ws = new WebSocket(`${proto}//${location.host}/ws`);
  ws.onmessage = (evt) => {
    let data;
    try { data = JSON.parse(evt.data); } catch { return; }
    if (data.type === "shot_trajectory") {
      clearShotImages();
      setMetrics(data.inputs, data.metrics);
      buildShot(data);
    } else if (data.type === "image_ready" && data.filename) {
      const ts = Date.now();
      addShotImage(`/images/${data.filename}?t=${ts}`, data.caption || null);
    } else if (data.result_type !== undefined || data.pitrac_running !== undefined) {
      updateBallStatus(data.result_type, data.message, data.pitrac_running);
    }
  };
  ws.onclose = () => setTimeout(connectWs, 2000);
}
connectWs();

updateBallStatus(null, null, false);

(function wrapPiTracStatusPoll() {
  if (typeof window === "undefined") return;
  if (typeof window.checkPiTracStatus !== "function") return;
  const original = window.checkPiTracStatus;
  window.checkPiTracStatus = async function () {
    const isRunning = await original();
    if (!isRunning) updateBallStatus(null, null, false);
    return isRunning;
  };
})();

window.addEventListener("resize", resize);
resize();
requestAnimationFrame(frame);
