#!/usr/bin/env python3
"""Linha do tempo de uma captura .sr da bancada 01.

Uso:
  sr_eventos.py <arq.sr> [--step D4] [--nrst D0] [--a D5] [--json saida.json]

Imprime:
  - trechos do STEP com frequência constante (início, fim, Hz, pulsos);
    um trecho novo começa quando o período muda mais de 2 % ou o pulso some
    por mais de 3 períodos;
  - frequência máxima do STEP (mediana de 5 períodos consecutivos, para que
    um glitch isolado não vire "máximo");
  - pulsos baixos do NRST (reinícios do MCU), com instante e duração;
  - último pulso do STEP e a subida do marcador A imediatamente anterior
    (decisão -> parada, usado no T3).
"""
import argparse
import importlib.util
import json
import os

import numpy as np

_aqui = os.path.dirname(os.path.abspath(__file__))
_sp = importlib.util.spec_from_file_location("sr_timing", os.path.join(_aqui, "sr_timing.py"))
sr = importlib.util.module_from_spec(_sp)
_sp.loader.exec_module(sr)


def trechos_step(sob, taxa):
    if len(sob) < 3:
        return []
    per = np.diff(sob) / taxa
    out, ini = [], 0
    ref = per[0]
    for i in range(1, len(per)):
        if per[i] > 3 * ref or abs(per[i] - ref) / ref > 0.02:
            out.append((ini, i))
            ini, ref = i, per[i]
        else:
            ref = 0.9 * ref + 0.1 * per[i]
    out.append((ini, len(per)))
    res = []
    for a, b in out:
        p = per[a:b]
        if len(p) == 0:
            continue
        if len(p) < 3 and len(out) > 1:  # pulso solto entre trechos
            continue
        res.append({"inicio_s": round(sob[a] / taxa, 6), "fim_s": round(sob[b] / taxa, 6),
                    "hz": round(1 / float(np.median(p)), 2), "pulsos": int(b - a + 1)})
    return res


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("arquivo")
    ap.add_argument("--step", default="D4")
    ap.add_argument("--nrst", default="D0")
    ap.add_argument("--a", default="D5")
    ap.add_argument("--json")
    a = ap.parse_args()

    d, taxa, c = sr.carregar_sr(a.arquivo)
    dur = len(d) / taxa
    st_sob, _ = sr.bordas(d, c[a.step])
    r = {"arquivo": a.arquivo, "duracao_s": round(dur, 3), "taxa_hz": taxa,
         "step_pulsos": int(len(st_sob))}

    r["step_trechos"] = trechos_step(st_sob, taxa)
    if len(st_sob) >= 6:
        per = np.diff(st_sob) / taxa
        med5 = np.convolve(per, np.ones(5) / 5, mode="valid")
        r["step_max_hz"] = round(1 / float(med5.min()), 2)
        r["step_ultimo_pulso_s"] = round(st_sob[-1] / taxa, 6)
        r["step_parado_no_fim_s"] = round(dur - st_sob[-1] / taxa, 3)
        r["step_maior_lacuna_s"] = round(float(per.max()), 6)

    n_sob, n_des = sr.bordas(d, c[a.nrst])
    pulsos = []
    for t0 in n_des:
        k = np.searchsorted(n_sob, t0)
        t1 = n_sob[k] if k < len(n_sob) else len(d)
        pulsos.append({"inicio_s": round(t0 / taxa, 6), "duracao_ms": round((t1 - t0) / taxa * 1e3, 3)})
    r["nrst_pulsos"] = pulsos
    r["nrst_baixo_no_fim"] = bool(((d[-1] >> c[a.nrst]) & 1) == 0)

    a_sob, _ = sr.bordas(d, c[a.a])
    r["marcador_a_subidas"] = int(len(a_sob))
    if len(st_sob) and len(a_sob):
        k = np.searchsorted(a_sob, st_sob[-1]) - 1
        if k >= 0:
            r["decisao_ate_ultimo_pulso_us"] = round((st_sob[-1] - a_sob[k]) / taxa * 1e6, 2)

    print(f"{os.path.basename(a.arquivo)}: {dur:.2f} s, {r['step_pulsos']} pulsos STEP, "
          f"max {r.get('step_max_hz', 0)} Hz, {len(pulsos)} pulsos NRST")
    for t in r["step_trechos"]:
        print(f"  STEP {t['inicio_s']:9.4f} -> {t['fim_s']:9.4f} s  {t['hz']:10.2f} Hz  ({t['pulsos']} pulsos)")
    for p in pulsos:
        print(f"  NRST baixo em {p['inicio_s']:.4f} s por {p['duracao_ms']} ms")
    if "decisao_ate_ultimo_pulso_us" in r:
        print(f"  último STEP em {r['step_ultimo_pulso_s']} s; A anterior -> último pulso: "
              f"{r['decisao_ate_ultimo_pulso_us']} us; parado nos últimos {r['step_parado_no_fim_s']} s")
    if a.json:
        with open(a.json, "w") as f:
            json.dump(r, f, indent=1)


if __name__ == "__main__":
    main()
