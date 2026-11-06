// Settings panel: Web Serial directly to the firmware's serial_cfg task.
// Protocol: one line commands ("status", "gestures on/off", "hid android/standard",
// "forget", "reboot"); responses are single lines starting with "U2B " + JSON.
// Nothing is sent until the user clicks a button. Port opens at 115200 with
// DTR and RTS held low so opening does not reset the chip.
(() => {
  const $ = (id) => document.getElementById(id);
  const logEl = $("log");
  let port = null;
  let reader = null;
  let reading = false;

  function log(msg) {
    logEl.textContent += msg + "\n";
    logEl.scrollTop = logEl.scrollHeight;
  }

  function setConnected(on) {
    $("connect").disabled = on;
    $("disconnect").disabled = !on;
    $("refresh").disabled = !on;
    $("conn-state").textContent = on ? "Connected." : "Not connected.";
    if (!on) { $("settings").hidden = true; }
  }

  function showStatus(j) {
    $("settings").hidden = false;
    $("version").textContent = j.version ?? "–";
    $("build").textContent = j.build ?? "–";
    $("bonds").textContent = String(j.bonds ?? "–");
    const gest = j.gestures ? "on" : "off";
    $("gestures-state").textContent =
      `${gest} (saved ${j.gestures_saved ? "on" : "off"})`;
    const hid = j.standard_hid ? "standard" : "android";
    $("hid-state").textContent =
      `${hid} (saved ${j.standard_hid_saved ? "standard" : "android"})`;
    document.querySelectorAll('input[name="hid"]').forEach((r) => {
      r.checked = (r.value === (j.standard_hid_saved ? "standard" : "android"));
    });
    const needReboot = j.reboot_required === true ||
      j.standard_hid !== j.standard_hid_saved;
    $("reboot-flag").textContent = needReboot ? "Reboot and re-pair to apply." : "";
    $("hid-hint").textContent = needReboot
      ? "Bluetooth mode change needs a reboot and re-pair."
      : "";
  }

  function handleLine(line) {
    if (!line.startsWith("U2B ")) { log(line); return; }
    const payload = line.slice(4);
    log(line);
    let j;
    try {
      j = JSON.parse(payload);
    } catch (e) {
      log("JSON parse error, ignored");
      return;
    }
    if (j && typeof j === "object" && ("ok" in j)) {
      if (j.ok) {
        if ("version" in j) { showStatus(j); }
      } else {
        log("Error: " + (j.error || "unknown"));
      }
    }
  }

  async function readLoop() {
    const decoder = new TextDecoder();
    let buf = "";
    reading = true;
    try {
      while (port && port.readable) {
        reader = port.readable.getReader();
        try {
          for (;;) {
            const { value, done } = await reader.read();
            if (done) break;
            buf += decoder.decode(value, { stream: true });
            let i;
            while ((i = buf.indexOf("\n")) >= 0) {
              const line = buf.slice(0, i).replace(/\r$/, "");
              buf = buf.slice(i + 1);
              handleLine(line);
            }
          }
        } finally {
          reader.releaseLock();
          reader = null;
        }
        break;
      }
    } catch (e) {
      log("Disconnected (" + (e && e.message ? e.message : e) + ")");
    } finally {
      reading = false;
      if (port) { try { await port.close(); } catch (_) {} port = null; }
      setConnected(false);
    }
  }

  async function send(line) {
    if (!port || !port.writable) { log("Not connected."); return; }
    const writer = port.writable.getWriter();
    try {
      await writer.write(new TextEncoder().encode(line + "\n"));
    } finally {
      writer.releaseLock();
    }
  }

  $("connect").addEventListener("click", async () => {
    if (!("serial" in navigator)) return;
    try {
      port = await navigator.serial.requestPort();
      await port.open({ baudRate: 115200 });
      // Keep DTR/RTS low so opening the port does not reset the chip.
      try {
        await port.setSignals({ dataTerminalReady: false, requestToSend: false });
      } catch (_) {}
      setConnected(true);
      log("Connected at 115200, DTR/RTS low.");
      readLoop();
      await send("status"); // part of the Connect click, nothing auto-sent before it
    } catch (e) {
      log("Connect failed: " + (e && e.message ? e.message : e));
      if (port) { try { await port.close(); } catch (_) {} port = null; }
      setConnected(false);
    }
  });

  $("disconnect").addEventListener("click", async () => {
    try {
      if (reader) { await reader.cancel(); }
    } catch (_) {}
  });

  $("refresh").addEventListener("click", () => send("status"));
  $("gestures-on").addEventListener("click", () => send("gestures on"));
  $("gestures-off").addEventListener("click", () => send("gestures off"));
  $("hid-apply").addEventListener("click", () => {
    const sel = document.querySelector('input[name="hid"]:checked');
    if (sel) { send("hid " + sel.value); }
  });
  $("forget").addEventListener("click", () => {
    if (window.confirm("Forget all Bluetooth pairings? Also remove the device on the TV.")) {
      send("forget");
    }
  });
  $("reboot").addEventListener("click", () => send("reboot"));

  if ("serial" in navigator) {
    $("serial-support").textContent = "";
    navigator.serial.addEventListener("disconnect", (e) => {
      if (e.target === port) { log("Port disconnected."); }
    });
  } else {
    $("serial-support").textContent =
      "Web Serial is unavailable here. Use desktop Chrome or Edge for Install and Settings.";
    $("connect").disabled = true;
  }
})();
