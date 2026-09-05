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

`src/cards` is portable C++ with no Qt dependency and can serve bridge, blackjack, or another card game. `src/poker` depends on that library, remains portable C++, and will contain all poker rules. Qt lives only at the application edge. Network code must translate JSON to typed commands; it must never mutate a `Table` directly.

Reference players are in-process `ReferencePlayerController` implementations. The controller interface exposes a player type, private diagnostic parameters for the server console, and a decision method that consumes only that controller's private `TableView`. Leo and Virgo are separate implementations and may occupy the same table. Adding another type requires a new controller implementation plus its server factory registration; it does not change poker rules or adapters.

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

For the first playable slice, serialize only at clean hand boundaries. Mid-hand recovery is a follow-up milestone because it requires persisting the exact deck order, current turn, pending obligations, action history, and all pot eligibility.

## Public and private views

The engine produces a `TableView` for each viewer rather than exposing domain objects. A public view has community cards, shown cards, stack totals, action history, pot/side-pot eligibility, dealer button, and acting seat. A player view additionally has that player’s hole cards. Internal deck order, folded hole cards, opponents’ hole cards, bot parameters, and secrets never cross this boundary.

## API evolution

REST is the initial command/query API. GraphQL should be a second adapter over the same application services, not a replacement engine. Notifications should begin as server-sent events; WebSockets can be added when bidirectional low-latency interactions demonstrably need them.

The current server is deliberately a small vertical scaffold: `GET /v1/health` and `POST /v1/competitions`. Its routing and server console establish the adapter boundary, not the final protocol.
