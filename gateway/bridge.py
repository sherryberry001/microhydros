#!/usr/bin/env python3
"""MicroHydros - seriell brygga

Laser telemetriramar fran en seriell port eller fran stdin och publicerar dem pa
MQTT.

Varfor det har finns: enheten talar tva vagar. Pa hardvaran ar MQTT over WiFi den
primara och den seriella utgangen en spegel for felsokning. Men uppgiften tillater
uttryckligen "seriell kommunikation till en gateway" som losning i sig, och det ar
den vagen simuleringsnoden anvander. Bryggan ar alltsa bade ett felsokningsverktyg
och en fullvardig andra transportvag.

Ramformat pa lanken:

    MH1 <topic> <json>

Synkordet MH1 ar inte pynt. En UART-lank ar aldrig ren: en ESP32 skriver ut sin
bootloaderlogg i 74880 baud innan applikationen tar over i 115200, och det blir
obegripliga tecken i mottagaren. Firmwarens egna loggrader borjar dessutom med
[mh] och ska inte tolkas som telemetri. Allt som inte borjar med MH1 raknas som
brus och kastas, och bryggan fortsatter utan att sta still.

Anvandning:

    # fran simuleringsnoden
    ../firmware/.pio/build/simnode/program --scenario faults --hours 6 | \
        python3 bridge.py --stdin --host localhost

    # fran ett riktigt kort
    python3 bridge.py --serial /dev/cu.usbserial-0001 --baud 115200
"""

import argparse
import json
import sys
import time

try:
    from mqtt_compat import make_client, publish_ok
except ImportError:
    make_client = None

SYNC = "MH1 "


class Stats:
    def __init__(self):
        self.lines = 0
        self.frames = 0
        self.noise = 0
        self.bad_json = 0
        self.published = 0
        self.failed = 0

    def report(self):
        return (
            f"rader {self.lines}, ramar {self.frames}, brus {self.noise}, "
            f"trasig json {self.bad_json}, publicerade {self.published}, "
            f"misslyckade {self.failed}"
        )


def parse_line(line, stats):
    """Plockar isar en rad. Returnerar (topic, payload) eller None."""
    stats.lines += 1
    line = line.strip()
    if not line:
        return None

    if not line.startswith(SYNC):
        # Bootloaderskrap, firmwarens egna loggrader, eller en halv rad efter en
        # omstart mitt i en sandning. Inget av det ar ett fel, det ar vad en
        # seriell lank ser ut i verkligheten.
        stats.noise += 1
        return None

    rest = line[len(SYNC):]
    if " " not in rest:
        stats.noise += 1
        return None

    topic, payload = rest.split(" ", 1)

    # Validera att nyttolasten faktiskt ar JSON innan den skickas vidare. En
    # brygga som vidarebefordrar skrap flyttar bara problemet till nasta led, dar
    # det ar svarare att felsoka.
    try:
        json.loads(payload)
    except json.JSONDecodeError:
        stats.bad_json += 1
        print(f"[bridge] trasig JSON pa {topic}, kastas", file=sys.stderr)
        return None

    stats.frames += 1
    return topic, payload


def open_source(args):
    if args.stdin:
        return sys.stdin
    import serial  # importeras bara nar den behovs, sa att --stdin funkar utan pyserial
    print(f"[bridge] oppnar {args.serial} i {args.baud} baud", file=sys.stderr)
    return serial.serial_for_url(args.serial, args.baud, timeout=1)


def read_lines(source, args):
    if args.stdin:
        for line in source:
            yield line
        return
    # Seriell port: las byte for byte och dela pa radslut. Vi kan inte lita pa
    # readline() eftersom en halv rad vid uppstart annars blockerar tills nasta
    # radslut kommer.
    buf = bytearray()
    while True:
        chunk = source.read(256)
        if not chunk:
            continue
        for b in chunk:
            if b in (10, 13):
                if buf:
                    yield buf.decode("utf-8", errors="replace")
                    buf = bytearray()
            else:
                buf.append(b)


def main():
    p = argparse.ArgumentParser(description="MicroHydros seriell brygga till MQTT")
    src = p.add_mutually_exclusive_group(required=True)
    src.add_argument("--stdin", action="store_true", help="las ramar fran stdin")
    src.add_argument("--serial", help="seriell port, t.ex. /dev/cu.usbserial-0001")
    p.add_argument("--baud", type=int, default=115200)
    p.add_argument("--host", default="localhost", help="MQTT-broker")
    p.add_argument("--port", type=int, default=1883)
    p.add_argument("--dry-run", action="store_true",
                   help="tolka ramarna men publicera inte, for felsokning")
    args = p.parse_args()

    stats = Stats()
    client = None

    if not args.dry_run:
        if make_client is None:
            print("[bridge] paho-mqtt saknas, installera med pip install paho-mqtt",
                  file=sys.stderr)
            return 2
        client = make_client("microhydros-bridge")
        print(f"[bridge] ansluter till {args.host}:{args.port}", file=sys.stderr)
        client.connect(args.host, args.port, keepalive=30)
        client.loop_start()

    source = open_source(args)
    last_report = time.time()

    try:
        for line in read_lines(source, args):
            parsed = parse_line(line, stats)
            if parsed is None:
                continue
            topic, payload = parsed

            if args.dry_run:
                print(f"{topic} {payload}")
                stats.published += 1
            else:
                # Statusramar publiceras retained, telemetri inte. Samma regel som
                # firmwaren foljer, sa att bryggan och den direkta MQTT-vagen ger
                # identiskt resultat hos mottagaren.
                retain = topic.endswith("/status")
                info = client.publish(topic, payload, qos=0, retain=retain)
                if publish_ok(info):
                    stats.published += 1
                else:
                    stats.failed += 1

            if time.time() - last_report > 10:
                print(f"[bridge] {stats.report()}", file=sys.stderr)
                last_report = time.time()
    except KeyboardInterrupt:
        pass
    finally:
        if client:
            client.loop_stop()
            client.disconnect()
        print(f"[bridge] klart: {stats.report()}", file=sys.stderr)

    return 0


if __name__ == "__main__":
    sys.exit(main())
