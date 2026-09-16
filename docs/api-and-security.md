# REST API and security design

## Prototype endpoints

All endpoints use JSON and return an `X-BluffSkill-Sequence` header when they are scoped to a competition. The current prototype implements health, competition listing/creation, reference-player creation, and API-player attachment.

| Method | Route | Purpose |
| --- | --- | --- |
| `POST` | `/v1/competitions` | create a single-table Texas Hold'em tournament |
| `POST` | `/v1/competitions/{competition}/reference-players` | add in-process bot players to available seats |
| `POST` | `/v1/competitions/{competition}/tables/{table}/players` | attach a human API player |
| `GET` | `/v1/competitions` | list visible competitions |
| `GET` | `/v1/competitions/{competition}/tables` | list tables in a competition |
| `GET` | `/v1/competitions/{competition}/tables/{table}/view` | fetch viewer-projected table state |
| `GET` | `/v1/competitions/{competition}/tables/{table}/hand-insight` | fetch the viewer's server-calculated transparent hand heuristic |
| `POST` | `/v1/competitions/{competition}/tables/{table}/actions` | submit an action |
| `POST` | `/v1/competitions/{competition}/tables/{table}/next-hand` | begin the next hand after the displayed result; body may name the response viewer |
| `POST` | `/v1/competitions/{competition}/tables/{table}/restart` | reset stacks and begin a new hand on the existing table; body may name the response viewer |
| `POST` | `/v1/competitions/{competition}/tables/{table}/quit` | end the table, record the chip leader as winner, and return that winner; body may name the local API player that requested it |
| `POST` | `/v1/competitions/{competition}/tables/{table}/pause` | pause presentation and the table's time-based blind clock |
| `POST` | `/v1/competitions/{competition}/tables/{table}/resume` | resume presentation and the table's time-based blind clock |
| `GET` | `/v1/competitions/{competition}/events` | stream view-projected notifications |

`GET /v1/health` includes `referencePlayerTypes`, the server's registered reference-player names. Clients use it to build Custom Game selection controls, rather than maintaining a type list of their own. Each table in the table-list response includes `maximumSeats`, its current color-named `game`, and a `players` array. Every player entry has a one-based `seat`, name, and player kind; a player may be added to a competition only when it is assigned to a free table seat. Tables use element names: the current one-table prototype always creates `Hydrogen`.

`POST /v1/competitions/{competition}/tables/{table}/players` accepts an API player `name` and optional `recordHoleCards` boolean, which defaults to false. The latter is consent for the private server action-log export to retain that API player's cards on Fold events; it is not a human/bot/GUI identity field and is never returned in public views. A human client and an automated remote client remain indistinguishable to the server.

`POST /v1/competitions/{competition}/reference-players` accepts a positive `count` and an optional `type`: `Leo` remains the backward-compatible default, while the accepted names are those advertised by `GET /v1/health` (currently `Leo`, `August Leo`, `Virgo`, `August Virgo`, and `Libra`). Requests may be repeated with different types to seat a mixture of reference players at the same table. The client chooses only those types; the server assigns each reference player's unique animal-and-three-digit name and its private randomized profile where applicable. Libra is deterministic and has no profile. Player entries in competition and table-list responses retain `kind: "Reference"` and add the selected `referenceType`; profile values never leave the server console.

Creation request example:

```json
{ "flavor": "NoLimitTexasHoldEm", "maximumPlayers": 10 }
```

For a locally running server at port `53153`, these commands list competitions, create one, then add nine reference players. `POST /v1/competitions` creates; it never lists. A table begins its first hand when all of its seats are occupied, allowing a client to place its API player between two reference-player creation groups.

```sh
curl http://127.0.0.1:53153/v1/competitions
curl --request POST http://127.0.0.1:53153/v1/competitions \
  --header 'Content-Type: application/json' \
  --data '{"flavor":"NoLimitTexasHoldEm","maximumPlayers":10}'
curl --request POST http://127.0.0.1:53153/v1/competitions/Valhalla/reference-players \
  --header 'Content-Type: application/json' \
  --data '{"count":5,"type":"Leo"}'
curl --request POST http://127.0.0.1:53153/v1/competitions/Valhalla/reference-players \
  --header 'Content-Type: application/json' \
  --data '{"count":4,"type":"Virgo"}'
```

Action request example:

```json
{ "player": "Arizona", "action": "raise", "amount": 500, "expectedSequence": 41 }
```

The creation/list response includes `chipDenominations`, the positive, sorted denomination list active for that competition. Every denomination after the first is an integer multiple of the preceding one. Each table-view response repeats the same list so a client can render its wager controls from the authoritative table state. The server gets the list, starting stack, and small blind from its shared configuration when the competition is created; client-provided stack values are ignored. [chips] `smallBlind` and `stack` must be positive multiples of the smallest denomination. If either is invalid, the server logs an error and starts that competition with the smallest denomination as its small blind and 300 small blinds as its stack.

The action request also carries the API-player name in this unauthenticated scaffold. `amount` is an integer chip amount and, for a bet or raise, is the player's **total commitment in the current betting round**, not an additional increment. Check, call, and fold use `0`. Bet and raise totals must be multiples of the competition's smallest `chipDenominations` value. A real authenticated adapter derives the player identity from its credential rather than accepting that field from the request body.

The action endpoint returns `400` for invalid syntax, `401/403` for authentication/authorization failure, `409` for a stale sequence or turn conflict, and `422` for a syntactically valid but illegal poker action.

The viewer-projected table response includes `currentBet`, the largest commitment in the active betting round. When the viewer is acting, `legalActions.callAmount` states the exact additional chips required to call, while `minimumAmount` is the total commitment for the legal opening bet or raise floor. The big blind is the first preflop bet; a full raise must add at least the previous full bet or raise amount. A short all-in below that floor is legal only at the actor's exact `maximumAmount` and does not reset the full-raise floor. A client must display these values rather than infer them from another player's stack.

`GET /v1/competitions/{competition}/tables/{table}/hand-insight?viewer={player}` requires a seated viewer in this unauthenticated prototype and returns a private engine projection for that player only. It includes the server-calculated hand description, position, stack (and big-blind equivalent), pot, player count, call price and required pot odds when applicable, straight/flush `improvementOuts`, next-card and by-river chances, a 0–100 strength signal, a public-board pressure signal, and an optional currently legal recommendation. Each out is a physical unknown card that completes a straight or flush using a private card and carries its own next-card and by-river chance; the top-level chances are for hitting any listed card. The chances say only how likely a listed improvement card is to arrive; they are neither showdown equity nor a guarantee that the player will win. `reasons` exposes the deterministic v1 heuristic's inputs and branch. No opponent hole cards, deck order, bot profile, GTO claim, or solver result is included. A recommendation is absent when the player is not the acting player, has been eliminated, or has no legal action.

The table projection also includes the dealer, small-blind, and big-blind seats; the current `smallBlind`, `bigBlind`, `blindLevel`, and total `handsPlayed`; each player's total `committed` chips, current-round `roundCommitted` chips, and `busted` status; the per-hand action history (including forced blind posts); and settled `payouts`. `busted` is true only after a hand is settled with a zero stack; it remains false for a live all-in player and persists for that player's later inactive hands. Clients can therefore display wagers in front of their owners until a betting round closes, without inferring them from the center pot or zero-stack state. Each payout carries its complete amount and explicit seat/amount awards, so clients never recreate side-pot allocation. A side pot is projected only where an all-in contribution caps a player's eligibility and other players have continued to contribute. Opponents' hole cards remain absent except for a completed, non-fold showdown; revealed player entries then include the server-calculated `showdownDescription` (for example, `Two Pair(Q,8), 10`). At the end of any hand, the requesting player alone receives a description of their own available cards, including when they folded or the hand ended before a showdown; that does not reveal an opponent's private state.

Each action-history item includes the acting player's `stackAfter`, plus the authoritative total `pot` and `currentBet` after that turn. This lets any connected client replay a server-returned batch of automated actions without reconstructing betting state or identifying as a player. Table views also include `startingStack`, so a restarted game can reset presentation before subsequent automated actions are shown.

A restart begins the next color-named game for the competition and resets stacks, blind level, dealer position, and hand history. The initial game is `Red`; after 100 games, the restart endpoint rejects the request with a conflict explaining that the game-name pool is exhausted.

A quit request ends a started table without settling the active hand. The server selects the player with the most chips currently held, including committed chips when a hand is active; an equal total is resolved by the lower one-based seat. It records `Quit Game` for the supplied API player, or `Dealer` when none is supplied, followed immediately by `Table Winner`. The ended table no longer prevents the house from being cleared.

When the local server console clears its house, existing competition and table routes fail because their resources no longer exist. Clients must discard stale game state, show the error from the failed command, and return to the empty-house flow before creating a new game.

The server's persistent `Detailed Server Logs` preference is off by default. When enabled in the Server application's Settings, `server.log` records each JSON request body and every JSON response body as compact, single-line records following the route/status entry. These local troubleshooting records may include a viewer-projected hole-card value, so keep the log private and disable detailed logging outside controlled development use.

## Development-only localhost authentication

The prototype may use a random per-server bearer token, printed only in the server console, accepted only on loopback, and reset on every process start. Store it only in the client’s memory. This is a convenience gate, not a security mechanism; it must refuse non-loopback bindings.

## Production message authentication

TLS protects transport confidentiality and integrity, but does not make a replayed valid request safe. Use standard authentication, not a home-grown shared-secret protocol:

1. Register/login over TLS using a mature identity provider or local password system backed by Argon2id.
2. Issue a short-lived, audience-bound access token and a rotating refresh token in secure, HttpOnly cookies or OS secure storage.
3. Bind each command to its authenticated player identity, table, `expectedSequence`, request ID, UTC expiry, and body digest.
4. For higher assurance program clients, require OAuth 2.1 DPoP or mTLS, using established libraries and audited key storage. Verify the proof’s method/URL, token binding, nonce, expiry, and replay identifier.
5. Rate-limit identity and IP; by default, log accepted/rejected command metadata without logging tokens, hole cards, deck order, or secret material. `Detailed Server Logs` is a user-controlled local-development exception that records JSON request and response bodies. The local server writes this operational log to `~/AzoneLayer/BluffSkill/server.log`, rotates it at midnight to `server-YYYYMMDD.log`, and keeps seven days. A user-requested server-console action-log CSV export is separate from operational security logging: it backfills a folding player's recorded cards, the winner's cards for a fold win, and cards shown at showdown into each matching player row for that hand. These local/private values must never be served through an API or client UI.

Sign server notifications with the TLS session in the normal case. If messages pass through an intermediary, use an established JWS/COSE implementation with rotating server keys and publish the verification key set. Never invent a custom MAC block, expose a symmetric client secret to a browser/client, or rely on a timestamp alone for replay protection.

## Information query budget

Apply a token bucket to information-producing endpoints per authenticated player and table. Commands are not charged. Send compact event deltas with a sequence cursor so a reconnect can request only missed events. Rate limiting must not be the privacy boundary: the table view projection is.
