# esp32-proxmox-oauth

An ESP32 touchscreen that logs in with the **OAuth 2.0 Device Authorization Grant** against Keycloak, then displays a live dashboard of a Proxmox server — read from an API that validates the access token.

Built as part of my [IAM Lab](https://leoborges.com) series while moving toward identity security engineering.

## What it does

- The board shows a **QR code**. You scan it with your phone and approve the login in Keycloak.
- The board receives an **access token** (and refresh token) and calls a small **resource server** (Flask) with it.
- The resource server **validates the token's signature** against Keycloak's public keys, then fetches live data from the **Proxmox API** and returns it.
- The board displays node CPU/RAM, storage usage, running VMs, and last backup status, refreshing every 5s.
- When the access token expires, the board **refreshes it automatically** and keeps going.

## The four OAuth roles

| Role | Component |
|---|---|
| Client | the ESP32 board (`esp32_proxmox.ino`) |
| Authorization server | Keycloak |
| Resource owner | you, approving on your phone |
| Resource server | `resource_server.py` (Flask) |

The board never sees your password and never talks to Proxmox directly. The Proxmox credentials live only in the resource server.

## Architecture

```
  ┌────────────┐   1. device code / QR     ┌────────────┐
  │   ESP32    │ ────────────────────────► │  Keycloak  │
  │  (client)  │ ◄──────────────────────── │ (auth srv) │
  └────────────┘   2. tokens (after you     └────────────┘
        │             approve on phone)
        │ 3. GET /stats
        │    Authorization: Bearer <access_token>
        ▼
  ┌────────────────┐   validates token signature (JWKS)
  │ resource_server │──────────────┐
  │    (Flask)      │              ▼
  └────────────────┘        ┌────────────┐
        │ 4. Proxmox API    │  Keycloak  │
        ▼    (backend only) │   certs    │
  ┌────────────┐            └────────────┘
  │  Proxmox   │
  │    API     │
  └────────────┘
```

## Repository layout

```
esp32_proxmox.ino     # the board firmware (Arduino / C++)
proxmox_logo.h        # Proxmox logo as an RGB565 array for the display
resource_server.py    # the token-validating API (Flask)
```

## Setup

### 1. Keycloak client

Create a public client `esp32-display` in your realm with **OAuth 2.0 Device Authorization Grant** enabled and client authentication **off**.

### 2. Proxmox API token

In Proxmox: `Datacenter → Permissions → API Tokens`, create a token (e.g. `root@pam!esp32`) and copy the secret.

### 3. Resource server

```bash
python3 -m venv venv && source venv/bin/activate
pip install flask pyjwt cryptography requests
```

Set your Keycloak realm, Proxmox host/node, and the Proxmox token in `resource_server.py`, then:

```bash
python resource_server.py
```

### 4. Flash the board

- Board: **ESP32 Dev Module**, Arduino IDE.
- Libraries: `TFT_eSPI` (configured for your panel), `ArduinoJson`.
- Fill in your WiFi, Keycloak host, and resource server URL at the top of `esp32_proxmox.ino`.
- Upload over USB once; after that, OTA (WiFi) uploads work.

> **Display note:** this was built for an ELEGOO E32R28T 2.8" board whose panel is an **ILI9341** (despite some docs labeling it ST7789). Configure `TFT_eSPI`'s `User_Setup.h` for your specific board: driver, pins, and reset pin.

## Security notes

- The device holds only short-lived tokens, never your password and never the Proxmox credentials.
- The resource server rejects any request without a valid, unexpired, correctly-signed token (`401`).
- Token **refresh is handled by the client**; the resource server only validates.
- For production you'd add: TLS everywhere (this lab is plaintext HTTP on a trusted LAN), audience validation, and mTLS between the board and the API.

## Roadmap

- [ ] Persist the refresh token in NVS so the board reboots already authenticated.
- [ ] Two permission levels (read vs. act) with a VM-restart action.
- [ ] Step-up with FIDO2 for sensitive actions.
- [ ] mTLS with a private CA.

## Author

Leonardo Borges — [leoborges.com](https://leoborges.com) · [@tekeia](https://github.com/tekeia)
