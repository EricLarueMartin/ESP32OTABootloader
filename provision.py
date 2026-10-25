"""One-time USB provisioning. Wi-Fi/password values never appear in arguments or output."""
import argparse, base64, hashlib, json, time
from pathlib import Path
import serial

def status(conn, seconds=10):
    until=time.monotonic()+seconds
    while time.monotonic()<until:
        conn.write(b"netstatus\n");conn.flush()
        line=conn.readline()
        try: value=json.loads(line)
        except (ValueError,UnicodeError):continue
        if value.get("role")=="recovery":return value
    raise RuntimeError("No recovery identity; do not repeat flashing")

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--port",required=True)
    ap.add_argument("--expected-mac",required=True)
    ap.add_argument("--key-file",type=Path,required=True)
    ap.add_argument("--wifi-file",type=Path,required=True,help="Private JSON with ssid and password")
    a=ap.parse_args();cfg=json.loads(a.key_file.read_text());wifi=json.loads(a.wifi_file.read_text())
    if cfg.get("provisioned"):raise RuntimeError("Already provisioned; reconcile rather than replace")
    if cfg["auth_sha256"]!=hashlib.sha256(cfg["key"].encode()).hexdigest():raise ValueError("Password/hash mismatch")
    with serial.Serial(a.port,115200,timeout=.25) as conn:
        initial=status(conn)
        if initial["mac"].lower()!=a.expected_mac.lower():raise RuntimeError("Wrong device")
        if initial["configured"]:raise RuntimeError("Device already configured; preserve its credential")
        line=b"RECOVERYCFG "+base64.b64encode(wifi["ssid"].encode())+b" "+base64.b64encode(wifi["password"].encode())+b" "+cfg["auth_sha256"].encode()+b" "+cfg["netid"].encode()+b"\n"
        conn.write(line);conn.flush();del line,wifi
        time.sleep(3)
        end=time.monotonic()+60
        while time.monotonic()<end:
            value=status(conn)
            if value["mac"].lower()!=a.expected_mac.lower():raise RuntimeError("Identity changed")
            if value["configured"] and value["ip"]!="0.0.0.0":break
        else:raise RuntimeError("Provisioning outcome uncertain; preserve key and inspect status")
    from client import request
    candidate=dict(cfg,address=value["ip"])
    live=json.loads(request(candidate,"/v1/status"))
    if live.get("netid")!=cfg["netid"]:raise RuntimeError("Network identity mismatch")
    cfg.update(address=value["ip"],mac=a.expected_mac,provisioned=True)
    # Credential already exists before device writes; failure here cannot lose its key.
    a.key_file.write_text(json.dumps(cfg,indent=2)+"\n")
    print("Provisioned and authenticated",cfg["netid"],cfg["address"],cfg["mac"])
if __name__=="__main__":main()
