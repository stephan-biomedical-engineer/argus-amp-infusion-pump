# Bancada 01 — passo a passo de execução

Versão de execução do [roteiro](bancada_01_modelo_de_ameaca.md). Os comandos
estão na ordem em que rodam, já com os endereços desta bancada. Para
interpretar um resultado, siga o link "→ roteiro" de cada etapa.

| | |
|---|---|
| Raspberry Pi 3B+ | `root@192.168.1.9` |
| Host (este PC) | `192.168.1.17` |
| Diretório de trabalho | `~/argus-bancada-ameaca` (fora dos repositórios) |

**Terminais.** Quase tudo roda no **terminal H1** do host: a função `pi`
executa na RPi por `ssh`, já com a biblioteca da campanha carregada. O
**terminal H2** só é usado no T6 (servidor `iperf3`) e no T8 (observadores).

**Registro.** Cada `pi "reg ..."` acrescenta uma linha a `ameacas.csv` na RPi.
Troque os `<...>` pelo valor medido antes de rodar.

---

## Já feito (21/10/2026)

- [x] Firmware legítimo **v1.1.23 (commit `5dd34d1`)** gravado por OTA na Nucleo
- [x] `argus-bench`, `argus-attack` e `argus-versao` do fonte atual em `/data/bancada01/bin`
- [x] Imagem legítima em `/data/ota/legitimo_v1.1.23_5dd34d1.signed.bin` (para restaurar)
- [x] Duração de um OTA medida: **45 s**
- [x] Etapa 1: `moreutils` e `iperf3` instalados; console da Nucleo em `/dev/ttyACM0`
- [x] As duas imagens 1.1.23 (12/09 e 21/10) têm o mesmo código; só a assinatura RSA-PSS difere

---

## Etapa 1 — Pré-requisitos no host · 10 min

```bash
sudo apt install moreutils iperf3
```

Ligue o analisador lógico por USB e confira que ele aparece (hoje só aparece o
dispositivo `demo`):

```bash
sigrok-cli --scan
```

Tem de listar uma linha com `fx2lafw`. Sem ela, nenhuma captura funciona.

**Opcional, mas muda a qualidade da evidência:** ligue o USB da Nucleo neste
host. Com ele, você tem o console (que mostra `FALHA CRITICA`, `REJEITADO` e
`Motor ON/OFF`) e o `west flash` para restaurar. Sem ele, o juiz é o
`argus-versao` e o analisador. Se ligar, deixe um terminal só para o console:

```bash
mkdir -p ~/argus-bancada-ameaca/vcp
stty -F /dev/ttyACM0 115200 raw -echo
ts '%Y-%m-%dT%H:%M:%.S' < /dev/ttyACM0 | tee ~/argus-bancada-ameaca/vcp/console.log
```

## Etapa 2 — Testes unitários · 5 min → roteiro 0.5

```bash
cd ~/Documents/Argus_Artigo/Argus_MCPS_Blackpill_STM32F411_ZephyrRTOS
export ZEPHYR_BASE=~/zephyrproject/zephyr && source ~/zephyrproject/.venv/bin/activate
west twister -T test/bancada01 -p native_sim/native/64
python3 -m pytest test/bancada01_estatico -q
```

```bash
cd ~/Documents/Argus_Artigo/Argus_MCPS_RPi-Zero2W_YoctoProject
cmake -S test -B test/build && cmake --build test/build -j && ctest --test-dir test/build
python3 -m pytest test/config -q
```

Esperado: 26/26 · 14 + 1 xfail · 18/18 · 34/34. Se algo falhar, **pare**: o
código mudou e o roteiro pode estar desatualizado.

## Etapa 3 — Ligações · 20 min → roteiro 2.2 e 2.3

1. SPI e READY entre RPi e Nucleo, conforme a tabela 2.2 (CE1 = pino **26**,
   READY = pino **29** ↔ **PB3**).
2. **Fio de reset (GPIO 4 → NRST) desligado.** Ele só entra na Etapa 8.
3. Analisador: D0 = PA0 (STEP), D1 = PA8, D2 = PE0, D3 = PB3, D4 = PA4,
   D5 = NRST, D6 = PB14 (opcional), GND comum.
4. Motor desligado ou fuso livre. **Sem seringa.**

## Etapa 4 — Preparar a RPi · 10 min → roteiro 3.3

Envie o `run_campaign.sh` atual (o da imagem é antigo):

```bash
scp ~/Documents/Argus_Artigo/Argus_MCPS_RPi-Zero2W_YoctoProject/meta-infusion-pump/recipes-apps/infusion-control/files/src/tools/run_campaign.sh root@192.168.1.9:/data/bancada01/bin/
ssh root@192.168.1.9 'chmod +x /data/bancada01/bin/run_campaign.sh'
```

Crie a biblioteca da campanha. Abra `ssh root@192.168.1.9` e **cole o bloco
inteiro da seção 3.3 do roteiro**, de `systemctl stop infusion-pump` até
`} > "$OUT/environment.txt"`. Rode só uma vez: ele cria o `ameacas.csv`. Depois:

```sh
cat $OUT/environment.txt | head -n 12      # confira mcu_firmware e ferramentas
exit
```

## Etapa 5 — Atalhos no terminal H1 · 2 min → roteiro 3.4

Rode isto em todo terminal novo do host:

```bash
export PI=root@192.168.1.9
export B=~/argus-bancada-ameaca
mkdir -p $B/la $B/t8 $B/imagens $B/chaves
pi()  { ssh -o ConnectTimeout=8 "$PI" ". /data/ameaca_lib.sh; $*"; }
cap() { sigrok-cli -d fx2lafw --config samplerate="$2" --samples "$3" \
                   -C D0,D1,D2,D3,D4,D5,D6 -o "$B/la/$1.sr"; }
estado() { pi 'argus-versao 2>/dev/null'; }   # versao e estado pelo barramento
```

Confira:

```bash
estado          # versao=1.1.23  estado=1 (IDLE) vazao=0
```

> **Sem console, como saber se o MCU reiniciou:** com a bomba infundindo,
> `estado` deve dizer `2 (RUNNING)`. Se disser `1 (IDLE)` sem que você tenha
> abortado, o MCU reiniciou. No analisador, o STEP (D0) para e o NRST (D5)
> mostra um pulso.

## Etapa 6 — Taxa de erro de fundo · 5 min → roteiro 3.5

```bash
pi 'systemctl stop infusion-pump; zerar_metricas; argus-bench -n 5000 --cmd mix --label ref_erros -o $OUT/R0_erros_rep1.csv --metrics $OUT/R0_erros_rep1_mcu.csv'
pi 'grep -E "crc_errors|driver_errors|spi_resets" $OUT/R0_erros_rep1_mcu.csv'
```

Erros de driver **isolados** são esperados: no console aparecem como
`err=32` → `Erro SPI Driver: -5` → `SPI Recuperado!`. É a corrida conhecida
do DATA READY (roteiro 3.5). Refaça a fiação só se `crc_errors > 0`, se
aparecerem dois `Erro SPI Driver` seguidos sem `SPI Recuperado` entre eles, ou
se `driver_errors` passar de ~50 nas 5.000 transações.

```bash
pi "reg R0 fundo 1 driver_errors <n>"
pi "reg R0 fundo 1 crc_errors <n>"
```

---

## Etapa 7 — Bloco A: protocolo · 2 h → roteiro 4

```bash
pi 'systemctl stop infusion-pump'
```

**T1a — dose excessiva**

```bash
pi 'infundir 600'
cap T1a_rep01 4m 60M &  sleep 1;  pi 'argus-attack --only E13 -o $OUT/T1a_E13_rep01.csv';  wait
pi 'pausar'
for r in $(seq -w 2 25); do pi "infundir 600; argus-attack --only E13 -o \$OUT/T1a_E13_rep$r.csv >/dev/null; pausar"; done
```

Abra `$B/la/T1a_rep01.sr` no PulseView. O STEP **nunca** pode passar de
**1.697 Hz**.

```bash
pi "reg T1 E13 25 pedidos_total 100"
pi "reg T1 E13 25 step_max_hz <Hz>"
```

> **T1b (ordem invertida)** já está estabelecido pelos testes unitários: o
> firmware recusa e mantém a vazão anterior. Na bancada é opcional e exige o
> patch do Apêndice A do roteiro.

**T2 — replay**

```bash
pi 'infundir 600'
cap T2_rep01 4m 60M &  sleep 1;  pi 'argus-attack --only E7 -o $OUT/T2_E7_rep01.csv';  wait
pi 'pausar'
for r in $(seq -w 2 10); do pi "infundir 600; argus-attack --only E7 -o \$OUT/T2_E7_rep$r.csv >/dev/null; pausar"; done
pi 'grep -h "^E7,aceitos" $OUT/T2_E7_rep*.csv'
```

Na captura, o STEP cai de **848 Hz** para **70 Hz**.

```bash
pi "reg T2 E7 10 reenvios 200"
pi "reg T2 E7 10 aceitos <soma>"
pi "reg T2 E7 1 step_depois_hz <Hz>"
```

**T3 — aborto forjado**

```bash
pi 'infundir 600'
cap T3_rep01 4m 60M &  sleep 1;  pi 'argus-attack --only E8 -o $OUT/T3_E8_rep01.csv';  wait
for r in $(seq -w 2 30); do pi "infundir 600; argus-attack --only E8 -o \$OUT/T3_E8_rep$r.csv >/dev/null"; done
pi 'grep -h "^E8,aceito" $OUT/T3_E8_rep*.csv | grep -c ",1$"'
```

Na captura: intervalo da subida de D1 até o último pulso de D0.

```bash
pi "reg T3 E8 30 aceitos <n>"
pi "reg T3 E8 1 decisao_ate_parada_us <us>"
```

**T4a — truncamento**

```bash
pi 'zerar_metricas'
for r in $(seq -w 1 20); do pi "argus-attack --only E9 -o \$OUT/T4a_E9_rep$r.csv >/dev/null"; done
pi 'grep -hE "^E9,(recuperados|tempo)" $OUT/T4a_E9_rep*.csv'
```

```bash
pi "reg T4a E9 20 recuperados <soma>"
pi "reg T4a E9 20 recuperacao_media_us <media>"
```

**T4b — rajada de CRC inválido**

```bash
pi 'infundir 600'
cap T4b 200k 24M &  sleep 1
for N in 5 10 11 20 100 1000; do pi "argus-bench --fault-every 1 -n $N --cmd status -o \$OUT/T4b_N$N.csv 2>>\$OUT/T4b.log"; sleep 3; done
wait;  estado
```

Esperado: `estado=2 (RUNNING)` e STEP contínuo nos 120 s — **sem reinício**.

```bash
pi "reg T4b rajada 1 reinicios <0|n>"
pi "reg T4b rajada 1 step_continuo <1|0>"
pi 'pausar'
```

**T4c — relógio de 2 MHz**

```bash
pi 'zerar_metricas; infundir 600'
cap T4c 200k 12M &  sleep 1
pi 'argus-bench --spi-hz 2000000 -n 200 --cmd status -o $OUT/T4c.csv 2>>$OUT/T4c.log'
wait;  estado
```

`RUNNING` = resistiu; `IDLE` = **reiniciou e a terapia não voltou**.

```bash
pi "reg T4c relogio 1 reinicio <0|1>"
pi "reg T4c relogio 1 terapia_retomou <0|1>"
pi 'pausar'
```

---

## Etapa 8 — Bloco B: perda do gateway e fio de reset · 1 h 30 → roteiro 5

**T5a — processo do gateway morre**

```bash
cap T5a 200k 24M &  sleep 1;  pi 'infundir 600';  wait
pi 'pausar'
pi "reg T5a morte_processo 1 step_continuo_s <s>"
```

**T5b — RPi sem energia, sem fio de reset**

```bash
pi 'infundir 600'
cap T5b 200k 24M &  sleep 20;  echo ">>> DESLIGUE A FONTE DA RPi AGORA";  wait
```

Puxe a fonte no aviso. Depois religue, espere ~1 min, e:

```bash
pi 'systemctl stop infusion-pump'; estado
pi "reg T5b perda_energia_sem_nrst 1 step_continuo <1|0>"
pi 'pausar'
```

**Ligue agora o fio GPIO 4 (pino 7) → 1 kΩ → NRST.** Confirme que a Nucleo
não ficou presa em reset:

```bash
estado          # tem de responder
```

**T9 — reset em laço**

```bash
pi 'infundir 600'
cap T9 200k 24M &  sleep 1
pi 'argus-attack --only E14 --with-reset --reset-cycles 20 -o $OUT/T9_E14.csv'
wait;  estado
```

Se o STEP ficar mudo depois do **primeiro** pulso em D5, a terapia não
retomou.

```bash
pi 'grep indisponibilidade $OUT/T9_E14.csv'
pi "reg T9 E14 20 indisponibilidade_media_ms <ms>"
pi "reg T9 E14 20 terapia_retomou_apos_reset <0|1>"
```

**T5c — RPi sem energia, com fio de reset**

```bash
pi 'infundir 600'
cap T5c 200k 24M &  sleep 20;  echo ">>> DESLIGUE A FONTE DA RPi AGORA";  wait
```

Olhe D5: o NRST fica preso em nível baixo? Religue a RPi e espere o boot.

```bash
pi 'systemctl stop infusion-pump'; estado
pi "reg T5c perda_energia_com_nrst 1 nrst_preso_baixo <1|0>"
pi "reg T5c perda_energia_com_nrst 1 step_continuo <1|0>"
```

**Desligue o fio de reset.**

**T6 — referência de latência**

No terminal **H2**:

```bash
iperf3 -s
```

No H1:

```bash
pi 'RUN_ATTACKS=0 IPERF_SERVER=192.168.1.17 OUTDIR=$OUT/T6 run_campaign.sh -s baseline,combined -r 3 -n 2000'
DEST=$(pi 'echo $OUT');  scp -r "$PI:$DEST/T6" $B/
python3 - $B/T6 <<'PY'
import csv, glob, statistics, sys
for c in ("baseline", "combined"):
    v = [float(r["latency_us"]) for f in sorted(glob.glob(f"{sys.argv[1]}/{c}_rep*.csv"))
         if not f.endswith("_mcu.csv") for r in csv.DictReader(open(f))]
    print(f"{c}: n={len(v)} mediana={statistics.median(v):.1f} us")
PY
```

Critério: baseline ≈ **2.837 µs**, combined ≈ **2.836 µs** (±1 %). Encerre o
`iperf3` no H2.

```bash
pi "reg T6 referencia 3 mediana_baseline_us <us>"
pi "reg T6 referencia 3 mediana_combined_us <us>"
```

---

## Etapa 9 — Bloco C: atualização · 2 h → roteiro 6

**9.1 Gerar as imagens** (no host; pode ser feito antes). Rode o bloco
**6.1 do roteiro inteiro**, depois de preparar o ambiente:

```bash
export ZEPHYR_BASE=~/zephyrproject/zephyr && source ~/zephyrproject/.venv/bin/activate
```

No fim, confira as quatro imagens. O último comando do 6.1 já as envia; se
não rodou, envie assim:

```bash
ls -la $B/imagens/{teste_v99,estranha_v98,adulterada_v66,corrompida_v99}.signed.bin
scp $B/imagens/{teste_v99,estranha_v98,adulterada_v66,corrompida_v99}.signed.bin $PI:/data/ota/
```

**9.2 Captura dimensionada para o OTA** (45 s medidos + 40 s de folga):

```bash
AMOSTRAS_OTA=8500000
pi 'systemctl stop infusion-pump'
```

**T7b — imagem corrompida** (esperado: rejeitada)

```bash
cap T7b 100k $AMOSTRAS_OTA &  sleep 1
pi 'stm32-updater /data/ota/corrompida_v99.signed.bin 2>&1 | tee $OUT/T7b_updater.log';  wait
pi 'sleep 10'; estado                       # esperado versao=1.1.23
pi "reg T7b ota_corrompida 1 versao_apos_boot <versao>"
```

**T7c — chave estranha** (esperado: rejeitada)

```bash
cap T7c 100k $AMOSTRAS_OTA &  sleep 1
pi 'stm32-updater /data/ota/estranha_v98.signed.bin 2>&1 | tee $OUT/T7c_updater.log';  wait
pi 'sleep 10'; estado                       # esperado versao=1.1.23
pi "reg T7c ota_chave_estranha 1 versao_apos_boot <versao>"
```

**T7d — firmware adulterado com a chave pública do MCUboot** (o experimento
mais importante)

```bash
cap T7d_ota 100k $AMOSTRAS_OTA &  sleep 1
pi 'stm32-updater /data/ota/adulterada_v66.signed.bin 2>&1 | tee $OUT/T7d_updater.log';  wait
pi 'sleep 10'; estado                       # H1 confirmada se versao=1.1.66
```

Se for `1.1.66`, prove a consequência na dose:

```bash
pi 'infundir 600'
cap T7d_dose 4m 60M &  sleep 1;  pi 'argus-attack --only E13 -o $OUT/T7d_E13.csv';  wait
pi 'pausar'
pi 'grep efetivo $OUT/T7d_E13.csv'          # 5000 e 65000 aceitos?
pi "reg T7d ota_chave_publica 1 versao_apos_boot <versao>"
pi "reg T7d ota_chave_publica 1 step_max_hz <Hz>"
```

**Restaure a imagem legítima antes de seguir:**

```bash
pi 'stm32-updater /data/ota/legitimo_v1.1.23_5dd34d1.signed.bin && sleep 10'; estado   # versao=1.1.23
```

**T7e — OTA legítimo durante a infusão**

```bash
pi 'infundir 600'
cap T7e 100k $AMOSTRAS_OTA &  sleep 1
pi 'stm32-updater /data/ota/teste_v99.signed.bin 2>&1 | tee $OUT/T7e_updater.log';  wait
pi 'sleep 10'; estado                       # H2: versao=1.1.99 e estado=1 (IDLE)
```

No analisador: o STEP continua durante os 45 s de transferência e para ~2 s
depois do fim?

```bash
pi "reg T7e ota_durante_infusao 1 aceita_durante_running <1|0>"
pi "reg T7e ota_durante_infusao 1 terapia_retomou <1|0>"
pi "reg T7e ota_durante_infusao 1 tempo_ate_parada_s <s>"
pi 'stm32-updater /data/ota/legitimo_v1.1.23_5dd34d1.signed.bin && sleep 10'; estado   # volta a 1.1.23
```

**T7a — bundle RAUC adulterado**

```bash
BUNDLE=~/Documents/Argus_Artigo/Argus_MCPS_RPi-Zero2W_YoctoProject/build/tmp/deploy/images/raspberrypi3/infusion-bundle-raspberrypi3.raucb
cp $BUNDLE $B/imagens/bundle_legitimo.raucb && cp $BUNDLE $B/imagens/bundle_adulterado.raucb
TAM=$(stat -c %s $B/imagens/bundle_adulterado.raucb)
printf '\xff' | dd of=$B/imagens/bundle_adulterado.raucb bs=1 seek=$((TAM/3)) conv=notrunc 2>/dev/null
scp $B/imagens/bundle_legitimo.raucb $B/imagens/bundle_adulterado.raucb $PI:/data/ota/
pi 'rauc info /data/ota/bundle_legitimo.raucb 2>&1 | tee $OUT/T7a_info_legitimo.log | tail -n 3'
pi 'rauc info /data/ota/bundle_adulterado.raucb 2>&1 | tee $OUT/T7a_info_adulterado.log | tail -n 3'
pi 'rauc install /data/ota/bundle_adulterado.raucb 2>&1 | tee $OUT/T7a_install.log | tail -n 3'
```

**Nunca** rode `rauc install` no bundle legítimo.

```bash
pi "reg T7a rauc_adulterado 1 rejeitado <1|0>"
```

---

## Etapa 10 — Bloco D: certificados · 1 h 30 → roteiro 7

```bash
pi 'systemctl stop infusion-pump; systemctl is-active mosquitto'
echo "192.168.1.9 argus-pump.local" | sudo tee -a /etc/hosts
```

1. Rode o bloco **7.1** (variáveis `Y`, `CA_CRT`, `CA_KEY`, `BK_CRT`, `BK_KEY`
   e `cd $B/t8`) e a **pré-verificação**. Tem de dar `Verify return code: 0 (ok)`.
2. Rode o bloco **7.2** inteiro (mini-CA e certificados C2–C5).
3. No terminal **H2**, depois de rodar as definições das Etapas 5 e 10.1 nele,
   suba os dois observadores do **7.3**.
4. No H1, defina a função `tenta` do **7.3** e rode os casos **nesta ordem**:

```bash
tenta C0 bomba/comando 100 --cert $BK_CRT --key $BK_KEY    # tem de dar 100 aceitas
tenta C1 bomba/comando 100
tenta C2 bomba/comando 100 --cert c2.crt --key c2.key
tenta C3 bomba/comando 100 --cert c3.crt --key c3.key
tenta C4 bomba/comando 100 --cert c4.crt --key c4.key
tenta C5 bomba/comando 100 --cert c5.crt --key c5.key
tenta C6 bomba/status  100 --cert $BK_CRT --key $BK_KEY
```

Se o **C0** não der 100 aceitas, pare: os outros casos ficam sem leitura.

5. Conte o que chegou aos observadores e guarde o log do broker (fim do 7.3).
6. Latência com e sem ataque (**7.4**): `pi 'argus-bench -n 20000 --interval-us 2000 --cmd mix --label T8_ref -o $OUT/T8_latencia_ref.csv'`,
   depois o martelo no H1 e a mesma medição com `--label T8_sob_ataque -o $OUT/T8_latencia_ataque.csv`.
7. Registre os `reg T8 ...` do 7.4 e copie `resultado.csv` e `obs_*.log` para a campanha.

---

## Etapa 11 — Restauração · 15 min → roteiro 8

```bash
estado                                      # tem de ser versao=1.1.23
pi 'rauc status | head -n 15'
pi 'cd /data/ota && rm -f teste_v99.signed.bin corrompida_v99.signed.bin estranha_v98.signed.bin adulterada_v66.signed.bin bundle_adulterado.raucb'
pi 'systemctl start infusion-pump; systemctl is-active infusion-pump'
rm -rf $B/arvores/v66                       # o firmware adulterado
sudo sed -i '/argus-pump.local/d' /etc/hosts
```

- [ ] Fio de reset desligado
- [ ] `versao=1.1.23`
- [ ] `infusion-pump` ativo
- [ ] A imagem legítima continua em `/data/ota/`

## Etapa 12 — Consolidação · 30 min → roteiro 9

```bash
DEST=$(pi 'echo $OUT')
scp -r "$PI:$DEST" ~/Documents/Argus_Artigo/resultados/
cp -r $B/la ~/Documents/Argus_Artigo/resultados/$(basename $DEST)/capturas
find ~/Documents/Argus_Artigo/resultados/$(basename $DEST) \( -name '*.key' -o -name '*.pem' \) -print   # tem de sair vazio
```

Preencha a tabela de rastreamento da seção **9.2** do roteiro a partir de
`ameacas.csv` e das capturas, e use a **9.3** para levar cada resultado ao
artigo.

---

## Se o dia for curto

| Prioridade | Etapas | Por quê |
|---|---|---|
| 1 | 1 → 6 e **9 (T7d)** | é o único resultado que pode mudar uma afirmação central do artigo |
| 2 | 7 (Bloco A) | dá a física (STEP) aos números já publicados |
| 3 | 8 (Bloco B) | autonomia e o fio de reset |
| 4 | 10 (Bloco D) | completa o item 3 do IEEE (falha de certificado) |

Cada bloco pode ser feito num dia diferente: basta refazer a Etapa 5 no
terminal novo. A biblioteca da RPi (Etapa 4) é criada uma única vez.
