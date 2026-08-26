# Implementation roadmap

## Milestone 0 — foundation (completed)

- CMake/Qt project, portable card library, test harness, Qt server/client shells.
- Architecture, domain, API/security, and UI decisions captured in public docs.

## Milestone 1 — deterministic heads-up engine

- `HandState`, seating, blinds, deck/deal, legal action validation, and event journal.
- Texas Hold'em evaluator with exhaustive unit tests, including ace-low straight and ties.
- All-in and side-pot calculation tests before any bot work.

## Milestone 2 — single-table tournament

- Up to eight seats, blind schedule, elimination, dealer movement, showdown/payout.
- In-process reference bots behind `PlayerController` with seeded profiles.
- Snapshot only at hand boundaries; server console state tree and OPML export.

## Milestone 3 — usable client/API

- Complete REST endpoints, localhost bearer-token gate, server-sent event stream.
- Connection flow, table projection, action composer, history and accessibility.
- Integration tests that drive the REST adapter against a real engine.

## Milestone 4 — production foundations

- TLS deployment, OAuth/DPoP or mTLS, persistent event journal, recovery tests.
- Generalized competition/table management and GraphQL adapter.
- Windows and Ubuntu CI, then multi-table tournament behavior.
