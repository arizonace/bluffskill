# UI design

## Client

The client is a desktop Qt Widgets application with a landscape table. It launches into a calm, non-interactive empty state: the table has eight seat positions, while server, competition, table, and action controls remain disabled until a connection makes them meaningful.

```text
Connection menu → Connect dialog → server field enabled
                                  ↓
                          competition list enabled
                                  ↓
                             table list enabled
                                  ↓
               player joins → action panel enabled on that player’s turn
```

The eight seats use a fixed semantic layout: three north, three south, east, and west, with each player circle centered directly on the table border. Every circle has horizontal text in the same top-to-bottom order: player name, larger semi-bold chip count, then folded-hands marker at the circle bottom. A player’s front is cardinal and inward (south for north seats, west for east, north for south, and east for west). The reserved button area abuts the circle at that front; dealer-plus-blind buttons run left-to-right at north/south seats and top-to-bottom at east/west seats. Cards follow in the same inward direction, always in a left-to-right row; a hand-category description has a visible vertical margin below the cards except at south seats, where it appears above. The latest action is in a white box directly counter-clockwise from the circle and the yellow two-line winnings box directly clockwise; both straddle the table border. An awaiting player shows `Action` in that box until the accepted action replaces it. A busted player's seat is dark gray. The table canvas has a 960 x 720 minimum so its full button, card, description, action, and winnings areas do not overlap. The center presents the pot, side pots, current betting-round wager, and community cards, without a betting-street label. Community and local hole cards render as compact rank-and-suit card faces. At a completed hand, each winner has a yellow two-line result box: total chips awarded from the pot and net hand result. The client does not reconstruct per-pot allocation or draw chip stacks. The requesting player sees their own ranking description even after folding or a non-showdown hand. The clickable dealer button starts the next hand immediately; otherwise the client requests it after the configured deal clock (20 seconds by default), or after the uninterrupted dealer delay when the local player has been eliminated.

The top status area presents server, competition, and table selectors in one row. A second row shows remaining players, a fixed-width whole-second next-deal countdown, and a Deal Now button enabled only during that countdown. When the human player can act, the action panel shows a fixed-width `Action Clock` field and a Pause/Play control. On expiry, the client submits Check if still legal, otherwise Fold; the server continues to validate the command. The wager composer sits below the action buttons: it accepts an editable total commitment, the current small/big blind amounts, and 25/100/500/1000 chip increment/decrement controls.

Both applications read and create the shared INI-style configuration at `~/.config/azonelayer/blindskill/blindskill.conf`. It stores the player and deal clock values, millisecond-resolution uninterrupted dealer and automated-player presentation delays, blind-level hand/minute thresholds, default human player name, three preferred server ports, and client auto-connect preference. The client presents each reference-player action for the configured automated-player delay even if the server returned several authoritative actions together. The server raises the blind level before a new hand after either configured threshold is reached. Do not encode state in color alone; provide text labels and accessible names.

Action history is an expandable accessible panel per seat (and a compact hover summary for pointer users). It contains street, action, and amount. The action composer gives only legal action buttons, a formatted amount field, denomination increment/decrement controls, and concise explanations of the current wager, call amount, and min/max constraints.

Connection UI accepts host and port, then progressively enables competition and table pickers. It displays a copyable connection URL following `https://host:port[/competition[/table[/player]]]`. The later player segment should appear only after the viewer/player identity is attached.

## Server console

The server window has a live event log, an expandable tree (House → competition → table → player), and an action table with anchored headers for player, round, action, and value. It displays its listening address, records incoming calls and response status without secrets, and eventually exposes controlled actions: create competition, add reference players, pause/resume, snapshot, and export OPML.

## Product principles

- Make it obvious whose turn it is and what action is legal.
- Prefer persistent labels to tooltip-only information; supply keyboard paths and screen-reader names.
- Keep poker information dense but staged: table state first, details on demand.
- Treat reconnect and stale state as ordinary: show a quiet reconnect indicator and refresh from event sequence.
