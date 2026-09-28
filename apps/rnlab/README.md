# `apps/rnlab` — Rechnernetze-Praktikum

## Was ist das?

Der Firmware-Rahmen für das lwIP-Praktikum auf dem ITS-Board. Mit
`CADS_APP_RNLAB=ON` (Default auf `praktikum/start`) gilt:

- **Netz läuft ab dem Boot.** Ohne Konsolenbefehl ist das Board unter
  `192.168.33.99/24` (Gateway `192.168.33.1`) erreichbar — `ping` und
  Telnet auf Port 4242 funktionieren direkt nach dem Reset.
- **Kommando `lab`** auf der seriellen Konsole (ST-Link-VCP, 115200 Baud)
  **und** per `nc 192.168.33.99 4242`:

| Befehl | Wirkung |
|---|---|
| `lab help` | Übersicht |
| `lab info` | IP, Maske, Gateway, DNS, MAC, Link, DHCP-Zustand, RX/TX-Frames, Uptime |
| `lab net static [ip mask gw]` | statische Adresse (ohne Argumente: Default oben) |
| `lab net dhcp` | Adresse per DHCP beziehen (Setup S2) |
| `lab NN <cmd> [args]` | Befehl der Lektion `NN` (`01` … `11`) |

Die Adresse wird nur im RAM gehalten; nach einem Reset gilt wieder der
Default (bzw. `/config.txt`, falls dort etwas anderes steht).

## Aufbau

```
apps/rnlab/
  include/rnlab/rnlab.h          Einstieg für apps/bringup (Init, Poll, serielle Sitzung)
  include/rnlab/rnlab_lesson.h   Schnittstelle Dispatcher <-> Lektionen
  src/rnlab.c                    `lab`-Kommando: help/info/net + Verteiler auf Lektionen
  src/rnlab_args_logic.c/.h      Argument-Parser (host-getestet)
  src/rnlab_lessons_sim.c        Platzhalter der Lektionen für den Simulator
  src/lNN_<slug>.c               Board-Integration der Lektion NN (darf lwIP nutzen)
  src/lNN_<slug>_logic.c/.h      reine Logik der Lektion NN (kein HAL, kein lwIP)
tests/unit/test_rnlab_lNN.c      Unity-Tests der Lektion NN, ctest-Label rnlab-LNN
```

| NN | Slug | Dateien |
|---|---|---|
| 01 | schichten-kapselung | `l01_schichten_kapselung*` |
| 02 | ethernet-arp | `l02_ethernet_arp*` |
| 03 | ipv4-subnetting | `l03_ipv4_subnetting*` |
| 04 | icmp | `l04_icmp*` |
| 05 | dhcp | `l05_dhcp*` |
| 06 | dns-nat | `l06_dns_nat*` |
| 07 | udp-transport | `l07_udp_transport*` |
| 08 | tcp-flusskontrolle | `l08_tcp_flusskontrolle*` |
| 09 | congestion-control | `l09_congestion_control*` |
| 10 | http-wetter-1 | `l10_http_wetter_1*` |
| 11 | wetter-app | `l11_wetter_app*` |

**Eine Lektion ändert nur ihre eigenen Dateien:** `src/lNN_<slug>.c`,
`src/lNN_<slug>_logic.c`, `src/lNN_<slug>_logic.h` und
`tests/unit/test_rnlab_lNN.c`. Alle Dateien sind bereits in CMake
eingetragen; `CMakeLists.txt`, `src/rnlab.c` und `tests/unit/CMakeLists.txt`
bleiben unverändert. (Ausnahme L11: die GUI-App `apps/wetter` ist ein
eigenes Modul.)

### Der Lektions-Handler

```c
void rnlab_l03_command(cads_cli_session_t* session, int argc, char* argv[]);
```

`lab 03 netmask 255.255.0.0` ruft ihn mit `argc = 2`,
`argv = {"netmask", "255.255.0.0"}` auf; nur `lab 03` ergibt `argc = 0`.
Ausgaben gehen über `cads_cli_write(session, "...")` und
`cads_cli_write_uint(session, n)` an genau die Verbindung (UART oder TCP),
von der der Befehl kam. Zeilen sind höchstens 95 Zeichen lang, höchstens 8
Wörter.

Der Handler läuft im Konsolen-Task, synchron zwischen zwei
`cads_net_poll()`-Aufrufen. Wer auf Antworten aus dem Netz warten muss,
ruft in seiner Warteschleife selbst `cads_net_poll()` auf (Beispiel:
`cads_net_ping()` in `modules/net/src/cads_net_board.c`) — und bleibt dabei
kurz, denn solange der Handler läuft, steht die Oberfläche.

### Tests

```bash
cmake --preset host && cmake --build build/host
ctest --test-dir build/host -L rnlab-L03      # nur Lektion 03
```

Die `_logic`-Dateien werden auf dem Host getestet, deshalb dort weder HAL
noch lwIP einbinden. Die Board-Datei `lNN_<slug>.c` wird nur für das Board
übersetzt (im Simulator antwortet `lab NN` mit „nur auf dem Board
verfügbar“).

## Hook-Punkte im Netztreiber

Deklariert in `modules/net/include/cads/net/rnlab_hooks.h`, Default ist
jeweils ein schwaches (`weak`) No-op in `modules/net/src/cads_net_board.c`.
Eine Lektion überschreibt einen Hook, indem sie die Funktion (ohne `weak`)
in **ihrer** `src/lNN_<slug>.c` definiert — nicht in der `_logic.c`, denn
nur die Board-Datei wird garantiert gelinkt.

| Hook | Wann | Rückgabe |
|---|---|---|
| `void rnlab_hook_rx_frame(const uint8_t* frame, size_t len)` | jeder empfangene Ethernet-Frame (ab Ziel-MAC, ohne FCS) | — |
| `bool rnlab_hook_rx_drop(const uint8_t* frame, size_t len)` | danach, vor lwIP | `true` = Frame verwerfen (zählt in `rx_dropped`) |
| `void rnlab_hook_tx_frame(const uint8_t* frame, size_t len)` | jeder Frame, den lwIP senden will | — |
| `bool rnlab_hook_tx_drop(const uint8_t* frame, size_t len)` | danach, vor dem MAC | `true` = nicht senden; lwIP hält ihn für gesendet |
| `int rnlab_hook_ip4_input(struct pbuf* p, struct netif* inp)` | `LWIP_HOOK_IP4_INPUT`, jedes empfangene IPv4-Paket, `p->payload` am IP-Header | `0` = normal weiter; sonst verbraucht (dann selbst `pbuf_free(p)`) |

Hooks laufen mitten im Empfangs-/Sendepfad: kurz halten, nichts blockieren,
kein `cads_net_poll()` darin aufrufen. Jeder Hook kann im gelinkten Image
nur **einmal** stark definiert sein — brauchen zwei Lektionen denselben
Hook, muss eine Lektion ihn übernehmen und an die andere weiterreichen.

**lwIP-Statistik:** mit `CADS_RNLAB_LWIP_STATS=ON` (Default) ist
`LWIP_STATS` aktiv; die Zähler stehen in `lwip_stats` (`#include
"lwip/stats.h"`), z. B. `lwip_stats.etharp.recv` oder `lwip_stats.tcp.rexmit`.

## Grenzen

- Eine Telnet-Verbindung gleichzeitig (siehe `modules/cli`).
- Solange ein anderes Explorer-Kommando läuft (z. B. `C`-Sniffer), gehört die
  Konsole diesem Kommando.
- Im App-Baum (Menü auf dem Display, Standard nach dem Boot) gehen alle
  Konsolenzeichen < 0x80 an die `lab`-Sitzung; `scripts/board_key.py quit`
  verlässt den App-Baum wie bisher.
- RAM ist knapp (`scripts/check_ram_budget.py`, ca. 380 B Reserve auf
  `praktikum/start`): große Puffer gehören auf den Stack des Konsolen-Tasks
  (CCM) oder in bestehende lwIP-Pools, nicht in neue statische Arrays.
