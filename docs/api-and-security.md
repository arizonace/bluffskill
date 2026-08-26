# REST API and security design

## Prototype endpoints

All endpoints use JSON and return an `X-BluffSkill-Sequence` header when they are scoped to a competition. The current prototype implements health, competition listing/creation, and reference-player creation.

| Method | Route | Purpose |
| --- | --- | --- |
| `POST` | `/v1/competitions` | create a single-table Texas Hold'em tournament |
| `POST` | `/v1/competitions/{competition}/reference-players` | add 1–7 in-process bot players |
| `POST` | `/v1/competitions/{competition}/tables/{table}/players` | attach a human API player |
| `GET` | `/v1/competitions` | list visible competitions |
| `GET` | `/v1/competitions/{competition}/tables` | list tables in a competition |
| `GET` | `/v1/competitions/{competition}/tables/{table}/view` | fetch viewer-projected table state |
| `POST` | `/v1/competitions/{competition}/tables/{table}/actions` | submit an action |
| `GET` | `/v1/competitions/{competition}/events` | stream view-projected notifications |

Creation request example:

```json
{ "flavor": "NoLimitTexasHoldEm", "maximumPlayers": 8, "startingStack": 7000 }
```

For a locally running server at port `53153`, these commands list competitions, create one, then add six reference players. `POST /v1/competitions` creates; it never lists.

```sh
curl http://127.0.0.1:53153/v1/competitions
curl --request POST http://127.0.0.1:53153/v1/competitions \
  --header 'Content-Type: application/json' \
  --data '{"flavor":"NoLimitTexasHoldEm","maximumPlayers":8,"startingStack":7000}'
curl --request POST http://127.0.0.1:53153/v1/competitions/Valhalla/reference-players \
  --header 'Content-Type: application/json' \
  --data '{"count":6}'
```

Action request example:

```json
{ "action": "raise", "amount": 500, "expectedSequence": 41 }
```

The action endpoint returns `400` for invalid syntax, `401/403` for authentication/authorization failure, `409` for a stale sequence or turn conflict, and `422` for a syntactically valid but illegal poker action.

## Development-only localhost authentication

The prototype may use a random per-server bearer token, printed only in the server console, accepted only on loopback, and reset on every process start. Store it only in the client’s memory. This is a convenience gate, not a security mechanism; it must refuse non-loopback bindings.

## Production message authentication

TLS protects transport confidentiality and integrity, but does not make a replayed valid request safe. Use standard authentication, not a home-grown shared-secret protocol:

1. Register/login over TLS using a mature identity provider or local password system backed by Argon2id.
2. Issue a short-lived, audience-bound access token and a rotating refresh token in secure, HttpOnly cookies or OS secure storage.
3. Bind each command to its authenticated player identity, table, `expectedSequence`, request ID, UTC expiry, and body digest.
4. For higher assurance program clients, require OAuth 2.1 DPoP or mTLS, using established libraries and audited key storage. Verify the proof’s method/URL, token binding, nonce, expiry, and replay identifier.
5. Rate-limit identity and IP; log accepted/rejected command metadata without logging tokens, hole cards, deck order, or secret material.

Sign server notifications with the TLS session in the normal case. If messages pass through an intermediary, use an established JWS/COSE implementation with rotating server keys and publish the verification key set. Never invent a custom MAC block, expose a symmetric client secret to a browser/client, or rely on a timestamp alone for replay protection.

## Information query budget

Apply a token bucket to information-producing endpoints per authenticated player and table. Commands are not charged. Send compact event deltas with a sequence cursor so a reconnect can request only missed events. Rate limiting must not be the privacy boundary: the table view projection is.
