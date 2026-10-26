# Bench campaigns for the IEEE Proceedings version (25 Oct 2026)

Raw data, extraction scripts and procedures behind the results added to the
paper in response to the seven IEEE requirements. Per-folder notes
(`README.md`, `RESULTADOS_itens_1_2_4.md`) and the runbooks are in Portuguese.

Setup common to both benches: control-domain firmware v1.1.23 (commit
`5dd34d1`), gateway running the evaluated Yocto image, SPI at 500 kHz, logic
analyser at 4 MS/s via sigrok. No actuator connected: verdicts are read from
the actuation signal (STEP: 600 mL/h = 847.5 Hz; ceiling 1,200 mL/h = 1,697 Hz).

Analyser channels: D0 NRST, D1 chip select, D2 DATA READY, D3 marker C
(unused), D4 STEP, D5 frame-validation marker, D6 actuation marker.
Captures (`.sr`) open in PulseView or `sigrok-cli`.

## Layout

| Folder | Content | Requirement |
|---|---|---|
| `bancada01/` | Bench 01 — systematic fault and attack campaign | 1, 3, 5, 6 |
| `bancada02/` | Bench 02 — timing distributions of the critical loop | 2 |
| `scripts/` | extraction and analysis scripts | 7 |
| `runbooks/` | step-by-step bench procedures | 7 |

## Bench 01 — where each result is

| Experiment | What | Files |
|---|---|---|
| T1 | excessive rate requests (600 / 1,200 / 5,000 / 65,000 mL/h) | `capturas_protocolo/T1a_rep01.sr`, `csv_protocolo/T1a_E13_*.csv` |
| T2 | replay of a valid frame (20×) | `capturas_protocolo/T2_rep01.sr`, `csv_protocolo/` |
| T3 | forged abort with valid CRC | `capturas_protocolo/T3_rep01.sr`, `csv_protocolo/` |
| T4a | truncated frames (recovery, worst case 21.6 ms) | `csv_protocolo/` (MCU counters) |
| T4b | invalid-CRC bursts (5–1,000 frames) | `capturas_protocolo/T4b.sr` |
| T4c | bus clock at 2 MHz (reset loop, idle) | `capturas_protocolo/T4c.sr` |
| T5a | gateway process killed | `capturas_protocolo/T5a.sr` |
| T5c | gateway unpowered, reset wire connected | `capturas_energia/T5c.sr`, `T5c_rpioff.sr` |
| T9 | reset line driven by a powered gateway (~220 ms per reset) | `capturas_energia/T9.sr` |
| T7a | tampered RAUC bundle | `rauc_status_apos.log` |
| T7b–T7e | control-domain update: corrupted, foreign key, example-key tampered, update during infusion | `capturas_ota/`, `ameacas.csv` |
| C0–C6 | MQTT broker with mutual TLS: valid, no certificate, unknown CA, ACL write | `cert_observador.log`, `README.md` |
| T6 | transaction latency under resource exhaustion | `T6/` |

`ameacas.csv` summarises every experiment as `threat, experiment, rep,
metric, value`. After T7d the legitimate firmware was restored and T1 repeated
(`csv_protocolo/pos_restauro_E13.csv`).

Not executed: C3 (expired), C4 (no ACL permission), C5 (revoked) — they need
the private key of the broker's CA; T5b (power loss without reset wire).

## Bench 02 — timing distributions

| Quantity | Source | Files |
|---|---|---|
| G1 control-loop period | MCU metric windows, status-only traffic | `janelas_mcu/`, `G1_distribuicao_mcu/`, `gateway/G1_*` |
| G2 frame-to-actuation | marker D5 → D6, 8 captures per condition | `capturas/*_mix_r*.sr`, `csv/G2_*` |
| G3 STEP period | D4, 2 captures per condition | `capturas/*_status_r*.sr`, `csv/G3_*` |
| G4 frame validation | width of D5 | `csv/G4_*` |

Conditions: `baseline`, `cpu`, `io`, `net`, `combined`. CPU and I/O load
from `stress-ng --temp-path /data/stress-tmp` (the root file system is
read-only); network load from `iperf3` at ~42 Mbit/s driven by an external
host. Load records per run in `gateway/`.

Results: `distribuicoes/tabela.md` (percentiles, KS against baseline) and
`distribuicoes/ecdf_G*.png`.

## Reproducing the analysis

```bash
python3 scripts/sr_timing.py   <capture.sr> ...   # periods, marker intervals, widths
python3 scripts/sr_eventos.py  <capture.sr> ...   # STEP segments, NRST pulses
python3 scripts/janelas_para_amostras.py ...      # G1 samples from MCU windows
python3 scripts/distribuicoes.py ...              # percentile table, KS, ECDF
```

Each script prints its usage with `-h`.
