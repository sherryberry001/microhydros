"""MicroHydros - tunn kompatibilitetshinna over paho-mqtt

paho-mqtt 2.0 bytte callback-API och gjorde det gamla utfasat. Signaturen for
on_connect andrades fran

    on_connect(client, userdata, flags, rc)

till

    on_connect(client, userdata, flags, reason_code, properties)

Koden i ingest.py och bridge.py ska inte behova bry sig om vilken version som
rakar vara installerad. Den har modulen normaliserar skillnaden till ett anrop:

    client = make_client("mitt-id", on_connect=..., on_message=...)

on_connect far alltid (client, rc_int), dar rc_int ar 0 vid lyckad anslutning.

Att lagga tio rader har istallet for att lasa fast versionen i requirements.txt
ar ett medvetet val: prototypen ska kunna plockas upp av nagon annan om ett halvar
utan att forst behova felsoka en pip-konflikt.
"""

import paho.mqtt.client as mqtt

_HAS_V2 = hasattr(mqtt, "CallbackAPIVersion")


def make_client(client_id, on_connect=None, on_message=None):
    if _HAS_V2:
        client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=client_id)
    else:
        client = mqtt.Client(client_id=client_id)

    if on_connect is not None:
        if _HAS_V2:
            def _on_connect(c, userdata, flags, reason_code, properties=None):
                # I VERSION2 ar reason_code ett objekt som jamfors mot 0 via
                # sitt heltalsvarde. Vi normaliserar till int.
                code = getattr(reason_code, "value", reason_code)
                on_connect(c, int(code))
        else:
            def _on_connect(c, userdata, flags, rc):
                on_connect(c, int(rc))
        client.on_connect = _on_connect

    if on_message is not None:
        def _on_message(c, userdata, message):
            on_message(message.topic, message.payload)
        client.on_message = _on_message

    return client


def publish_ok(info):
    """Sant om publiceringen accepterades av klienten."""
    return info.rc == mqtt.MQTT_ERR_SUCCESS
