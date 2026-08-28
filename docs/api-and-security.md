# REST API and security design

## Prototype endpoints

All endpoints use JSON and return an `X-BluffSkill-Sequence` header when they are scoped to a competition. The current prototype implements health, competition listing/creation, reference-player creation, and API-player attachment.

| Method | Route | Purpose |
| --- | --- | --- |
| `POST` | `/v1/competitions` | create a single-table Texas Hold'em tournament |
| `POST` | `/v1/competitions/{competition}/reference-players` | add 1–7 in-process bot players |
| `POST` | `/v1/competitions/{competition}/tables/{table}/players` | attach a human API player |
| `GET` | `/v1/competitions` | list visible competitions |
| `GET` | `/v1/competitions/{competition}/tables` | list tables in a competition |
| `GET` | `/v1/competitions/{competition}/tables/{table}/view` | fetch viewer-projected table state |
| `POST` | `/v1/competitions/{competition}/tables/{table}/actions` | submit an action |
| `POST` | `/v1/competitions/{competition}/tables/{table}/next-hand` | begin the next hand after the displayed result; body may name the response viewer |
| `POST` | `/v1/competitions/{competition}/tables/{table}/restart` | reset stacks and begin a new hand on the existing table; body may name the response viewer |
| `GET` | `/v1/competitions/{competition}/events` | stream view-projected notifications |

Each table in the table-list response includes `maximumSeats` and a `players` array. Every player entry has a one-based `seat`, name, and player kind; a player may be added to a competition only when it is assigned to a free table seat.

`POST /v1/competitions/{competition}/tables/{table}/players` accepts only an API player name. The server constructs an `ApiPlayer` and deliberately has no human/bot/GUI field: a human client and an automated remote client are indistinguishable to it.

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
{ "player": "Arizona", "action": "raise", "amount": 500, "expectedSequence": 41 }
```

The creation/list response includes `chipDenominations`, the positive, sorted denomination list active for that competition. Each table-view response repeats the same list so a client can render its wager controls from the authoritative table state. The server gets the list from its shared configuration when the competition is created; the client must not use a local configuration value for it.

The action request also carries the API-player name in this unauthenticated scaffold. `amount` is an integer chip amount and, for a bet or raise, is the player's **total commitment in the current betting round**, not an additional increment. Check, call, and fold use `0`. Bet and raise totals must be multiples of the competition's smallest `chipDenominations` value. A real authenticated adapter derives the player identity from its credential rather than accepting that field from the request body.

The action endpoint returns `400` for invalid syntax, `401/403` for authentication/authorization failure, `409` for a stale sequence or turn conflict, and `422` for a syntactically valid but illegal poker action.

The viewer-projected table response includes `currentBet`, the largest commitment in the active betting round. When the viewer is acting, `legalActions.callAmount` states the exact additional chips required to call; a client must display these values rather than infer them from another player's stack.

The table projection also includes the dealer, small-blind, and big-blind seats; the current `smallBlind`, `bigBlind`, and `blindLevel`; the per-hand action history (including forced blind posts); and settled `payouts`. Each payout carries its complete amount and explicit seat/amount awards, so clients never recreate side-pot allocation. A side pot is projected only where an all-in contribution caps a player's eligibility and other players have continued to contribute. Opponents' hole cards remain absent except for a completed, non-fold showdown; revealed player entries then include the server-calculated `showdownDescription` (for example, `Two Pair(Q,8), 10`). At the end of any hand, the requesting player alone receives a description of their own available cards, including when they folded or the hand ended before a showdown; that does not reveal an opponent's private state.

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
