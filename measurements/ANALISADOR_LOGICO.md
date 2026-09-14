# Medições com analisador lógico — Argus MCPS

Registro completo das medições feitas com analisador lógico em 12/09/2026.
Complementa o [RESULTS.md](RESULTS.md) (campanhas de software) e o
[REPORT.md](REPORT.md) (histórico do trabalho).

O motivo de existir: a instrumentação de software do próprio sistema já
produziu números plausíveis e errados quatro vezes (REPORT.md, Parte IV).
Aqui o instrumento é **externo e independente** do software medido.

---

## 1 — Montagem

| Item | Valor |
|---|---|
| Instrumento | Saleae Logic (FX2), `0925:3881`, via sigrok / PulseView |
| Taxa | 4 MHz → **resolução de 0,25 µs** |
| Canais | 8 |
| Domínio crítico | Cortex-M33 a 160 MHz, Zephyr, firmware v1.1.23 |
| Barramento | SPI escravo, 500 kHz, quadros de 64 bytes, CRC-16 |

| Canal | Sinal | Pino |
|---|---|---|
| CH0 | MOSI | PA7 |
| CH1 | MISO | PA6 |
| CH2 | SCK | PA5 |
| CH3 | NSS | PA4 |
| CH4 | READY | PB3 |
| CH5 | Pulso de atuação | PA0 (TIM2_CH1) |
| CH6 | **Marcador A** | PA8 (Zio D33, CN10-31) |
| CH7 | **Marcador B** | PE0 (Zio D34, CN10-33) |

Os marcadores foram acrescentados ao firmware para este trabalho
(`include/markers.h`):

- **Marcador A** sobe quando um quadro é **decodificado com sucesso**;
  desce quando o hub rearma o SPI para o quadro seguinte.
- **Marcador B** fica alto enquanto `update_motor_hardware()` executa.

A grandeza de interesse é **subida de A até subida de B**: o caminho do
quadro recebido até o motor atualizado, medido em hardware. Como o
protocolo é síncrono, há exatamente um A antes de cada B.

Em placas cujo Device Tree não define os marcadores, o código se compila
fora por inteiro.

---

## 2 — Como os dados foram extraídos

Cada `.sr` do sigrok é um ZIP com blocos binários: **uma amostra por byte**,
um bit por canal. A análise lê os blocos na ordem numérica, concatena, e
trabalha direto com numpy — sem exportar CSV, que para 60 M amostras seria
inviável.

```python
z = zipfile.ZipFile(arquivo)
blocos = sorted([n for n in z.namelist() if n.startswith("logic-1-")],
                key=lambda n: int(n.rsplit("-", 1)[1]))
d = np.frombuffer(b"".join(z.read(n) for n in blocos), dtype=np.uint8)
canal = lambda c: ((d >> c) & 1).astype(np.uint8)          # bit c = canal c
bordas = lambda s: np.flatnonzero(np.diff(s.astype(np.int8))) + 1
```

A partir disso, cada grandeza sai assim:

| Grandeza | Como é obtida | Unidade |
|---|---|---|
| **Período do pulso** | diferença entre subidas consecutivas do CH5 | amostras → µs (÷ 4) |
| **Largura do pulso** | de cada subida à descida seguinte do CH5 | µs |
| **Quadro → atuação** | de cada subida do CH7 até a subida do CH6 imediatamente anterior | µs |
| **Transferência** | janela entre a descida e a subida seguinte do CH3 (NSS baixo) | µs |
| **Taxa efetiva do barramento** | 512 ciclos ÷ duração da janela | Hz |
| **Período base do SCK** | mediana dos intervalos entre subidas do CH2, descartando as pausas | µs |
| **Pausas entre bytes** | intervalos do SCK acima de 1,8× o período base | µs |
| **Janela do READY** | de cada descida à subida seguinte do CH4 | µs |
| **Bytes no fio** | amostrar CH0/CH1 nas subidas do CH2 (modo 0), empacotar 8 bits MSB primeiro | — |
| **Validade do quadro** | achar `AA 55`, ler o comprimento no cabeçalho, recalcular CRC-16 CCITT (semente `0xFFFF`) sobre o pacote e comparar com o trailer | — |

Duas decisões de método que importam para a leitura dos números:

1. **Janelas de pedido e de resposta são separadas.** Em toda transferência
   o escravo também transmite; na janela do *pedido* o MISO carrega
   conteúdo residual e é irrelevante. Só a janela de *resposta* conta para
   a taxa de quadros malformados. A separação é feita pelo MOSI: se ele
   traz um quadro válido, aquela janela é o pedido.
2. **O CRC é o mesmo do firmware**, reimplementado a partir de
   `utl/utl_crc16.c` (tabela CCITT, semente `0xFFFF`, cobrindo o pacote
   inteiro incluindo o SOF). Assim a validação externa usa exatamente o
   critério do código embarcado.

---

## 3 — Capturas

| Arquivo | Duração | Condição | Observação |
|---|---|---|---|
| `pulso_baseline.sr` | 7,53 s | Serviço em regime (1 Hz), bomba parada | primeira captura; valida a montagem |
| `pulso_run600.sr` | 18,96 s | Consulta contínua, bomba a 600 mL/h, sem carga | interrompida pelo operador |
| `pos_fix_run600.sr` | 15,0 s | Idem, com firmware em modo assíncrono | experimento descartado (ver §5.3) |
| `capturas/pulso_limpo.sr` | 15,0 s | Consulta contínua, bomba infundindo, sem carga | CH3 solto; superada |
| `capturas/pulso_estresse.sr` | 15,0 s | Idem, com carga | CH3 solto; superada |
| `capturas/atuacao_limpa.sr` | 15,0 s | Modo `mix`, sem carga | CH3 solto; superada |
| `capturas/teste_sem_rede.sr` | 15,0 s | Modo `mix`, **CPU + disco + memória** | completa |
| `capturas/atuacao_rede_limitada.sr` | 15,0 s | Modo `mix`, carga completa | completa |
| **`capturas/pulso_limpo2.sr`** | 14,9 s | **Consulta contínua, sem carga** | rodada definitiva |
| **`capturas/pulso_estresse2.sr`** | 15,0 s | **Consulta contínua, com carga** | rodada definitiva |
| **`capturas/atuacao_limpa2.sr`** | 15,0 s | **Modo `mix`, sem carga** | rodada definitiva |
| **`capturas/atuacao_estresse3.sr`** | 15,0 s | **Modo `mix`, com carga** | rodada definitiva |

As quatro primeiras capturas da sessão final ficaram com a sonda do NSS
solta, o que impede a análise quadro a quadro. Elas foram **refeitas** com
os oito canais e com a carga de rede limitada, o que também tornou as
condições com carga comparáveis entre si. Os valores de pulso e de caminho
até a atuação reproduziram os da primeira rodada, o que é uma verificação
de repetibilidade e não apenas uma correção.

Em todas, a bomba infunde a 600 mL/h (pulso a 847,5 Hz) e o serviço de
produção fica parado, porque disputaria o mesmo `spidev`.

O script [captura_bancada.sh](captura_bancada.sh) executa uma captura com
condição fixa: para o serviço, sobe a carga antes de medir, inicia a
infusão, captura, e encerra tudo no fim.

---

## 4 — Resultados

### 4.1 — Jitter da atuação, com e sem carga

| | Sem carga | **Carga completa** |
|---|---|---|
| Pulsos | 12.657 | 12.712 |
| Período mediano | 1.180,00 µs | **1.180,00 µs** |
| Mínimo / máximo | 1.179,75 / 1.180,25 µs | 1.179,75 / 1.180,25 µs |
| **Pico a pico** | **0,50 µs** | **0,50 µs** |
| Desvio padrão | 0,09 µs | 0,09 µs |
| Razão cíclica | 50% (589,5 µs em nível alto) | 50% |

Idênticos, até o último dígito que o instrumento resolve. **Os 0,50 µs são
dois degraus de quantização a 4 MHz**, ou seja, o jitter real é menor ou
igual a isso nas duas condições — o número a reportar é um limite superior,
não uma medida.

É a resposta direta à afirmação que o artigo fazia desde a primeira versão
sem nunca ter medido.

### 4.2 — Caminho quadro → atuação

| Condição | n | Mediana | p95 | p99 | Máximo |
|---|---|---|---|---|---|
| Sem carga | 677 | 123,8 µs | 131,2 | 136,2 | 136,5 |
| CPU + disco + memória | 676 | 121,8 µs | 131,1 | 136,2 | 136,5 |
| **Carga completa** | 668 | **123,2 µs** | 130,7 | 136,1 | **136,5** |

A primeira rodada de capturas, com a sonda do NSS solta mas com os
marcadores íntegros, deu medianas de 121,8 a 124,3 µs e máximos de 136,5 a
138,2 µs — as mesmas grandezas, o que serve de verificação de
repetibilidade.

A instrumentação **interna** do firmware reportou, nas campanhas, um pior
caso de 125 a 129 µs para o mesmo caminho. O analisador, com outro relógio
e sem depender de nenhum software do sistema, deu medianas de 121,8 a
123,8 µs e máximo de 136,5 µs.

**Dois instrumentos independentes concordando.** Num trabalho que descartou
quatro campanhas por defeito de instrumentação, essa concordância vale mais
que qualquer um dos dois números isolado.

### 4.3 — O barramento é limitado pela CPU do mestre

| | Serviço a 1 Hz | Bench em laço | Bench sob carga |
|---|---|---|---|
| Período base do SCK | 2,00 µs (500 kHz) | 2,00 µs (500 kHz) | 2,00 µs |
| Pausas entre bytes | mediana 6,25 µs | mediana 4,00 µs | — |
| **Duração da transferência** | **1.870 µs** | **1.177 µs** | 1.178–1.181 µs |
| Taxa efetiva | 274 kHz | 435 kHz | ~435 kHz |

Os 512 ciclos de clock por transferência são sempre os mesmos; o que muda é
**quanto tempo o barramento passa parado entre bytes**. Com o mesmo clock
nominal, a transferência dura 60% mais quando o mestre está ocioso.

Consequência para a latência: duas transferências de 1.177 µs são 2.354 µs
dos 2.837 µs de mediana medidos nas campanhas — **83% da transação é
ocupação do barramento**. A estimativa anterior de 72%, feita supondo
500 kHz contínuos, estava errada.

E aumentar o clock rende menos que proporcional: por byte são hoje ~16 µs
de clock mais ~13 µs de pausa.

### 4.4 — Integridade elétrica

| | |
|---|---|
| Ciclos de SCK por janela | **512 em todas as janelas** (mín = máx) |
| Pedidos do gateway com CRC válido | **100%** |
| Glitches, bits perdidos, quadros truncados | nenhum |

Não há evidência de ruído, reflexão ou perda de bits nesta montagem, com
jumpers e a 500 kHz.

---

### 4.5 — Integridade do quadro depende da cadência, não da carga

Com os oito canais nas quatro condições, a taxa de respostas malformadas
medida **no fio**:

| Condição | Respostas | Malformadas | Taxa |
|---|---|---|---|
| Consulta contínua, sem carga | 2.854 | 9 | 0,32% |
| Consulta contínua, **com carga** | 2.857 | 8 | **0,28%** |
| Cadência das campanhas (`mix`), sem carga | 2.709 | **0** | 0,00% |
| Cadência das campanhas (`mix`), **com carga** | 2.677 | **0** | 0,00% |

O gateway, contando de forma independente, registrou 14 e 12 recusas nas
duas primeiras condições e 1 e 0 nas duas últimas.

A leitura é direta: **a saturação do gateway não altera a integridade do
enlace; o que altera é o ritmo com que o mestre pede.** Sob consulta
contínua o escravo não repõe a FIFO a tempo em uma fração das
transferências; com comandos que atravessam a fila do domínio crítico
intercalados, a folga é suficiente e nenhuma resposta sai malformada.

---

## 5 — Achados que mudaram o entendimento do sistema

### 5.1 — O escravo não sustenta 500 kHz em consulta contínua

Na cadência de consulta contínua (um `GET_STATUS` atrás do outro), parte
das respostas sai malformada **do próprio microcontrolador**: cabeçalho
correto, payload pela metade e CRC zerado, tipicamente parando em 8 bytes —
o tamanho da FIFO de transmissão.

| Clock | Quadros recusados pelo gateway |
|---|---|
| 100 kHz | **0,00%** (3.000 transações) |
| 500 kHz | 0,42% a 1,27% |
| 2 MHz | **94,07%** |

O microcontrolador percebe: **477 erros de driver com 477 resets do
periférico** em 15.913 quadros. E o analisador confirma que o problema não
é o mestre: **todas** as transferências começam com o READY alto há cerca
de 220 µs, ou seja, o handshake é respeitado.

Descartados por medição: motor girando (ocorre igual com a bomba parada),
log pela UART (ocorre igual com `CONFIG_LOG=n`), intervalo entre
transações, os marcadores, e a ordem de armar e sinalizar.

**Na cadência das campanhas o fenômeno não aparece:** ver §4.5 — em modo
`mix`, 5.386 respostas com e sem carga, **nenhuma malformada**. Os comandos
que atravessam a fila do domínio crítico introduzem pausas que dão folga ao
escravo.

### 5.2 — DMA não é possível neste caminho

O driver do Zephyr recusa explicitamente:

```
<err> spi_stm32: SPI DMA is only supported for full-duplex controller mode
<err> hub: Erro SPI Driver: -134        (-ENOTSUP)
```

A condição está em `drivers/spi/spi_stm32.c:2048`: DMA só é usado quando o
modo é **controlador** e full-duplex. Em modo escravo toda transferência
retorna `-ENOTSUP`, o hub reseta o periférico e o firmware reinicia após
dez erros seguidos — foi o que produziu 100% de falha quando o DMA foi
habilitado.

A configuração testada estava correta: `gpdma1` habilitado pela própria
DTS da placa, e os slots conforme o HAL (`SPI1_RX = 6`, `SPI1_TX = 7`).

**Consequência estrutural:** em modo escravo, a FIFO é sempre alimentada
pela CPU, e o teto de taxa é o atendimento da interrupção. Isso é limite da
pilha, não do projeto — e é o que explica §5.1.

### 5.3 — Uma correção que não era correção

A hipótese inicial para os quadros malformados era a corrida "o READY sobe
antes de o periférico estar armado". Foi implementada a versão assíncrona
(armar primeiro, sinalizar depois) e medida: **não resolveu**, e numa
corrida piorou (1,83%). O código permanece no firmware, documentado e
desligado (`CONFIG_SPI_ASYNC=n`). Registrado aqui para que a hipótese não
seja tentada de novo sem o dado.

### 5.4 — Por que algumas capturas truncaram

O `fx2lafw` entregou menos amostras que o pedido em várias tentativas
(`Device only sent N samples`). A causa **não** é silêncio no barramento —
o instrumento amostra continuamente, haja sinal ou não.

Todas as capturas truncadas tinham **carga de rede**, e o `iperf3` roda da
Raspberry Pi **contra o mesmo host** que recebe as amostras pela USB. O
host, ocupado tratando o tráfego, atrasa o serviço da USB.

Verificado: com CPU, disco e memória saturados mas **sem** a carga de rede,
a captura completou os 15 s. Com a rede limitada a 100 Mbit
(`IPERF_BW=100M`), também completou.

Isso precisa constar da metodologia do artigo: **nas capturas com
analisador a carga de rede foi limitada**, enquanto nas campanhas de
software o gerador de tráfego rodou sem limite.

---

## 6 — Limitações destas medições

1. **Resolução.** 0,25 µs a 4 MHz. O jitter do pulso está no piso do
   instrumento: o valor reportado é um limite superior.
2. **Carga de rede limitada** nas capturas com analisador (§5.4).
3. **Sonda do NSS solta** nas quatro primeiras capturas da sessão final;
   elas foram refeitas e não são mais usadas para nenhum número.
4. **Duração.** 15 s por condição. Suficiente para 12.712 pulsos e ~700
   eventos de atuação, curto para caudas raras.
5. **Sem base de comparação monolítica.** Não há medida equivalente de uma
   implementação sem separação de domínios.

---

## 7 — Figuras que estes dados sustentam

Especificação para quem for desenhá-las. Requisito geral: **zoom suficiente
para a forma de onda ser legível** — uma janela única de milissegundos vira
borrão — e paleta com contraste, uma cor por condição, mantida igual em
todas as figuras.

### F1 — Jitter da atuação, com e sem carga

- **Pergunta:** a saturação do gateway altera o sinal que move o motor?
- **Dados:** `capturas/pulso_limpo2.sr` e `capturas/pulso_estresse2.sr`, canal CH5.
- **Painéis:** (a) três a quatro pulsos, eixo em µs, mostrando forma e
  período; (b) distribuição dos períodos nas duas condições — o período só
  assume três valores, então barras agrupadas comunicam melhor que
  histograma; (c) tabela com n, mediana, mínimo, máximo, pico a pico e
  desvio.
- **Precisa aparecer:** as duas condições com a mesma altura de barra, e a
  nota de que o pico a pico equivale a dois degraus de quantização.

### F2 — Caminho quadro → atuação

- **Pergunta:** a carga do gateway atrasa o caminho até o motor?
- **Dados:** `capturas/atuacao_limpa2.sr`, `capturas/teste_sem_rede.sr` e
  `capturas/atuacao_estresse3.sr`; intervalo entre subidas de CH6 e CH7.
- **Painéis:** (a) distribuição nas três condições; (b) acumulada, que
  mostra as curvas praticamente sobrepostas.
- **Precisa aparecer:** a faixa de 125 a 129 µs, que foi o que a
  instrumentação interna reportou, sombreada ao fundo. É a validação
  cruzada, e é o ponto da figura.

### F3 — Anatomia de uma transação, com o protocolo decodificado

*Estado: **feita**. `figuras/exportar_f3.py` → `figuras/dados/f3.json` →
`figuras/gerar.mjs` → `images/fig_transaction.pdf` (figura de duas colunas,
Fig. 8 do artigo).*

- **Pergunta:** como o protocolo se comporta no tempo, onde está o custo, e o
  que de fato trafega no fio?
- **Dados:** `capturas/pulso_limpo2.sr` (rodada definitiva, sem carga). O
  exportador escolhe um par pedido/resposta cujas duas transferências ficam a
  menos de 2% da mediana do arquivo — 1.185,8 e 1.175,8 µs contra mediana de
  1.191 µs —, para a figura não mostrar um caso atípico da cauda (que vai de
  1.161 a 1.913 µs conforme a ocupação do mestre).
- **Decodificação:** os bits são amostrados na **subida do SCK**, agrupados de
  8 em 8 com MSB primeiro, e o quadro é localizado pelo SOF `AA 55`; o
  tamanho vem dos bytes 5–6 e o **CRC-16/CCITT é recalculado sobre o pacote
  inteiro e conferido** antes de o JSON ser escrito. Os rótulos da figura são,
  portanto, o que o instrumento externo leu — não o que o firmware diz ter
  enviado.
- **Painéis:** (a) o ciclo inteiro, com os seis canais, as duas envoltórias de
  NSS, o READY e o pulso do marcador A; (b) o pedido decodificado no MOSI;
  (c) a resposta decodificada no MISO, com os campos do protocolo (SOF, DST,
  SRC, CMD, LEN, PAYLOAD, CRC) e a quebra do payload na estrutura de estado
  (estado, volume infundido, vazão configurada, pressão, alarme).
- **O que aparece:** pedido `GET_STATUS` (CMD 0x03, LEN 0x0000, quadro de 9
  bytes dentro do slot de 64) e resposta `GET_STATUS_RES` (CMD 0x04, LEN
  0x000E) lendo estado = RUNNING, volume = 0 mL, vazão = 600 mL/h, sem
  alarme; clock em 500 kHz dentro do byte (2,0 µs por bit) com pausa de
  2,0 µs entre bytes; e o marcador A alto por **23,3 µs** entre o fim do
  pedido e o rearme do periférico — o custo de validar o quadro e preparar a
  resposta.
- **Para que serve no artigo:** sustenta três afirmações sem depender do
  auto-relato de nenhum dos domínios — a ocupação do barramento (1.186 µs por
  transferência, base dos 83% da transação), o formato do quadro e a validade
  do campo de integridade, e o custo da validação no domínio crítico.

### F4 — Quadro malformado ao lado de um quadro válido

- **Pergunta:** o que exatamente o CRC detecta?
- **Dados:** `pulso_run600.sr`, janelas de resposta com CRC inválido.
- **Painéis:** dois quadros lado a lado, com os bytes decodificados; no
  malformado, destacar onde os dados cessam e o CRC zerado.
- **Precisa aparecer:** que a corrupção é interna ao quadro, e que o
  cabeçalho é válido — o que explica por que só o CRC a pega.

### F5 — Contenção de vazão (não vem do analisador)

- **Pergunta:** o domínio crítico limita um comando fora da faixa?
- **Dados:** `resultados/bench-p3v2/ataques.csv`, experimento E13.
- **Forma:** vazão pedida contra vazão efetiva, quatro pontos, com a faixa
  terapêutica sombreada e o teto de 1.200 mL/h como linha.
- **Substitui** a figura sintética `seguranca_rtos.pdf`, que mostra a vazão
  indo a zero — comportamento que nunca existiu.

**Descartar:** `SPI_Dump_Stress3.png` (captura de um quadro de 240 µs
apresentada como prova de ausência de jitter no laço) e
`seguranca_rtos.pdf` (sintética). A figura de estrutura do quadro
(`SPI_Auto_Healing.png`) pode ir para a metodologia, corrigindo o tamanho
do quadro de 24 para 64 bytes.

---

## 8 — Procedência e reprodução

| | |
|---|---|
| Arquivos | `pulso_baseline.sr`, `pulso_run600.sr`, `pos_fix_run600.sr`, `capturas/*.sr` |
| Firmware | v1.1.23, marcadores em PA8 e PE0, modo interrupção, sem DMA, assíncrono desligado |
| Gateway | `argus-bench` de `/data/bin`, SPI a 500 kHz, bomba a 600 mL/h |
| Análise | leitura direta dos `.sr` com numpy; CRC-16 CCITT replicado do firmware |

Uma captura se reproduz com:

```bash
./captura_bancada.sh <rotulo> <status|mix> <sem-carga|com-carga>
```

Para a condição com rede, limitar a banda evita o truncamento:

```bash
IPERF_BW=100M ./captura_bancada.sh <rotulo> mix com-carga
```
