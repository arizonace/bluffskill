# BluffSkill

BluffSkill is a native Qt/C++ poker application designed around a portable, deterministic game engine. The first playable milestone is a single-table, no-limit Texas Hold'em tournament with one human and reference bots.

The repository deliberately separates rules (`src/cards`, `src/poker`) from transports and Qt presentation (`apps`). See [docs/architecture.md](docs/architecture.md) for the system boundary and [docs/getting-started-macos.md](docs/getting-started-macos.md) to build it on macOS.

## Current scaffold

- `bluffskill_cards`: reusable 52-card deck library with no Qt dependency.
- `bluffskill_poker`: prototype domain model for the house and tournament creation rules.
- `bluffskill-server`: local Qt server console and REST-shaped HTTP endpoints.
- `bluffskill-client`: Qt Widgets table-shell UI.

This is an intentionally narrow foundation, not yet a playable poker implementation.
