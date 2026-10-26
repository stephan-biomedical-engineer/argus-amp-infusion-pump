# Bancada 01 — item 3 do IEEE, parte gateway · 25/10/2026

Item 3: *"Test gateway compromise, malformed messages, network loss, update
failure, and certificate failure systematically."*

Estado do enlace MCU↔gateway neste dia: **retorno do MCU mudo** (gateway lê só
zeros, 100 % de falhas; o MCU decodifica e dirige o READY, mas as respostas não
chegam). Logo, só as partes **gateway-only** do item 3 rodaram. As demais
(comandos forjados, mensagens malformadas, perda de rede observada no STEP,
veredictos de OTA no MCU) ficam para quando o enlace voltar.

## Certificate failure — broker MQTT (mTLS) — **rodado**

Broker `mosquitto` na RPi: listener TLS 8883, `require_certificate true`, sem
`crlfile`. Observador legítimo (cert `backend-service`) assinando
`bomba/comando` e `bomba/status`; publicações por IP com `--insecure` (só
desliga a checagem de nome do servidor; a cadeia e o cert de cliente seguem
validados).

| Caso | Cert do cliente | Alvo | Esperado | Observado |
|---|---|---|---|---|
| C0 | `backend-service` (legítimo) | `bomba/comando` | aceito, entregue | rc=0, **entregue** |
| C1 | nenhum | `bomba/comando` | mTLS recusa | rc=7, conexão perdida |
| C2 | CN válido, **CA estranha** | `bomba/comando` | broker rejeita | rc=7, conexão perdida |
| C6 | `backend-service` | `bomba/status` (escrita) | ACL nega escrita | rc=0 mas **não entregue** |

Cobre: mTLS obrigatório (C1), emissor não confiável (C2), credencial legítima
(C0) e restrição de escrita da ACL — o backend não pode forjar `bomba/status`
(C6).

### Correções de procedência (o roteiro estava errado)

1. **A CA do broker é `backend/certs/ca.crt`**, não a
   `meta-infusion-pump/.../certs/ca.crt` que o roteiro (7.1) apontava. O cert
   do servidor em execução (`CN=argus-pump.local`, emissor
   `Argus-Root-CA / Argus-Medical`, fingerprint SHA1 25:F7:C8…) só verifica
   contra `backend/certs/ca.crt` (código 0); contra a do roteiro dá erro 7.
2. **A chave privada dessa CA não está no repositório.** A única `ca.key`
   presente (`backend_certs/ca.key`) pertence à outra CA, a que o broker **não**
   usa. Sem a chave da CA do broker não dá para emitir os certs que exigem
   assinatura por ela:
   - **C3** (expirado), **C4** (CN sem permissão na ACL), **C5** (revogado).

   Esses três casos ficam pendentes. C4 (autorização negada para cert válido)
   é parcialmente coberto por C6, que mostra a ACL barrando uma operação não
   permitida de um cert válido. C5 (revogado) seria o caso interessante porque
   o broker não tem `crlfile` — continua pendente.

Arquivos: `cert_observador.log` (o que chegou ao observador).

## Update failure — bundle RAUC (lado gateway) — **rodado**

| Passo | Resultado |
|---|---|
| `rauc info` bundle legítimo | assinatura verificada (`CN=Argus-RAUC-Dev`), `Compatible: infusion-pump-rpi` |
| `rauc install` bundle adulterado (1 byte invertido no payload, offset ~1/3) | **falha em 99 %**: bloco corrompido falha o hash dm-verity → erro de I/O; slot não atualizado |
| `rauc status` após | `booted`/`activated` seguem em `rootfs.0 (A)` — **sem troca de slot** |

Nuance: a adulteração do payload é pega no install pelo dm-verity (erro de
leitura do bloco), não na assinatura. Adulterar o manifesto seria pego antes,
na verificação de assinatura do `rauc info`. Em qualquer caso o bundle
adulterado **não instala**. O bundle legítimo **não** foi instalado (só
inspecionado). `rauc_status_apos.log` confirma o slot intacto.

## Pendente (exige o enlace MCU)

| Sub-item | Experimentos |
|---|---|
| Gateway compromise | T1–T3 (comandos forjados), T7d (firmware adulterado no MCU) |
| Malformed messages | T4a/b/c |
| Network loss | T5a/b/c (observa STEP, exige infusão via SPI) |
| Update failure (MCU) | T7b–T7e (veredicto de boot do MCUboot) |
| Certificate failure | C3, C4, C5 — além do enlace, exigem a chave da CA do broker |
