"""Headless-Chrome smoke test of the built OTA dashboard bundle against a local
mock device on 127.0.0.1 (never a real strip). Serves the exact gzip assets
from ui_assets.h and mocks /api/*; drives: save OTA URL -> check -> install
(confirm) -> reboot with a new build -> page reload.

usage: python3 source/standalone/tests/pull_dashboard_smoke.py \
         source/standalone/results/network-ota-test/sdk/ui_assets.h [screenshot-dir]
Needs Python playwright and Google Chrome at /opt/google/chrome/chrome."""
import json, re, sys, tempfile, threading
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
from playwright.sync_api import sync_playwright
header = open(sys.argv[1]).read()
shots = sys.argv[2] if len(sys.argv) > 2 else tempfile.mkdtemp(prefix="mttl-pull-smoke-")
arrays = {int(i): bytes(map(int, b.split(','))) for i, b in re.findall(r'mttl_asset_(\d+)\[\]=\{([0-9,]+)\}', header)}
table = re.findall(r'\{"([^"]+)","([^"]+)",mttl_asset_(\d+),', header)
assets = {p: (t, arrays[int(i)]) for p, t, i in table}
offer = {"state": 3, "error": 0, "rom_ready": 1, "version": "2.2.0", "build_id": "0123456789abcdef", "size": 434000, "sha256": "ab" * 32, "expires_at": 1900000000}
missing = []
S = {"build": "1111111111111111", "pull": {"state": 0, "error": 0, "rom_ready": 0}, "base": "", "posts": [], "rebooted": False}
cfg = lambda: {"device": "MTTL-W01", "channels": ["SW 1", "SW 2", "SW 3", "SW 4"], "language": 1, "broker": "", "port": 1883, "user": "", "topic": "mttl", "enabled": False,
               "wifi_password_set": True, "mqtt_password_set": False, "persistent": True, "protection": {"enabled": 0, "temperature": 0, "current": 0, "power": 0},
               "schedule_hex": "00" * 50, "button_lock": 0, "timer_minutes": [0, 0, 0, 0], "pull_base": S["base"]}
class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def send(self, code, body=b"", ctype="application/json", extra=()):
        self.send_response(code); self.send_header("Content-Type", ctype); self.send_header("Content-Length", str(len(body)))
        for k, v in extra: self.send_header(k, v)
        self.end_headers(); self.wfile.write(body)
    def j(self, obj, code=200): self.send(code, json.dumps(obj).encode())
    def do_GET(self):
        p = self.path.split('?')[0]
        if p in assets: t, d = assets[p]; return self.send(200, d, t, [("Content-Encoding", "gzip"), ("Cache-Control", "no-store")])
        if p == "/api/status": return self.j({"state": 5, "ip": 0x7f000001, "uptime_ms": 1000, "rssi": -50, "ssid_hex": "6d6f636b", "mac": "02:00:00:00:00:02", "unix_ms": 1800000000000, "connected_unix_ms": None, "meter_initialized": 1, "meter_reads": 1, "energy_reads": 1, "energy_counter_raw": [0, 0, 0, 0], "energy_restore_pending": 0, "storage": [0, 0, 0, 0], "hardware_ready": True})
        if p == "/api/device": return self.j({"name": "MTTL-W01", "language": 1, "revision": 1, "persistent": True, "hardware_ready": True, "voltage_dv": 2200, "power_mw": 0, "meter_wh": 0, "current_ma": 0, "mqtt_state": 0, "channels": [{"name": f"SW {i+1}", "on": False, "locked": False, "power_mw": 0, "meter_wh": 0, "current_ma": 0, "temp_dc": 250} for i in range(4)], "protection": {"reason": 0, "error": 0}, "timer_running": 0, "timer_end": [0, 0, 0, 0]})
        if p == "/api/config": return self.j(cfg())
        if p == "/api/firmware/status": return self.j({"version": "2.2.0" if S["rebooted"] else "2.1.1", "build_id": S["build"], "ota_ready": True, "active_slot": 1, "settings_persistent": True, "restore_versions": [], "sha256_header": True})
        if p == "/api/pull/status": return self.j(S["pull"])
        missing.append(p); return self.send(404, b"")
    def do_POST(self):
        p = self.path; n = int(self.headers.get("Content-Length") or 0); body = self.rfile.read(n)
        S["posts"].append((p, body, self.headers.get("Origin")))
        if p == "/api/pull/base":
            b = list(body)
            if b and b[0] not in (10, 172, 192): return self.send(400, b"Bad Request", "text/plain")
            S["base"] = f"http://{b[0]}.{b[1]}.{b[2]}.{b[3]}:{b[4]*256+b[5]}" if b else ""; S["pull"] = {"state": 1 if b else 0, "error": 0, "rom_ready": 0}
            return self.j({"accepted": True, "pending": True})
        if p == "/api/pull/check": S["pull"] = offer; return self.j(offer)
        if p == "/api/pull/install":
            assert body == b"0123456789abcdef", body
            S["build"] = "2222222222222222"; S["rebooted"] = True; S["pull"] = {"state": 1, "error": 0, "rom_ready": 0}
            return self.j({"accepted": True, "pending": True})
        return self.send(404, b"")
srv = ThreadingHTTPServer(("127.0.0.1", 0), H); port = srv.server_address[1]
threading.Thread(target=srv.serve_forever, daemon=True).start()
errors, dialogs = [], []
with sync_playwright() as pw:
    browser = pw.chromium.launch(executable_path="/opt/google/chrome/chrome", headless=True)
    page = browser.new_page()
    page.on("console", lambda m: m.type == "error" and errors.append(m.text))
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.on("dialog", lambda d: (dialogs.append(d.message), d.accept()))
    page.goto(f"http://127.0.0.1:{port}/#firmware")
    page.click('[aria-controls="firmware"]')
    page.wait_for_selector("#pull-status:has-text('Off')", timeout=15000)
    assert not any(p.startswith("/api/pull/") for p, *_ in S["posts"]), S["posts"]
    assert page.is_disabled("#pull-check") and page.is_hidden("#pull-install")
    page.fill("#pull-base", "http://192.168.0.10:8080"); page.click("#pull-save")
    page.wait_for_selector("#pull-status:has-text('Not checked')", timeout=10000)
    assert S["posts"][-1][0] == "/api/pull/base" and list(S["posts"][-1][1]) == [192, 168, 0, 10, 31, 144]
    assert S["posts"][-1][2] == f"http://127.0.0.1:{port}", S["posts"][-1][2]
    page.screenshot(path=shots + "/pull-1-saved.png", full_page=True)
    page.click("#pull-check")
    page.wait_for_selector("#pull-install:visible", timeout=10000)
    status = page.inner_text("#pull-status")
    assert "2.2.0" in status and "0123456789abcdef" in status and "434000 bytes" in status and "2030-03-17" in status, status
    page.screenshot(path=shots + "/pull-2-offer.png", full_page=True)
    with page.expect_navigation(timeout=30000):
        page.click("#pull-install")
    page.wait_for_selector("#current-build-id:has-text('2222222222222222')", timeout=15000)
    page.screenshot(path=shots + "/pull-3-reloaded.png", full_page=True)
    browser.close()
srv.shutdown()
installs = [p for p, *_ in S["posts"] if p == "/api/pull/install"]
assert len(installs) == 1 and len(dialogs) == 1 and "outlets turn off" in dialogs[0], (installs, dialogs)
# Only the mock's intentionally absent SSE stream / favicon may 404.
assert set(missing) <= {"/api/events", "/favicon.ico"}, missing
assert all("404" in e for e in errors) and len(errors) == len(missing), (errors, missing)
print(json.dumps({"passed": True, "posts": [p for p, *_ in S["posts"]], "dialog": dialogs[0], "console_errors_other_than_mock_404": 0, "mock_404": sorted(set(missing)), "screenshots": shots}))
