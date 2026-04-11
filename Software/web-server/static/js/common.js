// Common functionality for all PiTrac pages

if (typeof lucide !== 'undefined') {
  lucide.createIcons();
}

// -- Theme --

let currentTheme = "system";

function getSystemTheme() {
  return window.matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "light";
}

function applyTheme(theme) {
  const root = document.documentElement;
  document.querySelectorAll(".theme-btn").forEach(b => b.classList.remove("active"));

  if (theme === "system") {
    root.setAttribute("data-theme", getSystemTheme());
    const btn = document.querySelector('.theme-btn[data-theme="system"]');
    if (btn) btn.classList.add("active");
  } else {
    root.setAttribute("data-theme", theme);
    const btn = document.querySelector(`.theme-btn[data-theme="${theme}"]`);
    if (btn) btn.classList.add("active");
  }
}

function setTheme(theme) {
  currentTheme = theme;
  localStorage.setItem("pitrac-theme", theme);
  applyTheme(theme);
}

function initTheme() {
  applyTheme(localStorage.getItem("pitrac-theme") || "system");
}

window.matchMedia("(prefers-color-scheme: dark)").addEventListener("change", () => {
  if (currentTheme === "system") applyTheme("system");
});

// -- PiTrac Controls --

function getButtons(action) {
  return ["desktop", "mobile"]
    .map(s => document.getElementById(`pitrac-${action}-btn-${s}`))
    .filter(Boolean);
}

async function controlPiTrac(action) {
  if ((action === "start" || action === "restart") && !(await requireStrobeSafe()))
    return;

  document.querySelectorAll(".control-btn").forEach(b => { b.disabled = true; });
  getButtons(action).forEach(b => b.classList.add("loading"));

  try {
    const res = await fetch(`/api/pitrac/${action}`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
    });
    if (!res.ok) {
      const err = await res.json();
      throw new Error(err.error || `Failed to ${action} PiTrac`);
    }
    const data = await res.json();
    if (typeof showStatusMessage === "function") showStatusMessage(data.message, "success");
    setTimeout(() => { if (typeof checkSystemStatus === "function") checkSystemStatus(); }, 2000);
  } catch (err) {
    console.error(`Error ${action}ing PiTrac:`, err);
    if (typeof showStatusMessage === "function") showStatusMessage(err.message, "error");
  } finally {
    getButtons(action).forEach(b => b.classList.remove("loading"));
    setTimeout(() => {
      if (typeof checkPiTracStatus === "function") {
        checkPiTracStatus();
      } else {
        document.querySelectorAll(".control-btn").forEach(b => { b.disabled = false; });
      }
    }, 1000);
  }
}

function updatePiTracButtons(isRunning) {
  const show = (btns) => btns.forEach(b => { b.style.display = ""; b.disabled = false; });
  const hide = (btns) => btns.forEach(b => { b.style.display = "none"; });

  if (isRunning) {
    hide(getButtons("start"));
    show(getButtons("stop"));
    show(getButtons("restart"));
  } else {
    show(getButtons("start"));
    hide(getButtons("stop"));
    hide(getButtons("restart"));
  }
}

// -- Status Polling --

async function checkSystemStatus() {
  try {
    const res = await fetch("/health");
    if (res.ok) return await res.json();
  } catch (e) {
    console.error("Error checking system status:", e);
  }
  return null;
}

async function checkPiTracStatus() {
  try {
    const res = await fetch("/api/pitrac/status");
    const status = await res.json();
    updatePiTracButtons(status.is_running);

    const dot = document.getElementById("pitrac-status-dot");
    if (dot) {
      dot.classList.toggle("connected", !!status.pid);
      dot.classList.toggle("disconnected", !status.pid);
      dot.title = status.pid ? `PiTrac Running (PID: ${status.pid})` : "PiTrac Stopped";
    }
    return status.is_running;
  } catch (e) {
    console.error("Failed to check PiTrac status:", e);
    return false;
  }
}

// -- Strobe Safety --

async function requireStrobeSafe() {
  try {
    const res = await fetch("/api/strobe-safety");
    const data = await res.json();
    if (data.safe) return true;
    showStrobeSafetyModal(data.reason || "");
    return false;
  } catch (_) {
    showStrobeSafetyModal("Could not verify strobe safety — check server connection.");
    return false;
  }
}

function showStrobeSafetyModal(reason) {
  const modal = document.getElementById("strobe-safety-modal");
  if (!modal) return;
  const msg = document.getElementById("strobe-safety-msg");
  if (msg) msg.textContent = reason || "V3 board requires strobe calibration before use.";
  modal.showModal();
  if (typeof lucide !== "undefined") lucide.createIcons();
}

// -- Init --

document.addEventListener("DOMContentLoaded", () => {
  initTheme();
  checkSystemStatus();
  checkPiTracStatus();
  setInterval(checkSystemStatus, 5000);
  setInterval(checkPiTracStatus, 5000);
});
