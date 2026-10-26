#!/usr/bin/env python3
"""Converte as janelas de métricas do MCU em amostras por evento.

Bancada 02, modo sem analisador. A função `janelas` da timing_lib.sh lê
GET_METRICS em janelas curtas e grava uma linha por janela. Como a leitura
zera os contadores, cada linha cobre só o intervalo desde a anterior.

Uso:
  janelas_para_amostras.py <janelas_laco_<cond>.csv>    --out <pasta>
  janelas_para_amostras.py <janelas_comando_<cond>.csv> --out <pasta>

laco    -> <pasta>/G1_<cond>.csv, um período do laço por linha (us).
           Janela com 1 volta: min = max = o período. Com 2 voltas: min e
           max são os dois períodos. Com 3 ou mais, as voltas do meio se
           perdem; o script grava min e max e informa quantas se perderam.
comando -> <pasta>/G5_<cond>.csv, o pior caso de comando -> atuação (MCU)
           por ciclo de 8 quadros do modo mix (SET_CONFIG, RUN, PAUSE).
"""
import argparse
import csv
import os
import re
import sys


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("arquivo")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    m = re.match(r"janelas_(laco|comando)_([a-z]+)\.csv$", os.path.basename(a.arquivo))
    if not m:
        sys.exit("nome esperado: janelas_<laco|comando>_<condicao>.csv")
    modo, cond = m.groups()
    os.makedirs(a.out, exist_ok=True)

    linhas = []
    with open(a.arquivo) as f:
        for r in csv.DictReader(f):
            try:
                linhas.append({k: float(v) for k, v in r.items()})
            except (TypeError, ValueError):
                pass  # linha truncada (leitura interrompida)
    if not linhas:
        sys.exit("nenhuma janela válida")

    dur = linhas[-1]["t_s"] - linhas[0]["t_s"]
    crc = sum(l["crc_errors"] for l in linhas)
    print(f"{a.arquivo}: {len(linhas)} janelas em {dur:.0f} s, crc_errors={crc:.0f}")

    if modo == "laco":
        amostras, perdidas, hist = [], 0, {}
        misses = 0
        for l in linhas:
            n = int(l["loop_count"])
            hist[min(n, 3)] = hist.get(min(n, 3), 0) + 1
            misses += int(l["deadline_misses"])
            if n == 1:
                amostras.append(l["loop_period_min_us"])
            elif n >= 2:
                amostras += [l["loop_period_min_us"], l["loop_period_max_us"]]
                perdidas += n - 2
        total = len(amostras) + perdidas
        print(f"janelas por nº de voltas: 0={hist.get(0,0)} 1={hist.get(1,0)} "
              f"2={hist.get(2,0)} 3+={hist.get(3,0)}")
        print(f"voltas: {total} (esperado ~{dur/0.1:.0f}), gravadas {len(amostras)}, "
              f"perdidas {perdidas} ({100*perdidas/max(total,1):.1f}%), "
              f"deadline_misses={misses}")
        destino = os.path.join(a.out, f"G1_{cond}.csv")
    else:
        amostras = [l["cmd_lat_max_us"] for l in linhas if l["cmd_count"] > 0]
        cmds = sum(l["cmd_count"] for l in linhas)
        print(f"ciclos com comando: {len(amostras)}, comandos: {cmds:.0f}")
        destino = os.path.join(a.out, f"G5_{cond}.csv")

    with open(destino, "w") as f:
        f.write("us\n")
        for v in amostras:
            f.write(f"{v:.0f}\n")
    if amostras:
        s = sorted(amostras)
        q = lambda p: s[min(len(s) - 1, int(p * len(s)))]
        print(f"-> {destino}: n={len(s)} min={s[0]:.0f} p50={q(.5):.0f} "
              f"p99={q(.99):.0f} max={s[-1]:.0f} us")


if __name__ == "__main__":
    main()
