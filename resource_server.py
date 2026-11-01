from flask import Flask, request, jsonify
from jwt import PyJWKClient
import jwt
import requests
import urllib3

# Proxmox uses a self-signed certificate; silence the TLS warning
urllib3.disable_warnings()

app = Flask(__name__)

# ---- Keycloak (token validation) ----
KEYCLOAK = "http://localhost:8080/realms/homelab"
JWKS_URL = f"{KEYCLOAK}/protocol/openid-connect/certs"
jwks_client = PyJWKClient(JWKS_URL)

# ---- Proxmox (data source) ----
PVE_HOST  = "https://192.168.4.70:8006/api2/json"
PVE_NODE  = "proxmox"
# Paste the secret of the token you created (format: user!tokenid=SECRET)
PVE_TOKEN = "PVEAPIToken=root@pam!esp32=PASTE_YOUR_SECRET_HERE"
PVE_HEADERS = {"Authorization": PVE_TOKEN}


def validate_token(req):
    """Validate the Keycloak access token (signature, expiry)."""
    auth = req.headers.get("Authorization", "")
    if not auth.startswith("Bearer "):
        return None
    token = auth.replace("Bearer ", "")
    try:
        signing_key = jwks_client.get_signing_key_from_jwt(token)
        return jwt.decode(
            token,
            signing_key.key,
            algorithms=["RS256"],
            options={"verify_aud": False},
        )
    except Exception as e:
        print("Invalid token:", e)
        return None


def pve_get(path):
    """Call the Proxmox API and return the 'data' field."""
    r = requests.get(f"{PVE_HOST}{path}", headers=PVE_HEADERS,
                     verify=False, timeout=5)
    r.raise_for_status()
    return r.json()["data"]


@app.route("/stats")
def stats():
    # 1) validate the Keycloak token BEFORE anything else
    claims = validate_token(request)
    if not claims:
        return jsonify({"error": "invalid token"}), 401

    # 2) Proxmox host CPU and RAM
    node = pve_get(f"/nodes/{PVE_NODE}/status")
    cpu = round(node["cpu"] * 100, 1)
    ram = round(node["memory"]["used"] / node["memory"]["total"] * 100, 1)

    # 3) disk usage per storage
    storages = []
    for s in pve_get(f"/nodes/{PVE_NODE}/storage"):
        if s.get("total"):
            storages.append({
                "name": s["storage"],
                "used_pct": round(s["used"] / s["total"] * 100, 1),
                "total_gb": round(s["total"] / (1024 ** 3), 1),
            })

    # 4) VMs (qemu) + containers (lxc): how many are running
    everything = pve_get(f"/nodes/{PVE_NODE}/qemu") + pve_get(f"/nodes/{PVE_NODE}/lxc")
    running = sum(1 for v in everything if v.get("status") == "running")

    # 5) last backup status (most recent vzdump task)
    tasks = pve_get(f"/nodes/{PVE_NODE}/tasks?typefilter=vzdump&limit=1")
    last_backup = tasks[0].get("status", "unknown") if tasks else "none"

    return jsonify({
        "user": claims.get("preferred_username"),
        "cpu": cpu,
        "ram": ram,
        "storages": storages,
        "vms_running": running,
        "vms_total": len(everything),
        "last_backup": last_backup,
    })


app.run(host="0.0.0.0", port=5000)
