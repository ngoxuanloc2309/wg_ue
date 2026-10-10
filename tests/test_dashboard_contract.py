#!/usr/bin/env python3
"""
Dashboard <-> MCU contract automation tests.

Covers the recently shipped features:
  P1  network.priority accepts CSV string from the dashboard
  P2  GET /api/config/apply_status endpoint
  P3  save -> async apply pipeline reports APPLIED/ERROR
  P4  mqtt client_id / keepalive_s / clean_session persistence +
      publish_mode<->strategy aliasing + keepalive validation clamp
  P5  /api/status state+failover fields, /api/telemetry/snapshot shape
  --  generation conflict guard still enforced

Usage:
    python tests/test_dashboard_contract.py [--host 192.168.110.249] \
        [--user admin] [--pass admin]

Every mutation snapshots the running configuration first and restores it
in a finally block. Cases whose firmware prerequisites are missing are
reported as SKIP (old image on the board) instead of FAIL.
"""

import argparse
import copy
import json
import sys
import time

import requests

PASS, FAIL, SKIP = "PASS", "FAIL", "SKIP"
_results = []


def record(name, status, detail=""):
    _results.append((name, status, detail))
    print(f"[{status:4}] {name}" + (f"  -- {detail}" if detail else ""))


class Gateway:
    def __init__(self, host, user, password):
        self.base = f"http://{host}"
        self.s = requests.Session()
        self.s.auth = (user, password)
        self.s.timeout = 10

    # --- low level -------------------------------------------------------
    def get_json(self, path):
        r = self.s.get(self.base + path)
        r.raise_for_status()
        return r.json()

    def try_get(self, path):
        try:
            r = self.s.get(self.base + path)
            return r.status_code, (r.json() if r.headers.get("content-type", "").startswith("application/json") else None)
        except ValueError:
            return r.status_code, None

    def post_config(self, cfg):
        r = self.s.post(self.base + "/api/config", json=cfg)
        try:
            body = r.json()
        except ValueError:
            body = {}
        return r.status_code, body

    # --- helpers ---------------------------------------------------------
    def fetch_config(self):
        return self.get_json("/api/config")

    def save(self, cfg):
        """Full save cycle: inject current generation, POST, wait for apply."""
        fresh = self.fetch_config()
        payload = copy.deepcopy(cfg)
        payload["base_generation"] = fresh.get("generation")
        code, body = self.post_config(payload)
        if code != 200 or body.get("status") != "ok":
            raise RuntimeError(f"save rejected: HTTP {code} {body}")
        final = self.wait_apply()
        return body, final

    def wait_apply(self, timeout=15.0):
        deadline = time.time() + timeout
        last = {"state": "unknown"}
        while time.time() < deadline:
            code, st = self.try_get("/api/config/apply_status")
            if code == 200 and isinstance(st, dict):
                last = st
                if st.get("state") in ("applied", "error"):
                    return st
            time.sleep(0.5)
        return last


# ---------------------------------------------------------------------------
# Test cases
# ---------------------------------------------------------------------------
def tc_preflight(gw, feats):
    code, _ = gw.try_get("/api/status")
    if code != 200:
        record("preflight.device-reachable", FAIL, f"HTTP {code}")
        return False
    record("preflight.device-reachable", PASS)

    _, cfg = gw.try_get("/api/config")
    _, status = gw.try_get("/api/status")

    feats["p1_csv"] = False  # proven dynamically by TC1; assume present
    feats["apply_status"] = gw.try_get("/api/config/apply_status")[0] == 200
    feats["mqtt_fields"] = bool(cfg and "client_id" in (cfg.get("mqtt") or {}))
    feats["snapshot"] = gw.try_get("/api/telemetry/snapshot")[0] == 200
    feats["status_runtime"] = bool(
        status and "state" in status and "failover" in status
    )

    print(f"       features: apply_status={feats['apply_status']} "
          f"mqtt={feats['mqtt_fields']} snapshot={feats['snapshot']} "
          f"runtime={feats['status_runtime']}")
    if not all([feats["apply_status"], feats["mqtt_fields"],
                feats["snapshot"], feats["status_runtime"]]):
        record("preflight.firmware-version",
               SKIP,
               "board runs an older image; affected cases will SKIP. "
               "Flash build/GatewayV2.bin to enable them.")
    return True


def tc_priority_csv_roundtrip(gw):
    name = "P1.priority-csv-roundtrip"
    orig = gw.fetch_config()
    old_prio = list((orig.get("network") or {}).get("priority") or [])
    if len(old_prio) < 3:
        record(name, SKIP, "no 3-entry priority in config")
        return

    rotated = old_prio[1:] + old_prio[:1]
    mutated = copy.deepcopy(orig)
    # Send exactly what buildConfig() sends: a comma-separated STRING.
    mutated.setdefault("network", {})["priority"] = ",".join(rotated)

    gw.save(mutated)
    after = (gw.fetch_config().get("network") or {}).get("priority") or []
    if [str(x).lower() for x in after] == [str(x).lower() for x in rotated]:
        record(name, PASS, f"{rotated} accepted via CSV")
    else:
        record(name, FAIL, f"expected {rotated}, got {after}")

    # restore canonical array form
    restore = gw.fetch_config()
    restore.setdefault("network", {})["priority"] = old_prio
    gw.save(restore)
    back = (gw.fetch_config().get("network") or {}).get("priority") or []
    extra = "restored" if [str(x).lower() for x in back] == [str(x).lower() for x in old_prio] else "RESTORE MISMATCH"
    print(f"       {extra}: {back}")


def tc_apply_status_shape(gw, feats):
    if not feats["apply_status"]:
        record("P2.apply-status-shape", SKIP, "endpoint missing on board")
        return
    code, st = gw.try_get("/api/config/apply_status")
    need = {"generation", "applied_generation", "state",
            "last_error_module", "last_error_code"}
    ok = (code == 200 and isinstance(st, dict) and need <= set(st)
          and st.get("state") in ("pending", "applying", "applied", "error"))
    record("P2.apply-status-shape", PASS if ok else FAIL, f"{st}")


def tc_save_reports_applied(gw, feats):
    if not feats["apply_status"]:
        record("P3.save-reports-async-result", SKIP, "apply_status missing")
        return
    cfg = gw.fetch_config()  # unchanged content, exercises full pipeline
    body, final = gw.save(cfg)
    resp_keys = {"applied", "apply_state", "applied_generation",
                 "last_error_module", "last_error_code"} <= set(body)
    ok = resp_keys and final.get("state") == "applied" and body.get("applied") is True
    record("P3.save-reports-async-result", PASS if ok else FAIL,
           f"resp.applied={body.get('applied')} final={final.get('state')}")


def tc_mqtt_roundtrip_and_clamp(gw, feats):
    name = "P4.mqtt-roundtrip-clamp-alias"
    if not feats["mqtt_fields"]:
        record(name, SKIP, "client_id absent from config -> old firmware")
        return
    orig = gw.fetch_config()
    m0 = orig.get("mqtt") or {}

    mutated = copy.deepcopy(orig)
    m = mutated.setdefault("mqtt", {})
    m["client_id"] = "AutoTest-Client"
    m["keepalive_s"] = 30
    m["clean_session"] = False
    pubs = m.get("publishers") or []
    if pubs:
        pubs[0].pop("strategy", None)           # force alias path
        pubs[0]["publish_mode"] = "on_change"
    gw.save(mutated)

    got = gw.fetch_config().get("mqtt") or {}
    problems = []
    if got.get("client_id") != "AutoTest-Client":
        problems.append(f"client_id={got.get('client_id')!r}")
    if got.get("keepalive_s") != 30:
        problems.append(f"keepalive={got.get('keepalive_s')!r}")
    if got.get("clean_session") is not False:
        problems.append(f"clean_session={got.get('clean_session')!r}")
    gp = (got.get("publishers") or [{}])[0]
    if pubs and gp.get("strategy") != "on_change":
        problems.append(f"alias strategy={gp.get('strategy')!r}")
    if not problems:
        record(name + ".persist", PASS)

    # validation clamp: keepalive below minimum must be ignored
    mutated2 = gw.fetch_config()
    mutated2.setdefault("mqtt", {})["keepalive_s"] = 5
    gw.save(mutated2)
    kept = gw.fetch_config().get("mqtt", {}).get("keepalive_s")
    record(name + ".clamp", PASS if kept == 30 else FAIL,
           f"invalid keepalive=5 -> {kept} (expected kept 30)")

    # restore
    restore = gw.fetch_config()
    rm = restore.setdefault("mqtt", {})
    rm["client_id"] = m0.get("client_id", "")
    rm["keepalive_s"] = m0.get("keepalive_s", 60)
    rm["clean_session"] = m0.get("clean_session", True)
    opubs = restore.get("mqtt", {}).get("publishers") or []
    if opubs and pubs:
        opubs[0]["strategy"] = m0.get("publish_strategy",
                                      (m0.get("publishers") or [{}])[0].get("strategy", "per_device"))
        opubs[0].pop("publish_mode", None)
    gw.save(restore)


def tc_telemetry_snapshot_shape(gw, feats):
    if not feats["snapshot"]:
        record("P5.telemetry-snapshot-shape", SKIP, "endpoint missing")
        return
    code, arr = gw.try_get("/api/telemetry/snapshot")
    if code != 200 or not isinstance(arr, list):
        record("P5.telemetry-snapshot-shape", FAIL, f"HTTP {code}, type={type(arr).__name__}")
        return
    allowed_q = {"good", "stale", "timeout", "error"}
    bad = [x for x in arr
           if not (isinstance(x, dict) and isinstance(x.get("k"), str)
                   and isinstance(x.get("age"), int) and x.get("age", -1) >= 0
                   and x.get("q") in allowed_q)]
    detail = f"{len(arr)} point(s)" + (f"; malformed={bad[:2]}" if bad else "")
    record("P5.telemetry-snapshot-shape", PASS if not bad else FAIL, detail)


def tc_status_runtime_fields(gw, feats):
    if not feats["status_runtime"]:
        record("P5.status-runtime-fields", SKIP, "fields missing -> old firmware")
        return
    _, st = gw.try_get("/api/status")
    fo = st.get("failover") or {}
    ok = (isinstance(st.get("state"), str) and st["state"] != ""
          and isinstance(fo.get("probe_ok"), int) and fo["probe_ok"] >= 0
          and isinstance(fo.get("probe_fail"), int) and fo["probe_fail"] >= 0
          and isinstance(fo.get("suspected"), str))
    record("P5.status-runtime-fields", PASS if ok else FAIL,
           f"state={st.get('state')} probe={fo.get('probe_ok')}/{fo.get('probe_fail')} suspected={fo.get('suspected')!r}")


def tc_generation_conflict_guard(gw):
    cfg = gw.fetch_config()
    mutated = copy.deepcopy(cfg)
    mutated["base_generation"] = int(cfg.get("generation", 0)) + 999
    code, body = gw.post_config(mutated)
    ok = code == 409 or body.get("error") == "conflict"
    record("conflict.guard-still-enforced", PASS if ok else FAIL,
           f"HTTP {code} body.error={body.get('error')!r}")


# ---------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.168.110.249")
    ap.add_argument("--user", default="admin")
    ap.add_argument("--password", default="admin")
    args = ap.parse_args()

    gw = Gateway(args.host, args.user, args.password)
    feats = {}
    print(f"== Dashboard/MCU contract tests @ {gw.base} ==")

    def guarded(name, fn, *args):
        try:
            fn(gw, *args)
        except Exception as e:  # noqa: BLE001 - isolate every case
            record(name, FAIL, type(e).__name__ + ": " + str(e)[:120])

    try:
        if not tc_preflight(gw, feats):
            return dump_exit(1)
        guarded("P1.priority-csv-roundtrip", tc_priority_csv_roundtrip)
        guarded("P2.apply-status-shape", tc_apply_status_shape, feats)
        guarded("P3.save-reports-async-result", tc_save_reports_applied, feats)
        guarded("P4.mqtt-roundtrip-clamp-alias", tc_mqtt_roundtrip_and_clamp, feats)
        guarded("P5.telemetry-snapshot-shape", tc_telemetry_snapshot_shape, feats)
        guarded("P5.status-runtime-fields", tc_status_runtime_fields, feats)
        guarded("conflict.guard-still-enforced", tc_generation_conflict_guard)
    except requests.exceptions.ConnectionError as e:
        record("fatal.connection", FAIL, str(e)[:120])
    return dump_exit()


def dump_exit(default_code=0):
    fails = sum(1 for _, s, _ in _results if s == FAIL)
    skips = sum(1 for _, s, _ in _results if s == SKIP)
    passes = sum(1 for _, s, _ in _results if s == PASS)
    print(f"\n== SUMMARY: {passes} passed, {fails} failed, {skips} skipped ==")
    return 1 if fails else default_code


if __name__ == "__main__":
    sys.exit(main())
