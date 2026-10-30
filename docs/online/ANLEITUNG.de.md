# DriveClub online mit Freunden (Kurzanleitung)

Experimentell. Sessions, private Lobbys und gemeinsame Rennen, Clubs, Statistiken, Fame und Level, Bestenlisten und
die Freundesliste mit Online-Status funktionieren. Öffentliches Matchmaking und Ghosts noch nicht. Details:
[README.md](README.md).

## Was du brauchst

- Deine eigene Kopie von DriveClub (PS4, CUSA00093 oder CUSA00003, Version 1.28). Spieldateien sind nirgends dabei.
- Ein Release-Paket (oder einen Build dieses Zweigs). Alle Spieler brauchen dasselbe Paket.
- **Nur der Host:** den Community-Server (`server`-Ordner im Paket).
- **Wer nicht im selben Heimnetz ist:** ein gemeinsames VPN. Am schnellsten ist Tailscale (kostenlos, direkte
  Verbindungen); Hamachi und Radmin gehen auch.

## Einmalig einrichten (jeder)

1. Paket entpacken.
2. `Firewall-Freigabe.bat` doppelklicken, Admin-Frage mit Ja beantworten. Spiel und Server sind danach aus dem
   Heimnetz und dem VPN erreichbar, aber nicht aus dem Internet.
3. `DriveClub.exe` einmal starten, bei „Browse“ Update-Ordner (1.28, mit `eboot.bin`) und Basisspiel-Ordner
   (mit `game000.dat`) wählen, dann schließen. Als Bildrate „60 FPS“ oder „Unlocked“ nehmen, **nicht** „60 FPS
   (fixed tick)“.
4. VPN: alle im selben Netz. Bei Tailscale lädt der Host die anderen unter *Users → Invite users* ein; sie wählen
   beim Anmelden das Tailnet des Hosts.

## Spielen

1. **Host:** `Online-Spielen.bat`, beim ersten Mal Name und `self` eingeben. Der Server startet, das Fenster zeigt
   deine IPs (Tailscale `100.x`, Hamachi `25.x`, Radmin `26.x`, Heimnetz `192.168.x`).
2. **Mitspieler:** `Online-Spielen.bat`, beim ersten Mal Name und die IP des Hosts eingeben.
3. Über die Freundesliste der Session des anderen beitreten oder in eine private Lobby einladen, Event starten.
   Leader befördern und Spieler kicken geht auch.

Was ein Freund gerade macht, erscheint oben im Bild als kurze Einblendung, sobald es sich ändert. In der
Freundesliste des Spiels selbst steht immer nur „Playing DRIVECLUB“.

Clubnamen tippst du mit der PC-Tastatur (Enter bestätigt, Esc bricht ab).

Name oder IP ändern: `online.ini` löschen.

## Extras in der privaten Lobby (Tasten am PC, der Host)

| Taste | Was passiert |
|---|---|
| F1 | ein KI-Bot mehr (in allen Spielen gleich, auch gerammt sichtbar) |
| Shift+F1 | KI-Stärke wechseln (Spiel-Standard, Rookie … Legend), gilt online und offline |
| F2 | das eingestellte Rennen zur Meisterschaft hinzufügen |
| F4 | Meisterschaft starten/stoppen; ab Rennen 2 startet jedes Rennen von selbst |
| Shift+F4 | Zufalls-Meisterschaft: 3 zufällige Strecken, Tageszeiten und Wetter, startet sofort |
| Shift+F2 | XP-Boost an/aus (doppelte Fame online) |

## Wenn es hakt

- **„Server nicht erreichbar“:** VPN an? Host hat `Online-Spielen.bat` gestartet? Richtige IP? Firewall-Freigabe
  beim Host gemacht?
- **„Cannot connect to players“:** Firewall-Freigabe bei beiden ausführen; bei Tailscale prüfen, dass beide im
  selben Tailnet sind (der Host-PC muss im Tailscale-Menü des Mitspielers auftauchen).
- **Rennzeiten laufen auseinander:** Nicht „60 FPS (fixed tick)“ verwenden, sondern „60 FPS“ oder „Unlocked“. Dann
  läuft die Rennuhr bei jeder Bildrate mit der echten Zeit.
- **Etwas fehlt nur bei einem Spieler** (Level, Club-Werte): Das Spiel merkt sich Profile. Einmal neu starten.
