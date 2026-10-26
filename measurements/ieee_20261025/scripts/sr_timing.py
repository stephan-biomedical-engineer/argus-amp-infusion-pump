#!/usr/bin/env python3
"""Extrai intervalos de tempo, um por evento, de capturas .sr do sigrok.

Bancada 02 (distribuições de timing). Lê o .sr direto (é um zip com
`metadata` + blocos `logic-1-N` de amostras cruas), sem decodificador do
sigrok, e grava um CSV com um valor por linha, em microssegundos.

Subcomandos:

  periodos   <arq.sr> --canal D0 --out G3_baseline.csv
      Intervalo entre subidas consecutivas do canal. Serve para o pulso
      STEP (G3, canal D0). Para o marcador C do laço (G1, canal D7), que
      inverte o nível a cada volta, use --bordas todas.

  marcadores <arq.sr> --a D1 --b D2 --out G2_baseline.csv
      G2: para cada subida de B, a subida de A imediatamente anterior
      (quadro validado -> início da atuação). O protocolo é síncrono, então
      há um único A antes de cada B (ver markers.h). Use uma captura em
      modo --cmd mix: só RUN/PAUSE produzem B.

  largura    <arq.sr> --canal D1 --out G4_baseline.csv
      G4: largura de cada pulso de A (quadro decodificado -> SPI rearmado
      com a resposta pronta; é o "23,3 us" do artigo). Use uma captura em
      modo --cmd status. Em modo mix a largura tem três modos (status
      ~24 us, RUN/PAUSE ~87 us, SET_CONFIG ~150-250 us) e o script avisa.

Validado contra a tab:critical_timing publicada: capturas/atuacao_limpa2.sr
com --a D6 --b D7 (fiação antiga) dá n=677, p50=123,75, máx=136,5 us.

Ao final, imprime n, p50, p95, p99 e máximo para conferência na bancada.
"""
import argparse
import configparser
import sys
import zipfile

import numpy as np


def carregar_sr(caminho):
    """Devolve (amostras, samplerate_hz, {nome_canal: bit})."""
    with zipfile.ZipFile(caminho) as z:
        meta = configparser.ConfigParser()
        meta.read_string(z.read("metadata").decode())
        dev = meta["device 1"]
        unit = int(dev.get("unitsize", "1"))
        num, _, suf = dev["samplerate"].strip().partition(" ")
        taxa = float(num) * {"Hz": 1, "kHz": 1e3, "MHz": 1e6, "GHz": 1e9}[suf or "Hz"]
        # probeN=D3 -> o bit N-1 da amostra carrega o canal D3
        canais = {v: int(k[5:]) - 1 for k, v in dev.items() if k.startswith("probe")}
        prefixo = dev.get("capturefile", "logic-1")
        blocos = sorted(
            (n for n in z.namelist() if n.startswith(prefixo + "-")),
            key=lambda n: int(n.rsplit("-", 1)[1]),
        )
        bruto = b"".join(z.read(n) for n in blocos)
    dtype = {1: np.uint8, 2: np.uint16, 4: np.uint32}[unit]
    amostras = np.frombuffer(bruto, dtype=dtype)
    print(f"{caminho}: {len(amostras)} amostras, {taxa/1e6:g} MHz, "
          f"{len(amostras)/taxa:.2f} s")
    return amostras, taxa, canais


def bordas(amostras, bit):
    """Índices das subidas e das descidas de um canal."""
    d = np.diff(((amostras >> bit) & 1).astype(np.int8))
    return np.flatnonzero(d == 1) + 1, np.flatnonzero(d == -1) + 1


def resumo(rotulo, us):
    if len(us) == 0:
        print(f"{rotulo}: n=0 (nenhum evento; confira canal e fiação)")
        return
    p50, p95, p99 = np.percentile(us, [50, 95, 99])
    print(f"{rotulo}: n={len(us)}  p50={p50:.2f}  p95={p95:.2f}  "
          f"p99={p99:.2f}  max={us.max():.2f}  min={us.min():.2f} us")


def gravar(caminho, us):
    np.savetxt(caminho, us, fmt="%.3f", header="us", comments="")


def cmd_periodos(a):
    amostras, taxa, canais = carregar_sr(a.arquivo)
    sob, des = bordas(amostras, canais[a.canal])
    # "todas": o marcador C inverte o nível a cada volta, então cada borda,
    # de subida ou de descida, marca uma volta do laço.
    ev = np.sort(np.concatenate([sob, des])) if a.bordas == "todas" else sob
    us = np.diff(ev) / taxa * 1e6
    resumo(f"periodos {a.canal} ({a.bordas})", us)
    gravar(a.out, us)


def cmd_marcadores(a):
    amostras, taxa, canais = carregar_sr(a.arquivo)
    a_sob, _ = bordas(amostras, canais[a.a])
    b_sob, _ = bordas(amostras, canais[a.b])
    print(f"{len(a_sob)} subidas de A, {len(b_sob)} subidas de B")

    idx = np.searchsorted(a_sob, b_sob, side="right") - 1
    ok = idx >= 0
    # Dois B com o mesmo A não deveriam existir num protocolo síncrono;
    # se existirem, fica só o primeiro e o descarte é avisado.
    _, primeiro = np.unique(idx[ok], return_index=True)
    descartados = np.count_nonzero(ok) - len(primeiro)
    if descartados:
        print(f"AVISO: {descartados} B compartilharam o mesmo A (descartados)")
    g2 = (b_sob[ok][primeiro] - a_sob[idx[ok][primeiro]]) / taxa * 1e6
    resumo("G2 A->B", g2)
    gravar(a.out, g2)


def cmd_largura(a):
    amostras, taxa, canais = carregar_sr(a.arquivo)
    sob, des = bordas(amostras, canais[a.canal])
    j = np.searchsorted(des, sob, side="right")
    ok = j < len(des)
    us = (des[j[ok]] - sob[ok]) / taxa * 1e6
    resumo(f"largura {a.canal}", us)
    if len(us):
        longos = np.mean(us > 60) * 100
        if longos > 1:
            print(f"AVISO: {longos:.0f}% dos pulsos passam de 60 us -- a captura "
                  f"não é só de status (modo mix?). G4 deve vir de --cmd status.")
    gravar(a.out, us)


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)

    pp = sub.add_parser("periodos")
    pp.add_argument("arquivo")
    pp.add_argument("--canal", default="D0")
    pp.add_argument("--bordas", choices=["subida", "todas"], default="subida",
                    help="subida: STEP (G3); todas: marcador C, que inverte (G1)")
    pp.add_argument("--out", required=True)
    pp.set_defaults(f=cmd_periodos)

    pm = sub.add_parser("marcadores")
    pm.add_argument("arquivo")
    pm.add_argument("--a", default="D1")
    pm.add_argument("--b", default="D2")
    pm.add_argument("--out", required=True)
    pm.set_defaults(f=cmd_marcadores)

    pl = sub.add_parser("largura")
    pl.add_argument("arquivo")
    pl.add_argument("--canal", default="D1")
    pl.add_argument("--out", required=True)
    pl.set_defaults(f=cmd_largura)

    a = p.parse_args()
    a.f(a)


if __name__ == "__main__":
    sys.exit(main())
