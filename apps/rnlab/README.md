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
- Siehe „RAM-Budget“ unten, bevor eine Lektion statische Puffer anlegt.

## RAM-Budget

Maßstab ist `scripts/check_ram_budget.py`: die SRAM-Reserve über dem 48-KB-
Boden des Linkerskripts; mindestens 256 B müssen immer übrig bleiben (CI-Gate).
Auf `praktikum/start` beträgt sie **24 768 B (24,2 KB)**. Möglich machen das
zwei Maßnahmen:

| Maßnahme | SRAM frei | Wo |
|---|---:|---|
| Stand vor dem Umbau (alle Apps, lwIP im SRAM) | 384 B | — |
| Nicht-lehrrelevante Apps aus: `CADS_APP_MARAUDER`, `CADS_APP_ACTIVE` (offensive M9-Tools), `CADS_APP_GAME`, `CADS_APP_FILEBROWSER` | +5 632 B | `CMakeLists.txt`, Defaults auf diesem Branch `OFF` |
| lwIP-Speicher (MEM_SIZE-Heap, alle memp-Pools inkl. PBUF_POOL) nach CCM | +12 608 B | `modules/net/include/lwipopts.h` |
| Kopierpuffer des Netztreibers (RX/TX je 1536 B) nach CCM | +3 072 B | `modules/net/src/cads_net_board.c` |
| Explorer-Capture-Puffer, FreeRTOS-Idle-/Timer-Stack nach CCM | +3 072 B | `apps/bringup/explorer_capture_buffer.c`, `modules/kernel/src/kernel.c` |
| **Summe (praktikum/start)** | **24 768 B** | |

Alles nach CCM Verschobene wird nur von der CPU angefasst (der Ethernet-
Treiber kopiert zwischen seinen DMA-Puffern im SRAM und lwIP). Ohne
`CADS_APP_RNLAB` bleibt die Platzierung wie auf main. Die ausgeschalteten
Apps lassen sich mit `-DCADS_APP_<NAME>=ON` wieder einschalten, das kostet
dann entsprechend Reserve.

**Richtmaß je Lektion** (SRAM, statisch). Die Werte summieren sich, weil ein
Studierenden-Fork alle Lektionen nacheinander enthält:

| Lektion | Richtmaß | Wofür typischerweise |
|---|---:|---|
| L01 schichten-kapselung | 1 KB | Trace-Ring der Frame-Zusammenfassungen |
| L02 ethernet-arp | 0,5 KB | Parser-Zustand, kleine Tabelle |
| L03 ipv4-subnetting | 0,5 KB | Zähler/Filter im IPv4-Hook |
| L04 icmp | 1 KB | RTT-Statistik, RAW-PCB-Kontext |
| L05 dhcp | 0,5 KB | Zustandsprotokoll |
| L06 dns-nat | 1 KB | DNS-Antwortpuffer |
| L07 udp-transport | 2 KB | Sequenz-/Verlustfenster |
| L08 tcp-flusskontrolle | 3 KB | Sink-Zustand, Messreihen |
| L09 congestion-control | 2 KB | cwnd/ssthresh-Trace |
| L10 http-wetter-1 | 4 KB | HTTP-Antwortpuffer (~2 KB) + JSON |
| L11 wetter-app | 6 KB | GUI-View-Zustand, Texte |
| Reserve | 2,5 KB | nicht verplanen |
| **Summe** | **24 KB** | |

Größere, reine CPU-Puffer gehören nach CCM: `RNLAB_CCM static uint8_t
buf[4096];` (Makro in `rnlab/rnlab_lesson.h`; CCM wird beim Boot **nicht**
genullt und ist **nie** DMA-Ziel). In CCM sind auf `praktikum/start` noch rund
31 KB frei (64 KB minus 29,5 KB Sektionen minus 4 KB Hauptstack), bei
maximalen TCP-Optionen noch rund 24 KB. Lokale Variablen landen auf dem Stack des Konsolen-Tasks (4 KB, CCM):
einzelne Puffer über ~1 KB dort vermeiden.

## TCP-Fenster für L08/L09

| CMake-Option | Default (wie main) | Bereich | lwIP |
|---|---:|---|---|
| `CADS_RNLAB_TCP_WND_MSS` | 8 | 2..16 | `TCP_WND = n * TCP_MSS` (MSS 536 B) |
| `CADS_RNLAB_TCP_SND_BUF_MSS` | 4 | 2..8 | `TCP_SND_BUF = n * TCP_MSS` |

```bash
bash scripts/build.sh Debug -DCADS_RNLAB_TCP_WND_MSS=16 -DCADS_RNLAB_TCP_SND_BUF_MSS=8
```

Mitwachsende Pools (`PBUF_POOL_SIZE`, `MEMP_NUM_TCP_SEG`, `MEM_SIZE`) werden in
`lwipopts.h` daraus abgeleitet und liegen in CCM. Die SRAM-Reserve ändert sich
dadurch nicht; auch bei 16/8 bleibt sie bei 24 768 B (CCM dann 36,1 KB belegt).
Die Option gilt für den ganzen Build-Ordner, also nach dem Test wieder auf den
Default zurücksetzen (`-DCADS_RNLAB_TCP_WND_MSS=8 -DCADS_RNLAB_TCP_SND_BUF_MSS=4`).
