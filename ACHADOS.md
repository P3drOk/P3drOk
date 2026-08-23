# RoboCNC v6 — relatório de anomalias

Análise do firmware + banco de testes executável (`testes/`), que compila os
módulos reais contra mocks de Arduino/FastAccelStepper/NVS/FreeRTOS e roda
cenários de operação no PC.

```
antes das correções   →   2 passaram, 15 anomalias
depois                →  26 passaram,  1 anomalia   (./testes/compilar.sh)
```

Nada aqui é opinião de estilo. Cada item tem um caminho de código e, quando
reproduzível, um cenário no banco.

**Estado:** toda a severidade 1 está corrigida, mais S2.1, S2.2, S3.1 e S4.3.
Cada seção corrigida traz o que mudou. O que ficou de fora está listado no
fim, em "Não corrigido".

---

## Severidade 1 — segurança

### S1.1 · A parada de emergência disputa fila com o heartbeat de jog  `A01`  ✅

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

**Corrigido.** `handleParar()` chama `solicitarParada()`, que escreve
`volatile bool pedidoParada` (`estado.cpp`). O `loop()` testa essa flag no topo,
antes de drenar a fila. A parada não depende mais de haver espaço em buffer.

`pararTudo()` também chama `limparFilaComandos()`: sem isso os heartbeats de jog
que já estavam enfileirados eram processados logo depois da parada e o braço
voltava a andar no mesmo ciclo — foi o que o banco pegou na primeira rodada da
correção.

Todos os outros handlers passaram a usar `enfileirar()`, que responde **HTTP 503**
quando `enviarComando()` falha, em vez de `200 ok`.

---

### S1.2 · O botão de emergência físico não impede o rearme dos servos  `A08`  ✅

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

**Corrigido.** Emergência virou condição de nível:

```c
if (estop) {
  if (!emergenciaAtiva) { emergenciaAtiva = true; pararTudo("EMERGENCIA ..."); }
  if (servosLigados) servosHabilitar(false);      // todo ciclo, não só na borda
}
```

E `CMD_SERVOS(1)` é recusado enquanto `emergenciaAtiva`. O jog fica bloqueado
pelo portão `movimentoLiberado` (S1.3), que inclui `!estop && !emergenciaAtiva`.

O banco compila com `-DESTOP_FISICO_INSTALADO=true` e exercita o ciclo inteiro:
soca o botão (torque, movimento e arco caem), tenta rearmar e jogar com ele
pressionado (recusado), solta (rearme e jog voltam).

---

### S1.3 · Jog e reprodução com os drivers desabilitados  `A02` `A12`  ✅

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

**Corrigido.** Um portão único, `bool movimentoLiberado` (`estado.h`), escrito
por `supervisionar()` a cada ciclo:

```c
movimentoLiberado = servosLigados && !alarme && !estop &&
                    !emergenciaAtiva && !semConexao && modoAtual != MODO_FALHA;
```

`jogAtualizar()` sai imediatamente quando ele é falso, zerando o jog e avisando.
`trajIniciarReproducao()`, `progIniciar()` (inclusive no ensaio), `irParaPassos()`
e `calibIniciar()` exigem `servosLigados` explicitamente.

O mesmo portão passou a alimentar `soldaPermitir()`, que antes repetia a
expressão à mão.

**Ficou de fora de propósito:** invalidar a calibração ao desabilitar os servos.
O braço pode cair por gravidade com o torque removido, e nesse caso o contador
diverge de qualquer jeito — mas invalidar obrigaria a recalibrar toda vez que os
servos fossem desligados. Isso só se resolve direito com sensor de home, que já
está na lista de pendências do LEIA-ME.

---

### S1.4 · Queda de conexão durante a solda congela o programa em vez de pará-lo  `A05`  ✅

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

**Corrigido.** `pararTudo()` no `.ino` é o encerramento único — arco, jog, fila
de comandos, programa, trajetória (gravação e reprodução), calibração, parada
suave, e restauração de velocidade e aceleração. `MODO_FALHA` só sai por rearme
explícito.

Chamam `pararTudo()`: os três ramos de `supervisionar()` (alarme, emergência,
conexão perdida), `CMD_PARAR` e a flag `pedidoParada`.

---

### S1.5 · A faixa da margem de segurança é uma armadilha sem saída  `A03` `A04`  ✅

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

**Corrigido.** (a) `gravidadeViolacao()` passou a descontar
`MARGEM_LIMITE_GRAUS` exatamente como `posturaValida()`. A faixa morta deixou de
existir: no cenário do banco, a junta em −89,75° com limite −90,0° agora volta
para dentro do curso (−2493 → −817 passos).

(b) `ajustarCurso()` mede em graus contra `CURSO_MINIMO_GRAUS = 5.0f`
(`config.h`), com a mensagem dizendo o mínimo exigido. A calibração de 0,43° do
cenário é recusada.

---

### S1.6 · "Ir para o ponto" não revalida a postura  `A06`  ✅

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

**Corrigido.** Todo posicionamento passa por `irParaPassos()`, porta única que
confere, nessa ordem: calibração, servos, a postura de destino e o interior do
caminho até ela. `CMD_IR_PARA_PONTO`, `CMD_MOVER_ANGULOS` e `CMD_IR_HOME` usam
essa porta.

---

### S1.7 · O caminho de deslocamento não é validado, só as pontas  `A07`  ✅

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

**Corrigido.** `caminhoJuntasValido()` em `cinematica.cpp` amostra a
interpolação a cada `PASSO_VALIDACAO_GRAUS` (2°, teto de 360 checagens) e roda
`posturaValida()` em cada amostra. Usam a função:

- `progIniciar()` — na aproximação até o ponto 1 e em **cada trecho sem solda**
  (os trechos com solda continuam validados por `retaPercorrivel()`).
- `trajIniciarReproducao()` — na aproximação até o primeiro waypoint.
- `irParaPassos()` — em qualquer posicionamento.

A mensagem de recusa diz qual trecho e por quê: *"o deslocamento 1→2 passa por:
abaixo do Y mínimo (mesa)"*.

Com a proteção de envelope desligada (padrão de fábrica) nada muda no
comportamento; com ela ligada, o programa é recusado antes de sair do lugar.

---

## Severidade 2 — arquitetura e concorrência

### S2.1 · A rota de configuração viola a regra de ouro do projeto  `A14`  ✅

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

**Corrigido.** Área de preparo `ConfigPendente` (`estado.h`). Os handlers
validam os argumentos, chamam `prepararConfigPendente()` (que copia o estado
vivo), sobrescrevem só os campos recebidos e enfileiram `CMD_APLICAR_CONFIG`.
Nenhum handler escreve em variável viva nem chama `recalcularResolucao()`.

O core 1 aplica em `CMD_APLICAR_CONFIG`, e **só em `MODO_MANUAL`** — os handlers
já devolvem 400 pelo snapshot como filtro rápido, e o core 1 faz a checagem
autoritativa. Vale para `/api/config`, `/api/geometria`, `/api/protecoes` e
`/api/config/reset`.

---

### S2.2 · Cancelar a calibração no meio deixa limites deslocados  `A11`  ✅

`calibConfirmar()` no estado `CAL_HOME` chama `zerarPosicoes()`. Se o operador
desistir depois disso, `calibCancelar()` faz `carregarConfiguracoes()` e
restaura `passosMin`/`passosMax` do NVS — que se referem ao **zero antigo**. O
`zerarPosicoes()` nunca é desfeito.

*Reproduzido: calibração válida de ±90°, novo HOME definido 30° adiante, depois
cancelado → `calibrada = true`, limites ±90° e a origem deslocada 30°. As
proteções passam a guardar a região errada, com erro igual à distância entre os
dois zeros.*

**Corrigido.** `calibracao.cpp` guarda a posição dos dois contadores antes do
`zerarPosicoes()` do `CAL_HOME`. Ao cancelar, restaura
`posicaoJ1() + origemAntesDoZero1` — a posição atual expressa na referência
antiga. No cenário do banco: braço em 30°, novo HOME zerado ali, cancelamento →
volta a ler 30° com os limites originais de ±90°.

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

### S3.1 · `definirMensagem()` dentro do laço de 1 ms  `A09`  ✅

O jog de recuperação (`motores.cpp:179`) chama `definirMensagem()` **a cada
ciclo** enquanto durar a recuperação, e `definirMensagem()` escreve na serial.

*Medido: 130 mensagens idênticas por segundo, 5,5 bytes/ms = **48% da UART** a
115200 8N1 ocupados por uma única frase repetida.* Quando o buffer de TX enche,
`Serial.print()` bloqueia — e quem trava é o `loop()` do core 1, o mesmo laço
que roda `supervisionar()` e `jogAtualizar()`. Ou seja: uma mensagem informativa
tem prioridade sobre a supervisão de segurança.

**Corrigido.** `definirMensagem()` sempre atualiza `ultimaMensagem` (a interface
continua vendo tudo); o que passou a ser poupado é o eco na serial — repetição
idêntica não imprime, e há piso de 50 ms entre impressões. O cenário do banco
saiu de 130 mensagens/s para 0.

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
| S4.3 ✅ | Após `/api/config/reset`, os campos do formulário **não eram recarregados** (`if(!carregou)` roda uma vez só) e o próximo "Salvar" reaplicava os valores antigos por cima do padrão de fábrica. **Corrigido:** o botão zera `carregou` no sucesso, e o próximo status repreenche os campos. | `pagina_web.h` |
| S4.4 | `recalcularResolucao()` sobrescreve os padrões `±90°` de `grausMin/grausMax` por `0/0` quando a junta não está calibrada. A interface mostra "0…0°" e a zona permitida desenhada tem largura zero. | `estado.cpp:50` |
| S4.5 | `s.mensagem` é injetada crua no JSON. Nenhuma mensagem atual tem aspas ou barra, mas basta uma para quebrar o `r.json()` do navegador e a interface anunciar "sem comunicação" com o robô inteiro funcionando. | `servidor_web.cpp:90` |
| S4.6 ✅ | `restaurarPadroes()` não restaurava `escalaVelocidadeTraj`. **Corrigido.** | `estado.cpp` |
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

## Não corrigido (e por quê)

| # | Item | Por quê |
|---|------|---------|
| **S2.3** | Handlers HTTP lendo `progLista()` / `trajBuffer()` sem trava | Consequência é visual (coordenada rasgada na lista), não mecânica. A correção certa é publicar essas listas num snapshot como o de status, e é uma refatoração maior. |
| **S2.4** | `if (motoresEmMovimento()) return;` como detector de chegada | A biblioteca sinaliza `isRunning()` de forma síncrona hoje, então não aparece. Trocar por comparação posição-alvo mexe em cinco transições de estado de uma vez; melhor fazer isolado e com o banco cobrindo cada uma. |
| **S3.2** | Cordão perto do braço esticado deixa de ser reto (`A13`, única anomalia que resta no banco) | Não é bug de estado, é dimensionamento: precisa recusar cordão perto de `L1+L2` e calcular `velSeg` pelo maior ΔΘ por passo interpolado em vez da média. Afeta a *qualidade* do cordão, não a segurança. |
| **S3.3** | `moverCoordenado()` não devolve a aceleração | Resolvido na prática por `pararTudo()` (S1.4), que restaura em toda saída. Falta ainda o caso de sucesso normal, que já passava por `progParar()`. |
| **S4.1** | API sem autenticação | Decisão de produto: autenticar num AP de bancada atrapalha o operador. Se o robô for para uma rede compartilhada, aí vira obrigatório. |
| **S4.2** | Clique na mesa de traçado comanda movimento | Precisa distinguir clique de arraste e provavelmente pedir confirmação — mudança de interação que vale desenhar com você antes. |
| **S4.4 / S4.5 / S4.7 / S4.8** | Limites `0…0°` sem calibração, `msg` crua no JSON, NVS sem validação, polling HTTP | Cosméticos ou de robustez; nenhum quebra a máquina. |

---

## Resumo das mudanças

| Arquivo | O que mudou |
|---------|-------------|
| `config.h` | `CURSO_MINIMO_GRAUS`, `PASSO_VALIDACAO_GRAUS`; `#ifndef` nos flags de e-stop e alarme |
| `estado.h/.cpp` | `pedidoParada` + `solicitarParada()` + `limparFilaComandos()`; portão `movimentoLiberado`; área `ConfigPendente`; `definirMensagem()` com eco de serial poupado |
| `cinematica.h/.cpp` | `gravidadeViolacao()` com a mesma margem de `posturaValida()`; `caminhoJuntasValido()` |
| `motores.cpp` | `jogAtualizar()` atrás do portão de movimento; `pararEmergencia()` preserva `MODO_FALHA` |
| `trajetoria.cpp` | reprodução exige servos e valida a aproximação |
| `programa.cpp` | execução exige servos (ensaio incluído); valida aproximação e cada deslocamento |
| `calibracao.cpp` | exige servos; curso mínimo em graus; cancelamento restaura a origem |
| `RoboCNC.ino` | `pararTudo()`; `irParaPassos()`; emergência por nível; `pedidoParada` no topo do `loop()`; config aplicada só em manual |
| `servidor_web.cpp` | `handleParar()` fora da fila; `enfileirar()` com 503; config via área de preparo com `exigirManual()` |
| `pagina_web.h` | formulário recarrega após restaurar padrões |
| `testes/` | A07/A08/A11/A14 reescritos para verificar a recusa; **A15** novo: regressão ponta a ponta |

O banco também passa a conferir a compilação de `servidor_web.cpp`, que fica
fora da execução por depender de rede.

---

## Antes de subir para a máquina

O banco roda no PC com steppers simulados. Ele prova a lógica de estado, não a
eletrônica. Confira na bancada, nesta ordem:

1. **Jog com servos desligados** deve ser recusado com mensagem, e a posição em
   graus na tela não pode mudar.
2. **Calibração completa** — o assistente tem que exigir servos ligados e
   recusar curso menor que 5°.
3. **Cancelar a calibração** depois do passo HOME: o ângulo mostrado tem que
   voltar ao que era antes de começar.
4. **Ensaio e execução com arco** de um programa de 3 pontos, para confirmar que
   a validação nova não recusa programa legítimo. Se recusar, a mensagem diz
   qual trecho — provavelmente é a proteção de mesa com elo mal medido.
5. **Puxar o Wi-Fi no meio do cordão**: arco fecha, braço para, e o jog seguinte
   tem que ter a aceleração normal (não a 4× de solda).
6. **Ajustes durante execução** devem ser recusados com *"ajuste só com o robô
   parado no modo manual"*.

O botão físico de emergência (S1.2) só entra em serviço depois de trocar
`ESTOP_FISICO_INSTALADO` para `true` em `config.h` — a lógica já está pronta e
testada.

---

# Rodada 2 — controles mortos na interface

Encontrados depois de o operador relatar que *"Gravar ponto na posição
atual"* e *"Ir para o zero da máquina"* não faziam nada. A auditoria foi
feita clicando **todo** controle num Chromium de verdade e comparando as
rotas registradas no firmware com as chamadas que a interface faz.

## R1 · A sanfona era global, e escondia o painel de outra aba  ✅

```js
document.querySelectorAll(".et").forEach(x => x.classList.remove("aberta"));
```

Abrir uma seção fechava **todas** as seções da página, não só as do mesmo
painel. Como a aba Mover tem uma seção só, bastava abrir *"Ensaiar sem
arco"* na aba Programa para que, ao voltar ao Mover, o joystick, *"Gravar
ponto"* e *"Ir para o zero"* estivessem recolhidos — parecendo botão que
não responde.

**Corrigido.** A sanfona fecha apenas as seções do mesmo `.pane`, e só
age em cabeçalhos que têm seta (`.chv`). As seções do joystick e do
cartão não recolhem: são a superfície principal de cada aba.

## R2 · Botão desabilitado sem dizer por quê  ✅

`btHome` ficava desabilitado quando faltava calibração ou servos —
correto, mas mudo. Clicar não fazia nada e não explicava nada.

**Corrigido.** Toda ação principal passa por `acao(id, motivo)`: quando
bloqueada, aparece uma linha laranja abaixo do botão dizendo o que
resolver, na ordem em que resolver (*"habilite os servos (aba Ajustes,
etapa 1)"* → *"calibre as juntas"* → *"robô ocupado"*). O joystick
também apaga e mostra o motivo em vez de parecer pronto.

## R3 · Três rotas do firmware sem nenhum acionamento  ✅

Auditoria automática comparando `server.on(...)` com as chamadas do
`pagina_web.h`:

| Rota | O que estava perdido |
|------|----------------------|
| `/api/solda` | **Nenhum botão abria o arco manualmente.** A gravação a mão livre registra o estado do relé em cada instante do percurso — e não havia como ligar o relé. O modo estava quebrado desde sempre. |
| `/api/trajetoria` | O firmware reamostra o caminho gravado e converte para XY para desenhar. Nada consumia: a trajetória era gravada e nunca aparecia na mesa. |
| `/api/mover` | Posicionar por ângulo digitado não tinha campo. |

**Corrigido.** Botão *Abrir arco* (com confirmação) na seção de
trajetória, desenho do caminho gravado na mesa (laranja onde o arco
estava aberto, cinza onde era só deslocamento) e dois campos de ângulo na
aba Mover.

Também apareceu que `escalaVelocidadeTraj` existia no firmware e não
tinha campo — a velocidade de reprodução ficava presa em 100%.

## R4 · A tira de mensagem rolava para fora da tela  ✅

A resposta de cada ação (*"Ponto 3 gravado"*, *"Movimento recusado: …"*)
ficava no topo da coluna e sumia ao rolar. **Corrigido:** `position:
sticky`.

## Como isso não volta

O banco da interface passou a clicar **todo** botão de **toda** seção, uma
seção aberta por vez, e reprova se algum:

- estiver invisível dentro da própria seção aberta;
- não disparar nenhuma requisição ao ser clicado;
- estiver desabilitado sem motivo escrito na tela.

Mais: ids repetidos, botão sem handler, sanfona vazando entre abas,
controles da lista de pontos, clique na mesa de traçado, setas de jog, e
a máquina sem servos nem calibração. **38 verificações.**

E `testes/conferir_ligacoes.py` reprova se `LIGACOES.md` divergir dos
pinos de `config.h` — documento de fiação que mente é pior que documento
nenhum.

---

# Rodada 3 — a recusa que parecia erro, e o Bluetooth

## R5 · "ponto no fim do curso" mesmo com os pontos folgados  ✅

Reproduzido no banco antes de mexer em qualquer linha. **A máquina estava
certa** — a mensagem é que não dizia nada.

Num braço 2R, aproximar a ponta da base obriga o cotovelo a dobrar. A
reta cartesiana entre dois pontos folgados pode exigir muito mais curso
do que as pontas:

```
cordão (288, −105) → (−53, 302) mm
   pontas          θ2 =  80°
   41% do trecho   θ2 = 133°      curso da junta: 90°
```

A frase antiga era *"junta 2 no fim do curso calibrado"*. O operador
olhava dois pontos a 80° num curso de 90° e concluía que o sistema estava
quebrado.

**Corrigido em quatro camadas:**

1. **`struct Violacao`** em `cinematica.cpp` carrega causa, junta, valor
   exigido, limite e a fração do percurso. `violacaoTexto()` monta a
   frase. `posturaValida(motivo)` continua existindo como casca.
2. **A pior violação, não a primeira.** `retaCartesianaValida()` e
   `caminhoJuntasValidoDet()` varrem o percurso inteiro e guardam o pior
   excesso. Relatar a primeira (89,8° neste cordão) faria abrir o limite
   em 1° e continuar recusado.
3. **Contexto em `progIniciar()`:** prefixo dizendo se é um ponto, um
   cordão, um deslocamento, a aproximação, ou **a posição atual do
   braço** — este último era o pior: braço parado fora da área útil
   reprovava o programa com uma mensagem sobre junta, e o operador ia
   procurar defeito nos pontos.
4. **Conferência enquanto ensina.** `progConferirTrecho()` é exposto em
   `/api/pontos`; um trecho impercorrível fica vermelho na lista com a
   frase embaixo e tracejado em vermelho na mesa. Descobrir isso só ao
   apertar Executar é tarde.

Antes / depois, do próprio banco:

```
antes    junta 2 no fim do curso calibrado
depois   cordao 1->2: junta 2 precisa ir a 132.8 graus a 41% do trecho,
         e o curso vai ate 89.5
```

E, para o braço fora da área:

```
o braco esta parado fora da area util (junta 1 precisa ir a 92.0 graus,
e o curso vai ate 89.5). Traga-o de volta com o jog
```

**Um erro no diagnóstico, vale registrar:** a primeira rodada mostrou
`progIniciar` recusando "sem motivo". Era bug do *diagnóstico* — a ordem
de avaliação dos argumentos do `printf` lia o ponteiro de motivo antes de
chamar a função. O firmware sempre escreveu o motivo.

## R6 · Controle por Bluetooth  ⬅ removido depois

> **Removido a pedido, para reduzir o tamanho do sketch.** A pilha BLE era
> de longe o maior pedaço do firmware. O texto abaixo fica como registro
> do que existiu e de como estava resolvido; `git revert` traz de volta.


`controle_bt.h/.cpp`, aplicativo Dabble no modo GamePad. Roda no core 0
junto com o servidor web e, como tudo de lá, **só enfileira `Comando`** —
não toca em motor, relé nem estado.

Usa o mesmo `CMD_JOG_XY` do joystick da tela: uma só implementação de
zona morta e velocidade proporcional no firmware, duas interfaces em
cima.

Três decisões de segurança:

- **X é parada**, pelo caminho fora da fila (`solicitarParada()`), igual
  ao botão PARAR da tela.
- **`start` roda sempre o ensaio.** O gamepad nunca abre arco — executar
  com solda exige a confirmação da tela.
- **Gamepad conectado conta como operador presente.** Sem isso, quem
  usasse só o Bluetooth veria o supervisor cortar o movimento em 2,5 s
  por "conexão perdida". O heartbeat passou a se chamar
  `registrarContatoOperador()`, com duas fontes.

Desconexão do aplicativo manda o zero na borda; o heartbeat de 350 ms do
firmware é a segunda rede.

## Cobertura

| banco | antes | agora |
|-------|-------|-------|
| firmware | 45 / 1 | **62 / 1** |
| interface | 38 / 0 | **39 / 0** |

Novos: C01–C03 (mensagens de recusa, braço fora da área, cordão bom
continua passando) e D01–D03 (jog pelo gamepad, botões e o que eles não
podem fazer, Bluetooth como único operador).

A anomalia que resta continua sendo a A13, severidade 3.

---

# Rodada 4 — o que o operador pediu depois de usar a máquina

Cinco queixas de uso real, todas reproduzidas antes de mexer no código.

## R7 · A velocidade de cordão não salvava  `H01`  ✅

**Reproduzido.** Mudar o valor na tela, salvar, e ele voltava ao anterior.

O defeito estava em `handleConfig`, não na interface. `velC` é o nome
antigo do mesmo parâmetro; o handler aceitava os dois, mas dava
prioridade ao **antigo** e usava o valor **vivo** como padrão dele:

```c
const float vs = argF("velC", argF("velCordao", velCordaoMmS));  // errado
```

Como a página só manda `velCordao`, o `velC` ausente sempre vencia com o
valor que já estava lá. A leitura passou a olhar qual dos dois nomes
realmente veio:

```c
float vs = velCordaoMmS;
if      (server.hasArg("velCordao")) vs = argF("velCordao", vs);
else if (server.hasArg("velC"))      vs = argF("velC", vs);
```

Esse defeito viveu numa camada que o banco **compilava e nunca
executava**. Ver *Cobertura*, abaixo.

## R8 · Tranco na partida  `H02`  ✅

Aceleração constante faz a aceleração aparecer de uma vez no instante da
partida — jerk infinito, que é o que se sente como tranco e o que faz o
eixo leve perder passo no arranque.

- `suavidadePartida` (0–255, padrão 120, NVS `suav`) liga a rampa em **S**
  do FastAccelStepper via `setLinearAcceleration()`, aplicada às duas
  juntas junto com a rampa.
- `seguirSetpoint()` parou de chamar `setSpeedInHz()` a cada ciclo de
  1 ms. Reprogramar a velocidade reinicia o cálculo da rampa; agora só
  reprograma quando o valor muda de verdade.

## R9 · Zerar a máquina na posição atual  `H03`  ✅

`calibReferenciar()` + `CMD_REFERENCIAR` + `POST /api/referenciar`: para
suave, zera o jog e zera a contagem dos dois eixos, como no boot. É o
conserto para o desenho na tela ficar deslocado do braço depois de perder
passo.

Só em modo manual, conferido nas duas pontas — no handler (para o
operador ver a recusa) e no core 1 (porque o modo pode mudar entre uma
coisa e outra). Reescrever a contagem debaixo do gerador de pulso em
movimento manda o braço para o batente.

## R10 · Aferir a redução sem calcular  `H04`  ✅

Marcar → girar → medir com transferidor → gravar.
`passosPorGrau = pulsos contados / graus medidos`, e a redução mecânica é
reescrita a partir disso. É a mesma conta da última etapa do assistente,
agora avulsa: dá para aferir um eixo sem refazer a medição de curso.

Recusas na porta: junta inexistente, ângulo ≤ 0,5° ou > 3600° (dividir
por quase nada manda a resolução para o infinito) e falta de marca.

## R11 · Desenhar o caminho com o dedo  `H05`  ✅

O botão **DES** na mesa de traçado grava uma polilinha por cima do
desenho do braço. O navegador simplifica com Douglas-Peucker, apertando a
tolerância até caber nos 40 pontos do programa, e manda
`x,y;x,y;…` em milímetros para `POST /api/prog/desenho`.

O firmware resolve a cinemática inversa de cada ponto **a partir do
anterior** (o cotovelo não troca de lado no meio do traço) e reaproveita
o caminho de carregamento de arquivo: preenche a área de troca e
enfileira `CMD_ARQ_APLICAR_PROG`, que valida tudo antes de trocar. Traço
que passe fora da área útil é recusado dizendo **qual ponto**, e o
programa que estava na máquina não é tocado.

## O banco passou a executar a camada HTTP

Dois dos defeitos mais caros deste projeto — "gravar ponto não faz nada"
(rodada 2) e a velocidade de cordão que não salvava (R7) — moravam em
`servidor_web.cpp`, que o banco **compilava e nunca rodava**. Nenhum
teste de motor pegaria qualquer um dos dois.

- `mocks/WebServer.h` deixou de ser esqueleto: registra as rotas de
  verdade e despacha um pedido direto no handler (`webPost`, `webGet`).
- `servidor_web.cpp` entra no link do banco.
- `testes/conferir_rotas.py` compara as rotas que a página chama com as
  que o firmware registra. Rota chamada e não registrada é 404
  silencioso — exatamente o botão mudo.

Um efeito colateral apareceu na hora: `reiniciarSistema()` não zerava
`modoAtual`. No ESP32 o boot reinicializa as globais; no banco elas
sobrevivem ao `setup()`, então um cenário que terminava gravando fazia o
seguinte recusar todo ajuste por um motivo que não era o do teste.

## Cobertura

| banco | rodada 3 | agora |
|-------|----------|-------|
| firmware | 62 / 1 | **104 / 1** |
| interface | 39 / 0 | **59 / 0** |

Novos no firmware: E01–E03, F01–F03, G01 (rodadas anteriores) e H01–H06
(camada HTTP). Novos na interface: modo de desenho, zerar na posição e as
três etapas da aferição.

A anomalia que resta continua sendo a A13, severidade 3, documentada em
*Não corrigido (e por quê)*.

---

# Rodada 5 — DXF, e o braço que fugia no ziguezague

## R12 · O cotovelo virava no meio do cordão  `A13` `I01`–`I03`  ✅

Esta era a **A13**, a única anomalia que o banco carregava desde a
primeira rodada, classificada severidade 3 e adiada. Ela apareceu na
máquina: com vários pontos em ziguezague, o braço largou a reta e fez uma
volta.

`resolverXY()` escolhe entre os dois ramos do cotovelo pelo critério "o
que exige menos movimento agora", e `atualizarReta()` o chamava a cada
1,5 mm. Perto do braço esticado os ramos praticamente coincidem: um
arredondamento troca a escolha, e a troca é uma descontinuidade de até
2 × |θ2|.

Com os elos do operador (450 e 400 mm) e curso de ±120°, o banco acha a
reta (−360, −770) → (−240, −770): 20,7° de θ2 num passo de 1,5 mm, com
troca de ramo. Com o ramo travado, 4,4° e nenhuma troca.

- `resolverXYRamo()` resolve num ramo fixo; `prepararReta()` trava o ramo
  e o mantém até o fim do trecho.
- `retaCartesianaValida()` trava o mesmo ramo. Antes ela reescolhia igual
  à execução — as duas erravam juntas e a validação aprovava.
- Recusa nova **"derivada"**: passo de 1,5 mm que exija mais de
  `SALTO_MAX_GRAUS` é recusado antes de o arco abrir.
- `prepararReta()` dimensiona a velocidade de seguimento pelo **pior**
  passo (`retaMaiorSalto`), não pela média.

**Lição:** a A13 estava certa e a classificação estava errada. O relatório
dela já dizia, na primeira rodada, exatamente o que fazer — "travar o ramo
do cotovelo no início do trecho em vez de reescolher a cada passo". Uma
anomalia reproduzível adiada por severidade continua sendo um defeito
esperando o dia de aparecer na peça.

## R13 · Cache de velocidade privado ficava velho  `I03`  ✅

Defeito que **eu** introduzi na rodada 4. O `static ultima1/ultima2`
dentro de `seguirSetpoint()` não sabia dos `setSpeedInHz()` feitos por
`moverCoordenado()` e `aplicarVelocidadeManual()`: um deslocamento no meio
deixava a lembrança velha, e o trecho seguinte podia rodar na velocidade
do deslocamento sem reprogramar nada. Agora há um único `velProgramada[2]`
por onde passa toda escrita de velocidade, zerado em `motoresIniciar()`.

## R14 · Importar DXF  `H05j`–`H05l`  ✅

O arquivo é lido **no navegador**; o ESP32 recebe a lista de pontos
pronta, pela mesma rota `POST /api/prog/desenho` do traço a dedo — mesma
validação, mesma área de troca, mesmo `CMD_ARQ_APLICAR_PROG`.

- Entidades: LINE, LWPOLYLINE (com bulge), POLYLINE, ARC, CIRCLE. Texto,
  cotas e hachuras são contados e ignorados.
- Contornos com pontas encostadas são emendados (um retângulo sai do CAD
  como quatro LINE soltas), e depois ordenados pelo mais próximo.
- Posicionamento na mesa: arrastar, girar, espelhar, escalar, centralizar.
  A barra conta em tempo real os pontos fora do alcance e trava o aplicar
  enquanto houver algum.
- `/api/prog/desenho` passou a aceitar um terceiro campo por ponto
  (`x,y,solda`): vários contornos viram vários cordões com deslocamento
  entre eles.
- `MAX_PONTOS` de 40 para **120**. Um retângulo com cantos arredondados já
  passava de 40. Custo: 2,9 kB de RAM em duas listas. O limite passou a
  sair no `/api/status`, então a página não simplifica para um número que
  o firmware não tem mais.

## R15 · "Não consegui salvar o desenho no cartão"  ✅

O botão respondia **200 sempre**: `handleSdSalvar` só enfileira, e a
recusa de verdade ("nada para salvar", "salve com o robô em manual",
"cartão ausente") aparecia apenas na tira de mensagem, que rola.

É a mesma família do defeito da rodada 2 — botão que parece funcionar e
não faz nada. Agora a aba Arquivos diz, ao vivo, **o que** vai ser gravado
("vai gravar o programa que está na máquina: 11 pontos" / "não há programa
na máquina. Desenhe na mesa, importe um DXF ou grave pontos na aba
Mover") e **por que** não pode agora (cartão ausente, robô fora de manual,
nome vazio ou com caractere proibido).

## Cobertura

| banco | rodada 4 | agora |
|-------|----------|-------|
| firmware | 104 / 1 | **120 / 0** |
| interface | 59 / 0 | **74 / 0** |

Primeira rodada sem nenhuma anomalia aberta.

---

# Rodada 6 — rede: os dois jeitos ao mesmo tempo

## R16 · Ponto de acesso próprio **e** rede da oficina  `J01`–`J04`  ✅

O rádio sobe em `WIFI_AP_STA`: o Wi-Fi da própria máquina **nunca sai do
ar**, mesmo com ela dentro da rede da oficina. Não é desperdício — é a
saída de emergência. Senha trocada, roteador desligado, sinal fraco no
fundo do galpão: em qualquer desses casos o painel continua alcançável em
`192.168.4.1`. Não há como desligar o AP próprio, e é de propósito: um
equipamento que se move não pode ficar inacessível por causa da rede de
outra pessoa.

`J01d`/`J01e` fixam isso: com a senha da oficina errada, o estado sai como
*senha recusada* (não como falha genérica) **e** o AP continua no ar.

- IP do AP fixado pelo projeto (`WIFI_AP_IP`), não herdado do padrão da
  biblioteca — assim não muda quando o core do ESP32 for atualizado.
- `robo2dof.local` por mDNS, anunciado nas **duas** interfaces, mais o
  mesmo nome como hostname de DHCP.
- Credenciais no NVS do projeto, gravadas pelo core 1 via
  `CMD_APLICAR_REDE`; o core 0 só recebe `redePedidoReconectar`.
  `WiFi.persistent(false)` impede a biblioteca de guardar uma segunda
  cópia que brigaria com a nossa.
- Retentativa com recuo progressivo (15 s → 5 min). Insistir de segundo
  em segundo com a senha errada só atrapalha o próprio AP: cada tentativa
  tira o rádio do canal.

### O ponto perigoso: a varredura  `J03`

A varredura de redes do ESP32 tira o rádio do canal do AP por vários
segundos. **Feita de forma síncrona dentro da tarefa web**, ela faria o
servidor parar de responder, o heartbeat do operador venceria em
`TIMEOUT_CONEXAO_MS` e o supervisor cortaria movimento e arco — um botão
de tela derrubando o braço.

Por isso ela é assíncrona (`WiFi.scanNetworks(true)` + `scanComplete()`),
e o cenário J03 mede exatamente isso: o handler devolve em 0 ms, o
servidor responde 30 de 30 pedidos de status durante a varredura, e
`movimentoLiberado` não cai. Além disso, tanto a varredura quanto a troca
de rede exigem `MODO_MANUAL` nas duas pontas.

## Duas falhas do próprio banco, encontradas por estes cenários

**O banco não executava a tarefa de rede.** `tarefaRede()` roda num
`xTaskCreatePinnedToCore` que o mock de FreeRTOS não executa, então
`redeAtender()` nunca era chamado — quatro cenários falharam por isso
antes de eu perceber que o defeito era do banco. Agora `rodar()` bombeia
`redeAtender()` como já bombeava `armCicloTeste()`.

**O mock de WebServer não decodificava percent-encoding.** O WebServer do
ESP32 decodifica `%20` e `+` antes de entregar o argumento; o mock
entregava cru. A página manda tudo por `encodeURIComponent`, então o
banco via `Oficina%202G` e teria deixado passar um defeito de
decodificação. Corrigido no mock — vale para todas as rotas, não só as de
rede.

`J04f` fecha o buraco correspondente do outro lado: SSID com aspas é nome
válido de rede e sai escapado nos dois JSON.

## Cobertura

| banco | rodada 5 | agora |
|-------|----------|-------|
| firmware | 120 / 0 | **142 / 0** |
| interface | 74 / 0 | **83 / 0** |

---

# Rodada 7 — o mock que mentia

## R17 · `rede.cpp` não compilava na IDE, e o banco não viu  ✅

```
error: cannot convert 'String' to 'const char*' in initialization
     const char* s = WiFi.SSID(i);
error: invalid user-defined conversion from 'IPAddress' to 'const char*'
     strncpy(ipEstacao, WiFi.localIP(), ...);
```

O código estava errado, mas o defeito de fundo é do **banco**: o mock de
`WiFi` devolvia `const char*` onde o core do ESP32 devolve `String` e
`IPAddress`. Compilou limpo aqui e falhou na máquina do operador — o pior
lugar possível para descobrir.

É a segunda vez na mesma rodada: o mock de `WebServer` também não
decodificava percent-encoding, enquanto o do ESP32 decodifica.

**Um mock que aceita mais que a biblioteca de verdade não é um mock, é
uma armadilha.** A regra virou documento
([`testes/mocks/LEIA-ME.md`](testes/mocks/LEIA-ME.md)): a assinatura do
mock é a do core, não a conveniente, e onde não for óbvia fica escrito
`// core: <assinatura real>` ao lado.

Corrigido nos dois lados:

- `rede.cpp` guarda `WiFi.SSID(i)` numa `String` com nome (o `c_str()` de
  um temporário é ponteiro pendurado) e usa `WiFi.localIP().toString()`.
- O mock passou a devolver `String` e `IPAddress`, e `IPAddress` ganhou
  `toString()` sem conversão implícita para `const char*` — é justamente
  essa recusa que o banco precisa reproduzir.
- Junto, as assinaturas de `Preferences` e do resto de `WiFi` foram
  alinhadas com o core (`size_t putString`, `bool setHostname`,
  `int16_t scanComplete`, `int8_t RSSI()`).

## R18 · Botões de rede nasciam clicáveis  ✅

Achado pela varredura de interface, não por mim: `btRedeConectar` e
`btRedeEsquecer` ficavam habilitados até o primeiro `/api/rede` chegar —
meio segundo em que a tela mente sobre o que dá para fazer. Mesma família
do botão mudo. Agora `redeEstadoBotoes()` roda uma vez na carga, e sem
contato com o robô o motivo é "sem contato com o robo" em vez de um
palpite sobre o modo.

---

# Rodada 8 — fora o modo estação

## R19 · Entrar na rede da oficina saiu  `J01`–`J03`  ✅

Pedido do operador, com a queixa certa: a máquina ficou mais lenta e o
joystick com atraso depois que o modo estação entrou. A queixa tem
mecanismo, não é impressão.

**O ESP32 tem um rádio só.** Em `WIFI_AP_STA` o ponto de acesso é
obrigado a acompanhar o canal do roteador e o rádio divide tempo de
antena entre as duas redes. O heartbeat do jog — que corta o movimento se
faltar por 350 ms — é justamente o tráfego que não pode atrasar. Rede de
terceiro não vale latência no controle de uma máquina que se move.

Removido: varredura, escolha de rede, senha pelo painel, credenciais no
NVS, `CMD_APLICAR_REDE`, `RedePendente`, a máquina de estados da conexão
e quatro rotas HTTP. O rádio sobe em `WIFI_AP` puro e nunca sai do canal.

Fica: IP fixo `192.168.4.1` declarado pelo projeto, mDNS
`robo2dof.local`, e um `GET /api/rede` de leitura que diz ao operador por
onde chegar no painel.

O ganho de flash é modesto (a pilha de Wi-Fi já estava lá para o ponto de
acesso; a página encolheu 1,5 kB). O ganho real é de latência, e é o que
motivou a remoção.

## R20 · DNS de captura  `J03`  ✅

Uma adição pequena no lugar do que saiu, para atender o que o operador
pediu: chegar no painel sem decorar endereço. Um servidor de DNS na porta
53 responde qualquer nome com o IP da máquina.

- Ao entrar na rede, o celular detecta portal cativo e costuma **oferecer
  abrir o painel sozinho** — em vez de reclamar que não há internet e
  pular para os dados móveis, que é o comportamento chato do Android.
- Qualquer coisa digitada na barra de endereço cai no painel.

Honestidade sobre o limite: nome de uma palavra só (`robo2dof`, sem
`.local` e sem barra) depende do navegador — vários tratam como busca
antes de tentar resolver, e isso acontece **antes** do DNS.
`robo2dof.local` e `192.168.4.1` funcionam sempre.

## Cobertura

| banco | rodada 7 | agora |
|-------|----------|-------|
| firmware | 142 / 0 | **130 / 0** |
| interface | 83 / 0 | **76 / 0** |

Menos cenários porque há menos sistema: doze verificações do modo estação
saíram junto com o código que elas cobriam.

---

# Rodada 9 — a seta que não queria dizer nada

## R21 · "Aperto a seta para a esquerda e o braço vai no horário"  `K01`–`K03`  ✅

Duas coisas diferentes por trás da mesma queixa, e só uma delas era o que
o operador achava.

**1. O rótulo estava errado, não o movimento.** Os botões de jog eram
`←` `→` para a junta 1 e `↓` `↑` para a junta 2. Junta é coisa que
**gira**: seta para os lados não quer dizer nada, porque para que lado a
ponta anda depende de onde o braço está. A convenção da cinemática é
θ crescente no anti-horário, então `←` (θ−) girava no horário — correto
pela convenção, e ilegível na tela.

Os botões passaram a falar em rotação: **↺ anti-horário** e
**↻ horário**, olhando o eixo de cima. Agora a frase "apertei ↻ e girou
anti-horário" é sem ambiguidade, e tem um conserto só.

**2. O conserto existia e estava fora de alcance.** `inv1`/`inv2` iam por
`/api/config`, que passa por `exigirManual()`. Durante o assistente o modo
é `MODO_CALIBRANDO`, então era recusado — o operador descobria o problema
exatamente onde não podia resolvê-lo, e a saída era cancelar o assistente,
ir em Ajustes e recomeçar.

Agora há `POST /api/sentido?j=&v=` e `CMD_INVERTER_EIXO`, aceitos em
manual **e na etapa de referência da calibração**, onde uma conferência
de sentido com as duas chaves aparece no próprio assistente.

Três recusas, todas com motivo na tela:

- **Depois da etapa de referência, não.** Dali em diante já existe limite
  medido; trocar o sinal do eixo inverteria o significado do que foi
  medido. A conferência some da tela e o firmware recusa (`K02c`).
- **Com o eixo andando, não.** Reescrever o `DIR` por baixo do gerador de
  pulso é o jeito mais rápido de mandar o braço para o batente (`K03`).
- Junta inexistente é barrada na porta.

Um conceito, um caminho: as chaves de Ajustes e as do assistente chamam a
mesma rota. Os parâmetros `inv1`/`inv2` continuam aceitos em
`/api/config` para uma página em cache, pelo mesmo motivo do `velC`.

## Cobertura

| banco | rodada 8 | agora |
|-------|----------|-------|
| firmware | 130 / 0 | **141 / 0** |
| interface | 76 / 0 | **80 / 0** |
