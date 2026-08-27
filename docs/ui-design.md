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

The eight seats use a fixed semantic layout: three top, three bottom, left, and right. A seat presents name, stack, circular dealer and blind buttons touching its inward edge (side by side when both apply), a folded-hands marker inside its inward edge after folding, an action arrow when active, and a clear local-player ring. A busted player's seat is dark gray. The center presents the pot, side pots, current betting-round wager, community cards, and street. Community and local hole cards render as compact rank-and-suit card faces; after a completed showdown, the required non-folded players' cards and each main/side-pot award appear. The clickable dealer button starts the next hand immediately; otherwise the client requests it after ten seconds.

The top status area presents server, competition, and table selectors in one row. A second row shows remaining players, a visible next-deal countdown, and a Deal Now button enabled only during that countdown. When the human player can act, the action panel shows a 60-second turn countdown. On expiry, the client submits Check if still legal, otherwise Fold; the server continues to validate the command. Do not encode state in color alone; provide text labels and accessible names.

Action history is an expandable accessible panel per seat (and a compact hover summary for pointer users). It contains street, action, and amount. The action composer gives only legal action buttons, a formatted amount field, denomination increment/decrement controls, and concise explanations of the current wager, call amount, and min/max constraints.

Connection UI accepts host and port, then progressively enables competition and table pickers. It displays a copyable connection URL following `https://host:port[/competition[/table[/player]]]`. The later player segment should appear only after the viewer/player identity is attached.

## Server console

The server window has a live event log, an expandable tree (House → competition → table → player), and an action table with anchored headers for player, round, action, and value. It displays its listening address, records incoming calls and response status without secrets, and eventually exposes controlled actions: create competition, add reference players, pause/resume, snapshot, and export OPML.

## Product principles

- Make it obvious whose turn it is and what action is legal.
- Prefer persistent labels to tooltip-only information; supply keyboard paths and screen-reader names.
- Keep poker information dense but staged: table state first, details on demand.
- Treat reconnect and stale state as ordinary: show a quiet reconnect indicator and refresh from event sequence.
