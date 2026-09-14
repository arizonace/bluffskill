# Libra Canon v1

Libra is a deterministic, value-first reference player for the current no-limit Texas Hold'em tournament. It is a reproducible baseline for comparing the personality-based reference players; it is not a claim to be a complete game-theoretic-optimal strategy.

## Fixed objective and assumptions

- Objective: preserve and grow chips through positive expected-value decisions. The policy does not model tournament payout equity, player-specific tendencies, rake, or implied odds.
- Information: Libra sees only its own private `TableView`: its cards, public board, stacks, pots, blinds, and public action history.
- Action menu: the engine's legal actions. All requested bet and raise amounts are rounded and clamped by the engine-provided legal bounds.
- Determinism: the decision does not consume the controller's random generator. Equity simulation uses a seed derived from Libra's cards, the public board, and public action history.
- Bluffing: disabled. Libra does not bet or raise an unmade draw. A preflop raise represents an opening-range or premium re-raise decision, not a bluff attempt.

## Preflop chart

Libra assigns every two-card holding the shared preflop-strength score:

- Pocket pair: `0.58 + 0.42 × normalized pair rank`.
- Non-pair: `0.05 + 0.50 × high rank + 0.14 × low rank`, plus `0.07` suited, `0.09` connected, or `0.04` one-gap.

For an unopened pot over 10 effective big blinds, Libra raises when the score reaches the threshold below. It otherwise checks its big blind or folds; it does not open-limp.

| Remaining players | Button | Late | Middle | Blind | Early |
| --- | ---: | ---: | ---: | ---: | ---: |
| Heads-up | 0.46 | — | — | 0.55 | — |
| 3–4 | 0.48 | 0.52 | 0.57 | 0.60 | 0.63 |
| 5–10 | 0.48 | 0.54 | 0.61 | 0.60 | 0.68 |

Libra opens to 2.5 big blinds. Against one raise it 3-bets at `0.79` heads-up or `0.84` otherwise, using 3 times the open in position and 3.5 times from a blind. It calls one raise at `0.60` heads-up, `0.66` with 3–4 players, or `0.71` with 5–10 players. Against two or more raises it only re-raises at `0.92`; it calls only premium holdings at a favorable immediate price.

At 10 effective big blinds or fewer, Libra uses a simplified raise-or-fold chart: it makes the legal all-in raise at `0.46` heads-up, `0.56` with 3–4 players, or `0.66` with 5–10 players. Facing one raise, the all-in threshold is `0.70` heads-up or `0.80` otherwise.

## Postflop value and draw rules

When checked to, Libra bets only a sufficiently strong made hand:

- Flop and turn: made-hand score at least `0.70 + 0.04 × (opponents - 1)`.
- River: made-hand score at least `0.72 + 0.04 × (opponents - 1)`.
- Sizing: 50% pot on the flop, 67% on the turn, and 75% on the river, each subject to the legal bound and its documented stack cap.
- Raise: only with a made-hand score of at least `0.90`, at 75% pot.

Libra's direct draw calculation enumerates the unseen cards that complete an already-present straight or flush draw. On the turn it uses the exact one-card probability. On the flop it uses the probability that at least one of those existing outs arrives on the turn or river. It does not count backdoor draws, future fold equity, or implied odds.

## Calls and opponent ranges

For a call, Libra compares pot odds with the greater of direct-draw equity and deterministic sampled showdown equity. It calls only when that equity exceeds pot odds by three percentage points; out of position before the river, it deducts a further three percentage points for equity realization.

The sampled equity model runs 128 completed-board simulations. Each active opponent receives a random legal two-card holding from a static range implied by that opponent's public preflop action:

| Last public preflop action | Minimum preflop-strength score |
| --- | ---: |
| No voluntary action | 0.00 |
| Call | 0.35 |
| First raise | 0.58 |
| Re-raise | 0.76 |

The model does not infer personality traits or react to Leo, Virgo, Libra, or August types. It is intentionally a fixed prior. This makes a decision reproducible and suitable for baseline testing, while leaving future canon revisions free to add a stated tournament-equity or range-realization model.
