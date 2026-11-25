// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

(() => {
  "use strict";

  const mock = new URLSearchParams(location.search).get("mock") === "1";
  const sample = {
    name: "Samsung M2020W", state: "ready", status_text: "Ready", toner_percent: 72,
    last_job_age_s: 246, wifi_ssid: "Office WiFi", wifi_rssi: -52, ip: "192.168.1.42", uptime_s: 273840, fw: "1.0.0"
  };
  let status = mock ? sample : null;
  let jobs = mock ? [{ id: 12, name: "hello0.urf", bytes: 184320, result: "ok", age_s: 246 }] : [];
  let activeTab = "status";
  const $ = (id) => document.getElementById(id);

  function formatUptime(seconds) {
    const days = Math.floor(seconds / 86400), hours = Math.floor((seconds % 86400) / 3600);
    const minutes = Math.floor((seconds % 3600) / 60);
    return days ? `${days} d ${hours} h` : hours ? `${hours} h ${minutes} min` : `${minutes} min`;
  }
  function formatAge(seconds) {
    if (seconds < 60) return "less than a minute ago";
    return `${Math.floor(seconds / 60)} minute${Math.floor(seconds / 60) === 1 ? "" : "s"} ago`;
  }
  function formatBytes(bytes) { return bytes < 1024 ? `${bytes} B` : `${(bytes / 1024).toFixed(bytes < 1048576 ? 0 : 1)} ${bytes < 1048576 ? "KB" : "MB"}`; }
  function setMessage(id, text) { $(id).textContent = text; }

  function renderStatus() {
    if (!status) return;
    $("device-name").textContent = status.name;
    $("status-text").textContent = status.status_text;
    $("status-indicator").className = `status-indicator ${status.state === "error" ? "error" : status.state === "printing" ? "printing" : ""}`;
    $("toner-percent").textContent = status.toner_percent == null ? "-" : `${status.toner_percent}%`;
    $("toner-ring").style.strokeDashoffset = status.toner_percent == null ? "351.86" : 351.86 * (1 - status.toner_percent / 100);
    $("last-job").textContent = status.last_job_age_s == null ? "" : `Last job ${formatAge(status.last_job_age_s)}`;
    $("printer-model").textContent = status.printer_model || "-";
    $("wifi-ssid").textContent = status.wifi_ssid || "Not connected";
    $("ip-address").textContent = status.ip || "-";
    $("uptime").textContent = formatUptime(status.uptime_s || 0);
    $("cancel-job").classList.toggle("hidden", status.state !== "printing");
  }
  function renderJobs() {
    $("job-count").textContent = jobs.length ? `${jobs.length} of 5` : "";
    $("job-list").innerHTML = jobs.length ? jobs.slice(0, 5).map((job) => `<article class="job"><div class="job-name">${escapeHtml(job.name)}</div><div class="job-meta"><span>${formatBytes(job.bytes)} · <span class="result-${job.result}">${escapeHtml(job.result)}</span></span><span>${formatAge(job.age_s)}</span></div></article>`).join("") : '<div class="empty">No jobs yet.</div>';
  }
  function renderSettings(data) {
    ["name", "location", "hostname"].forEach((key) => { $("device-form").elements[key].value = data[key] || ""; });
    $("device-form").elements.color_mode.value = String(data.color_mode || 0);
    $("device-form").elements.duplex.value = data.duplex ? "1" : "0";
    $("device-form").elements.jobs_clear_hours.value = String(data.jobs_clear_hours || 0);
    $("wifi-form").elements.ssid.value = data.ssid || "";
    $("password-save").textContent = data.password_protected ? "Change password" : "Set password";
    $("remove-password").classList.toggle("hidden", !data.password_protected);
  }
  function escapeHtml(value) { return String(value).replace(/[&<>"']/g, (char) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[char])); }
  function uiFetch(path, options = {}) {
    if ((options.method || "GET").toUpperCase() === "POST") {
      const headers = new Headers(options.headers);
      headers.set("X-Requested-With", "printer-ui");
      options = { ...options, headers };
    }
    return fetch(path, options);
  }
  async function api(path, options) {
    if (mock) return path === "/api/jobs" ? jobs : path === "/api/settings" ? { name: sample.name, location: "", hostname: "printer", ssid: sample.wifi_ssid } : status;
    const response = await uiFetch(path, options);
    if (!response.ok) throw new Error((await response.json().catch(() => ({}))).error || `Request failed (${response.status})`);
    return response.status === 204 ? {} : response.json();
  }
  async function refresh() {
    try { status = await api("/api/status"); renderStatus(); if (activeTab === "jobs") { jobs = await api("/api/jobs"); renderJobs(); } } catch (error) { setMessage("settings-message", error.message); }
  }
  async function showTab(tab) {
    activeTab = tab;
    document.querySelectorAll(".tab, .panel").forEach((element) => element.classList.toggle("is-active", element.dataset.tab === tab || element.dataset.panel === tab));
    if (tab === "jobs") { try { jobs = await api("/api/jobs"); renderJobs(); } catch (error) { setMessage("settings-message", error.message); } }
    if (tab === "settings") { try { renderSettings(await api("/api/settings")); api("/api/wifi/networks").then((list) => { $("networks").innerHTML = list.map((network) => `<option value="${escapeHtml(network.ssid)}"></option>`).join(""); }).catch(() => {}); } catch (error) { setMessage("settings-message", error.message); } }
  }

  document.querySelectorAll(".tab").forEach((button) => button.addEventListener("click", () => showTab(button.dataset.tab)));
  $("test-page").addEventListener("click", async () => { try { await api("/api/testpage", { method: "POST" }); setMessage("settings-message", "Test page started"); } catch (error) { setMessage("settings-message", error.message); } });
  $("cancel-job").addEventListener("click", async () => { try { await api("/api/jobs/cancel", { method: "POST" }); await refresh(); } catch (error) { setMessage("settings-message", error.message); } });
  $("clear-jobs").addEventListener("click", async () => { try { await api("/api/jobs/clear", { method: "POST" }); jobs = await api("/api/jobs"); renderJobs(); } catch (error) { setMessage("settings-message", error.message); } });
  $("password-form").addEventListener("submit", async (event) => { event.preventDefault(); const body = Object.fromEntries(new FormData(event.target)); try { await api("/api/password", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) }); event.target.reset(); setMessage("settings-message", "Password saved"); renderSettings(await api("/api/settings")); } catch (error) { setMessage("settings-message", error.message); } });
  $("remove-password").addEventListener("click", async () => { try { await api("/api/password", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ password: "" }) }); setMessage("settings-message", "Password removed"); renderSettings(await api("/api/settings")); } catch (error) { setMessage("settings-message", error.message); } });
  $("reboot").addEventListener("click", async () => { try { await api("/api/reboot", { method: "POST" }); setMessage("settings-message", "Reboot started"); } catch (error) { setMessage("settings-message", error.message); } });
  $("device-form").addEventListener("submit", async (event) => { event.preventDefault(); const body = Object.fromEntries(new FormData(event.target)); try { await api("/api/settings", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) }); setMessage("settings-message", "Settings saved"); } catch (error) { setMessage("settings-message", error.message); } });
  $("wifi-form").addEventListener("submit", async (event) => { event.preventDefault(); const body = Object.fromEntries(new FormData(event.target)); try { await api("/api/wifi", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) }); setMessage("settings-message", "WiFi settings saved. Rebooting"); } catch (error) { setMessage("settings-message", error.message); } });
  $("ota-form").addEventListener("submit", (event) => { event.preventDefault(); const file = $("firmware-file").files[0]; if (!file || mock) { setMessage("ota-message", mock ? "Upload is disabled in mock mode" : "Choose a firmware file"); return; } const request = new XMLHttpRequest(); request.open("POST", "/api/ota"); request.setRequestHeader("X-Requested-With", "printer-ui"); request.upload.onprogress = (progress) => { if (progress.lengthComputable) { $("ota-progress").hidden = false; $("ota-progress").value = progress.loaded / progress.total * 100; } }; request.onload = () => setMessage("ota-message", request.status >= 200 && request.status < 300 ? "Update uploaded. Rebooting" : "Update failed"); request.onerror = () => setMessage("ota-message", "Update failed"); request.send(file); });
  renderStatus(); renderJobs(); refresh();
  setInterval(() => { if (document.visibilityState === "visible") refresh(); }, 3000);
})();
