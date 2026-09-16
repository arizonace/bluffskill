# Domain model and invariants

## Aggregate boundaries

`House` owns a collection of `Competition` aggregates. A competition owns its tables; a table owns a hand and is the serialization boundary for poker actions. A player identity can belong to a competition, while a player’s **seat** and stack are table-local.

The initial prototype uses one `TournamentCompetition`, one `Table`, at most ten seats, automated dealer operations, and one human API player. Avoid making the prototype’s one-table assumption a general `Competition` invariant.

## Core types

| Type | Responsibility |
| --- | --- |
| `Card`, `Deck`, `CardCollection` | general playing-card primitives; immutable card values; deck is mutable |
| `FlavorRules` | rule policy: streets, hole/community count, betting form, showdown evaluation |
| `Table` | seating, dealer button, hand lifecycle, turn and action validation; authoritative post-settlement bust status |
| `HandState` | deck, community, hole cards, betting rounds, action history, pots |
| `Stack` | integer chip amounts, always representable by the competition's smallest active denomination; optional chip inventory for display/withdrawal |
| `Pot` / `SidePot` | amount and eligible seats, derived from committed amounts |
| `PlayerController` | adapter interface for human/API/bot decisions; not the player identity |
| `ReferencePlayerController` | private in-process bot-controller interface: type, console-only parameters, and legal decision selection |
| `AugustLeoReferencePlayer` / `AugustVirgoReferencePlayer` | preserved September 2026 policies for baseline and regression comparison |
| `LeoReferencePlayer` | assertive policy with opponent-count-aware preflop ranges, deliberate opens and re-raises, street-to-street plans, heads-up adjustments, and intent-based pot-relative sizing |
| `VirgoReferencePlayer` | selective policy using made-hand strength, draws, pot odds, public threat, measured value/protection bets, and a guarded heads-up regime |
| `LibraReferencePlayer` | deterministic [Libra Canon v1](libra-canon-v1.md) baseline: fixed position/stack-aware preflop ranges, static-range equity simulation, direct pot-odds draw calls, fixed legal sizing, and no bluff or semi-bluff actions |
| `Dealer` | automated system actor that advances forced operations |

Use integer chip units (`std::int64_t`) rather than floating point. Every competition has a sorted, positive list of chip denominations; each denomination must be an integer multiple of the preceding denomination. Its smallest denomination is the wagering unit: starting stacks, forced blinds, bets, raises, and awarded pots must be exact multiples of it. This preserves chip-representable stacks even when an odd chip is assigned while splitting a pot. The initial default is `25, 100, 500, 1000`; before a competition is created, server configuration supplies [chips] `smallBlind` and `stack` as amounts, each a positive multiple of the smallest denomination. The defaults are `25` and `7500`; the big blind is twice the configured small blind. If either persisted amount is invalid at competition creation, the server logs an error and uses the smallest denomination and a stack of 300 such blinds for that competition. Betting legality and pots otherwise operate on integer value, which keeps future currencies and tournament rebuy rules manageable.

The no-limit raise floor is the previous full bet or raise: the big blind begins each preflop round as the opening bet, an opening bet must be at least the big blind, and every full raise must add at least the size of the preceding full bet or raise. A player whose maximum commitment is below that floor may raise only by committing their entire remaining stack; that short all-in does not establish a new full-raise amount.

Reference-player type is public seat metadata (`Leo`, `August Leo`, `Virgo`, `August Virgo`, or `Libra`), while profile values and decision calculations remain server-private. The default Leo and Virgo policies share a showdown-aware hand estimate, draw detection, public board threat, current-street aggression, and pot-relative sizing. Leo applies pressure more readily; Virgo calls more selectively and value/protection bets strong made hands or credible draws. Libra has no profile: its versioned, deterministic canon uses fixed ranges and direct draw odds, while deliberately excluding bluff and semi-bluff actions. The August classes preserve the prior policies for reproducible baseline comparisons. The server's reference-player factory registry is the single source of truth for available types; adding a factory registration exposes that type to custom-game clients. No policy receives opponents' hole cards or bypasses `Table` action validation.

## Hand ranking

Represent an evaluated hand as `HandRank { category, tieBreakers }`, where categories have ascending numeric strength and `tieBreakers` is a fixed-capacity sequence of canonical ranks in descending comparison order. Examples: pair = `[pair, kicker1, kicker2, kicker3]`; full house = `[trips, pair]`; wheel straight = `[5]`.

Comparison is lexicographic: category first, then each tiebreak rank. This is conventional, deterministic, easy to test, and clearer than encoding a fragile single magic number. The evaluator returns the best five cards plus `HandRank`, which supports explaining a result in the UI.

## Naming and IDs

API route IDs are canonical human-readable names, not UUIDs. Parse them at the boundary and retain a normalized comparison key alongside their display form:

- competition: `Place` or a literal uniqueness suffix such as `Place-2300`; case-insensitive lookup;
- table: `Element:Competition`; the current one-table prototype always creates `Hydrogen`, while the static element-name pool reserves the finite future table namespace;
- game: a competition-local color name. A table's initial game is `Red`; each restart consumes the next name from the static 100-color pool and rejects a 101st game;
- player: `name@table:competition`; name is `[A-Za-z0-9-]+` and unique per competition.

Reject punctuation that conflicts with qualified forms. The built-in house data source shuffles its competition-name and doubled reference-player-name pools once per server instance. The server, not a client, assigns every reference player's name and private profile parameters. A generated reference name is an animal-style base plus a random zero-padded three-digit suffix (for example, `Ferret007`); it is validated as unique per competition. Do not infer semantic meaning from a collision suffix.

## Non-negotiable rules

- The house is one process-owned instance; a dealer is always automated and never seated.
- Only the player whose turn it is may submit an action. An accepted action is immutable history.
- A table transition is atomic: validate, alter commitments, recompute pots, resolve the next actor, append the event, then publish its views.
- A pause is server-authoritative. It suspends the table's time-based blind clock as well as the client's presentation timer, so paused wall-clock time never advances blinds.
- Side-pot eligibility is based on contributions and fold status, not on current stack.
- Only a view projection may leave the engine.
- A house may be cleared only when every started table has a winner or was deliberately quit; clearing removes all competitions, tables, and players. Quitting selects the chip leader (including chips committed in an active hand), resolving an equal total by the lower one-based seat, and ends that table without settling its active hand.
