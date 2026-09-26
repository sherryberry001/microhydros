#!/usr/bin/env python3
"""MicroHydros - mottagare

Prenumererar pa MQTT, validerar ramarna och lagrar dem i SQLite.

Rollfordelningen mot firmwaren ar medveten. Enheten avgor om ett matvarde ar
rimligt, eftersom bara den vet hur snabbt varden foljde pa varandra och bara den
kan reagera nar natet ligger nere. Gatewayen avgor om RAMEN ar begriplig:
schemaversion, obligatoriska falt, dubbletter och hal i lopnummerserien. Tva olika
fragor, tva olika stallen.

Gatewayen litar alltsa inte blint pa enheten, men den gor inte heller om enhetens
arbete. Den kontrollerar det den ar ensam om att kunna se.

Anvandning:
    python3 ingest.py --host localhost --db microhydros.db
"""

import argparse
import json
import os
import sqlite3
import sys
import time
from datetime import datetime, timezone

from mqtt_compat import make_client

SUPPORTED_SCHEMA_VERSIONS = {1}
TOPIC_TELEMETRY = "microhydros/v1/+/telemetry"
TOPIC_STATUS = "microhydros/v1/+/status"

REQUIRED_FRAME_FIELDS = ("schema_version", "device_id", "seq", "uptime_ms", "measurements")
REQUIRED_MEASUREMENT_FIELDS = ("sensor_id", "quantity", "location", "unit",
                               "value", "quality", "reason", "t_ms")


def now_utc():
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


class Store:
    def __init__(self, path, schema_path):
        fresh = not os.path.exists(path)
        self.conn = sqlite3.connect(path)
        self.conn.execute("PRAGMA foreign_keys = ON")
        with open(schema_path, encoding="utf-8") as fh:
            self.conn.executescript(fh.read())
        self.conn.commit()
        if fresh:
            print(f"[ingest] skapade ny databas {path}", file=sys.stderr)

    def last_seq(self, device_id):
        cur = self.conn.execute(
            "SELECT MAX(seq) FROM frames WHERE device_id = ?", (device_id,))
        row = cur.fetchone()
        return row[0] if row and row[0] is not None else None

    def insert_frame(self, frame, raw):
        """Returnerar frame_id, eller None om ramen redan fanns."""
        received = now_utc()
        trend = frame.get("trend") or {}
        health = frame.get("health") or {}
        try:
            cur = self.conn.execute(
                """INSERT INTO frames
                   (device_id, seq, schema_version, uptime_ms, received_utc,
                    trend_direction, trend_slope_per_h, trend_r2, trend_slope_ratio,
                    projected_equilibrium_c, tau_minutes, limit_exceeded,
                    minutes_to_limit, queued_frames, dropped_frames, bus_errors,
                    raw_json)
                   VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)""",
                (frame["device_id"], frame["seq"], frame["schema_version"],
                 frame["uptime_ms"], received,
                 trend.get("direction"), trend.get("slope_per_hour"),
                 trend.get("r_squared"), trend.get("slope_ratio"),
                 trend.get("projected_equilibrium_c"), trend.get("tau_minutes"),
                 1 if trend.get("limit_exceeded") else 0,
                 trend.get("minutes_to_limit"),
                 health.get("queued_frames"), health.get("dropped_frames"),
                 health.get("bus_errors"), raw))
        except sqlite3.IntegrityError:
            # Samma lopnummer igen. Det hander nar en enhet har skickat om en ram
            # ur sin kö efter ett natavbrott och den forsta anda kom fram. Att
            # bara ignorera den ar ratt: kön ar med flit enkel, och idempotensen
            # ligger har istallet.
            return None

        frame_id = cur.lastrowid
        rows = []
        for m in frame["measurements"]:
            rows.append((frame_id, frame["device_id"], m["sensor_id"], m["quantity"],
                         m["location"], m["unit"], m["value"], m["quality"],
                         m["reason"], m["t_ms"], received))
        self.conn.executemany(
            """INSERT INTO measurements
               (frame_id, device_id, sensor_id, quantity, location, unit,
                value, quality, reason, device_t_ms, received_utc)
               VALUES (?,?,?,?,?,?,?,?,?,?,?)""", rows)
        self.conn.commit()
        return frame_id


def validate_frame(payload):
    """Returnerar (frame, None) eller (None, felmeddelande)."""
    try:
        frame = json.loads(payload)
    except json.JSONDecodeError as exc:
        return None, f"trasig JSON: {exc}"

    if not isinstance(frame, dict):
        return None, "ramen ar inte ett objekt"

    missing = [f for f in REQUIRED_FRAME_FIELDS if f not in frame]
    if missing:
        return None, f"saknade falt: {', '.join(missing)}"

    version = frame["schema_version"]
    if version not in SUPPORTED_SCHEMA_VERSIONS:
        # Det har ar hela anledningen till att schema_version finns. En enhet med
        # nyare firmware kan dyka upp pa samma broker, och da ska mottagaren saga
        # ifran tydligt istallet for att tolka falten fel och lagra skrap.
        return None, f"schemaversion {version} stods inte"

    if not isinstance(frame["measurements"], list) or not frame["measurements"]:
        return None, "measurements ar tom eller inte en lista"

    for i, m in enumerate(frame["measurements"]):
        missing = [f for f in REQUIRED_MEASUREMENT_FIELDS if f not in m]
        if missing:
            return None, f"matning {i} saknar {', '.join(missing)}"
        if m["quality"] not in ("good", "suspect", "bad"):
            return None, f"matning {i} har okand kvalitet {m['quality']}"
        if m["quality"] == "bad" and m["value"] is not None:
            # Ett underkant matvarde ska inte ha nagot tal. Skulle det ha det ar
            # enheten och gatewayen oense om kontraktet, och det vill vi veta.
            return None, f"matning {i} ar bad men har vardet {m['value']}"

    return frame, None


class Ingest:
    def __init__(self, store, verbose=False):
        self.store = store
        self.verbose = verbose
        self.frames_ok = 0
        self.frames_rejected = 0
        self.frames_duplicate = 0
        self.gaps_detected = 0
        self.suspect_values = 0
        self.bad_values = 0
        self.suppressed_low_r2 = 0
        self.suppressed_short_window = 0
        self.limit_breaches = 0
        self.warnings = []

    def on_telemetry(self, payload):
        frame, error = validate_frame(payload)
        if error:
            self.frames_rejected += 1
            print(f"[ingest] ram avvisad: {error}", file=sys.stderr)
            return

        device = frame["device_id"]
        seq = frame["seq"]

        previous = self.store.last_seq(device)
        if previous is not None and seq > previous + 1:
            missing = seq - previous - 1
            self.gaps_detected += missing
            msg = (f"hal i lopnummerserien for {device}: "
                   f"{missing} ram(ar) saknas mellan {previous} och {seq}")
            self.warnings.append(msg)
            print(f"[ingest] {msg}", file=sys.stderr)

        frame_id = self.store.insert_frame(frame, payload)
        if frame_id is None:
            self.frames_duplicate += 1
            return

        self.frames_ok += 1
        for m in frame["measurements"]:
            if m["quality"] == "suspect":
                self.suspect_values += 1
                if self.verbose:
                    print(f"[ingest] {device}/{m['sensor_id']} tveksam: "
                          f"{m['reason']} (varde {m['value']})", file=sys.stderr)
            elif m["quality"] == "bad":
                self.bad_values += 1
                if self.verbose:
                    print(f"[ingest] {device}/{m['sensor_id']} underkand: "
                          f"{m['reason']}", file=sys.stderr)

        self.check_forecast(device, frame.get("trend") or {})

    # Larmpolicyn ligger har och inte i firmwaren. Enheten rapporterar fakta om
    # forloppet, mottagaren avgor vad som ar vart att vacka nagon for. Vilken
    # marginal som ar rimlig beror pa odlingen och pa vem som far notisen, inte
    # pa elektroniken, och den ska ga att andra utan att flasha om enheter.
    #
    # Sjalva fysiken, alltsa fragan om forloppet ens ar pa vag mot gransen, ligger
    # daremot i firmwaren. Den har en modell av skapet och kan svara pa det aven
    # nar natet ligger nere. Se Trend::projected_equilibrium i TrendEstimator.h.
    HORIZON_MIN = 60.0   # sa langt fram vi bryr oss
    MIN_R2 = 0.80        # under detta ar lutningen brus, inte en trend
    MIN_SAMPLES = 30     # minst 5 minuters data innan vi tror pa en prognos

    def check_forecast(self, device, trend):
        channel = trend.get("channel", "?")
        limit = trend.get("limit_c")

        # Redan over gransen. Det ar inte en forvarning utan ett pagaende
        # problem, och det ar ett annat meddelande till odlaren. En forvarning
        # sager "du hinner gora nagot", det har sager "det sker nu".
        if trend.get("limit_exceeded"):
            msg = (f"GRANS OVERSKRIDEN {device}: {channel} ligger over "
                   f"{limit} C just nu")
            if msg not in self.warnings[-1:]:
                self.warnings.append(msg)
                print(f"[ingest] {msg}", file=sys.stderr)
            self.limit_breaches += 1
            return

        mins = trend.get("minutes_to_limit")
        if mins is None or not (0 < mins < self.HORIZON_MIN):
            return

        samples = trend.get("samples") or 0
        r2 = trend.get("r_squared") or 0.0

        # Ett kort fonster ger en oparlitlig modell. Under de forsta minuterna
        # efter uppstart ser en exponentiell invangning ut som en rat linje, och
        # jamvikten gar inte att skatta. Kostnaden ar att systemet inte kan
        # forvarna under sina forsta fem minuter. Med en prognoshorisont pa en
        # timme, och en enhet som ska sta i manader, ar det en billig vaxel.
        if samples < self.MIN_SAMPLES:
            self.suppressed_short_window += 1
            return

        # Lag forklaringsgrad betyder att punkterna sprider sig sa mycket att
        # lutningen inte sager nagot. Da ar prognosen brus.
        if r2 < self.MIN_R2:
            self.suppressed_low_r2 += 1
            return

        eq = trend.get("projected_equilibrium_c")
        eq_text = "okand jamvikt" if eq is None else f"pa vag mot {eq:.1f} C"
        msg = (f"FORVARNING {device}: {channel} nar {limit} C om "
               f"{mins:.0f} min ({trend.get('slope_per_hour'):.2f} C/h, "
               f"{eq_text}, r2 {r2:.2f})")
        self.warnings.append(msg)
        print(f"[ingest] {msg}", file=sys.stderr)

    def summary(self):
        return (f"ramar lagrade {self.frames_ok}, avvisade {self.frames_rejected}, "
                f"dubbletter {self.frames_duplicate}, saknade {self.gaps_detected}, "
                f"tveksamma matvarden {self.suspect_values}, "
                f"underkanda {self.bad_values}, "
                f"forvarningar {len([w for w in self.warnings if w.startswith('FORVARNING')])}, "
                f"gransoverskridanden {self.limit_breaches}, "
                f"undertryckta (kort fonster {self.suppressed_short_window}, "
                f"lag r2 {self.suppressed_low_r2})")


def main():
    p = argparse.ArgumentParser(description="MicroHydros MQTT-mottagare")
    p.add_argument("--host", default="localhost")
    p.add_argument("--port", type=int, default=1883)
    p.add_argument("--db", default="microhydros.db")
    p.add_argument("--schema", default=os.path.join(os.path.dirname(__file__), "schema.sql"))
    p.add_argument("--verbose", action="store_true")
    p.add_argument("--run-seconds", type=float, default=0,
                   help="avsluta efter sa har manga sekunder, 0 = kor tills avbrott")
    args = p.parse_args()

    store = Store(args.db, args.schema)
    ingest = Ingest(store, verbose=args.verbose)

    def on_connect(client, rc):
        print(f"[ingest] ansluten till {args.host}:{args.port} (rc={rc})", file=sys.stderr)
        client.subscribe([(TOPIC_TELEMETRY, 0), (TOPIC_STATUS, 0)])

    def on_message(topic, raw):
        payload = raw.decode("utf-8", errors="replace")
        if topic.endswith("/telemetry"):
            ingest.on_telemetry(payload)
        elif topic.endswith("/status"):
            try:
                status = json.loads(payload)
                print(f"[ingest] status {status.get('device_id')}: "
                      f"{status.get('state')}", file=sys.stderr)
            except json.JSONDecodeError:
                print("[ingest] trasig statusram", file=sys.stderr)

    client = make_client("microhydros-ingest", on_connect=on_connect,
                         on_message=on_message)
    client.connect(args.host, args.port, keepalive=30)

    started = time.time()
    try:
        if args.run_seconds > 0:
            while time.time() - started < args.run_seconds:
                client.loop(timeout=0.5)
        else:
            client.loop_forever()
    except KeyboardInterrupt:
        pass
    finally:
        client.disconnect()
        print(f"[ingest] klart: {ingest.summary()}", file=sys.stderr)

    return 0


if __name__ == "__main__":
    sys.exit(main())
