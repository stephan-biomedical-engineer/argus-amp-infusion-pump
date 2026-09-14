// =========================================================================
// gerar.mjs — figuras do artigo, em SVG, a partir dos JSON exportados
// =========================================================================
//   node gerar.mjs           -> escreve saida/*.svg
// Conversao para PDF/PNG fica a cargo do rsvg-convert (ver Makefile do dir).
//
// Principios de desenho adotados, vindos da revisao das versoes anteriores:
//   - uma escala por painel: nada de janela unica de milissegundos tentando
//     mostrar pulsos de microssegundos;
//   - legenda SEMPRE fora da area de dados;
//   - uma cor por condicao, repetida em todas as figuras.
// =========================================================================

import * as d3 from "d3";
import fs from "node:fs";

const COR = {
  idle: "#1f5fa9",
  sat: "#c85a12",
  meio: "#2e7d32",
  grade: "#d9d9d9",
  eixo: "#333333",
  fraco: "#6b6b6b",
  faixa: "#e8e8e8",
};
const FONTE = "Helvetica, Arial, sans-serif";

const esc = (s) => String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;");
const txt = (x, y, s, o = {}) =>
  `<text x="${x.toFixed(1)}" y="${y.toFixed(1)}" font-family="${FONTE}" ` +
  `font-size="${o.fs ?? 10}" fill="${o.cor ?? COR.eixo}" ` +
  `text-anchor="${o.anc ?? "start"}" ${o.peso ? 'font-weight="bold"' : ""}>${esc(s)}</text>`;
const linha = (x1, y1, x2, y2, o = {}) =>
  `<line x1="${x1.toFixed(1)}" y1="${y1.toFixed(1)}" x2="${x2.toFixed(1)}" y2="${y2.toFixed(1)}" ` +
  `stroke="${o.cor ?? COR.eixo}" stroke-width="${o.w ?? 0.8}" ${o.tracejado ? 'stroke-dasharray="3,2"' : ""}/>`;
const ret = (x, y, w, h, cor, o = {}) =>
  `<rect x="${x.toFixed(1)}" y="${y.toFixed(1)}" width="${Math.max(w, 0).toFixed(1)}" ` +
  `height="${Math.max(h, 0).toFixed(1)}" fill="${cor}" ${o.op ? `opacity="${o.op}"` : ""} ` +
  `${o.borda ? `stroke="${o.borda}" stroke-width="0.6"` : ""}/>`;

// eixo X com marcas e rotulos
function eixoX(sx, y, ticks, rotulo, o = {}) {
  let s = linha(sx.range()[0], y, sx.range()[1], y);
  for (const t of ticks) {
    const x = sx(t);
    s += linha(x, y, x, y + 4);
    s += txt(x, y + 14, o.fmt ? o.fmt(t) : t, { fs: 8.5, anc: "middle" });
  }
  if (rotulo) s += txt((sx.range()[0] + sx.range()[1]) / 2, y + 30, rotulo, { fs: 9.5, anc: "middle" });
  return s;
}
function eixoY(sy, x, ticks, rotulo, o = {}) {
  let s = linha(x, sy.range()[0], x, sy.range()[1]);
  for (const t of ticks) {
    const y = sy(t);
    s += linha(x - 4, y, x, y);
    s += txt(x - 7, y + 3, o.fmt ? o.fmt(t) : t, { fs: 8.5, anc: "end" });
  }
  if (rotulo) {
    const ym = (sy.range()[0] + sy.range()[1]) / 2;
    s += `<text transform="translate(${x - 34},${ym}) rotate(-90)" font-family="${FONTE}" ` +
         `font-size="9.5" fill="${COR.eixo}" text-anchor="middle">${esc(rotulo)}</text>`;
  }
  return s;
}
const titulo = (x, y, s) => txt(x, y, s, { fs: 10.5, peso: true });

function svg(w, h, corpo) {
  return `<svg xmlns="http://www.w3.org/2000/svg" width="${w}" height="${h}" viewBox="0 0 ${w} ${h}">` +
    `<rect width="${w}" height="${h}" fill="white"/>${corpo}</svg>`;
}

// caminho em degraus a partir de (nivel inicial, instantes de transicao)
function degraus(sx, y0, y1, nivelInicial, transicoes, tIni, tFim) {
  let nivel = nivelInicial;
  let d = `M ${sx(tIni).toFixed(2)} ${(nivel ? y1 : y0).toFixed(2)}`;
  for (const t of transicoes) {
    if (t < tIni || t > tFim) continue;
    d += ` L ${sx(t).toFixed(2)} ${(nivel ? y1 : y0).toFixed(2)}`;
    nivel = nivel ? 0 : 1;
    d += ` L ${sx(t).toFixed(2)} ${(nivel ? y1 : y0).toFixed(2)}`;
  }
  d += ` L ${sx(tFim).toFixed(2)} ${(nivel ? y1 : y0).toFixed(2)}`;
  return d;
}

fs.mkdirSync("saida", { recursive: true });

// =========================================================================
// F1 — jitter da atuacao
// =========================================================================
{
  const D = JSON.parse(fs.readFileSync("dados/f1.json"));
  const W = 720, H = 424;
  let c = "";

  // ---- (a) forma de onda -------------------------------------------------
  const ax = { x: 58, y: 34, w: W - 58 - 18, h: 96 };
  const sx = d3.scaleLinear().domain([D.onda.t0_us, D.onda.fim_us]).range([ax.x, ax.x + ax.w]);
  c += titulo(18, 20, "(a)  Actuation pulse: three consecutive periods");
  const y0 = ax.y + ax.h, y1 = ax.y + 16;
  for (const t of [0, 590, 1180, 1770, 2360, 2950, 3540]) {
    if (t >= D.onda.t0_us && t <= D.onda.fim_us) c += linha(sx(t), ax.y, sx(t), y0, { cor: COR.grade, w: 0.6 });
  }
  c += `<path d="${degraus(sx, y0, y1, D.onda.nivel_inicial, D.onda.transicoes_us, D.onda.t0_us, D.onda.fim_us)}" ` +
       `fill="none" stroke="${COR.idle}" stroke-width="1.6"/>`;
  // cota do periodo
  const yc = ax.y + 6;
  c += linha(sx(0), yc, sx(1180), yc, { cor: COR.fraco });
  c += linha(sx(0), yc - 3, sx(0), yc + 3, { cor: COR.fraco });
  c += linha(sx(1180), yc - 3, sx(1180), yc + 3, { cor: COR.fraco });
  c += txt((sx(0) + sx(1180)) / 2, yc - 5, `period ${D.nominal_us.toFixed(2)} µs`, { fs: 9, anc: "middle", cor: COR.fraco });
  c += eixoY(d3.scaleLinear().domain([0, 1]).range([y0, y1]), ax.x, [0, 1], "level");
  c += eixoX(sx, y0 + 2, [0, 590, 1180, 1770, 2360, 2950, 3540], "Time (µs)");

  // ---- (b) proporcao por nivel medido -----------------------------------
  const bx = { x: 58, y: 196, w: 300, h: 74 };
  c += titulo(18, 182, "(b)  Where the measured period falls");
  const niveis = D.resumo.idle.niveis.map((n) => n.us);
  const sxb = d3.scaleLinear().domain([0, 100]).range([bx.x, bx.x + bx.w]);
  const tons = { [niveis[0]]: 0.45, [niveis[1]]: 1.0, [niveis[2]]: 0.2 };
  let yb = bx.y;
  for (const [rot, cor] of [["idle", COR.idle], ["saturated", COR.sat]]) {
    let acc = 0;
    c += txt(bx.x - 7, yb + 15, rot, { fs: 9, anc: "end" });
    for (const n of D.resumo[rot].niveis) {
      const x = sxb(acc), w = sxb(n.pct) - sxb(0);
      c += ret(x, yb, w, 22, cor, { op: tons[n.us] ?? 0.3, borda: "#ffffff" });
      if (n.pct > 6) c += txt(x + w / 2, yb + 15, `${n.pct.toFixed(1)}%`, { fs: 8.5, anc: "middle", cor: "#ffffff" });
      acc += n.pct;
    }
    yb += 30;
  }
  // legenda fora da area de dados, com o percentual de cada nivel nas duas
  // condicoes -- o terceiro nivel e fino demais para ser rotulado na barra
  let ly1 = bx.y + 70;
  for (let i = 0; i < niveis.length; i++) {
    const a = D.resumo.idle.niveis[i], b2 = D.resumo.saturated.niveis[i];
    c += ret(bx.x, ly1 - 8, 10, 10, COR.idle, { op: tons[niveis[i]] ?? 0.3 });
    c += txt(bx.x + 16, ly1, `${niveis[i].toFixed(2)} µs`, { fs: 8.5 });
    c += txt(bx.x + 150, ly1, `${a.pct.toFixed(1)}%`, { fs: 8.5, anc: "end", cor: COR.idle });
    c += txt(bx.x + 215, ly1, `${b2.pct.toFixed(1)}%`, { fs: 8.5, anc: "end", cor: COR.sat });
    ly1 += 14;
  }
  c += txt(bx.x + 150, bx.y + 58, "idle", { fs: 8, anc: "end", cor: COR.idle });
  c += txt(bx.x + 215, bx.y + 58, "saturated", { fs: 8, anc: "end", cor: COR.sat });

  // ---- (c) estabilidade ao longo da aquisicao ---------------------------
  const cx = { x: 430, y: 196, w: W - 430 - 24, h: 96 };
  c += titulo(400, 182, "(c)  Stability over the acquisition");
  const sxc = d3.scaleLinear().domain([0, 15]).range([cx.x, cx.x + cx.w]);
  const syc = d3.scaleLinear().domain([78, 92]).range([cx.y + cx.h, cx.y]);
  for (const t of [80, 85, 90]) c += linha(cx.x, syc(t), cx.x + cx.w, syc(t), { cor: COR.grade, w: 0.5 });
  for (const [rot, cor] of [["idle", COR.idle], ["saturated", COR.sat]]) {
    let d = "";
    D.binned[rot].forEach(([t, pct], i) => {
      d += (i === 0 ? "M" : " L") + sxc(t).toFixed(1) + " " + syc(pct).toFixed(1);
    });
    c += `<path d="${d}" fill="none" stroke="${cor}" stroke-width="1.2" opacity="0.9"/>`;
  }
  c += eixoY(syc, cx.x, [80, 85, 90], "At nominal (%)", { fmt: (t) => t });
  c += eixoX(sxc, cx.y + cx.h + 2, [0, 5, 10, 15], "Acquisition time (s)");
  c += txt(cx.x, cx.y + cx.h + 44, "Share of periods at the nominal value, in 0.25 s bins:", { fs: 8.5, cor: COR.fraco });
  c += txt(cx.x, cx.y + cx.h + 55, "flat and overlapping, so neither drift nor bursts occur.", { fs: 8.5, cor: COR.fraco });

  // ---- rodape numerico ---------------------------------------------------
  const r1 = D.resumo.idle, r2 = D.resumo.saturated;
  const linhaResumo = (rot, r, cor, y) =>
    txt(18, y, `${rot}: n = ${r.n.toLocaleString("en")} periods,  median ${r.mediana.toFixed(2)} µs,  ` +
        `peak-to-peak ${r.p2p.toFixed(2)} µs,  s = ${r.desvio.toFixed(2)} µs`, { fs: 9, cor });
  c += linhaResumo("idle", r1, COR.idle, H - 26);
  c += linhaResumo("saturated", r2, COR.sat, H - 12);

  fs.writeFileSync("saida/fig_pulse_jitter.svg", svg(W, H, c));
  console.log("F1 escrita");
}

// =========================================================================
// F2 — quadro -> atuacao
// =========================================================================
{
  const D = JSON.parse(fs.readFileSync("dados/f2.json"));
  const W = 720, H = 400;
  const series = [
    ["None", D.none, COR.idle],
    ["Processor, storage, memory", D.cpu_disk_mem, COR.meio],
    ["Processor, storage, memory, network", D.full, COR.sat],
  ];
  let c = "";
  const ax = { x: 62, y: 40, w: W - 62 - 150, h: 210 };
  const todos = series.flatMap((s) => s[1]);
  const sx = d3.scaleLinear().domain([d3.min(todos) - 2, d3.max(todos) + 2]).range([ax.x, ax.x + ax.w]).nice();
  const sy = d3.scaleLinear().domain([0, 100]).range([ax.y + ax.h, ax.y]);
  c += titulo(18, 22, "Frame-to-actuation interval, cumulative distribution");

  // faixa reportada pela instrumentacao interna
  c += ret(sx(D.faixa_interna[0]), ax.y, sx(D.faixa_interna[1]) - sx(D.faixa_interna[0]), ax.h, COR.faixa);
  c += txt((sx(D.faixa_interna[0]) + sx(D.faixa_interna[1])) / 2, ax.y - 6,
    "on-target instrumentation", { fs: 8, anc: "middle", cor: COR.fraco });

  for (const t of sy.ticks(5)) c += linha(ax.x, sy(t), ax.x + ax.w, sy(t), { cor: COR.grade, w: 0.5 });
  for (const [, v, cor] of series) {
    let d = "";
    v.forEach((x, i) => {
      const px = sx(x), py = sy(((i + 1) / v.length) * 100);
      d += i === 0 ? `M${px.toFixed(1)} ${sy(0).toFixed(1)} L${px.toFixed(1)} ${py.toFixed(1)}`
                   : ` L${px.toFixed(1)} ${py.toFixed(1)}`;
    });
    c += `<path d="${d}" fill="none" stroke="${cor}" stroke-width="1.6"/>`;
  }
  c += eixoY(sy, ax.x, [0, 25, 50, 75, 100], "Percentile (%)", { fmt: (t) => t });
  c += eixoX(sx, ax.y + ax.h + 2, sx.ticks(7), "Interval (µs)");

  // legenda FORA da area de dados, a direita
  let ly = ax.y + 14;
  c += txt(ax.x + ax.w + 22, ly - 14, "Load on the", { fs: 9, peso: true });
  c += txt(ax.x + ax.w + 22, ly - 3, "connectivity domain", { fs: 9, peso: true });
  for (const [rot, v, cor] of series) {
    c += linha(ax.x + ax.w + 22, ly + 12, ax.x + ax.w + 40, ly + 12, { cor, w: 2.4 });
    const palavras = rot.split(", ");
    let yy = ly + 15;
    for (let i = 0; i < palavras.length; i += 2) {
      c += txt(ax.x + ax.w + 45, yy, palavras.slice(i, i + 2).join(", ") + (i + 2 < palavras.length ? "," : ""), { fs: 8.5 });
      yy += 10;
    }
    c += txt(ax.x + ax.w + 45, yy, `n = ${v.length}`, { fs: 8, cor: COR.fraco });
    ly = yy + 20;
  }

  // resumo numerico abaixo do grafico
  let y = ax.y + ax.h + 52;
  c += txt(18, y, "Load", { fs: 9, peso: true });
  c += txt(300, y, "median", { fs: 9, peso: true, anc: "end" });
  c += txt(370, y, "p99", { fs: 9, peso: true, anc: "end" });
  c += txt(440, y, "max", { fs: 9, peso: true, anc: "end" });
  y += 14;
  const chaves = ["none", "cpu_disk_mem", "full"];
  series.forEach(([rot, v, cor], i) => {
    // numeros vindos do mesmo calculo usado nas tabelas do artigo
    const r = D.resumo[chaves[i]];
    c += ret(18, y - 8, 8, 8, cor);
    c += txt(32, y, rot, { fs: 8.5 });
    c += txt(300, y, r.mediana.toFixed(1), { fs: 8.5, anc: "end" });
    c += txt(370, y, r.p99.toFixed(1), { fs: 8.5, anc: "end" });
    c += txt(440, y, r.max.toFixed(1), { fs: 8.5, anc: "end" });
    y += 13;
  });
  c += txt(18, y + 12, "The three curves overlap: the interval does not change with load.", { fs: 8.5, cor: COR.fraco });

  fs.writeFileSync("saida/fig_frame_to_actuation.svg", svg(W, H, c));
  console.log("F2 escrita");
}

// =========================================================================
// F3 — anatomia de uma transacao, com a decodificacao do protocolo
// =========================================================================
// Painel (a): o ciclo completo pedido -> processamento -> resposta, com os
//   seis canais capturados. Paineis (b) e (c): os mesmos bytes que passaram
//   no fio, decodificados campo a campo (SOF, enderecos, comando, tamanho,
//   carga util, CRC). A decodificacao vem do exportador em Python, que
//   amostra MOSI/MISO na subida de SCK e confere o CRC-16 antes de escrever
//   f3.json -- os rotulos sao o que o instrumento externo leu, nao o que o
//   firmware diz ter enviado. O par pedido/resposta escolhido tem duracao
//   proxima da mediana do arquivo, para nao mostrar um caso atipico.
// Proporcao pensada para figura de duas colunas (figure*).
{
  const D = JSON.parse(fs.readFileSync("dados/f3.json"));
  const W = 900, H = 470;
  const ordem = ["NSS", "SCK", "MOSI", "MISO", "READY", "MARK_A"];
  const nomes = { NSS: "NSS", SCK: "SCK", MOSI: "MOSI", MISO: "MISO", READY: "READY", MARK_A: "Marker A" };
  const [t0, t1] = D.janela_us;
  const [req, res] = D.transferencias;
  const qReq = D.quadros[0], qRes = D.quadros[1];
  const marca = D.canais.MARK_A.transicoes_us;
  let c = "";

  const CAMPO = {
    SOF:     { cor: "#1f5fa9", rot: "SOF" },
    DST:     { cor: "#2e7d32", rot: "DST" },
    SRC:     { cor: "#2e7d32", rot: "SRC" },
    CMD:     { cor: "#c85a12", rot: "CMD" },
    LEN:     { cor: "#7b5aa6", rot: "LEN" },
    PAYLOAD: { cor: "#4a4a4a", rot: "PAYLOAD" },
    CRC:     { cor: "#b3243c", rot: "CRC-16" },
  };

  // ---------------- painel (a): o ciclo inteiro ----------------
  const xa = 18, ya = 46, wa = W - 36, ha = 150;
  const sxa = d3.scaleLinear().domain([t0, t1]).range([xa + 58, xa + wa]);
  c += txt(xa, ya - 24, "(a)  One request/response cycle on the interprocessor link", { fs: 13, peso: true });
  for (const [tr, rot] of [[req, "request"], [res, "reply"]]) {
    c += ret(sxa(tr.ini), ya, sxa(tr.fim) - sxa(tr.ini), ha - 22, COR.idle, { op: 0.07 });
    c += txt((sxa(tr.ini) + sxa(tr.fim)) / 2, ya - 7, `${rot} — 64 B in ${(tr.fim - tr.ini).toFixed(0)} µs`,
             { fs: 11, anc: "middle", cor: COR.idle });
  }
  c += ret(sxa(marca[0]), ya, Math.max(sxa(marca[1]) - sxa(marca[0]), 1.5), ha - 22, COR.sat, { op: 0.28 });
  {
    const alt = (ha - 22 - 16) / ordem.length;
    ordem.forEach((k, i) => {
      const yy = ya + i * alt, base = yy + alt - 3, topo = yy + 3;
      c += txt(xa + 54, (base + topo) / 2 + 3.5, nomes[k], { fs: 11, anc: "end", cor: COR.fraco });
      const ch = D.canais[k];
      c += `<path d="${degraus(sxa, base, topo, ch.nivel_inicial, ch.transicoes_us, t0, t1)}" ` +
           `fill="none" stroke="${k === "MARK_A" ? COR.sat : COR.idle}" stroke-width="${k === "SCK" ? 0.4 : 1.1}"/>`;
    });
  }
  c += eixoX(sxa, ya + ha - 14, d3.range(0, t1, 500), "Time (µs)");
  {
    const xm = sxa(marca[1]);
    c += linha(xm, ya + ha - 34, xm + 52, ya + ha - 48, { cor: COR.sat, w: 0.8 });
    c += txt(xm + 55, ya + ha - 45,
      `firmware validates the frame and arms the reply: ${(marca[1] - marca[0]).toFixed(1)} µs`,
      { fs: 11, cor: COR.sat });
  }
  const zb = [qReq.bytes[0].t0 - 6, qReq.bytes[qReq.bytes.length - 1].t1 + 6];
  const zc = [qRes.bytes[0].t0 - 6, qRes.bytes[qRes.bytes.length - 1].t1 + 6];
  for (const [z, rot] of [[zb, "(b)"], [zc, "(c)"]]) {
    const x0 = sxa(z[0]), x1 = sxa(z[1]), yb0 = ya - 2, yb1 = ya + ha - 22;
    c += ret(x0, yb0, Math.max(x1 - x0, 1.5), yb1 - yb0, "#000000", { op: 0.045 });
    c += `<rect x="${x0.toFixed(1)}" y="${yb0.toFixed(1)}" width="${Math.max(x1 - x0, 1.5).toFixed(1)}" ` +
         `height="${(yb1 - yb0).toFixed(1)}" fill="none" stroke="${COR.fraco}" stroke-width="0.8" ` +
         `stroke-dasharray="4,2"/>`;
    c += txt((x0 + x1) / 2, ya + ha - 27, `${rot} below`, { fs: 11, anc: "middle", cor: COR.fraco, peso: true });
  }

  // ---------------- paineis (b) e (c): decodificacao ----------------
  function decodificado(x, y, w, q, rotulo, sub) {
    const bs = q.bytes;
    const ini = bs[0].t0 - 5, fim = bs[bs.length - 1].t1 + 5;
    const sx = d3.scaleLinear().domain([ini, fim]).range([x + 44, x + w]);
    let s = txt(x, y - 10, rotulo, { fs: 13, peso: true });

    const hTr = 20;
    [["SCK", 0], [q.linha, 1]].forEach(([k, i]) => {
      const base = y + 6 + i * hTr + hTr - 5, topo = y + 6 + i * hTr + 2;
      s += txt(x + 40, (base + topo) / 2 + 3.5, k, { fs: 11, anc: "end", cor: COR.fraco });
      const ch = D.canais[k];
      s += `<path d="${degraus(sx, base, topo, ch.nivel_inicial, ch.transicoes_us, ini, fim)}" ` +
           `fill="none" stroke="${COR.idle}" stroke-width="${k === "SCK" ? 0.45 : 1.1}"/>`;
    });

    const yc = y + 6 + 2 * hTr + 6, hc = 17;
    const papel = new Array(bs.length).fill(null);
    for (const f of q.campos) for (let i = f.de; i <= f.ate; i++) papel[i] = f.nome;
    bs.forEach((b, i) => {
      const x0 = sx(b.t0), x1 = sx(b.t1);
      const cor = papel[i] ? CAMPO[papel[i]].cor : "#b0b0b0";
      s += ret(x0, yc, x1 - x0, hc, cor, { op: papel[i] ? 0.18 : 0.10, borda: cor });
      s += txt((x0 + x1) / 2, yc + 12.5, b.hex, { fs: 11, anc: "middle", cor: papel[i] ? cor : COR.fraco });
      s += linha(x0, yc - 6, x0, yc, { cor: COR.grade, w: 0.5 });
    });
    s += txt(x + 40, yc + 12.5, "byte", { fs: 11, anc: "end", cor: COR.fraco });

    const yf = yc + hc + 4;
    // rotulo de campo estreito desce uma linha, senao DST/SRC/CMD se encostam
    let baixo = false, fimAnt = -1e9;
    for (const f of q.campos) {
      const x0 = sx(bs[f.de].t0), x1 = sx(bs[f.ate].t1), cor = CAMPO[f.nome].cor;
      const larg = CAMPO[f.nome].rot.length * 5.6;
      baixo = (x1 - x0 < larg && x0 - fimAnt < larg) ? !baixo : false;
      if (x1 - x0 < larg) fimAnt = x1;
      s += linha(x0, yf, x1, yf, { cor, w: 1.2 });
      s += linha(x0, yf, x0, yf + 4, { cor, w: 1.2 });
      s += linha(x1, yf, x1, yf + 4, { cor, w: 1.2 });
      if (baixo) s += linha((x0 + x1) / 2, yf + 4, (x0 + x1) / 2, yf + 15, { cor, w: 0.6 });
      s += txt((x0 + x1) / 2, yf + (baixo ? 26 : 15), CAMPO[f.nome].rot,
               { fs: 10.5, anc: "middle", cor, peso: true });
    }
    {
      const ult = q.campos[q.campos.length - 1].ate;
      if (ult + 1 < bs.length) s += txt(sx(bs[ult + 1].t0) + 3, yf + 15, "padding →", { fs: 10, cor: COR.fraco });
    }

    let yb = yf + 30;
    if (sub) {
      s += txt(x + 40, yb + 15, "payload", { fs: 10.5, anc: "end", cor: COR.fraco });
      for (const f of sub) {
        const x0 = sx(bs[f.de].t0), x1 = sx(bs[f.ate].t1);
        s += linha(x0, yb + 4, x1, yb + 4, { cor: CAMPO.PAYLOAD.cor, w: 0.9 });
        s += linha(x0, yb, x0, yb + 4, { cor: CAMPO.PAYLOAD.cor, w: 0.9 });
        s += linha(x1, yb, x1, yb + 4, { cor: CAMPO.PAYLOAD.cor, w: 0.9 });
        s += txt((x0 + x1) / 2, yb + 15, f.nome, { fs: 10, anc: "middle", cor: CAMPO.PAYLOAD.cor });
      }
    }
    yb += 20;   // mesmo deslocamento com e sem payload, para os dois eixos ficarem alinhados
    s += eixoX(sx, yb + 10, [Math.round(ini), Math.round(fim)], null);
    return s;
  }

  // legenda dos dois realces do painel (a): eles dizem coisas diferentes
  {
    const yl = ya + ha + 26, x0 = xa + 58;
    c += ret(x0, yl - 8, 16, 10, COR.sat, { op: 0.28 });
    c += txt(x0 + 21, yl, "firmware processing the frame", { fs: 10.5, cor: COR.fraco });
    const x2 = x0 + 200;
    c += ret(x2, yl - 8, 16, 10, "#000000", { op: 0.045 });
    c += `<rect x="${x2}" y="${yl - 8}" width="16" height="10" fill="none" stroke="${COR.fraco}" ` +
         `stroke-width="0.8" stroke-dasharray="4,2"/>`;
    c += txt(x2 + 21, yl, "time window expanded in the panel below (the rest of the 64-byte slot is padding)",
             { fs: 10.5, cor: COR.fraco });
  }

  const yd = 276;
  c += decodificado(18, yd, 262, qReq, "(b)  The request, decoded from MOSI");
  c += decodificado(320, yd, W - 338, qRes, "(c)  The reply, decoded from MISO",
    [{ nome: "state", de: 7, ate: 7 }, { nome: "volume infused", de: 8, ate: 11 },
     { nome: "rate set", de: 12, ate: 15 }, { nome: "pressure", de: 16, ate: 19 },
     { nome: "alarm", de: 20, ate: 20 }]);

  // ---------------- rodape: leitura dos dois quadros ----------------
  {
    const E = D.estado;
    const ESTADOS = ["POWER_ON", "IDLE", "RUNNING", "BOLUS", "PURGE", "PAUSED"];
    const b = qReq.bytes, bit = (b[0].t1 - b[0].t0) / 7, per = b[1].t0 - b[0].t0;
    const texto =
      `The reply decodes to state = ${ESTADOS[E.state] ?? E.state}, volume infused = ${E.volume} mL, ` +
      `rate set = ${E.rate} mL/h and no alarm, the state of the pump at that instant. ` +
      `The clock keeps its nominal ${(1000 / bit).toFixed(0)} kHz inside each byte ` +
      `(${bit.toFixed(1)} µs per bit) and stalls for ${(per - 8 * bit).toFixed(1)} µs between bytes, ` +
      `so one 64-byte frame holds the bus for ${(req.fim - req.ini).toFixed(0)} µs.`;
    const max = 158;
    const linhas = [];
    let atual = "";
    for (const palavra of texto.split(" ")) {
      if ((atual + " " + palavra).trim().length > max) { linhas.push(atual); atual = palavra; }
      else atual = (atual ? atual + " " : "") + palavra;
    }
    if (atual) linhas.push(atual);
    linhas.forEach((ln, i) => { c += txt(18, H - 32 + i * 13, ln, { fs: 10.5, cor: COR.fraco }); });
  }

  fs.writeFileSync("saida/fig_transaction.svg", svg(W, H, c));
  console.log("F3 escrita");
}

// =========================================================================
// F5 — contencao de vazao (dado do experimento E13, nao do analisador)
// =========================================================================
{
  const D = JSON.parse(fs.readFileSync("dados/f5.json"));
  const W = 720, H = 400;
  let c = "";
  const ax = { x: 74, y: 40, w: W - 74 - 150, h: 260 };
  const dom = [400, 100000];
  const sx = d3.scaleLog().domain(dom).range([ax.x, ax.x + ax.w]);
  const sy = d3.scaleLog().domain([400, 100000]).range([ax.y + ax.h, ax.y]);
  c += titulo(18, 22, "Requested versus delivered infusion rate");

  // janela terapeutica: tudo abaixo do teto
  c += ret(ax.x, sy(D.teto), ax.w, ax.y + ax.h - sy(D.teto), COR.faixa);
  c += txt(ax.x + 8, sy(D.teto) + 14, "therapeutic window", { fs: 8.5, cor: COR.fraco });

  // grade
  const marcas = [1000, 10000, 100000];
  for (const t of marcas) {
    if (t >= dom[0] && t <= dom[1]) {
      c += linha(sx(t), ax.y, sx(t), ax.y + ax.h, { cor: COR.grade, w: 0.5 });
      c += linha(ax.x, sy(t), ax.x + ax.w, sy(t), { cor: COR.grade, w: 0.5 });
    }
  }

  // referencia: o que um sistema SEM contencao faria (y = x)
  c += `<path d="M${sx(dom[0]).toFixed(1)} ${sy(dom[0]).toFixed(1)} L${sx(dom[1]).toFixed(1)} ${sy(dom[1]).toFixed(1)}" ` +
       `fill="none" stroke="${COR.fraco}" stroke-width="1" stroke-dasharray="4,3"/>`;
  c += txt(sx(30000), sy(52000), "no containment", { fs: 8.5, cor: COR.fraco, anc: "middle" });

  // teto imposto pelo dominio critico
  c += linha(ax.x, sy(D.teto), ax.x + ax.w, sy(D.teto), { cor: COR.sat, w: 1.4, tracejado: true });
  c += txt(ax.x + ax.w + 6, sy(D.teto) + 3, `ceiling`, { fs: 8.5, cor: COR.sat });
  c += txt(ax.x + ax.w + 6, sy(D.teto) + 14, `${D.teto} mL/h`, { fs: 8.5, cor: COR.sat });

  // pontos medidos
  for (const p of D.pares) {
    const contido = p.efetivo < p.pedido;
    const x = sx(p.pedido), y = sy(p.efetivo);
    if (contido) {
      c += `<path d="M${x.toFixed(1)} ${sy(p.pedido).toFixed(1)} L${x.toFixed(1)} ${(y - 7).toFixed(1)}" ` +
           `stroke="${COR.sat}" stroke-width="1" stroke-dasharray="2,2" fill="none"/>`;
      c += `<circle cx="${x.toFixed(1)}" cy="${sy(p.pedido).toFixed(1)}" r="3" fill="none" stroke="${COR.fraco}" stroke-width="1"/>`;
    }
    c += `<circle cx="${x.toFixed(1)}" cy="${y.toFixed(1)}" r="5" fill="${contido ? COR.sat : COR.idle}"/>`;
    c += txt(x, y - 12, `${p.efetivo.toFixed(0)}`, { fs: 8.5, anc: "middle", cor: contido ? COR.sat : COR.idle });
  }

  const fmt = (t) => (t >= 1000 ? `${t / 1000}k` : `${t}`);
  c += eixoX(sx, ax.y + ax.h + 2, [1000, 10000, 100000], "Requested rate (mL/h, log scale)", { fmt });
  c += eixoY(sy, ax.x, [1000, 10000, 100000], "Delivered rate (mL/h, log scale)", { fmt });

  // legenda fora da area de dados
  let ly = ax.y + 40;
  c += txt(ax.x + ax.w + 40, ly - 18, "Measured", { fs: 9, peso: true });
  c += `<circle cx="${ax.x + ax.w + 48}" cy="${ly}" r="5" fill="${COR.idle}"/>`;
  c += txt(ax.x + ax.w + 60, ly + 3, "applied as", { fs: 8.5 });
  c += txt(ax.x + ax.w + 60, ly + 14, "requested", { fs: 8.5 });
  c += `<circle cx="${ax.x + ax.w + 48}" cy="${ly + 40}" r="5" fill="${COR.sat}"/>`;
  c += txt(ax.x + ax.w + 60, ly + 43, "limited by the", { fs: 8.5 });
  c += txt(ax.x + ax.w + 60, ly + 54, "control domain", { fs: 8.5 });
  c += `<circle cx="${ax.x + ax.w + 48}" cy="${ly + 80}" r="3" fill="none" stroke="${COR.fraco}"/>`;
  c += txt(ax.x + ax.w + 60, ly + 83, "what was", { fs: 8.5, cor: COR.fraco });
  c += txt(ax.x + ax.w + 60, ly + 94, "requested", { fs: 8.5, cor: COR.fraco });

  c += txt(18, H - 34, "Requests of 5 000 and 65 000 mL/h, issued from the position of a compromised", { fs: 9, cor: COR.fraco });
  c += txt(18, H - 22, "connectivity domain, were delivered as 1 200 mL/h: the ceiling holds regardless of", { fs: 9, cor: COR.fraco });
  c += txt(18, H - 10, "how large the request is, and the previously configured rate is preserved.", { fs: 9, cor: COR.fraco });

  fs.writeFileSync("saida/fig_containment.svg", svg(W, H, c));
  console.log("F5 escrita");
}
