# RoboCNC v6 — relatório de anomalias

Análise do firmware + banco de testes executável (`testes/`), que compila os
módulos reais contra mocks de Arduino/FastAccelStepper/NVS/FreeRTOS e roda
cenários de operação no PC.

```
./testes/compilar.sh        →  2 passaram, 15 anomalias
```

Nada aqui é opinião de estilo. Cada item tem um caminho de código e, quando
reproduzível, um cenário no banco.

---

## Severidade 1 — segurança

### S1.1 · A parada de emergência disputa fila com o heartbeat de jog  `A01`

`enviarComando()` empurra tudo na mesma fila FIFO de 24 posições. A interface
manda um `/api/jog` a cada 100 ms por eixo. Se o core 1 atrasar um ciclo, a
fila enche de jog e o `CMD_PARAR` é **descartado**.

Pior: `handleParar()` responde `200 ok` sem olhar o retorno de
`enviarComando()`. A interface diz que parou; o braço continua andando.

*Reproduzido: 24 jogs enfileirados → `CMD_PARAR` recusado pela fila.*

```
servidor_web.cpp:199   static void handleParar() { ...; enviarComando(CMD_PARAR); ok(); }
estado.cpp:57          return xQueueSend(filaComandos, &c, 0) == pdTRUE;   // retorno ignorado
```

**Correção.** Uma flag `volatile bool pedidoParada` escrita direto pelo handler
e testada no topo do `loop()`, antes de drenar a fila. A parada não pode
depender de haver espaço em buffer. Complementarmente: `xQueueSendToFront` para
comandos de parada e HTTP 503 quando `enviarComando()` falhar.

---

### S1.2 · O botão de emergência físico não impede o rearme dos servos  `A08`

```c
// RoboCNC.ino:288
if (estop && !emergenciaAtiva) { ...; servosHabilitar(false); }
else if (!estop && emergenciaAtiva) { emergenciaAtiva = false; }
```

A ação acontece só na **borda**. Com o botão ainda pressionado,
`emergenciaAtiva` já é `true`, então nada reage a um `CMD_SERVOS(1)` que chega
depois: `servosHabilitar(true)` religa o SON e `jogAtualizar()` — que não
consulta `estop` em lugar nenhum — volta a mover o braço com a emergência
acionada. O relé fica bloqueado (`soldaPermitir` testa `!estop`), o movimento
não.

Hoje está latente porque `ESTOP_FISICO_INSTALADO = false`. Vira um defeito
ativo no dia em que o botão for instalado — que é justamente o item na lista
"o que ainda falta" do LEIA-ME.

**Correção.** Emergência é condição de nível, não de evento: recusar
`CMD_SERVOS(1)` enquanto `estop` estiver ativo, e reavaliar `servosHabilitar`
todo ciclo enquanto `emergenciaAtiva`.

---

### S1.3 · Jog e reprodução com os drivers desabilitados  `A02` `A12`

Nem `jogAtualizar()` nem `trajIniciarReproducao()` consultam `servosLigados`.
O FastAccelStepper gera pulsos e **incrementa o contador de posição** mesmo com
o SON em nível baixo. O eixo não se move; a contagem sim.

*Reproduzido: com `servosLigados = 0`, um segundo de jog moveu a posição lógica
de J1 em 89,4° sem o eixo sair do lugar.*

A partir daí toda a proteção de curso — que é baseada nesse contador — protege
a região errada, e `J1.calibrada` continua `true`. O mesmo vale para o caso
inverso: desabilitar os servos tira o torque, o braço cai por gravidade e o
contador não acompanha.

`CMD_PROG_EXECUTAR` já checa `servosLigados` quando há arco. A reprodução de
trajetória, que também aciona o relé, não checa nada.

**Correção.** Recusar jog, reprodução e posicionamento com os servos
desligados; e invalidar `calibrada` (ou exigir re-referenciamento) sempre que
o torque for removido.

---

### S1.4 · Queda de conexão durante a solda congela o programa em vez de pará-lo  `A05`

```c
// RoboCNC.ino:300 — supervisionar(), ramo semConexao
if (modoAtual == MODO_REPRODUZINDO) trajPararReproducao();
if (modoAtual == MODO_GRAVANDO)     trajPararGravacao();
// falta:  if (progRodando()) progParar();
```

O arco fecha e os motores param (isso funciona — `A05a` passou). Mas
`progParar()` nunca é chamado: `fase` fica em `FASE_SOLDANDO` enquanto
`modoAtual` volta para `MANUAL`. Consequências medidas:

- `progRodando()` continua `true` para sempre — a interface mostra estado
  inconsistente e os botões de execução ficam com o rótulo errado.
- Em `MODO_MANUAL` o operador pode **apagar pontos** que a máquina de estados
  congelada ainda indexa (`CMD_PONTO_REMOVER` só exige modo manual).
- `progParar()` é quem restaura `aplicarAceleracao()`. Como não roda, a
  aceleração fica no valor de `prepararReta()`: **8000 → 32000, 4× mais alta**,
  e é com ela que o jog manual seguinte funciona.

O mesmo furo existe no ramo de alarme de driver.

**Correção.** Uma função `pararTudo()` chamada pelos três ramos de
`supervisionar()` e por `CMD_PARAR`, encerrando programa, trajetória, jog,
arco e restaurando velocidade/aceleração.

---

### S1.5 · A faixa da margem de segurança é uma armadilha sem saída  `A03` `A04`

`posturaValida()` reprova a partir de `grausMin + MARGEM_LIMITE_GRAUS`;
`gravidadeViolacao()` só começa a contar a partir de `grausMin` cru. Na faixa
de 0,5° entre os dois a postura é **inválida e a gravidade é zero**, então o
critério de recuperação do jog (`gAtual > 0.001f`) nunca se satisfaz.

*Reproduzido: com J1 em −89,75° (limite −90,0°), o jog na direção do centro
ficou bloqueado. O braço não sai de lá pelo jog.*

Isso fica grave combinado com **`ajustarCurso()` aceitar curso de 11 passos**
(`calibracao.cpp:70`), o que com a resolução padrão são 0,4° — menos que os
2 × 0,5° de margem:

*Reproduzido: calibração de 0,43° foi aceita, `grausMin + 0,5 = +0,28` ficou
maior que `grausMax − 0,5 = −0,28`, e **nenhuma postura** passou na validação.
Os dois eixos travaram.* Só se recupera refazendo a calibração ou desligando a
proteção de curso.

**Correção.** (a) `gravidadeViolacao()` tem que usar exatamente os mesmos
limites de `posturaValida()`, margem incluída. (b) `ajustarCurso()` deve exigir
curso mínimo em graus — algo como `> 10 * MARGEM_LIMITE_GRAUS` — e não
11 passos.

---

### S1.6 · "Ir para o ponto" não revalida a postura  `A06`

```c
// RoboCNC.ino:198 — CMD_IR_PARA_PONTO
const Ponto& p = progLista()[c.a];
moverCoordenado(p.p1, p.p2, velAuto);   // sem posturaValida()
```

O ponto foi validado quando gravado. Entre a gravação e o "ir", as proteções
podem ter sido ligadas, os elos remedidos ou a calibração refeita.

*Reproduzido: ponto gravado com a proteção de mesa desligada (padrão de
fábrica) tem a ponta em Y = −388 mm. Ligando a proteção como o LEIA-ME manda,
esse ponto passa a ser inválido — e o "ir" executou o movimento assim mesmo.*

O LEIA-ME promete "validação de postura em **todo** comando de movimento".

---

### S1.7 · O caminho de deslocamento não é validado, só as pontas  `A07`

`progIniciar()` valida cada ponto e, nos trechos com solda, a reta inteira.
Nos trechos **sem** solda o caminho é interpolação nas juntas, e nada verifica
o interior. Isso é seguro para os limites de curso (são caixas no espaço das
juntas, e uma reta entre dois pontos da caixa fica na caixa) — mas **não** para
o envelope cartesiano, que não é convexo nesse espaço.

*Reproduzido por busca exaustiva sobre uma grade de 10°:*

```
A:  t = (−10°, +10°)    ponta (397, −35) mm     VÁLIDO
B:  t = (+40°, −160°)   ponta ( 53, −45) mm     VÁLIDO
8% do percurso:  t = (−6°, −3°)  ponta (396, −53) mm  →  abaixo do Y mínimo
```

Com a proteção de mesa ligada, o robô mergulha a ponta na bancada indo de um
ponto permitido a outro. E `MODO_POSICIONANDO` não tem supervisão nenhuma
durante a execução — o `loop()` só espera os motores pararem.

**Correção.** Validar a interpolação de junta do mesmo jeito que
`retaPercorrivel()` valida a cartesiana, ou supervisionar a postura a cada
ciclo durante o movimento coordenado e abortar na violação.

---

## Severidade 2 — arquitetura e concorrência

### S2.1 · A rota de configuração viola a regra de ouro do projeto  `A14`

O LEIA-ME e três cabeçalhos afirmam: *"nenhum handler HTTP toca em estado do
core 1; eles apenas enfileiram Comando e leem Snapshot"*. `handleConfig()`,
`handleGeometria()` e `handleProtecoes()` fazem o contrário — escrevem direto,
**do core 0**, sem fila e sem checar o modo:

```c
// servidor_web.cpp:287
J1.passosPorVolta = (uint32_t)pv1;
J1.reducao        = rd1;
recalcularResolucao();     // reescreve passosPorGrau, grausMin e grausMax
```

`recalcularResolucao()` altera três campos por junta que o core 1 lê dentro de
`posturaValidaPassos()` a cada ciclo de `jogAtualizar()`. Não há barreira: existe
uma janela em que `passosPorGrau` já mudou e `grausMin/grausMax` ainda não.

*Reproduzido: dobrar `passosPorVolta` com o braço parado em 45° teleporta a
posição lógica para 22,5° sem que nada se mova.* Se isso acontecer durante um
cordão, o `grausParaPassos()` do próximo setpoint muda de escala no meio do
movimento.

`handleGeometria()` tem o mesmo problema com `elo1Mm`/`elo2Mm`, lidos dentro da
cinemática direta e inversa em execução.

**Correção.** Passar os parâmetros pela fila (`CMD_APLICAR_CONFIG` já existe —
falta carregar os valores nele em vez de escrever nas globais), e recusar
mudança de resolução/geometria com `modoAtual != MODO_MANUAL`.

---

### S2.2 · Cancelar a calibração no meio deixa limites deslocados  `A11`

`calibConfirmar()` no estado `CAL_HOME` chama `zerarPosicoes()`. Se o operador
desistir depois disso, `calibCancelar()` faz `carregarConfiguracoes()` e
restaura `passosMin`/`passosMax` do NVS — que se referem ao **zero antigo**. O
`zerarPosicoes()` nunca é desfeito.

*Reproduzido: calibração válida de ±90°, novo HOME definido 30° adiante, depois
cancelado → `calibrada = true`, limites ±90° e a origem deslocada 30°. As
proteções passam a guardar a região errada, com erro igual à distância entre os
dois zeros.*

**Correção.** Guardar a posição no momento do `zerarPosicoes()` e restaurá-la
no cancelamento; ou marcar `calibrada = false` ao cancelar depois do HOME.

---

### S2.3 · Leitura de buffers do core 1 a partir dos handlers HTTP

`handlePontos()` percorre `progLista()` enquanto o core 1 pode estar dentro de
`progRemoverPonto()`, que desloca o array. `handleTrajetoria()` lê
`trajBuffer()` protegido apenas por um `Snapshot` que pode estar 40 ms atrasado.
`handleStatus()` lê `J1.grausMin`, `elo1Mm`, `progQuantidade()` fora do mutex.

Consequência é visual (coordenada rasgada na lista de pontos), não mecânica —
mas é a mesma classe de bug que a arquitetura diz ter eliminado.

---

### S2.4 · `if (motoresEmMovimento()) return;` como detector de chegada

Cinco transições de estado dependem de `isRunning()` virar `true` **no mesmo
ciclo** em que `moveTo()` foi chamado:

```
RoboCNC.ino:433     MODO_POSICIONANDO
programa.cpp:232    FASE_INDO_INICIO, FASE_DESLOCANDO, FASE_ABRINDO_ARCO
calibracao.cpp:159  CAL_J1_VOLTA_NEG e os outros três retornos ao zero
```

Se o gerador de rampa levar um ciclo para sinalizar, a fase avança com o motor
prestes a arrancar. Em `MODO_POSICIONANDO` isso é pior que cosmético: o modo
volta para `MANUAL`, e no ciclo seguinte `jogAtualizar()` chama `stopMove()`
porque `jogDir == 0` — o posicionamento morre 1 ms depois de começar.

Hoje a biblioteca sinaliza síncrono e isso não aparece, mas é um contrato não
documentado com a implementação da lib.

**Correção.** Comparar posição com alvo (`getCurrentPosition() == alvo`), ou
guardar o alvo na transição e testar chegada explicitamente.

---

## Severidade 3 — qualidade do cordão

### S3.1 · `definirMensagem()` dentro do laço de 1 ms  `A09`

O jog de recuperação (`motores.cpp:179`) chama `definirMensagem()` **a cada
ciclo** enquanto durar a recuperação, e `definirMensagem()` escreve na serial.

*Medido: 130 mensagens idênticas por segundo, 5,5 bytes/ms = **48% da UART** a
115200 8N1 ocupados por uma única frase repetida.* Quando o buffer de TX enche,
`Serial.print()` bloqueia — e quem trava é o `loop()` do core 1, o mesmo laço
que roda `supervisionar()` e `jogAtualizar()`. Ou seja: uma mensagem informativa
tem prioridade sobre a supervisão de segurança.

**Correção.** Emitir só na transição de estado, ou limitar por tempo
(`if (millis() - ultimoLog > 500)`).

---

### S3.2 · Cordão perto do braço esticado deixa de ser reto  `A13`

*Medido por varredura: no cordão (250,0) → (400,0) mm com elos de 200+200, o
último passo de 1,5 mm exige que θ2 vá de −9,9° para 0,0° — **9,9° de junta em
1,5 mm de trajeto**.*

Perto de `|r| = L1 + L2` a cinemática inversa é mal condicionada. As
velocidades de seguimento em `prepararReta()` são calculadas pela **média** do
trecho (`Δpassos_total × 1000 / tSegTotal`), com fator 3 de folga — insuficiente
nessa região. O motor não alcança o setpoint, a ponta corta caminho, e o cordão
deixa de ser reto: exatamente o que a interpolação cartesiana existia para
garantir.

`retaPercorrivel()` não reprova, porque todas as posturas do caminho *são*
válidas. O que é inválido é a derivada.

**Correção.** Recusar cordão que passe a menos de ~5% de `L1 + L2` da base ou
do alcance máximo, e dimensionar `velSeg` pelo **maior** ΔΘ por passo
interpolado, não pela média. Travar o ramo do cotovelo no início do trecho
também elimina a possibilidade de troca de solução no meio.

---

### S3.3 · `moverCoordenado()` não devolve a aceleração

`prepararReta()` e `trajAtualizarReproducao()` multiplicam a aceleração por 4.
Só `progParar()` e `trajPararReproducao()` restauram. Qualquer saída que não
passe por eles (ver S1.4) deixa o valor 4× ativo — inclusive para o jog manual.

---

## Severidade 4 — interface e operação

| # | Item | Onde |
|---|------|------|
| S4.1 | **API sem autenticação nenhuma** num AP aberto com senha fixa `12345678` no código. Qualquer um no alcance faz POST em `/api/prog/executar?ensaio=0` e abre o arco. | `config.h:120` |
| S4.2 | **Um clique simples na mesa de traçado comanda movimento real** (`/api/mover_xy`), sem confirmação e sem distinguir clique de arraste. Num celular é fácil disparar sem querer. | `pagina_web.h:745` |
| S4.3 | Após `/api/config/reset`, os campos do formulário **não são recarregados** (`if(!carregou)` roda uma vez só). O próximo "Salvar" reaplica os valores antigos por cima do padrão de fábrica. | `pagina_web.h:831` |
| S4.4 | `recalcularResolucao()` sobrescreve os padrões `±90°` de `grausMin/grausMax` por `0/0` quando a junta não está calibrada. A interface mostra "0…0°" e a zona permitida desenhada tem largura zero. | `estado.cpp:50` |
| S4.5 | `s.mensagem` é injetada crua no JSON. Nenhuma mensagem atual tem aspas ou barra, mas basta uma para quebrar o `r.json()` do navegador e a interface anunciar "sem comunicação" com o robô inteiro funcionando. | `servidor_web.cpp:90` |
| S4.6 | `restaurarPadroes()` não restaura `escalaVelocidadeTraj`. | `estado.cpp:169` |
| S4.7 | `carregarConfiguracoes()` não valida nada vindo do NVS. Um `ppv = 0` gravado por uma versão anterior zera `passosPorGrau` e derruba toda a conversão em silêncio. | `estado.cpp:93` |
| S4.8 | Polling HTTP com `WebServer` (uma conexão por vez): status a 220 ms + heartbeat de jog a 100 ms por eixo se serializam. É a causa provável de jog engasgado, e o próprio LEIA-ME já lista a migração para WebSocket. | — |

---

## O que foi verificado e está correto

- **Buffer do JSON de status** (`A10`): pior caso construído com valores
  extremos e mensagem de 95 caracteres = **882 bytes** para um buffer de 1024.
  Tem folga, mas é estreita — qualquer campo novo precisa refazer a conta.
- **Corte do arco na queda de conexão** (`A05a`): funciona.
- `NOME_CMD[]` está alinhado com o enum `TipoComando` (24 × 24).
- A antecipação de frenagem no jog usa velocidade instantânea, como o LEIA-ME
  descreve, e para no limite sem reservar curso à toa.
- `soldaDesligar()` reforça o nível baixo mesmo quando o estado interno já
  indicava desligado — a porta única do relé está bem construída.
- `distanciaFreada()` usa a aceleração *configurada*; quando a real está em 4×
  (S3.3), a estimativa erra para o lado conservador.

---

## Ordem sugerida de correção

1. **S1.1** parada fora da fila — é o que separa "para" de "deveria parar".
2. **S1.5** margem × gravidade e curso mínimo da calibração — trava o braço hoje.
3. **S1.4** `pararTudo()` unificado nos três ramos de `supervisionar()`.
4. **S1.3** exigir servos habilitados para qualquer movimento.
5. **S1.6 / S1.7** revalidar postura no "ir" e no interior do deslocamento.
6. **S2.1** mover a configuração para a fila de comandos.
7. **S1.2** antes de instalar o botão físico de emergência.
8. **S3.1** tirar a serial do laço de controle.

Os itens de severidade 3 e 4 podem esperar; nenhum deles quebra a máquina.
