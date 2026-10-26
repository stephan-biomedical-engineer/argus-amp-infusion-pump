# Item 3 — gateway compromise, malformed messages, update failure · 25/10/2026

Firmware legítimo **v1.1.23** (restaurado ao final). Mapa de canais: D0 NRST,
D1 NSS, D2 READY, D4 STEP, D5 marcador A, D6 marcador B. Sem atuador — só sinal.
STEP: 600 mL/h = 847,5 Hz; teto legítimo 1.200 mL/h = 1.697 Hz.

## Gateway compromise (T1–T3) + Malformed messages (T4a–T4c)

| Exp | Ataque | Observado (analisador + CSV) | Defesa |
|---|---|---|---|
| T1 (E13) | dose excessiva comandada | STEP vai a 847→1696 Hz; 5.000 e 65.000 mL/h **contidos em 1.200** (máx 1696,5 Hz, nunca passa do teto) | **segura** (F3) |
| T2 (E7) | replay de `SET_CONFIG` 50 mL/h | 20/20 reenvios aceitos; STEP cai **847→70 Hz** | falha — sem anti-replay |
| T3 (E8) | aborto forjado (CRC válido) | aceito; STEP **para em 1,35 s** | falha — CRC não autentica origem |
| T4a (E9) | quadro truncado | 10/10 recuperados (~3,4 ms), 0 deadline miss | segura |
| T4b | rajada de CRC inválido (5…1000) | STEP **contínuo 51 s, 0 reset**; infusão segue a 600 | segura |
| T4c | relógio SPI a 2 MHz | **15 resets** (NRST), STEP para, MCU fica **IDLE** (não retoma) | falha — DoS por reset em loop |

As falhas são por desenho do protocolo (CRC-16 detecta erro, não autentica; sem
nonce; sem proteção do relógio) e vão para riscos residuais. Nenhuma viola o teto
de dose (F3), **exceto via OTA** — ver abaixo.

Capturas: `capturas_protocolo/`. CSVs: `csv_protocolo/`.

## Update failure (T7a–T7e)

OTA leva **45 s** (imagem de 82 KB a 100 kHz). Veredicto pela versão no banner
após o reboot (as imagens de teste têm versões distintas de propósito).

| Exp | Imagem | Esperado | Versão após boot | Veredicto |
|---|---|---|---|---|
| T7a | bundle RAUC adulterado (gateway) | rejeitar | — | install falha (dm-verity), sem troca de slot ✓ |
| T7b | corrompida (1 byte) | rejeitar | **1.1.23** | MCUboot rejeita (hash) ✓ |
| T7c | assinada com chave estranha | rejeitar | **1.1.23** | MCUboot rejeita (assinatura) ✓ |
| **T7d** | **adulterada, teto 65.000, chave do MCUboot** | **?** | **1.1.66** | **ACEITA — H1 confirmada** |
| T7e | legítima (v99) durante infusão | aceita, terapia? | **1.1.99** | aceita; terapia **não retoma** — H2 confirmada |

### T7d — o achado central

O firmware adulterado é assinado com `root-rsa-2048.pem`, a **chave de
desenvolvimento do MCUboot, pública no repositório**. O MCUboot da placa a
aceita: boot em **v1.1.66**. Com ela rodando, o E13 mostrou a consequência na
dose:

```
pedido 5.000  mL/h -> efetivo 5.000  mL/h  [ACEITO]   STEP ~7.079 Hz
pedido 65.000 mL/h -> efetivo 65.000 mL/h  [ACEITO]   STEP ~100.502 Hz
```

Contra o teto legítimo de 1.697 Hz, o STEP chegou a **~100 kHz (≈ 59× o teto)**.
Um gateway comprometido, de posse da chave pública do repositório, **reescreve o
teto de dose e comanda superdose arbitrária**. A afirmação do artigo *"F3 holds
independently of F1 and F2"* **não vale para o canal de OTA**.

Mitigação para o artigo: chave de assinatura própria e privada, fora do gateway
e fora do repositório — o gateway transporta imagens mas não consegue produzi-las.

### T7e — atualização durante a terapia

A infusão continuou pelos ~48 s da transferência, então o MCU reiniciou e voltou
em **IDLE**: o STEP parou e **não retomou** (capturado: último pulso em 48,28 s,
reset, 2,68 s parado até o fim). Atualizar durante a terapia a interrompe, sem
retomada automática.

**Firmware legítimo v1.1.23 restaurado e verificado ao final** (E13 pós-restauro:
5.000/65.000 novamente contidos em 1.200). Capturas: `capturas_ota/`.

## Pendente

- **Network loss**: T5a (morte do processo) e T6 (exaustão de recursos) são
  autônomos — a rodar. T5b/T5c (perda de energia) e T9 (reset por NRST) exigem
  ação física (puxar a fonte, ligar o fio de reset).
- **Certificate failure**: C3/C4/C5 (expirado/CN sem ACL/revogado) exigem a chave
  da CA do broker, ausente do repositório. C0/C1/C2/C6 já feitos.

## Network loss — parte autônoma (T5a, T6)

| Exp | Observado | Conclusão |
|---|---|---|
| T5a (morte do processo) | processo morto com `kill -9`; MCU segue **RUNNING@600**, STEP contínuo 28 s, 0 reset | terapia autônoma à perda do processo do gateway |
| T6 (exaustão de recursos) | baseline p50 **2830 µs** (publicado 2837); combined p99 **15981 µs** vs baseline 9254 | montagem reproduz o baseline; a cauda **dobra** sob carga real |

**Correção da campanha publicada:** no `combined` publicado o p99 (~9268 µs) mal
diferia do baseline porque o `stress-ng` abortava (raiz read-only, sem temp
gravável) — a carga nunca era aplicada. Com a carga real (temp-path corrigido +
rede empurrada pelo host), o p99 de transação dobra. É a latência **gateway↔MCU**,
não o laço crítico (bancada 02: 0 deadline miss) — o isolamento segura onde importa.

T5b/T5c (perda de energia) e T9 (reset por NRST) ficam pendentes: exigem ação
física (puxar a fonte da RPi; ligar o fio GPIO 4 → 1 kΩ → NRST).
