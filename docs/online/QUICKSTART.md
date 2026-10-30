# DriveClub online with friends (quick guide)

Sessions, private lobbies and races together, clubs, statistics, fame and levels, leaderboards and the friends list with
online status work. Public matchmaking and ghosts do not yet. Details: [README.md](README.md).

## What you need

- Your own copy of DriveClub (PS4, CUSA00093 or CUSA00003, version 1.28). No game files are included anywhere.
- The same release for every player.
- **The host only:** the community server (the `server` folder in the release).
- **Players not on the same home network:** a shared VPN. Tailscale is the quickest (free, direct connections);
  Hamachi and Radmin work too.

## Set up once (every player)

1. Extract the release.
2. Double-click `Allow-Firewall.bat` and answer Yes to the administrator question. The game and the server can then
   be reached from your home network and the VPN, but not from the internet.
3. Start `DriveClub.exe` once, choose the update folder (1.28, with `eboot.bin`) and the base game folder (with
   `game000.dat`) under **Browse**, then close it. Use the frame rate **60 FPS**, **Unlocked (VRR)** or **Unlimited**,
   not **60 FPS (fixed tick)**.
4. VPN: everyone in the same network. With Tailscale the host invites the others under *Users, Invite users*, and
   they pick the host's tailnet when they sign in.

## Play

1. **Host:** in the launcher, **General**, **ONLINE**, choose **Host a session**, then press Play. The server starts
   and the launcher shows your IP addresses (Tailscale `100.x`, Hamachi `25.x`, Radmin `26.x`, home network
   `192.168.x`). Give one of them to your friends.
2. **Friends:** **ONLINE**, **Join a session**. The **Servers** row lists the servers found on their own (your Tailscale
   devices, your home network, Hamachi or Radmin, the servers you joined lately) with their name, players and ping:
   choose one, then press Play. Its first entry searches again. Or type the host's IP address under
   **Host's IP address**.

A server can also list other servers: under **Host a session** the host types a **Server name** and, under **List my
server on**, the address of a friend's server (several separated by commas); everyone who finds that friend's server
finds this one too.
A list server for everyone (for example one that is always on) goes into the launcher's `launcher.ini` as
`online_lists=<address>`.
3. Join the other player's session from the friends list, or invite them to a private lobby, then start an event.
   Promoting a leader and kicking a player work too.

What a friend is doing appears at the top of the picture as a short message when it changes. The game's own friends
list always shows just "Playing DRIVECLUB".

Club names are typed with the PC keyboard (Enter confirms, Esc cancels).

To change your name or the host's IP: change it in the launcher.

## Online menu

Press L3+R3 (both sticks; on the keyboard Left Ctrl+V) in the game. Choose with the d-pad, change with left and
right, select with Cross, close with Circle.

| Row | What it does |
|---|---|
| Nitro for every car | Off, On or Unlimited, from the next race: hold X / Cross. On, the game's boost gauge runs empty in about 4 s and fills again when you let go; Unlimited never runs out (the gauge turns purple) |
| Face-offs in lobby races | On (default) or Off, from the next race. On, private lobby races get the game's face-offs (cornering, average speed, drift ...) against your friends' best, even when they drove another car |
| Objectives in lobby races | On (default) or Off, from the next race. Three goals on the left during a lobby race, the way the tour shows them: win (or finish in the top 3 / 5 in a bigger field), reach a speed that suits your car and the track (it rises with what you reached there before), lead the race for 15 s. A goal turns gold when it is met; after the race the panel shows how many you got |
| Bots | AI bots in the private lobby, set by the host (the same in every game, bumps included) |
| AI difficulty | the game's own, Rookie ... Legend, for every race (online and offline) |
| XP boost | double fame online |
| Add the lobby's race | adds the race as set up in the lobby to the championship |
| Start / stop the championship | from race 2 on each race starts by itself |
| Random championship | 3 random tracks, times of day and weather, starts at once |

After each championship race every player sees the standings for 20 s (bottom right): points as in Formula 1
(25, 18, 15, 12, 10, 8, 6, 4, 2, 1) and each driver's places so far; after the last race, who won.

## If something goes wrong

- **"Server not reachable":** is the VPN on? Has the host started the game with Host a session? Is the IP right? Did the
  host run `Allow-Firewall.bat`?
- **"Cannot connect to players":** run `Allow-Firewall.bat` on both PCs. With Tailscale, check that both are in the same
  tailnet (the host's PC must show up in the other player's Tailscale menu).
- **Race times drift apart:** do not use **60 FPS (fixed tick)**. Use **60 FPS**, **Unlocked (VRR)** or **Unlimited**; the
  race clock then follows real time at any frame rate.
- **Something is missing for one player only** (level, club values): the game remembers profiles. Restart once.
