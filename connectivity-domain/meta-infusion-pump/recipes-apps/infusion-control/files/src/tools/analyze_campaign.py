#!/usr/bin/env python3
"""
analyze_campaign.py — agrega os CSVs da campanha e emite as tabelas.

Roda no HOST, sobre os CSVs trazidos da Pi. Deliberadamente sem pandas
nem numpy: assim funciona em qualquer Python 3 sem instalar nada.

Emite:
  - tabela em texto, para conferencia rapida
  - tabela em LaTeX (booktabs), pronta para o artigo
  - CSV agregado, caso queira plotar depois

As metricas sao exatamente as que os revisores pediram:
  n, media, mediana, p95, p99, min, max, desvio, jitter (max-min),
  taxa de falha; e a variacao ENTRE repeticoes, que e o que mostra se o
  resultado e reprodutivel ou se foi uma tacada de sorte.

Uso:
    python3 analyze_campaign.py bench-20260908-1930/
    python3 analyze_campaign.py bench-*/ --latex tabela.tex
"""

import argparse
import csv
import re
import glob
import math
import os
import statistics as st
import sys
from collections import defaultdict


def pct(sorted_vals, q):
    """Percentil por interpolacao linear (mesmo metodo do numpy)."""
    if not sorted_vals:
        return float("nan")
    if len(sorted_vals) == 1:
        return sorted_vals[0]
    pos = (len(sorted_vals) - 1) * q
    lo = math.floor(pos)
    hi = math.ceil(pos)
    if lo == hi:
        return sorted_vals[int(pos)]
    return sorted_vals[lo] * (hi - pos) + sorted_vals[hi] * (pos - lo)


# Nome legivel dos comandos, para a quebra por tipo no modo "mix".
CMD_NAMES = {
    0x01: "VERSION", 0x03: "GET_STATUS", 0x10: "SET_CONFIG",
    0x20: "ACTION_RUN", 0x21: "ACTION_PAUSE", 0x22: "ACTION_ABORT",
    0x23: "ACTION_PURGE", 0x24: "ACTION_BOLUS", 0x30: "GET_METRICS",
}


def load_csv(path):
    """Devolve (latencias, falhas, total, por_comando).

    por_comando so e preenchido quando o CSV traz a coluna cmd_id, que o
    modo "mix" emite. CSVs antigos continuam a ser lidos normalmente.
    """
    lat, fails, total = [], 0, 0
    por_cmd = defaultdict(list)
    with open(path, newline="") as fh:
        for row in csv.DictReader(fh):
            total += 1
            ok = row.get("ok", "1").strip() == "1"
            if ok:
                v = float(row["latency_us"])
                lat.append(v)
                cid = row.get("cmd_id")
                if cid:
                    try:
                        por_cmd[int(cid, 16)].append(v)
                    except ValueError:
                        pass
            else:
                fails += 1
    return lat, fails, total, por_cmd


def summarize(lat):
    if not lat:
        return None
    s = sorted(lat)
    return {
        "n": len(s),
        "mean": st.mean(s),
        "median": st.median(s),
        "p95": pct(s, 0.95),
        "p99": pct(s, 0.99),
        "min": s[0],
        "max": s[-1],
        "stdev": st.pstdev(s) if len(s) > 1 else 0.0,
        "jitter": s[-1] - s[0],
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dirs", nargs="+", help="diretorio(s) da campanha")
    ap.add_argument("--latex", help="grava a tabela em LaTeX neste arquivo")
    ap.add_argument("--csv", help="grava o agregado neste CSV")
    args = ap.parse_args()

    files, mcu_files, attack_files = [], [], []
    for d in args.dirs:
        for f in sorted(glob.glob(os.path.join(d, "*.csv"))):
            base = os.path.basename(f)
            if base.startswith("ataque"):
                attack_files.append(f)
            elif base.endswith("_mcu.csv"):
                mcu_files.append(f)
            else:
                files.append(f)
    if not files:
        print("nenhum CSV encontrado", file=sys.stderr)
        return 1

    # cenario -> lista de repeticoes; cada repeticao e um resumo proprio
    per_rep = defaultdict(list)
    pooled = defaultdict(list)
    fail_count = defaultdict(int)
    total_count = defaultdict(int)

    per_cmd = defaultdict(lambda: defaultdict(list))
    for f in files:
        lat, fails, total, pc = load_csv(f)
        scen = os.path.basename(f).split("_rep")[0]
        for cid, vals in pc.items():
            per_cmd[scen][cid].extend(vals)
        s = summarize(lat)
        if s:
            per_rep[scen].append(s)
            pooled[scen].extend(lat)
        fail_count[scen] += fails
        total_count[scen] += total

    order = ["baseline", "cpu", "io", "net", "combined"]
    scens = [s for s in order if s in pooled] + [s for s in pooled if s not in order]

    print()
    print("Latencia de transacao SPI ponta a ponta (microssegundos)")
    print("=" * 104)
    print(f"{'cenario':<12} {'reps':>4} {'n':>7} {'media':>9} {'mediana':>9} "
          f"{'p95':>9} {'p99':>9} {'max':>10} {'jitter':>10} {'falhas':>9}")
    print("-" * 104)

    rows = []
    for scen in scens:
        agg = summarize(pooled[scen])
        reps = len(per_rep[scen])
        fr = 100.0 * fail_count[scen] / total_count[scen] if total_count[scen] else 0.0
        print(f"{scen:<12} {reps:>4} {agg['n']:>7} {agg['mean']:>9.1f} "
              f"{agg['median']:>9.1f} {agg['p95']:>9.1f} {agg['p99']:>9.1f} "
              f"{agg['max']:>10.1f} {agg['jitter']:>10.1f} {fr:>8.2f}%")
        rows.append((scen, reps, agg, fr))

    # --- Reprodutibilidade entre repeticoes --------------------------------
    # Uma media estavel com medianas por repeticao muito dispersas indica
    # que o resultado nao e reprodutivel. Isso precisa aparecer.
    print()
    print("Dispersao das medianas ENTRE repeticoes (mostra reprodutibilidade)")
    print("-" * 104)
    for scen in scens:
        meds = [r["median"] for r in per_rep[scen]]
        if len(meds) > 1:
            spread = 100.0 * (max(meds) - min(meds)) / st.mean(meds)
            print(f"{scen:<12} medianas: " +
                  ", ".join(f"{m:.1f}" for m in meds) +
                  f"   -> variacao {spread:.1f}%")
        else:
            print(f"{scen:<12} apenas 1 repeticao, sem dispersao a medir")

    # --- Latencia por tipo de comando --------------------------------------
    # No modo "mix" os comandos tem custos bem diferentes: GET_STATUS e
    # respondido direto no hub.c, enquanto SET_CONFIG e as acoes atravessam
    # a fila do logic_engine e esperam o ACK. A media conjunta esconderia
    # isso.
    if any(per_cmd.values()):
        print()
        print("Latencia por tipo de comando (microssegundos)")
        print("=" * 104)
        print(f"{'cenario':<12} {'comando':<14} {'n':>7} {'mediana':>10} "
              f"{'p95':>10} {'p99':>10} {'max':>10}")
        print("-" * 104)
        for scen in scens:
            if scen not in per_cmd:
                continue
            for cid in sorted(per_cmd[scen]):
                vals = sorted(per_cmd[scen][cid])
                nome = CMD_NAMES.get(cid, f"0x{cid:02X}")
                print(f"{scen:<12} {nome:<14} {len(vals):>7} "
                      f"{st.median(vals):>10.1f} {pct(vals, 0.95):>10.1f} "
                      f"{pct(vals, 0.99):>10.1f} {vals[-1]:>10.1f}")

    # --- Metricas do lado do MCU -------------------------------------------
    # O gateway mede latencia de transacao; jitter do laco, deadline miss e
    # WCET do caminho comando->atuacao so o MCU sabe.
    mcu = None
    acumulado = False
    if mcu_files:
        # Contadores: somam entre repeticoes. Extremos: min/max.
        # A distincao importa -- somar um "pior caso" nao significa nada, e
        # tirar o maximo de um contador descarta as outras repeticoes.
        CONTADORES = {"frames_seen", "crc_errors", "driver_errors", "spi_resets",
                      "recoveries", "cmd_count", "loop_count", "deadline_misses",
                      "nack_sent", "frames_ok"}

        # Ordena por (cenario, numero da repeticao) para que a diferenciacao
        # de acumuladores, abaixo, ande na ordem cronologica real.
        def _rep_no(path):
            m = re.search(r"_rep(\d+)_mcu\.csv$", os.path.basename(path))
            return int(m.group(1)) if m else 0
        ordenados = sorted(mcu_files,
                           key=lambda f: (os.path.basename(f).split("_rep")[0],
                                          _rep_no(f)))
        # Ordem cronologica da campanha e a ordem dos cenarios no run, nao a
        # alfabetica: reordena pelos cenarios conhecidos quando possivel.
        pos = {s: i for i, s in enumerate(scens)}
        ordenados.sort(key=lambda f: (pos.get(os.path.basename(f).split("_rep")[0], 99),
                                      _rep_no(f)))

        leituras = []          # [(cenario, {metrica: valor}), ...] em ordem
        for f in ordenados:
            scen = os.path.basename(f).split("_rep")[0]
            vals = {}
            with open(f, newline="") as fh:
                for row in csv.DictReader(fh):
                    try:
                        vals[row["metrica"]] = float(row["valor"])
                    except (KeyError, ValueError):
                        pass
            leituras.append((scen, vals))

        # --- Firmware antigo: acumuladores desde o boot ---------------------
        # Antes da v1.1.23 CMD_GET_METRICS nao zerava os contadores, entao
        # cada leitura trazia o total desde o boot do MCU e o ultimo cenario
        # carregava os erros de todos os anteriores.
        #
        # A deteccao e pelos DADOS, nao pela versao: builds da leitura
        # destrutiva feitos antes do incremento ainda se identificam como
        # v1.1.22 (foi assim na campanha C5). Criterio: se frames_seen so
        # cresce ao longo da campanha inteira, e acumulador.
        seq = [v.get("frames_seen", 0.0) for _, v in leituras]
        acumulado = (len(seq) > 2 and all(b >= a for a, b in zip(seq, seq[1:]))
                     and seq[-1] > 2 * seq[0] and seq[0] > 0)
        if acumulado:
            print()
            print("  AVISO: metricas do MCU sao acumuladores desde o boot")
            print("  (leitura nao destrutiva: firmware anterior a v1.1.23).")
            print("  Diferenciando entre leituras.")
            print("  A primeira leitura da campanha e descartada: nao ha")
            print("  referencia anterior, entao ela carrega o historico do boot.")
            print()
            print("  Contadores foram corrigidos. EXTREMOS NAO TEM CONSERTO:")
            print("  loop min/max, WCET e max rec sao recordes desde o boot e nao")
            print("  podem ser diferenciados. Eles so ALARGAM ao longo da campanha,")
            print("  entao a coluna jitter NAO mede efeito de carga por cenario --")
            print("  vale so o envelope da campanha inteira (ultima linha).")
            print("  Para atribuicao por cenario, use firmware >= v1.1.23.")
            difs = []
            for i in range(1, len(leituras)):
                scen, cur = leituras[i]
                prev = leituras[i - 1][1]
                d = dict(cur)
                for k in CONTADORES:
                    if k in cur and k in prev:
                        d[k] = max(0.0, cur[k] - prev[k])
                difs.append((scen, d))
            leituras = difs

        # Um CSV de latencia sem o _mcu.csv correspondente significa que o
        # bench RECUSOU gravar metricas implausiveis. E um dado ausente,
        # nao um dado zero: precisa aparecer, senao a media mente.
        faltando = []
        for f in files:
            base = os.path.basename(f)[:-4]
            if not any(os.path.basename(m) == base + "_mcu.csv" for m in mcu_files):
                faltando.append(base)
        if faltando:
            print()
            print("  AVISO: sem metricas do MCU em %d repeticao(oes): %s"
                  % (len(faltando), ", ".join(sorted(faltando))))
            print("  O bench recusou gravar (leitura fora de sincronia ou")
            print("  implausivel). Essas repeticoes nao entram na agregacao.")

        mcu = defaultdict(lambda: defaultdict(list))
        for scen, vals in leituras:
            for k, v in vals.items():
                mcu[scen][k].append(v)

        print()
        print("Metricas do dominio critico (MCU)")
        print("=" * 104)
        print(f"{'cenario':<11} {'quadros':>8} {'loop min':>9} {'loop max':>9} "
              f"{'jitter':>8} {'deadline':>8} {'WCET cmd':>9} {'CRC err':>8} "
              f"{'recup':>6} {'max rec':>9} {'drv err':>8}")
        print(f"{'':11} {'':>8} {'(us)':>9} {'(us)':>9} {'(us)':>8} {'miss':>8} "
              f"{'(us)':>9} {'':>8} {'':>6} {'(us)':>9} {'':>8}")
        print("-" * 104)
        for scen in [x for x in scens if x in mcu] + [x for x in mcu if x not in scens]:
            g = mcu[scen]
            def mx(k):
                """Contador: soma as repeticoes. Extremo: pega o maior."""
                if not g.get(k):
                    return 0.0
                return sum(g[k]) if k in CONTADORES else max(g[k])
            def mn(k):
                vals = [v for v in g.get(k, []) if v > 0]
                return min(vals) if vals else 0.0
            lo, hi = mn("loop_period_min_us"), mx("loop_period_max_us")
            print(f"{scen:<11} {mx('frames_seen'):>8.0f} {lo:>9.0f} {hi:>9.0f} "
                  f"{hi - lo:>8.0f} {mx('deadline_misses'):>8.0f} "
                  f"{mx('cmd_lat_max_us'):>9.0f} {mx('crc_errors'):>8.0f} "
                  f"{mx('recoveries'):>6.0f} {mx('max_recovery_us'):>9.0f} "
                  f"{mx('driver_errors'):>8.0f}")

        print()
        print("  loop min/max: intervalo real entre iteracoes do laco de controle")
        print(f"  deadline miss: iteracoes acima do limite (periodo alvo 100 ms + 10%)")
        print("  WCET cmd: pior latencia do caminho comando -> atuacao (software)")
        print("  Nao inclui o jitter do PULSO do motor: esse sai de timer em")
        print("  hardware e precisa do analisador logico.")
        print("  drv err: falhas do driver SPI do MCU (timeout/overrun), cada uma")
        print("  seguida de reset do periferico. Contadas em quadros, nao em")
        print("  transacoes: SET_CONFIG gasta 3 quadros.")

        # Taxa global de erro de driver: e o numero que responde se a carga
        # do gateway degrada o enlace, e o que sustenta ou nao a alegacao
        # de isolamento.
        tot_fr = sum(sum(mcu[s].get("frames_seen", [])) for s in mcu)
        tot_de = sum(sum(mcu[s].get("driver_errors", [])) for s in mcu)
        tot_crc = sum(sum(mcu[s].get("crc_errors", [])) for s in mcu)
        tot_rec = sum(sum(mcu[s].get("recoveries", [])) for s in mcu)
        if tot_fr:
            print()
            if acumulado:
                lo_g = min([v for s in mcu for v in mcu[s].get("loop_period_min_us", []) if v > 0] or [0])
                hi_g = max([v for s in mcu for v in mcu[s].get("loop_period_max_us", [])] or [0])
                print(f"  Envelope do laco na campanha inteira: [{lo_g:.0f}, {hi_g:.0f}] us "
                      f"-> {hi_g - lo_g:.0f} us de jitter, alvo 100000 us")
            print(f"  Campanha: {tot_fr:.0f} quadros, {tot_de:.0f} falhas de driver "
                  f"({100.0 * tot_de / tot_fr:.3f}%)")
            if tot_crc:
                print(f"            {tot_crc:.0f} CRC invalidos detectados, "
                      f"{tot_rec:.0f} recuperados "
                      f"({100.0 * tot_rec / tot_crc:.1f}%)")

    # --- Cenarios de ataque -------------------------------------------------
    if attack_files:
        print()
        print("Cenarios de ataque")
        print("=" * 104)
        for f in attack_files:
            with open(f, newline="") as fh:
                cur = None
                for row in csv.DictReader(fh):
                    exp = row.get("experimento", "")
                    if exp != cur:
                        print(f"\n  [{exp}]")
                        cur = exp
                    met, val = row.get("metrica", ""), row.get("valor", "")
                    if met == "defesa_segurou":
                        print(f"    {'DEFESA SEGUROU' if val == '1' else 'DEFESA FALHOU'}")
                    elif met == "observacao":
                        print(f"    {val}")
                    else:
                        print(f"    {met:<32} {val}")

    if args.csv:
        with open(args.csv, "w", newline="") as fh:
            w = csv.writer(fh)
            w.writerow(["cenario", "reps", "n", "media_us", "mediana_us",
                        "p95_us", "p99_us", "min_us", "max_us", "desvio_us",
                        "jitter_us", "falhas_pct"])
            for scen, reps, a, fr in rows:
                w.writerow([scen, reps, a["n"], f"{a['mean']:.3f}",
                            f"{a['median']:.3f}", f"{a['p95']:.3f}",
                            f"{a['p99']:.3f}", f"{a['min']:.3f}",
                            f"{a['max']:.3f}", f"{a['stdev']:.3f}",
                            f"{a['jitter']:.3f}", f"{fr:.4f}"])
        print(f"\nagregado gravado em {args.csv}")

    if args.latex:
        with open(args.latex, "w") as fh:
            fh.write("% Gerado por analyze_campaign.py\n")
            fh.write("\\begin{table}[t]\n\\centering\n")
            fh.write("\\caption{End-to-end SPI transaction latency on the gateway "
                     "under increasing load. Each scenario aggregates %d repetitions.}\n"
                     % (max(len(per_rep[s]) for s in scens)))
            fh.write("\\label{tab:latency}\n")
            fh.write("\\begin{tabular}{lrrrrrr}\n\\hline\n")
            fh.write("\\textbf{Scenario} & \\textbf{n} & \\textbf{Mean} & "
                     "\\textbf{Median} & \\textbf{p99} & \\textbf{Max} & "
                     "\\textbf{Fail} \\\\\n")
            fh.write(" & & ($\\mu$s) & ($\\mu$s) & ($\\mu$s) & ($\\mu$s) & (\\%) \\\\\n")
            fh.write("\\hline\n")
            for scen, reps, a, fr in rows:
                fh.write(f"{scen} & {a['n']} & {a['mean']:.1f} & {a['median']:.1f} & "
                         f"{a['p99']:.1f} & {a['max']:.1f} & {fr:.2f} \\\\\n")
            fh.write("\\hline\n\\end{tabular}\n\\end{table}\n")

            # --- Tabela do dominio critico -----------------------------
            # E a que responde ao revisor. A de latencia mede o gateway;
            # esta mede se a saturacao do gateway atravessa a fronteira.
            if mcu and not acumulado:
                def agg(scen, k):
                    v = mcu[scen].get(k)
                    if not v:
                        return 0.0
                    return sum(v) if k in CONTADORES else max(v)
                def agg_min(scen, k):
                    v = [x for x in mcu[scen].get(k, []) if x > 0]
                    return min(v) if v else 0.0

                fh.write("\n% Gerado por analyze_campaign.py\n")
                fh.write("\\begin{table}[t]\n\\centering\n")
                fh.write("\\caption{Control-loop timing inside the critical "
                         "domain (MCU) while the gateway is driven to "
                         "saturation. Target period is 100\\,ms; the deadline "
                         "is 110\\,ms.}\n")
                fh.write("\\label{tab:mcu_determinism}\n")
                fh.write("\\begin{tabular}{lrrrrr}\n\\hline\n")
                fh.write("\\textbf{Scenario} & \\textbf{Frames} & "
                         "\\textbf{Period} & \\textbf{Jitter} & "
                         "\\textbf{WCET} & \\textbf{Missed} \\\\\n")
                fh.write(" & & ($\\mu$s) & ($\\mu$s) & ($\\mu$s) & "
                         "\\textbf{deadlines} \\\\\n")
                fh.write("\\hline\n")
                ordem = [x for x in scens if x in mcu] + \
                        [x for x in mcu if x not in scens]
                for scen in ordem:
                    lo, hi = agg_min(scen, "loop_period_min_us"), agg(scen, "loop_period_max_us")
                    fh.write(f"{scen} & {agg(scen, 'frames_seen'):.0f} & "
                             f"{lo:.0f}--{hi:.0f} & {hi - lo:.0f} & "
                             f"{agg(scen, 'cmd_lat_max_us'):.0f} & "
                             f"{agg(scen, 'deadline_misses'):.0f} \\\\\n")
                fh.write("\\hline\n\\end{tabular}\n\\end{table}\n")
                print(f"tabela do MCU incluida em {args.latex}")
            elif acumulado:
                print("tabela do MCU OMITIDA: metricas acumuladas desde o boot,")
                print("  os extremos por cenario nao sao atribuiveis.")
        print(f"tabela LaTeX gravada em {args.latex}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
