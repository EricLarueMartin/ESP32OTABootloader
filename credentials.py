"""Generate a unique device password in an explicitly selected private directory.
Never prints a password and never replaces an existing credential.
"""
import argparse, hashlib, json, re, secrets
from pathlib import Path

def create(directory, netid):
    if not re.fullmatch(r"[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?", netid):
        raise ValueError("Invalid netid")
    directory = directory.resolve()
    if directory == Path(__file__).resolve().parent or Path(__file__).resolve().parent in directory.parents:
        raise ValueError("Credentials must be outside the source repository")
    directory.mkdir(parents=True, exist_ok=True)
    key = secrets.token_hex(32)
    value = dict(netid=netid, key=key, auth_sha256=hashlib.sha256(key.encode()).hexdigest(),
                 address=None, mac=None, provisioned=False)
    with (directory / (netid + ".json")).open("x", encoding="utf-8") as f:
        json.dump(value, f, indent=2)
    return directory / (netid + ".json")

if __name__ == "__main__":
    ap = argparse.ArgumentParser(); ap.add_argument("--directory", type=Path, required=True)
    ap.add_argument("netid"); a = ap.parse_args()
    print("Created credential file:", create(a.directory, a.netid))
