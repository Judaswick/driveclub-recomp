# Online Play 3 (based on DriveClub Recomp 0.4.1): test build

**Please read this first.** Everything below was built and checked by an AI (Claude) on one PC, with two copies of
the game racing each other while the game's own AI drove both cars. No human has played it yet: not with a controller,
not with a wheel, not between two real PCs, not over the internet. Expect bugs. The checklist at the end is what people
need to try; please report what works and what does not, with the files from the `logs` folder.

Everything of the official 0.4.1 is included (FSR 4, Reflex, DualSense adaptive triggers and haptics, button prompts,
the reworked force feedback, ...). The game files are not: you still need your own copy of DriveClub.

## What is new (compared with the official 0.4.1)

### In the game (private lobby races on your own server)

- **Online menu** with L3+R3 (keyboard: Left Ctrl+V): every extra below is switched there, no F keys.
- **Nitro for every car**: Off, On or Unlimited. Hold X / Cross. On, the game's own boost gauge runs empty in about
  4 s and fills again; Unlimited never runs out and the gauge turns purple.
- **Face-offs in lobby races** (on by default): the game's cornering, speed and drift face-offs against your friends'
  best, also when they drove another car.
- **Objectives in lobby races** (on by default), drawn like the tour's: win (top 3 / top 5 in a bigger field), reach a
  speed that suits your car and the track (it rises with what you reached there before), lead the race for 15 s. After
  the race: how many you got.
- **Championships**: add the lobby's race, start and stop, or a random championship (3 tracks, time of day, weather).
  From race 2 on each race starts by itself. **Standings** after every race (points as in Formula 1) and the winner at
  the end.
- **Bots** in the private lobby, the same in every game; **AI difficulty** Rookie ... Legend; **XP boost** (double fame).
- **Ghosts** in time trials: your own and your friends' best drive along.

### In the launcher

- **Online play: Public server**: the community server for everyone (0.4.1 had the code but no choice for it).
- **Servers** (Join a session): servers found on their own (Tailscale devices, home network, Hamachi, Radmin, servers
  joined lately) with name, players and ping. Pick one instead of typing an IP.
- **Server name** and **List my server on** (Host a session): your server's name in the list, and friends' servers
  that list it.
- **Reachable from the internet** (Host and Join): the router forwards the ports over UPnP, so friends can join over
  the internet without a VPN. Run `Allow-Firewall-Internet.bat` once. The internet address shows under Your IP address.
- Fixed: Online play and the save profile went back to their first choice when the launcher was opened again.

### Server (the `server` folder)

- One database per Windows user (`%LOCALAPPDATA%\driveclub-coop-server`): clubs, fame and ghosts no longer seem gone
  ("kicked from the club") when the server runs from another folder. The first start takes over the folder's own
  database.
- Championship standings, bots, XP boost, random championships, the server list (`dccoop/info`, `dccoop/servers`),
  UPnP port forwarding (`internet.upnp`).

## All keys and buttons

| Controller | Keyboard | What |
|---|---|---|
| L3+R3 | Left Ctrl+V | online menu (d-pad / arrows choose and change, Cross / Enter selects, Circle / Backspace closes) |
| X / Cross (hold) | | nitro, when switched on in the online menu |
| | F2 | pedal inputs on screen |
| | F3 | motion blur exposure slow / fast |
| | F4 | picture: brightness, contrast, gamma |
| | F5 / F6 | narrower / wider view |
| | F7 | mouse look on / off |
| | F8 (Ctrl+F8) | frame generation pause (pacing), when it is on |
| | F9 / F10 | anti-aliasing off/on to compare / FSR or DLSS |
| | F12 | performance overlay |
| | Page Up / Page Down | brightness (HDR brightness with HDR on) |
| | Print Screen | screenshot |
| | W S A D, Space, Q E, C, R T, Enter/Z, Backspace, Esc/P, arrows, I J K L | driving and menus (racing layout, `data\user\input_config\default.ini`) |

## What people need to test

Tick what works, write down what does not (and attach `logs`).

**Controller and keys**
- [ ] A PS5 DualSense (USB and Bluetooth) is found and works in menus and while driving. With two controllers paired,
      only one switched on.
- [ ] DualSense adaptive triggers and rumble (0.4.1 feature) still work with this build.
- [ ] L3+R3 opens and closes the online menu; d-pad, Cross and Circle work it; the game does not react while it is open.
- [ ] Left Ctrl+V does the same on the keyboard.
- [ ] Xbox controller and a steering wheel still work as in the official 0.4.1.

**Nitro**
- [ ] On: X pushes the car, the gauge empties and refills, the boost sound plays (with a car that has no boost of its own).
- [ ] Unlimited: the gauge stays full and purple; it disappears in the pause menu and replays.
- [ ] Off: every car is as before; the Wombat, McLaren P1 and Jaguar C-X75 keep their own boost.

**Private lobby (two real PCs)**
- [ ] Host a session / Join a session connects, the lobby fills, races start for both.
- [ ] Face-offs show up for both players and use the other one's scores.
- [ ] Objectives panel on the left, goals turn gold, the result after the race; the speed goal suits the car.
- [ ] Championship of 2 or 3 races: race 2 starts by itself, standings after each race on both PCs, the winner at the end.
- [ ] Random championship; bots (count the same for everyone); AI difficulty; XP boost.
- [ ] Pause in the middle of a lobby race: the objectives go on afterwards.

**Launcher and servers**
- [ ] Servers row finds the host (Tailscale, home network, Hamachi/Radmin); picking one fills the host's IP and joins.
- [ ] Server name shows in a friend's list; List my server on puts it into another server's list.
- [ ] Public server connects and races work (relayed).
- [ ] Online play and the other choices stay after closing and opening the launcher.

**Internet without VPN**
- [ ] With port sharing allowed in the router: Your IP address shows "Internet: x.x.x.x"; a friend joins with it;
      a race works.
- [ ] Without port sharing: the launcher says it is not reachable and why.

**Clubs and saves**
- [ ] After moving to this folder the host's clubs, fame and ghosts are still there.

## Known limits

- The extras of the online menu work on a server of your own (Host a session), not on the public server.
- Anyone who knows a server's internet address can join it (no passwords): give it only to friends.
- A router without a public IPv4 address (DS-Lite, carrier NAT) cannot be reached from the internet; use a VPN or the
  public server.
