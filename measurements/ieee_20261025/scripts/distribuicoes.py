#!/usr/bin/env python3
"""Tabela de percentis, teste KS e ECDF das distribuições da bancada 02.

Uso:
  distribuicoes.py <pasta_com_csvs> --out <pasta_saida>

Lê arquivos  G<k>_<condicao>.csv  ou  G<k>_<condicao>_r<N>.csv  (um valor
em us por linha, cabeçalho "us", como os gerados por sr_timing.py). As
repetições _r<N> de uma mesma condição são concatenadas, e a tabela mostra
quantas foram e a faixa das medianas entre elas.

Gera em <pasta_saida>:
  tabela.csv / tabela.md  n, min, p50, p95, p99, p99.9, max, média, desvio
                          por grandeza e condição, e o teste KS de duas
                          amostras de cada condição contra a "baseline"
  ecdf_G<k>.png           ECDF sobreposta das condições de cada grandeza

O KS responde à pergunta do revisor -- a distribuição muda sob estresse? --
sem supor normalidade. Com n grande, p < 0,05 aparece até para diferenças
de fração de microssegundo; por isso a tabela traz também a estatística D
(maior distância entre as ECDFs) e a diferença de mediana em us, que dizem
se a diferença importa.
"""
import argparse
import glob
import os
import re
import sys

import numpy as np

ORDEM = ["ocioso", "baseline", "cpu", "io", "net", "combined"]
# Paleta categórica validada (ordem fixa, cor segue a condição, não a posição).
COR = {"ocioso": "#6250d6", "baseline": "#2a78d6", "cpu": "#eb6834", "io": "#1baf7a",
       "net": "#eda100", "combined": "#e87ba4"}
# Estilo de linha como segunda codificação (impressão em tons de cinza).
TRACO = {"ocioso": ":", "baseline": "-", "cpu": "--", "io": "-.", "net": ":", "combined": "-"}
NOME = {"G1": "Período do laço de controle (µs)",
        "G2": "Quadro → atuação (µs)",
        "G3": "Período do pulso STEP (µs)",
        "G4": "Validação do quadro, largura do marcador A (µs)",
        "G5": "Comando → atuação, pior caso por ciclo, MCU (µs)"}


def ks(x, y):
    """KS de duas amostras: (D, p). Usa scipy se houver; senão, assintótico."""
    try:
        from scipy.stats import ks_2samp
        r = ks_2samp(x, y)
        return float(r.statistic), float(r.pvalue)
    except ImportError:
        tudo = np.sort(np.concatenate([x, y]))
        fx = np.searchsorted(np.sort(x), tudo, side="right") / len(x)
        fy = np.searchsorted(np.sort(y), tudo, side="right") / len(y)
        d = float(np.max(np.abs(fx - fy)))
        en = np.sqrt(len(x) * len(y) / (len(x) + len(y)))
        lam = (en + 0.12 + 0.11 / en) * d
        k = np.arange(1, 101)
        p = float(np.clip(2 * np.sum((-1) ** (k - 1) * np.exp(-2 * k**2 * lam**2)), 0, 1))
        return d, p


def chave_cond(c):
    return (ORDEM.index(c) if c in ORDEM else len(ORDEM), c)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pasta")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)

    partes = {}
    for f in sorted(glob.glob(os.path.join(a.pasta, "G*_*.csv"))):
        m = re.match(r"(G\d)_([a-z]+)(?:_r\d+)?\.csv$", os.path.basename(f))
        if not m:
            continue
        v = np.loadtxt(f, skiprows=1, ndmin=1)
        if len(v):
            partes.setdefault(m.group(1), {}).setdefault(m.group(2), []).append(v)
    dados = {g: {c: np.concatenate(vs) for c, vs in cs.items()}
             for g, cs in partes.items()}
    medianas_reps = {(g, c): [float(np.median(v)) for v in vs]
                     for g, cs in partes.items() for c, vs in cs.items()}
    if not dados:
        sys.exit(f"nenhum G<k>_<condicao>.csv em {a.pasta}")

    cab = ["grandeza", "condicao", "reps", "p50_reps", "n", "min", "p50", "p95", "p99", "p99.9",
           "max", "media", "desvio", "dp50_vs_base", "ks_D", "ks_p"]
    linhas = []
    for g in sorted(dados):
        base = dados[g].get("baseline")
        for c in sorted(dados[g], key=chave_cond):
            v = dados[g][c]
            q = np.percentile(v, [50, 95, 99, 99.9])
            if base is not None and c != "baseline" and len(v) and len(base):
                d, p = ks(v, base)
                dmed = q[0] - np.median(base)
                extra = [f"{dmed:+.2f}", f"{d:.3f}", f"{p:.2g}"]
            else:
                extra = ["", "", ""]
            mr = medianas_reps[(g, c)]
            faixa = f"{min(mr):.2f}-{max(mr):.2f}" if len(mr) > 1 else ""
            linhas.append([g, c, str(len(mr)), faixa, str(len(v)), f"{v.min():.2f}", *(f"{x:.2f}" for x in q),
                           f"{v.max():.2f}", f"{v.mean():.2f}", f"{v.std(ddof=1):.2f}",
                           *extra])

    with open(os.path.join(a.out, "tabela.csv"), "w") as f:
        f.write(",".join(cab) + "\n")
        for l in linhas:
            f.write(",".join(l) + "\n")
    with open(os.path.join(a.out, "tabela.md"), "w") as f:
        f.write("| " + " | ".join(cab) + " |\n|" + "---|" * len(cab) + "\n")
        for l in linhas:
            f.write("| " + " | ".join(l) + " |\n")
    print(open(os.path.join(a.out, "tabela.md")).read())

    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib ausente: tabela gerada, ECDF não")
        return

    for g in sorted(dados):
        fig, ax = plt.subplots(figsize=(3.5, 2.4), dpi=300)  # uma coluna IEEE
        for c in sorted(dados[g], key=chave_cond):
            v = np.sort(dados[g][c])
            y = np.arange(1, len(v) + 1) / len(v)
            ax.step(v, y, where="post", lw=1.2, color=COR.get(c, "#555555"),
                    ls=TRACO.get(c, "-"), label=f"{c} (n={len(v)})")
        ax.set_xlabel(NOME.get(g, g), fontsize=7)
        ax.set_ylabel("Fração acumulada", fontsize=7)
        ax.set_ylim(0, 1.005)
        ax.tick_params(labelsize=6, length=2, color="#999999")
        ax.grid(True, lw=0.3, color="#dddddd")
        for s in ("top", "right"):
            ax.spines[s].set_visible(False)
        for s in ("left", "bottom"):
            ax.spines[s].set_color("#999999")
        if g == "G1":  # deadline do laço
            ax.axvline(110000, lw=0.8, color="#333333", ls=":")
            ax.text(110000, 0.5, " deadline\n 110 ms", fontsize=6, color="#333333")
        ax.legend(fontsize=6, frameon=False, loc="lower right")
        fig.tight_layout()
        fig.savefig(os.path.join(a.out, f"ecdf_{g}.png"))
        plt.close(fig)
        print(f"-> {os.path.join(a.out, f'ecdf_{g}.png')}")


if __name__ == "__main__":
    main()
