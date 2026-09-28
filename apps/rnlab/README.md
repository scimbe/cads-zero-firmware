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
| `lab selftest` | Zusagen des Rahmens auf dem Board prüfen (Timer-Reserve) |
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

Der Handler läuft im Konsolen-Task, **außerhalb** jedes lwIP-Callbacks —
auch bei Telnet: der TCP-Empfang puffert die Zeile nur, ausgeführt wird sie
danach aus der Hauptschleife (`cads_cli_tcp_service()`). Wer auf Antworten
aus dem Netz warten muss, ruft deshalb in seiner Warteschleife gefahrlos
selbst `cads_net_poll()` auf (Beispiel: `cads_net_ping()` in
`modules/net/src/cads_net_board.c`) — und bleibt dabei kurz, denn solange
der Handler läuft, steht die Oberfläche.

**Nie** `cads_net_poll()` (oder `cads_net_ping()`, `cads_net_arp_probe()` …)
aus einem lwIP-Callback (`tcp_recv`, `udp_recv`, `raw_recv`, Hook, Timer)
aufrufen: lwIP ist nicht reentrant. Ein solcher verschachtelter Aufruf wird
abgewiesen (tut nichts) und in `lab info` unter `nested:` gezählt — steigt
der Zähler, pollt Lektions-Code an der falschen Stelle.

### Tests

```bash
cmake --preset host && cmake --build build/host
ctest --test-dir build/host -L rnlab-L03      # nur Lektion 03
```

Die `_logic`-Dateien werden auf dem Host getestet, deshalb dort weder HAL
noch lwIP einbinden. Die Board-Datei `lNN_<slug>.c` wird nur für das Board
übersetzt (im Simulator antwortet `lab NN` mit „nur auf dem Board
verfügbar“).

## Timer (`sys_timeout`)

lwIPs Timer-Pool ist nur für lwIPs eigene zyklische Timer bemessen; ein
`sys_timeout()` ohne freien Platz endet in einer Assertion, also einem
Absturz (`cads_hal_panic`). Mit `CADS_APP_RNLAB` hat der Pool
**6 zusätzliche Plätze** (`RNLAB_LESSON_TIMEOUTS`, `lwipopts.h`). Jede Lektion
hält gleichzeitig höchstens so viele eigene Timer:

| Lektion | max. gleichzeitige `sys_timeout()` |
|---|---:|
| L01 … L03, L05 … L07 | 0 (bei Bedarf 1 aus der Reserve anmelden) |
| L04 icmp | 1 |
| L08 tcp-flusskontrolle | 1 (10-ms-Tick) |
| L09 congestion-control | 1 (1-ms-Tick) |
| L10 http-wetter-1 | 1 |
| L11 wetter-app | 2 |
| **Summe** | **6** |

Ein Timer, der sich im Callback selbst neu setzt, belegt dabei nur einen Platz.
`lab selftest` belegt alle 6 gleichzeitig zusätzlich zu lwIPs eigenen Timern
und gibt sie wieder frei; kommt die Zeile „… - OK“, reicht der Pool.

## Hook-Punkte im Netztreiber

Der Netztreiber ruft fünf Hook-Punkte auf (`modules/net/include/cads/net/rnlab_hooks.h`).
Der Rahmen (`src/rnlab_hooks.c`) definiert sie und verteilt jeden Aufruf der
Reihe nach an die Lektionen 01 … 11. Jede Lektion hat dafür eigene Funktionen
mit schwachem (`weak`) No-op-Default; eine Lektion implementiert nur die, die
sie braucht — **in ihrer `src/lNN_<slug>.c`** (nicht in der `_logic.c`), mit
genau dieser Signatur und ohne `weak`. So können mehrere Lektionen denselben
Punkt gleichzeitig nutzen (z. B. L01 und L02 beide `rx_frame`).

| Lektions-Funktion (NN = 01 … 11) | Wann | Rückgabe / Verknüpfung |
|---|---|---|
| `void rnlab_lNN_hook_rx_frame(const uint8_t* frame, size_t len)` | jeder empfangene Ethernet-Frame (ab Ziel-MAC, ohne FCS) | — |
| `bool rnlab_lNN_hook_rx_drop(const uint8_t* frame, size_t len)` | danach, vor lwIP | `true` = verwerfen; ODER über alle Lektionen, jede wird gefragt; zählt in `rx_dropped` |
| `void rnlab_lNN_hook_tx_frame(const uint8_t* frame, size_t len)` | jeder Frame, den lwIP senden will | — |
| `bool rnlab_lNN_hook_tx_drop(const uint8_t* frame, size_t len)` | danach, vor dem MAC | `true` = nicht senden (lwIP hält ihn für gesendet); ODER über alle |
| `int rnlab_lNN_hook_ip4_input(struct pbuf* p, struct netif* inp)` | `LWIP_HOOK_IP4_INPUT`, jedes empfangene IPv4-Paket, `p->payload` am IP-Header | `0` = weiter; sonst verbraucht (dann selbst `pbuf_free(p)`); die erste Lektion mit `!= 0` gewinnt, spätere sehen das Paket nicht |

Die Prototypen stehen in `rnlab/rnlab_lesson.h`. Die Rahmen-Funktionen
`rnlab_hook_*` definiert eine Lektion **nie** selbst (das gäbe einen
Linkfehler). Hooks laufen mitten im Empfangs-/Sendepfad: kurz halten, nichts
blockieren, kein `cads_net_poll()` darin aufrufen.

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
- Siehe „RAM-Budget“ unten, bevor eine Lektion statische Puffer anlegt.

## RAM-Budget

Maßstab ist `scripts/check_ram_budget.py`: die SRAM-Reserve über dem Boden
des Linkerskripts; mindestens 256 B müssen immer übrig bleiben (CI-Gate).
Auf `praktikum/start` liegt der Boden bei **0 statt 48 KB**, und die Reserve
beträgt im Default **73 824 B (72,1 KB)**.

| Maßnahme | SRAM frei | Wo |
|---|---:|---|
| Stand vor dem Umbau (alle Apps, lwIP im SRAM, 48-KB-Boden) | 384 B | — |
| Nicht-lehrrelevante Apps aus: `CADS_APP_MARAUDER`, `CADS_APP_ACTIVE` (offensive M9-Tools), `CADS_APP_GAME`, `CADS_APP_FILEBROWSER` | +5 632 B | `CMakeLists.txt`, Defaults auf diesem Branch `OFF` |
| lwIP-Speicher (MEM_SIZE-Heap, memp-Pools inkl. PBUF_POOL) nach CCM | +12 608 B | `modules/net/include/lwipopts.h` |
| Kopierpuffer des Netztreibers (RX/TX je 1536 B) nach CCM | +3 072 B | `modules/net/src/cads_net_board.c` |
| Explorer-Capture-Puffer, FreeRTOS-Idle-/Timer-Stack nach CCM | +3 072 B | `apps/bringup/explorer_capture_buffer.c`, `modules/kernel/src/kernel.c` |
| CLI-Robustheit und entkoppelte Ausführung (Zustand je Sitzung) | −64 B | `modules/cli` |
| 48-KB-„Heap“-Boden auf 0 (CTO-Freigabe 2026-09-28, nur dieser Branch) | +49 152 B | `targets/itsboard/linker/cads_itsboard.ld`, `CMakeLists.txt` |
| RX-Überlaufzähler (Summen im HAL) | −32 B | `targets/itsboard/hal/hal_eth_mac.c` |
| **Summe (praktikum/start, Default-TCP)** | **73 824 B** | |

**Warum der 48-KB-Boden fallen darf:** Nichts belegt diesen Bereich zur
Laufzeit. Im Image ist kein `malloc`/`_sbrk`/`free` gelinkt (`nm`), lwIP
arbeitet mit statischen Pools, der Framebuffer ist statisch, und der
Hauptstack (MSP, 4 KB, `0x1000F000`–`0x10010000`) liegt im CCM, nicht im
SRAM. Seine Reserve und der Stack-Guard (`apps/bringup/tasks.c`) bleiben
unverändert. Der Build setzt `-Wl,--defsym=__cads_heap_floor=0`; Linker-ASSERT
und `check_ram_budget.py` lesen dasselbe Symbol. Auf main bleibt der Boden
bei 48 KB.

Alles nach CCM Verschobene wird nur von der CPU angefasst (der Ethernet-
Treiber kopiert zwischen seinen DMA-Puffern im SRAM und lwIP). Ohne
`CADS_APP_RNLAB` bleibt die Platzierung wie auf main. Die ausgeschalteten
Apps lassen sich mit `-DCADS_APP_<NAME>=ON` wieder einschalten, das kostet
dann entsprechend Reserve.

**Richtmaß je Lektion** (SRAM, statisch). Die Werte summieren sich, weil ein
Studierenden-Fork alle Lektionen nacheinander enthält. Sie sind so bemessen,
dass sie auch in der größten TCP-Konfiguration (1460/32/16, 19,3 KB Reserve)
noch passen:

| Lektion | Richtmaß | Wofür typischerweise |
|---|---:|---|
| L01 schichten-kapselung | 1 KB | Trace-Ring der Frame-Zusammenfassungen |
| L02 ethernet-arp | 0,5 KB | Parser-Zustand, kleine Tabelle |
| L03 ipv4-subnetting | 0,5 KB | Zähler/Filter im IPv4-Hook |
| L04 icmp | 1 KB | RTT-Statistik, RAW-PCB-Kontext |
| L05 dhcp | 0,5 KB | Zustandsprotokoll |
| L06 dns-nat | 1 KB | DNS-Antwortpuffer |
| L07 udp-transport | 2 KB | Sequenz-/Verlustfenster |
| L08 tcp-flusskontrolle | 2 KB | Sink-Zustand, Messreihen |
| L09 congestion-control | 2 KB | cwnd/ssthresh-Trace |
| L10 http-wetter-1 | 3 KB | HTTP-Antwortpuffer (~2 KB) + JSON |
| L11 wetter-app | 4 KB | GUI-View-Zustand, Texte |
| Reserve | 1,5 KB | nicht verplanen |
| **Summe** | **19 KB** | |

Größere, reine CPU-Puffer gehören nach CCM: `RNLAB_CCM static uint8_t
buf[4096];` (Makro in `rnlab/rnlab_lesson.h`; CCM wird beim Boot **nicht**
genullt und ist **nie** DMA-Ziel). Wie viel CCM je TCP-Konfiguration frei
bleibt, steht in der Tabelle unten. Lokale Variablen landen auf dem Stack des
Konsolen-Tasks (4 KB, CCM): einzelne Puffer über ~1 KB dort vermeiden.

## TCP-Parameter für L08/L09

| CMake-Option | Default (wie main) | Bereich | lwIP |
|---|---:|---|---|
| `CADS_RNLAB_TCP_MSS` | 536 | 536 oder 1460 | `TCP_MSS` (1460 = MTU 1500 − 40 B IP/TCP) |
| `CADS_RNLAB_TCP_WND_MSS` | 8 | 2..32 | `TCP_WND = n * TCP_MSS` (max. 46 720 B, ohne Window Scaling < 64 KB) |
| `CADS_RNLAB_TCP_SND_BUF_MSS` | 4 | 2..16 | `TCP_SND_BUF = n * TCP_MSS` |
| `CADS_RNLAB_ETH_RX_COUNT` | 8 | 4..32 | Tiefe des Ethernet-RX-DMA-Rings (`hal_eth_mac.c`); je Deskriptor 1536 B **SRAM** (DMA, nie CCM) |

```bash
bash scripts/build.sh Debug -DCADS_RNLAB_TCP_MSS=1460 -DCADS_RNLAB_TCP_WND_MSS=32 -DCADS_RNLAB_TCP_SND_BUF_MSS=16
```

Mitwachsende Pools (`PBUF_POOL_SIZE` = Fenster + 1 Puffer, `PBUF_POOL_BUFSIZE`
über `TCP_MSS`, `MEMP_NUM_TCP_SEG`, `MEM_SIZE`) werden in `lwipopts.h`
abgeleitet. Der lwIP-Heap liegt immer im CCM, die memp-Pools ebenfalls,
solange beides zusammen ins CCM passt; sonst wandern die Pools ins SRAM
(automatisch, gemessen):

| MSS / WND / SND_BUF | Pools | SRAM-Reserve | CCM belegt | CCM frei¹ |
|---|---|---:|---:|---:|
| 536 / 2 / 2 | CCM | 73 824 B | 30,1 KB | 30,6 KB |
| 536 / 8 / 4 (Default) | CCM | 73 824 B | 30,4 KB | 30,3 KB |
| 536 / 16 / 8 | CCM | 73 824 B | 36,9 KB | 23,1 KB |
| 536 / 32 / 16 | CCM | 73 824 B | 51,2 KB | 8,8 KB |
| 1460 / 8 / 4 | CCM | 73 824 B | 43,0 KB | 17,0 KB |
| 1460 / 16 / 8 | SRAM | 44 928 B | 31,4 KB | 28,6 KB |
| 1460 / 32 / 16 | SRAM | 19 776 B | 42,8 KB | 17,1 KB |

¹ 64 KB minus belegt minus 4 KB Hauptstack.

**RX-Ring:** Jeder Deskriptor über 8 kostet 1 536 B SRAM-Reserve. Messwerte:
536/8/4 mit RX 8 hat 73 824 B Reserve, 536/32/16 mit RX 32 hat 36 576 B,
1460/16/8 mit RX 32 hat 7 680 B. **1460/32/16 verträgt höchstens RX 20**
(1 152 B Reserve; ab RX 21 läuft das SRAM über, der Linker bricht ab).
Framebuffer (75 KB), Display-Stage (15 KB, DMA) und der PBUF_POOL (50 KB bei
33 × 1 460 B) lassen daneben keinen Platz. Solche Kombinationen sind reine
L08/L09-Messbuilds, in denen die Lektions-Richtmaße oben nicht mehr passen.
Überläufe zeigt `lab info` als `rx_ring_overruns` (kein freier Deskriptor,
DMAMFBOCR.MFC) und `FIFO` (DMAMFBOCR.MFA), jeweils seit dem Boot.

Die Optionen gelten für den ganzen Build-Ordner, danach also wieder auf die
Defaults zurücksetzen
(`-DCADS_RNLAB_TCP_MSS=536 -DCADS_RNLAB_TCP_WND_MSS=8 -DCADS_RNLAB_TCP_SND_BUF_MSS=4`).
