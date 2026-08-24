# O que este software é hoje, e o que falta

Documento de avaliação do Robo2dof. Escrito a pedido: *"quanto valerá
um software desse do jeito em que está, e quais melhorias posso fazer
ainda"*.

---

## 1. O que existe

| Parte | Linhas | O que é |
|-------|--------|---------|
| Firmware | ~4 400 | 10 módulos em C++, dois núcleos, sem `delay()` no laço de controle |
| Interface | ~2 100 | página única, sem dependência externa, servida comprimida |
| Bancos de teste | ~4 000 | 120 cenários de firmware + 74 de interface, rodando no PC |
| Documentação | ~2 200 | ligações, uso, achados, cartão |

**Estado de qualidade:** 120 cenários de firmware e 74 de interface,
**zero anomalias abertas**. O firmware real é compilado contra mocks de
Arduino/FastAccelStepper/NVS/FreeRTOS/SD e executado; a interface roda num
Chromium de verdade contra um ESP32 falso. Três guardas impedem regressão
silenciosa: `conferir_ligacoes.py` (documento × `config.h`),
`conferir_rotas.py` (página × servidor) e `gerar_pagina_gz.py --conferir`
(página servida × fonte).

Isso não é um sketch de Arduino. É um produto de software pequeno, com
arquitetura declarada, testes executáveis e documentação de manutenção.

---

## 2. Quanto vale

Não existe preço de tabela para isto, e qualquer número aqui é ordem de
grandeza, não cotação. Vale separar três perguntas diferentes, porque as
respostas são muito distantes entre si.

### 2.1 Custo de reposição — "quanto custaria mandar fazer de novo"

É o número mais defensável, porque é aritmética. Um desenvolvedor
*embarcado* competente, com cinemática e controle de movimento no
currículo, leva na faixa de **250 a 450 horas** para chegar neste ponto —
incluindo a parte que ninguém orça e é metade do trabalho: descobrir por
que o braço perde passo, por que a área desenhada saía errada, por que o
cotovelo virava no meio do cordão.

No Brasil, hora de contratação desse perfil em 2026 fica entre R$ 120 e
R$ 250. Dá **R$ 30 000 a R$ 110 000** de custo de reposição.

### 2.2 Valor embutido na máquina — o caso realista

Software de controle raramente é vendido separado: ele é o que faz a
máquina valer o que vale. Um posicionador/braço de solda de 2 eixos com
controle manual simples e um com **interface de celular, importação de
DXF, calibração assistida e programa por pontos** não são o mesmo produto
nem o mesmo preço.

Aqui o software não tem um preço; ele **desloca a máquina de categoria**.
É onde está o retorno de verdade do que você construiu.

### 2.3 Licenciar para outros integradores

Se outra pessoa fosse montar a mesma máquina e comprar só o controle
pronto, a faixa praticada para software embarcado de nicho, com suporte,
é de **R$ 3 000 a R$ 15 000 por máquina** — o degrau depende quase
inteiramente de você assumir ou não obrigação de suporte e de segurança.

### 2.4 O que hoje derruba o valor

Ordem de importância. Nenhum destes é código:

1. **Não há avaliação de risco nem conformidade.** Um equipamento de
   solda automatizado vendido no Brasil precisa de NR-12. Sem isso, não
   se vende para empresa — e o risco de responsabilidade é seu.
2. **O botão de emergência físico não está instalado.** A lógica existe
   e está testada (`ESTOP_FISICO_INSTALADO` em `config.h`); falta o botão
   e a fiação. Enquanto isso, a única parada é por software, sobre Wi-Fi.
3. **Os fios de alarme (ALM) dos drivers não estão ligados.** Um servo
   que perde referência sabe disso e avisa; o firmware sabe reagir; o
   fio não está lá. Hoje ele perde passo em silêncio.
4. **Malha aberta.** Nada confere se o braço foi para onde foi mandado.

Resolver 1–4 vale mais, comercialmente, do que qualquer função nova.

---

## 3. Melhorias, em ordem de retorno

### Nível 1 — o que transforma protótipo em equipamento

**1.1 Botão de emergência físico.** A lógica está pronta e testada por
cenário (A08). Falta botão NF, fiação e mudar uma constante. É a
diferença entre "tem parada de emergência" e "não tem".

**1.2 Ligar os fios ALM dos servos.** Pull-up de 10 k e
`ALARME_FISICO_INSTALADO = true`. Um drive que perdeu referência avisa,
e o sistema vai para FALHA em vez de continuar soldando torto.

**1.3 Sensores de origem por junta e *homing* automático.** Hoje a
referência é estabelecida a mão ("Zerar a máquina aqui") e se perde a
cada perda de passo. Dois sensores indutivos baratos e o braço se
referencia sozinho ao ligar. **Resolve de raiz a queixa recorrente de
"o desenho não bate com o braço".**

**1.4 Realimentação de posição (malha fechada).** Os drivers T3D têm
saída de encoder (PA/PB). Ler essas fases num contador de pulso do ESP32
e comparar com o comandado dá **detecção de perda de passo em tempo
real** — o sistema para e avisa em vez de soldar torto até o fim. É a
melhoria técnica de maior valor da lista.

> **Modbus pelo RS485 do driver não substitui isso.** Cada leitura custa
> de 5 a 20 ms, com jitter alto — dá para mostrar posição na tela e ler
> alarme, não para pegar perda de passo no meio de um cordão. A
> ferramenta de diagnóstico do RS485, e o porquê de ele normalmente não
> responder, estão em
> [`ferramentas/RS485_T3D.md`](ferramentas/RS485_T3D.md).

### Nível 2 — o que melhora a solda

**2.1 Parâmetros por trecho.** Hoje a velocidade de cordão é uma só para
o programa inteiro. Velocidade, corrente e tempo de arco por trecho é o
que separa "faz um cordão" de "faz o cordão certo em cada junta".

**2.2 Tecimento (*weaving*).** Oscilação transversal ao cordão, com
amplitude e frequência ajustáveis. Em solda de chanfro é praticamente
obrigatório, e num braço de 2 eixos é só somar uma senoide ao setpoint
cartesiano — a interpolação já existe.

**2.3 Entrada e saída de cordão (*lead-in* / *lead-out*).** Abrir e
fechar o arco fora da peça, ou fora da linha de corte.

**2.4 Compensação de espessura (*kerf offset*).** Para corte, deslocar o
contorno meia largura de corte para fora ou para dentro.

**2.5 Múltiplos passes.** Repetir o mesmo contorno com deslocamento, para
chanfro em vários passes.

### Nível 3 — o que melhora o uso

**3.1 WebSocket no lugar da consulta HTTP.** Hoje a interface pergunta o
estado várias vezes por segundo, e o `WebServer` do ESP32 atende uma
conexão por vez. WebSocket derruba a latência do joystick e libera o
barramento.

**3.2 Jog cartesiano.** Mover a ponta em X e Y direto, em vez das juntas.
Muito mais intuitivo para posicionar sobre a peça.

**3.3 Programa direto do cartão (*streaming*).** Executar lendo uma
janela de pontos por vez, em vez de carregar tudo na RAM. Acaba com o
limite de 120 pontos — é o que permite DXF de peça grande. Ver
[`CARTAO_SD.md`](CARTAO_SD.md) §4.4.

**3.4 Importar G-code.** Quem já tem CAM para corte sai em G-code, não
em DXF. O trabalho é pequeno: o importador de DXF já entrega no formato
que o firmware consome.

**3.5 Relógio de tempo real.** Hoje o log marca milissegundos desde a
partida e a ordem entre partidas vem de um contador no NVS. Um DS3231 de
poucos reais dá data e hora de verdade — o que importa se o log tiver de
servir de registro de produção.

**3.6 Biblioteca de peças com miniatura.** A aba Arquivos lista nomes.
Com uma miniatura do contorno, achar a peça certa deixa de ser adivinhação.

### Nível 4 — o que amplia o mercado

**4.1 Terceiro eixo.** Um eixo Z (altura da tocha) ou um posicionador
rotativo. A arquitetura aguenta: velocidade e rampa já são por junta, e
a cinemática está isolada num módulo.

**4.2 Controle de altura por tensão do arco (AVC/THC).** Para corte
plasma é o que separa corte bom de corte ruim.

**4.3 Perfis de máquina.** Guardar no cartão o conjunto completo
(elos, reduções, cursos, velocidades) e trocar de perfil, para quem
monta mais de uma máquina.

---

## 4. Se fosse escolher três

1. **Sensores de origem + homing automático** (1.3) — mata a queixa mais
   recorrente e é barato.
2. **Realimentação de encoder** (1.4) — a máquina passa a *saber* quando
   errou, em vez de você descobrir na peça.
3. **Emergência física + ALM** (1.1, 1.2) — é o que permite mostrar a
   máquina para um cliente sem ressalva.

As três são de hardware barato e mudança pequena de firmware. Nenhuma
função nova de software chega perto disso em retorno.
