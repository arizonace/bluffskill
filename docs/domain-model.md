# Domain model and invariants

## Aggregate boundaries

`House` owns a collection of `Competition` aggregates. A competition owns its tables; a table owns a hand and is the serialization boundary for poker actions. A player identity can belong to a competition, while a player’s **seat** and stack are table-local.

The initial prototype uses one `TournamentCompetition`, one `Table`, at most ten seats, automated dealer operations, and one human API player. Avoid making the prototype’s one-table assumption a general `Competition` invariant.

## Core types

| Type | Responsibility |
| --- | --- |
| `Card`, `Deck`, `CardCollection` | general playing-card primitives; immutable card values; deck is mutable |
| `FlavorRules` | rule policy: streets, hole/community count, betting form, showdown evaluation |
| `Table` | seating, dealer button, hand lifecycle, turn and action validation |
| `HandState` | deck, community, hole cards, betting rounds, action history, pots |
| `Stack` | integer chip amounts, always representable by the competition's smallest active denomination; optional chip inventory for display/withdrawal |
| `Pot` / `SidePot` | amount and eligible seats, derived from committed amounts |
| `PlayerController` | adapter interface for human/API/bot decisions; not the player identity |
| `Dealer` | automated system actor that advances forced operations |

Use integer chip units (`std::int64_t`) rather than floating point. Every competition has a sorted, positive list of chip denominations; each denomination must be an integer multiple of the preceding denomination. Its smallest denomination is the wagering unit: starting stacks, forced blinds, bets, raises, and awarded pots must be exact multiples of it. This preserves chip-representable stacks even when an odd chip is assigned while splitting a pot. The initial default is `25, 100, 500, 1000`; server configuration may replace it before a competition is created. Betting legality and pots otherwise operate on integer value, which keeps future currencies and tournament rebuy rules manageable.

The no-limit raise floor is the previous full bet or raise: the big blind begins each preflop round as the opening bet, an opening bet must be at least the big blind, and every full raise must add at least the size of the preceding full bet or raise. A player whose maximum commitment is below that floor may raise only by committing their entire remaining stack; that short all-in does not establish a new full-raise amount.

## Hand ranking

Represent an evaluated hand as `HandRank { category, tieBreakers }`, where categories have ascending numeric strength and `tieBreakers` is a fixed-capacity sequence of canonical ranks in descending comparison order. Examples: pair = `[pair, kicker1, kicker2, kicker3]`; full house = `[trips, pair]`; wheel straight = `[5]`.

Comparison is lexicographic: category first, then each tiebreak rank. This is conventional, deterministic, easy to test, and clearer than encoding a fragile single magic number. The evaluator returns the best five cards plus `HandRank`, which supports explaining a result in the UI.

## Naming and IDs

API route IDs are canonical human-readable names, not UUIDs. Parse them at the boundary and retain a normalized comparison key alongside their display form:

- competition: `Place` or a literal uniqueness suffix such as `Place-2300`; case-insensitive lookup;
- table: `Color:Competition`;
- player: `name@table:competition`; name is `[A-Za-z0-9-]+` and unique per competition.

Reject punctuation that conflicts with qualified forms. Name generation is an injectable service with a deterministic test data source. Do not infer semantic meaning from a collision suffix.

## Non-negotiable rules

- The house is one process-owned instance; a dealer is always automated and never seated.
- Only the player whose turn it is may submit an action. An accepted action is immutable history.
- A table transition is atomic: validate, alter commitments, recompute pots, resolve the next actor, append the event, then publish its views.
- Side-pot eligibility is based on contributions and fold status, not on current stack.
- Only a view projection may leave the engine.
