// Common functionality for all PiTrac pages

// Theme management
let currentTheme = "system";

function getSystemTheme() {
  return window.matchMedia("(prefers-color-scheme: dark)").matches
    ? "dark"
    : "light";
}

function applyTheme(theme) {
  const root = document.documentElement;

  root.removeAttribute("data-theme");

  document.querySelectorAll(".theme-btn").forEach((btn) => {
    btn.classList.remove("active");
  });

  if (theme === "system") {
    const systemTheme = getSystemTheme();
    root.setAttribute("data-theme", systemTheme);
    const systemBtn = document.querySelector('.theme-btn[data-theme="system"]');
    if (systemBtn) systemBtn.classList.add("active");
  } else {
    root.setAttribute("data-theme", theme);
    const themeBtn = document.querySelector(
      `.theme-btn[data-theme="${theme}"]`,
    );
    if (themeBtn) themeBtn.classList.add("active");
  }
}

function setTheme(theme) {
  currentTheme = theme;
  localStorage.setItem("pitrac-theme", theme);
  applyTheme(theme);
}

function initTheme() {
  const savedTheme = localStorage.getItem("pitrac-theme") || "system";
  currentTheme = savedTheme;
  applyTheme(savedTheme);
}

// Listen for system theme changes
window
  .matchMedia("(prefers-color-scheme: dark)")
  .addEventListener("change", (e) => {
    if (currentTheme === "system") {
      applyTheme("system");
    }
  });

function initDropdown() {
  const dropdown = document.querySelector(".dropdown");
  const toggle = document.querySelector(".dropdown-toggle");

  if (toggle && dropdown) {
    toggle.addEventListener("click", (e) => {
      e.stopPropagation();
      dropdown.classList.toggle("active");
    });

    document.addEventListener("click", () => {
      dropdown.classList.remove("active");
    });

    const dropdownMenu = document.querySelector(".dropdown-menu");
    if (dropdownMenu) {
      dropdownMenu.addEventListener("click", (e) => {
        e.stopPropagation();
      });
    }
  }
}

async function controlPiTrac(action) {
  if (
    (action === "start" || action === "restart") &&
    !(await requireStrobeSafe())
  )
    return;

  const buttonMap = {
    start: ["pitrac-start-btn-desktop", "pitrac-start-btn-mobile"],
    stop: ["pitrac-stop-btn-desktop", "pitrac-stop-btn-mobile"],
    restart: ["pitrac-restart-btn-desktop", "pitrac-restart-btn-mobile"],
  };

  const buttons = buttonMap[action]
    .map((id) => document.getElementById(id))
    .filter((btn) => btn);

  document.querySelectorAll(".control-btn").forEach((btn) => {
    btn.disabled = true;
  });

  buttons.forEach((btn) => {
    if (btn) {
      btn.classList.add("loading");
    }
  });

  try {
    const response = await fetch(`/api/pitrac/${action}`, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
      },
    });

    if (!response.ok) {
      const errorData = await response.json();
      throw new Error(errorData.error || `Failed to ${action} PiTrac`);
    }

    const data = await response.json();

    if (typeof showStatusMessage === "function") {
      showStatusMessage(data.message, "success");
    }

    setTimeout(() => {
      if (typeof checkSystemStatus === "function") {
        checkSystemStatus();
      }
    }, 2000);
  } catch (error) {
    console.error(`Error ${action}ing PiTrac:`, error);
    if (typeof showStatusMessage === "function") {
      showStatusMessage(error.message || `Failed to ${action} PiTrac`, "error");
    }
  } finally {
    buttons.forEach((btn) => {
      if (btn) {
        btn.classList.remove("loading");
      }
    });

    setTimeout(() => {
      if (typeof checkPiTracStatus === "function") {
        checkPiTracStatus();
      } else {
        document.querySelectorAll(".control-btn").forEach((btn) => {
          btn.disabled = false;
        });
      }
    }, 1000);
  }
}

function updatePiTracButtons(isRunning) {
  const startBtns = ["pitrac-start-btn-desktop", "pitrac-start-btn-mobile"]
    .map((id) => document.getElementById(id))
    .filter((btn) => btn);

  const stopBtns = ["pitrac-stop-btn-desktop", "pitrac-stop-btn-mobile"]
    .map((id) => document.getElementById(id))
    .filter((btn) => btn);

  const restartBtns = [
    "pitrac-restart-btn-desktop",
    "pitrac-restart-btn-mobile",
  ]
    .map((id) => document.getElementById(id))
    .filter((btn) => btn);

  if (isRunning) {
    startBtns.forEach((btn) => {
      btn.style.display = "none";
    });
    stopBtns.forEach((btn) => {
      btn.style.display = "";
      btn.disabled = false;
      btn.title = "Stop PiTrac";
    });
    restartBtns.forEach((btn) => {
      btn.style.display = "";
      btn.disabled = false;
      btn.title = "Restart PiTrac";
    });
  } else {
    startBtns.forEach((btn) => {
      btn.style.display = "";
      btn.disabled = false;
      btn.title = "Start PiTrac";
    });
    stopBtns.forEach((btn) => {
      btn.style.display = "none";
    });
    restartBtns.forEach((btn) => {
      btn.style.display = "none";
    });
  }
}

async function checkSystemStatus() {
    try {
        const response = await fetch('/health');
        if (response.ok) {
            return await response.json();
        }
    } catch (error) {
        console.error('Error checking system status:', error);
    }
    return null;
}

async function checkPiTracStatus() {
    try {
        const response = await fetch('/api/pitrac/status');
        const status = await response.json();
        updatePiTracButtons(status.is_running);

        const statusDot = document.getElementById('pitrac-status-dot');
        if (statusDot) {
            if (status.pid) {
                statusDot.classList.add('connected');
                statusDot.classList.remove('disconnected');
                statusDot.title = `PiTrac Running (PID: ${status.pid})`;
            } else {
                statusDot.classList.remove('connected');
                statusDot.classList.add('disconnected');
                statusDot.title = 'PiTrac Stopped';
            }
        }

        return status.is_running;
    } catch (error) {
        console.error('Failed to check PiTrac status:', error);
        return false;
    }
}

let strobeUnsafeReason = "";

async function requireStrobeSafe() {
  try {
    const response = await fetch("/api/strobe-safety");
    const data = await response.json();
    if (data.safe) return true;
    strobeUnsafeReason = data.reason || "";
    showStrobeSafetyModal();
    return false;
  } catch (error) {
    strobeUnsafeReason =
      "Could not verify strobe safety — check server connection.";
    showStrobeSafetyModal();
    return false;
  }
}

function showStrobeSafetyModal() {
  const existing = document.getElementById("strobe-safety-modal");
  if (existing) {
    existing.style.display = "flex";
    return;
  }

  const overlay = document.createElement("div");
  overlay.id = "strobe-safety-modal";
  overlay.style.cssText =
    "position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.5);display:flex;justify-content:center;align-items:center;z-index:1000;";

  const card = document.createElement("div");
  card.style.cssText =
    "background:var(--bg-card);border-radius:0.75rem;padding:2rem;max-width:420px;width:90%;position:relative;box-shadow:0 20px 60px rgba(0,0,0,0.3);border:1px solid var(--border-color);";

  const icon = document.createElement("div");
  icon.style.cssText = "text-align:center;margin-bottom:1rem;";
  icon.innerHTML =
    '<svg width="48" height="48" viewBox="0 0 24 24" fill="none" stroke="#e53e3e" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M10.29 3.86L1.82 18a2 2 0 001.71 3h16.94a2 2 0 001.71-3L13.71 3.86a2 2 0 00-3.42 0z"/><line x1="12" y1="9" x2="12" y2="13"/><line x1="12" y1="17" x2="12.01" y2="17"/></svg>';

  const title = document.createElement("h3");
  title.style.cssText =
    "color:var(--text-primary);margin:0 0 0.75rem;text-align:center;font-size:1.1rem;";
  title.textContent = "Strobe Calibration Required";

  const msg = document.createElement("p");
  msg.style.cssText =
    "color:var(--text-secondary);margin:0 0 1.5rem;text-align:center;font-size:0.9rem;line-height:1.5;";
  msg.textContent =
    strobeUnsafeReason ||
    "V3 board requires strobe calibration before use.";

  const btnRow = document.createElement("div");
  btnRow.style.cssText =
    "display:flex;gap:0.75rem;justify-content:center;";

  const calBtn = document.createElement("a");
  calBtn.href = "/calibration";
  calBtn.style.cssText =
    "background:var(--accent-gradient);color:white;border:none;padding:0.6rem 1.25rem;border-radius:0.5rem;font-size:0.9rem;font-weight:500;cursor:pointer;text-decoration:none;display:inline-block;";
  calBtn.textContent = "Go to Calibration";

  const dismissBtn = document.createElement("button");
  dismissBtn.style.cssText =
    "background:transparent;color:var(--text-secondary);border:1px solid var(--border-color);padding:0.6rem 1.25rem;border-radius:0.5rem;font-size:0.9rem;cursor:pointer;";
  dismissBtn.textContent = "Dismiss";
  dismissBtn.onclick = () => {
    overlay.style.display = "none";
  };

  btnRow.appendChild(calBtn);
  btnRow.appendChild(dismissBtn);
  card.appendChild(icon);
  card.appendChild(title);
  card.appendChild(msg);
  card.appendChild(btnRow);
  overlay.appendChild(card);

  overlay.addEventListener("click", (e) => {
    if (e.target === overlay) overlay.style.display = "none";
  });

  document.body.appendChild(overlay);
}

document.addEventListener("DOMContentLoaded", () => {
  initTheme();
  initDropdown();

  checkSystemStatus();
  checkPiTracStatus();

  setInterval(checkSystemStatus, 5000);
  setInterval(checkPiTracStatus, 5000);
});
