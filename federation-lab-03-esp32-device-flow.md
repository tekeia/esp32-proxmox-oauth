---
title: "Federation Lab #3: An ESP32 that logs in with the OAuth 2.0 Device Flow"
date: 2026-11-01
description: "I built all four OAuth roles on real hardware: an ESP32 authenticates with the Device Authorization Grant, calls a token-validating API, and shows a live Proxmox dashboard."
series: "Federation Lab"
part: 3
project: sso-federation-lab
tags: ["IAM", "OAuth 2.0", "OIDC", "Keycloak", "ESP32", "Device Flow"]
---

In the [OIDC post](/blog/federation-lab-02-oidc-tokens-postman) I pulled tokens from Keycloak with Postman and finally understood the ID, access and refresh tokens. Reading tokens from the outside only goes so far, though. This time I wanted to *build* something that uses them, with all four OAuth roles in play, on real hardware.

The result: an ESP32 with a small touchscreen that logs in through the **OAuth 2.0 Device Authorization Grant** (the "scan this code to sign in" flow you see on smart TVs), then displays a live dashboard of my Proxmox server.

## The four roles, mapped to hardware

Everything in OAuth comes down to four actors. For the first time I had a physical thing playing each one:

| Role | In this project |
|---|---|
| **Client** | the ESP32 board |
| **Authorization server** | Keycloak (in an LXC container on Proxmox) |
| **Resource owner** | me, approving on my phone |
| **Resource server** | a small Flask API I wrote that validates the token |

The whole point was to make the board never touch my password and never talk to Proxmox directly.

## Why device flow

The board has no keyboard. Typing a password on a tiny resistive touchscreen would be miserable, and worse, it would mean the device handles my credentials. The **Device Authorization Grant** solves exactly this: the device shows a short code and a URL, you approve on a *different* device (your phone), and the board polls until it's approved.

The flow, step by step:

1. The board POSTs to Keycloak's `auth/device` endpoint with its `client_id`.
2. Keycloak returns a `user_code`, a `verification_uri`, a `device_code`, and a poll `interval`.
3. The board renders the approval URL as a **QR code** on the screen.
4. I scan it, log in as my Keycloak user, and approve.
5. Meanwhile the board polls the token endpoint every few seconds with the `device_code`.
6. Once I approve, that poll returns the tokens. Done.

<!-- screenshot: ESP32 showing the QR code and user code -->

The board uses the ESP32's built-in QR library to draw the `verification_uri_complete` (the URL with the code already embedded), so scanning drops me straight onto the approval screen.

## The resource server: the part that actually matters

This is the piece I care about most. The board doesn't get Proxmox data directly. It calls my own API, and that API's single most important job is to **validate the access token before returning anything**.

The validation fetches Keycloak's public keys (JWKS) and verifies the token's signature — exactly the mechanism I'd been configuring in products without ever implementing myself:

```python
from jwt import PyJWKClient
import jwt

jwks_client = PyJWKClient(f"{KEYCLOAK}/protocol/openid-connect/certs")

def validate_token(req):
    token = req.headers.get("Authorization", "").replace("Bearer ", "")
    signing_key = jwks_client.get_signing_key_from_jwt(token)
    return jwt.decode(token, signing_key.key, algorithms=["RS256"],
                      options={"verify_aud": False})
```

No token, or a bad signature, gets a flat `401`. I tested this by curling the endpoint with no header, and it refused, which is the whole point.

Only after the token checks out does the API call the **Proxmox API** on the backend (holding the Proxmox credentials that the board never sees) and return node CPU/RAM, storage usage, running VMs, and the last backup status.

<!-- screenshot: ESP32 showing the Proxmox dashboard -->

This is the "backend for frontend" / gateway pattern: the device authenticates with a short-lived token against my API; my API is the only thing that holds the real infrastructure credentials. If the board is stolen, it holds nothing but an expiring token.

## Refresh tokens, the hard way

Access tokens live about five minutes. My first version worked, then started throwing `401`s a few minutes later — a perfect live demonstration of expiry. The fix is the refresh token: when the API returns `401`, the board uses its stored refresh token to get a new access token and retries, without me approving anything again.

The key design lesson: **refresh is the client's job, not the resource server's.** The board holds the tokens, so the board renews them. The API only ever validates and, if expired, says no.

## The insight I didn't expect: do I even need OIDC here?

While wiring up the username display, I realized something. The board requests `scope=openid`, which makes this OIDC and issues an ID token. But I never read the ID token. The username on screen comes from my API reading `preferred_username` out of the **access token**, because Keycloak happens to include it there.

So for *this* project, plain OAuth would behave identically. That led me down a good rabbit hole:

- The **access token** is meant for the API (authorization: what you can do). Relying on it for identity is a convenience Keycloak allows, not a guarantee, and in many providers the access token is an opaque string you can't read at all.
- The **ID token** is the one addressed to the client, specifically for answering "who is this user", in a standardized format.
- The rule I'm keeping: **access token for authorization, ID token for identity, don't substitute one for the other.**

It "works" without OIDC because my *API* reads the identity, not the client, which is the more acceptable side of that line. But understanding *why* it works was worth more than the feature itself.

## Security notes (this is a lab)

Being honest about what this is and isn't:

- Everything runs over plaintext **HTTP** on a trusted LAN. Tokens are in the clear; production needs TLS everywhere.
- The resource server validates the signature and expiry, but not the token's **audience** — a hardening step I left as a known gap.
- It authenticates but doesn't yet **authorize** by role: any valid realm user can read the dashboard.
- The Proxmox token is broad. A dedicated read-only (`PVEAuditor`) user would be the right call.

I'd rather ship it labeled as a lab and name these gaps than pretend it's production.

## What I built, in one line

A device with no keyboard that authenticates a human through a phone, receives a short-lived token, and uses it to read a protected API that guards my infrastructure, with automatic token renewal. Every layer enforces something.

## Next

- Persist the refresh token in the ESP32's non-volatile storage so it comes back authenticated after a reboot.
- Two permission levels (read vs. act) with a "restart VM" button, gated by a step-up with FIDO2.
- mTLS between the board and the API using my own CA.

Code on GitHub: [esp32-proxmox-oauth](https://github.com/tekeia/esp32-proxmox-oauth).
