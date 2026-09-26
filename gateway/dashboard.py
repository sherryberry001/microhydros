#!/usr/bin/env python3
"""MicroHydros - enkel instrumentpanel

Laser SQLite och visar de senaste matvardena, en kurva per kanal och en lista
over underkanda matvarden.

Den har ar avsiktligt liten. Uppgiften listar "avancerat webbgranssnitt" under
det som inte ar obligatoriskt, och sager uttryckligen att omfattning inte ger
hogre betyg. Panelen finns for att en demonstration behover nagot att peka pa och
for att visa att den lagrade datan faktiskt gar att anvanda. Inga externa
bibliotek, ingen byggkedja, bara standardbiblioteket och en handritad SVG.

Anvandning:
    python3 dashboard.py --db microhydros.db --port 8080
"""

import argparse
import json
import os
import sqlite3
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

SENSOR_LABELS = {
    "air_in": ("Luft inne", "#e8763a"),
    "air_out": ("Luft ute", "#4a90d9"),
    "water": ("Vatten", "#2d9d78"),
    "rh_in": ("Fukt inne", "#8a63c7"),
    "dew_in": ("Daggpunkt", "#b0863a"),
    "vpd_in": ("VPD", "#c2557a"),
}


def query(db, sql, params=()):
    conn = sqlite3.connect(f"file:{db}?mode=ro", uri=True)
    conn.row_factory = sqlite3.Row
    try:
        return [dict(r) for r in conn.execute(sql, params).fetchall()]
    finally:
        conn.close()


def devices(db):
    """Alla enheter som har rapporterat, nyast aktiva forst."""
    return query(db, """
        SELECT device_id, COUNT(*) AS n_frames, MAX(received_utc) AS last_seen
        FROM frames GROUP BY device_id ORDER BY last_seen DESC
    """)


def latest(db, device):
    return query(db, """
        SELECT m.sensor_id, m.quantity, m.unit, m.value, m.quality, m.reason,
               m.received_utc
        FROM measurements m
        JOIN (SELECT sensor_id, MAX(id) AS max_id
              FROM measurements WHERE device_id = ? GROUP BY sensor_id) last
          ON last.max_id = m.id
        ORDER BY m.sensor_id
    """, (device,))


def series(db, sensor_id, device, limit=400):
    rows = query(db, """
        SELECT value, received_utc, quality
        FROM measurements
        WHERE sensor_id = ? AND device_id = ? AND value IS NOT NULL
        ORDER BY id DESC LIMIT ?
    """, (sensor_id, device, limit))
    return list(reversed(rows))


def health(db, device):
    return query(db, """
        SELECT * FROM v_sensor_health WHERE device_id = ? ORDER BY sensor_id
    """, (device,))


def problems(db, device, limit=40):
    return query(db, """
        SELECT sensor_id, quality, reason, value, received_utc
        FROM measurements
        WHERE quality != 'good' AND device_id = ?
        ORDER BY id DESC LIMIT ?
    """, (device, limit))


def trend(db, device):
    rows = query(db, """
        SELECT device_id, seq, trend_direction, trend_slope_per_h, trend_r2,
               projected_equilibrium_c, tau_minutes, limit_exceeded,
               minutes_to_limit, queued_frames, dropped_frames, bus_errors,
               received_utc
        FROM frames WHERE device_id = ? ORDER BY id DESC LIMIT 1
    """, (device,))
    return rows[0] if rows else None


def gaps(db, device):
    return query(db, """
        SELECT * FROM v_sequence_gaps WHERE device_id = ?
        ORDER BY previous_seq DESC LIMIT 20
    """, (device,))


def sparkline(points, colour, width=560, height=110):
    """Ritar en kurva som inline-SVG. Inga bibliotek, ingen byggkedja."""
    vals = [p["value"] for p in points]
    if len(vals) < 2:
        return '<p class="muted">för få mätvärden ännu</p>'

    lo, hi = min(vals), max(vals)
    if hi - lo < 1e-6:
        lo -= 0.5
        hi += 0.5
    pad = (hi - lo) * 0.12
    lo -= pad
    hi += pad

    dx = width / (len(vals) - 1)
    coords = []
    for i, v in enumerate(vals):
        x = i * dx
        y = height - (v - lo) / (hi - lo) * height
        coords.append(f"{x:.1f},{y:.1f}")

    path = " ".join(coords)
    area = f"0,{height} " + path + f" {width},{height}"
    return (
        f'<svg viewBox="0 0 {width} {height}" class="spark" preserveAspectRatio="none">'
        f'<polygon points="{area}" fill="{colour}" opacity="0.13"/>'
        f'<polyline points="{path}" fill="none" stroke="{colour}" stroke-width="2" '
        f'stroke-linejoin="round" stroke-linecap="round"/>'
        f'</svg>'
        f'<div class="scale"><span>{hi:.1f}</span><span>{lo:.1f}</span></div>'
    )


PAGE = """<!doctype html>
<html lang="sv"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>MicroHydros</title>
<style>
 :root {{ color-scheme: light dark; --bg:#12140f; --card:#1c1f18; --fg:#e8eadf;
          --muted:#8f9484; --line:#2c3026; }}
 * {{ box-sizing:border-box; }}
 body {{ margin:0; padding:24px; background:var(--bg); color:var(--fg);
         font:15px/1.5 -apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif; }}
 h1 {{ font-size:20px; margin:0 0 4px; }}
 .sub {{ color:var(--muted); font-size:13px; margin-bottom:22px; }}
 .grid {{ display:grid; gap:14px; grid-template-columns:repeat(auto-fit,minmax(290px,1fr)); }}
 .card {{ background:var(--card); border:1px solid var(--line); border-radius:10px; padding:16px; }}
 .card h2 {{ font-size:13px; font-weight:600; color:var(--muted); margin:0 0 10px;
             text-transform:uppercase; letter-spacing:.06em; }}
 .value {{ font-size:32px; font-variant-numeric:tabular-nums; }}
 .value small {{ font-size:14px; color:var(--muted); margin-left:4px; }}
 .spark {{ width:100%; height:110px; margin-top:10px; display:block; }}
 .scale {{ display:flex; justify-content:space-between; font-size:11px; color:var(--muted); }}
 .badge {{ display:inline-block; font-size:11px; padding:2px 8px; border-radius:999px;
           border:1px solid var(--line); }}
 .good {{ color:#7fc99a; border-color:#2f5540; }}
 .suspect {{ color:#e2b34f; border-color:#5a4a1e; }}
 .bad {{ color:#e07070; border-color:#5a2a2a; }}
 table {{ width:100%; border-collapse:collapse; font-size:13px; }}
 th,td {{ text-align:left; padding:6px 8px; border-bottom:1px solid var(--line); }}
 th {{ color:var(--muted); font-weight:500; }}
 .muted {{ color:var(--muted); font-size:13px; }}
 .wide {{ grid-column:1/-1; }}
 code {{ font-size:12px; color:var(--muted); }}
 .nav {{ display:flex; flex-wrap:wrap; gap:8px; margin-bottom:18px; }}
 .nav a {{ display:flex; gap:8px; align-items:center; padding:6px 12px;
           border:1px solid var(--line); border-radius:999px; font-size:13px;
           color:var(--muted); text-decoration:none; background:var(--card); }}
 .nav a.on {{ color:var(--fg); border-color:#4a7a5c; }}
 .nav a span {{ font-size:11px; opacity:.65; }}
</style></head><body>
<h1>MicroHydros</h1>
<div class="sub">{subtitle}</div>
{nav}
<div class="grid">{cards}</div>
<div class="grid" style="margin-top:14px">{tables}</div>
<p class="muted" style="margin-top:22px">Uppdateras var 5:e sekund. Data läses
ur SQLite som fylls av <code>ingest.py</code>.</p>
<script>setTimeout(function(){{location.reload()}},5000)</script>
</body></html>"""


def render(db, device=None):
    devs = devices(db)
    if not devs:
        return PAGE.format(subtitle="Väntar på första mätvärdet", nav="",
                           cards="", tables="")

    known = [d["device_id"] for d in devs]
    if device not in known:
        device = known[0]  # senast aktiva

    # Enhetsvaljare. Kunden beskrev flera odlingsenheter mot en gemensam tjanst,
    # sa panelen ar byggd for mer an en enhet fran borjan aven om prototypen
    # oftast kor en i taget.
    nav_parts = []
    for d in devs:
        active = ' class="on"' if d["device_id"] == device else ""
        nav_parts.append(
            f'<a href="/?device={d["device_id"]}"{active}>{d["device_id"]}'
            f'<span>{d["n_frames"]}</span></a>')
    nav = '<div class="nav">' + "".join(nav_parts) + "</div>"

    rows = latest(db, device)
    if not rows:
        return PAGE.format(subtitle=device, nav=nav, cards="", tables="")

    cards = []
    for r in rows:
        label, colour = SENSOR_LABELS.get(r["sensor_id"], (r["sensor_id"], "#888"))
        value = "-" if r["value"] is None else f"{r['value']:.2f}"
        unit = {"degC": "°C", "percentRH": "% RH", "kPa": "kPa"}.get(
            r["unit"], r["unit"])
        badge = f'<span class="badge {r["quality"]}">{r["quality"]}'
        badge += "" if r["reason"] == "none" else f" · {r['reason']}"
        badge += "</span>"
        chart = sparkline(series(db, r["sensor_id"], device), colour)
        cards.append(
            f'<div class="card"><h2>{label}</h2>'
            f'<div class="value">{value}<small>{unit}</small></div>'
            f'{badge}{chart}</div>')

    t = trend(db, device)
    tr_html = '<p class="muted">ingen ram annu</p>'
    if t:
        if t["limit_exceeded"]:
            forecast = '<span class="bad">gransen ar redan passerad</span>'
        elif t["minutes_to_limit"] is None:
            forecast = "gränsen nås inte"
        else:
            forecast = f'<span class="suspect">{t["minutes_to_limit"]:.0f} min till gränsen</span>'
        eq = t["projected_equilibrium_c"]
        eq_text = "kunde inte skattas" if eq is None else f"{eq:.1f} °C"
        tau = t["tau_minutes"]
        tau_text = "-" if tau is None else f"{tau:.0f} min"
        tr_html = (
            f'<table>'
            f'<tr><th>Riktning</th><td>{t["trend_direction"]}</td></tr>'
            f'<tr><th>Lutning</th><td>{(t["trend_slope_per_h"] or 0):.2f} °C/h</td></tr>'
            f'<tr><th>Förklaringsgrad</th><td>{(t["trend_r2"] or 0):.2f}</td></tr>'
            f'<tr><th>På väg mot</th><td>{eq_text}</td></tr>'
            f'<tr><th>Tidskonstant</th><td>{tau_text}</td></tr>'
            f'<tr><th>Förvarning</th><td>{forecast}</td></tr>'
            f'<tr><th>Ramar i kö</th><td>{t["queued_frames"]}</td></tr>'
            f'<tr><th>Tappade ramar</th><td>{t["dropped_frames"]}</td></tr>'
            f'<tr><th>Bussfel</th><td>{t["bus_errors"]}</td></tr>'
            f'</table>')

    h_rows = "".join(
        f'<tr><td>{h["sensor_id"]}</td><td>{h["n_total"]}</td>'
        f'<td class="good">{h["n_good"]}</td>'
        f'<td class="suspect">{h["n_suspect"]}</td>'
        f'<td class="bad">{h["n_bad"]}</td>'
        f'<td>{h["min_value"]} ... {h["max_value"]}</td></tr>'
        for h in health(db, device))

    p_parts = []
    for p in problems(db, device):
        shown = "-" if p["value"] is None else f'{p["value"]:.2f}'
        p_parts.append(
            f'<tr><td>{p["sensor_id"]}</td>'
            f'<td class="{p["quality"]}">{p["quality"]}</td>'
            f'<td>{p["reason"]}</td><td>{shown}</td>'
            f'<td>{p["received_utc"][11:19]}</td></tr>')
    p_rows = "".join(p_parts)
    if not p_rows:
        p_rows = '<tr><td colspan="5" class="muted">inga avvikelser</td></tr>'

    g = gaps(db, device)
    g_html = ('<p class="muted">inga hål i löpnummerserien</p>' if not g else
              "".join(f'<p class="bad">{x["missing_frames"]} ram(ar) saknas mellan '
                      f'{x["previous_seq"]} och {x["next_seq"]}</p>' for x in g))

    tables = (
        f'<div class="card"><h2>Trend och hälsa</h2>{tr_html}</div>'
        f'<div class="card wide"><h2>Mätvärden per givare</h2><table>'
        f'<tr><th>Givare</th><th>Antal</th><th>Godkända</th><th>Tveksamma</th>'
        f'<th>Underkända</th><th>Spann</th></tr>{h_rows}</table></div>'
        f'<div class="card wide"><h2>Senaste avvikelser</h2><table>'
        f'<tr><th>Givare</th><th>Kvalitet</th><th>Orsak</th><th>Värde</th>'
        f'<th>Tid</th></tr>{p_rows}</table></div>'
        f'<div class="card wide"><h2>Dataförlust</h2>{g_html}</div>')

    subtitle = (f"{device} · senast mottaget "
                f"{rows[0]['received_utc'][:19].replace('T', ' ')} UTC")
    return PAGE.format(subtitle=subtitle, nav=nav, cards="".join(cards),
                       tables=tables)


class Handler(BaseHTTPRequestHandler):
    db = "microhydros.db"

    def _default_device(self):
        d = devices(self.db)
        return d[0]["device_id"] if d else ""

    def log_message(self, *a):
        pass  # tyst, annars dranks terminalen av uppdateringarna var femte sekund

    def do_GET(self):
        path = urlparse(self.path).path
        try:
            q = parse_qs(urlparse(self.path).query)
            device = q.get("device", [None])[0]

            if path == "/":
                body = render(self.db, device).encode("utf-8")
                ctype = "text/html; charset=utf-8"
            elif path == "/api/devices":
                body = json.dumps(devices(self.db), ensure_ascii=False).encode()
                ctype = "application/json"
            elif path == "/api/latest":
                body = json.dumps(latest(self.db, device or self._default_device()),
                                  ensure_ascii=False).encode()
                ctype = "application/json"
            elif path == "/api/series":
                sensor = q.get("sensor", ["air_in"])[0]
                body = json.dumps(series(self.db, sensor,
                                         device or self._default_device()),
                                  ensure_ascii=False).encode()
                ctype = "application/json"
            elif path == "/api/health":
                body = json.dumps(health(self.db, device or self._default_device()),
                                  ensure_ascii=False).encode()
                ctype = "application/json"
            else:
                self.send_error(404)
                return
        except sqlite3.OperationalError as exc:
            self.send_error(503, f"databasen inte redo: {exc}")
            return

        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def main():
    p = argparse.ArgumentParser(description="MicroHydros instrumentpanel")
    p.add_argument("--db", default="microhydros.db")
    p.add_argument("--port", type=int, default=8080)
    args = p.parse_args()

    Handler.db = os.path.abspath(args.db)
    server = ThreadingHTTPServer(("0.0.0.0", args.port), Handler)
    print(f"[dashboard] http://localhost:{args.port}  (databas {Handler.db})")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
