# Architecture

## Decision

BluffSkill is a server-authoritative game. The engine owns the truth; every client, bot, UI, CLI, REST handler, and future GraphQL resolver is an adapter around it. No rules, hand evaluation, or private-card access belongs in an adapter.

```text
Qt client / CLI / reference bot
              │ authenticated command + cursor
              ▼
 REST adapter (now) ── GraphQL adapter (later) ── notification stream
              │
              ▼
     application service / command dispatcher
              │
              ▼
 cards library ◄── poker engine ◄── persistence/event journal
              │
              ▼
              House (one per server process)
```

`src/cards` is portable C++ with no Qt dependency and can serve bridge, blackjack, or another card game. `src/poker` depends on that library, remains portable C++, and contains poker rules plus viewer-specific explanations derived from them. Qt lives only at the application edge. Network code must translate JSON to typed commands; it must never mutate a `Table` directly or calculate hand advice.

Reference players are in-process `ReferencePlayerController` implementations. The controller interface exposes a player type, private diagnostic parameters for the server console, and a decision method that consumes only that controller's private `TableView`. Leo, Virgo, and the deterministic value-first Libra baseline are separate implementations and may occupy the same table. The `August*ReferencePlayer` classes retain the prior policies as regression baselines; the default Leo and Virgo classes are the actively improved policies. Adding another type requires a new controller implementation plus its server factory registration; that registration automatically advertises the type to custom-game clients, without changing poker rules or adapters.

## Runtime responsibilities

| Layer | Owns | Does not own |
| --- | --- | --- |
| Cards | card identity, deck construction/shuffle/draw | poker meaning or a UI |
| Poker engine | legal state transitions, pots, hand ranking, visibility projections | HTTP, Qt widgets, socket state |
| Application service | command authorization, transaction boundary, event publication | HTTP parsing or rendering |
| REST/GraphQL | schema, input parsing, status codes, request authentication | rules or private-state selection |
| Qt client/server UI | display, input, accessibility, connection state | game authority |

## State and persistence

Use a monotonically increasing `eventSequence` per competition. A command carries the expected sequence; the server rejects a stale command with `409 Conflict`. Persist an append-only game-event journal plus periodic snapshots. On recovery, load a snapshot and replay later events. This makes bot evaluation and debugging reproducible when the shuffle seed is recorded in development only (never expose it through an API).

For the first playable slice, serialize only at clean hand boundaries. The server also maintains `~/AzoneLayer/BluffSkill/actions.csv` as a flushed CSV backing store for the visible action log and `players.csv` as flattened player-profile records captured when players are created. Every action-log row records the board visible at that action; its `Hole` value is the card information available when the row is appended. A user-requested action-log CSV export is instead written from the in-memory log and backfills each player's saved `Hole` values from the last recorded cards in that game and hand. A `bluffskill-server.dirty` running marker is created at launch and removed on clean exit. If it remains at the next launch, the server renames the prior `actions.csv` and `players.csv` to timestamped `*-recovered.csv` files before starting fresh backing stores. `actions.csv` is reset at launch and by Clear Log; `players.csv` is reset at launch and by Clear House. The files are export-recovery aids, not authoritative game-state recovery. Mid-hand recovery remains a follow-up milestone because it requires persisting the exact deck order, current turn, pending obligations, action history, and all pot eligibility.

## Public and private views

The engine produces a `TableView` for each viewer rather than exposing domain objects. A public view has community cards, shown cards, stack totals, action history, pot/side-pot eligibility, dealer button, acting seat, and each player's server-derived `busted` status. That status is set only after a hand settles with a zero stack, so a live all-in player is never presented as eliminated. A player view additionally has that player’s hole cards. Internal deck order, folded hole cards, opponents’ hole cards, bot parameters, and secrets never cross this boundary. `HandInsight` is a second private projection: it uses only the requesting player's cards and the public table state. The current BluffSkill heuristic returns its inputs, improvement-card probabilities, signals, and decision reasons from the engine; it is explicitly not a GTO or equity result. The Qt client only renders that projection. The trusted server console may place a folding reference player's cards, or the cards of an API player who explicitly opted in at creation, in its hidden local action-log `Hole` field. Those local values may be backfilled only in a user-requested saved CSV export; they must not add cards to a public view, REST response, or client UI.

## API evolution

REST is the initial command/query API. GraphQL should be a second adapter over the same application services, not a replacement engine. Notifications should begin as server-sent events; WebSockets can be added when bidirectional low-latency interactions demonstrably need them.

The current server implements the single-table command/query slice: health, competition and table discovery, reference/API-player seating, viewer-projected table reads, actions, next hand, restart, and quit. A restart creates the next color-named game within the same element-named table; notifications and persistence remain follow-up work. Its routing and server console establish the adapter boundary, not the final protocol.
