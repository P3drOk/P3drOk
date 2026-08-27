# Robo2dof v6 — relatório de anomalias

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
// Robo2dof.ino:288
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
// Robo2dof.ino:300 — supervisionar(), ramo semConexao
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
// Robo2dof.ino:198 — CMD_IR_PARA_PONTO
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
Robo2dof.ino:433     MODO_POSICIONANDO
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
| `Robo2dof.ino` | `pararTudo()`; `irParaPassos()`; emergência por nível; `pedidoParada` no topo do `loop()`; config aplicada só em manual |
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

## R78 · Uma leitura absurda do encoder virava a posicao da maquina  ✅

O pior desta rodada. O encoder e a unica testemunha de onde o braco esta,
e o firmware escrevia a leitura na contagem de passos **sem conferir
nada**. Uma leitura errada -- registrador errado, contagens por volta
erradas, ruido que passou no CRC -- virava a posicao oficial, e a partir
dali toda protecao de curso se apoiava num numero inventado.

O caso que assusta e o boot: a maquina se localiza por UMA leitura e, com
"ir ao zero" ligado, sai andando de onde acha que esta. Achando que esta
a 300 graus num braco de +/-90, ela manda 300 graus de pulso contra o
batente. E `zeroAtualizar()` chama `moverCoordenado()` direto, sem passar
pela porta que valida caminho.

A defesa nao e estatistica, e fisica: **o braco nao pode estar fora do
curso que o proprio operador mediu**. `leituraPlausivel()` recusa o que
cai fora dele (com 10 graus de folga para o batente e o empurrao a mao), e
`leituraConfiavel()` junta os tres criterios -- valida, recente e
possivel -- num lugar so, usado pela localizacao, pelo seguidor de eixo
solto, pelo vigia de travamento e pela leitura que aparece na tela.
Cenario `T01`.

Sintoma colateral que isto tambem resolve: a regua chegava a mostrar
"177667 graus medido" com ar de leitura boa.

## R79 · Travar no meio de um programa era lido como "cheguei"  ✅

O vigia de travamento parava o EIXO -- continuar dando pulso contra o
batente aquece o servo. Mas as maquinas de estado que rodam por cima
(programa, reproducao, posicionamento) esperam o movimento acabar para
seguir, e "parou" e exatamente o sinal delas de **cheguei**.

Resultado: um travamento no caminho ate o primeiro ponto fazia o programa
concluir a aproximacao ali mesmo e **abrir o arco onde o braco travou**,
dezenas de graus antes do inicio do cordao -- e depois arrastar a ponta
ate la com o arco aberto.

Agora um travamento durante movimento automatico interrompe o movimento
automatico. Travou = a maquina nao esta onde acha que esta; nao ha
percurso que se possa continuar dali. Cenario `T05`.

## R80 · Pausar na aproximacao retomava do lugar errado  ✅

Da mesma familia. Pausar durante a ida ao primeiro ponto guardava a fase,
mas retomar nao reemitia o movimento -- o ciclo seguinte via o braco
parado e concluia "cheguei". Cenario `T04`.

## R81 · Mensagem com aspas quebrava o JSON e derrubava a tela  ✅

Oito mensagens do firmware trazem aspas. A mais comum sai **toda vez que
alguem grava no cartao**:

    {"msg":"programa "peca 1" salvo"}

Isso nao e JSON. O `r.json()` do navegador lanca excecao, o contador de
quedas sobe e a interface anuncia **"sem comunicacao"** -- com a maquina
funcionando perfeitamente, mandando o operador procurar defeito no Wi-Fi.

O detalhe que explica por que sobreviveu tanto tempo: o numero de aspas
fica **par**. Nenhuma conferencia frouxa pega -- a primeira versao do
proprio cenario que caca isto passou com o defeito na tela. So um
analisador de gramatica de verdade reprova.

Escapar virou trabalho de quem escreve o JSON (`jsonTexto()`), nao de
quem escreve a mensagem: nenhum modulo deveria precisar saber que o texto
dele um dia viaja entre aspas. Cenarios `T02` e `T03`, este ultimo
varrendo TODA rota JSON em sete estados diferentes da maquina.

## R82 · O mock de String transformava numero em byte cru  ✅

Achado pelo `T03`. `String` do banco herda de `std::string`, que nao tem
`operator+=(int)` -- entao `out += (int)n` caia no `operator+=(char)` por
conversao implicita e acrescentava **um byte cru** no lugar do numero.

O String do core acrescenta o numero em decimal. Enquanto isso faltou, o
banco conferiu um JSON diferente do que a maquina produz: `{"n":<0x01>}`
aqui, `{"n":1}` la. Duas rotas (`/api/registro` e `/api/pontos`) estavam
sendo validadas contra a saida errada.

E o caso mais puro da regra do projeto: **a assinatura do mock e a
assinatura do core, nao a conveniente.** Um mock que aceita mais que a
biblioteca de verdade nao falha -- ele mente.

## R83 · Todo clique da pagina disparava a troca de aba  ✅

`document.querySelectorAll("[data-aba]")` tambem casava com o proprio
`<body>`, que carrega `data-aba` para o CSS. O resultado era um ouvinte de
clique no body inteiro: **todo** clique da pagina chamava `irAba()`,
regravava o `localStorage` e, na aba Mesa, remedia o canvas.

Passou anos despercebido porque trocar para a aba em que ja se esta nao
muda nada visivel. Apareceu na hora em que `irAba()` ganhou uma acao de
verdade (fechar a gaveta de configuracao): a gaveta fechava sozinha no
instante seguinte ao clique que a abriu.

## R84 · A gaveta cobria o botao de emergencia  ✅

Pego pelo proprio teste que eu tinha escrito errado: ele conferia
`isVisible()`, e visivel nao e clicavel -- um veu por cima deixa o botao
perfeitamente visivel e completamente morto. O teste passou a perguntar
**quem esta no ponto onde o dedo vai encostar** (`elementFromPoint`), e ai
reprovou.

Cabecalho e barra de abas ficam acima da gaveta. Parada de emergencia que
exige fechar uma janela antes nao e parada de emergencia.

## R85 · Achados menores da mesma rodada  ✅

- **Teste de rele contava como hora de arco.** O teste de saida existe
  para ver o rele clicar na bancada, sem tocha nem arame; somar aqueles
  segundos faria o contador mandar trocar bico antes da hora.
- **`math.h` faltando** em tres arquivos que usam `cosf`, `fabsf` e
  `lroundf`. Funcionava por chegar de carona em outro cabecalho -- o tipo
  de dependencia que quebra na primeira reorganizacao.
- **`btCalApagar` desabilitava sem dizer por que**, o unico botao do
  painel que ainda escapava da regra.
- **O espelho do eixo do banco so existia para a junta 1**, e a etapa 1
  do assistente apontava para uma aba que deixou de existir.

## Ferramentas novas

- `testes/sanitizar.sh` -- o banco inteiro sob AddressSanitizer e
  UndefinedBehaviorSanitizer. Num ESP32 ler um vetor uma posicao alem nao
  da erro: devolve o byte que estiver la e a maquina segue com um numero
  errado que aparece meia hora depois, em outro lugar. Aqui o mesmo acesso
  para o programa e aponta a linha.
- `testes/conferir_qr.py` -- ja existia; segue rodando antes de cada
  compilacao.
- Cenario `S01` -- 2948 requisicoes com valor hostil em toda rota
  (`nan`, `inf`, `-2147483648`, `../../etc/passwd`, texto vazio) exigindo
  que elos, curso, resolucao e velocidades continuem numeros finitos e
  positivos depois.
- Cenario `T03` -- analisador de JSON estrito aplicado a toda rota, em
  sete estados da maquina.

## R86 · A reducao nao sai do angulo do encoder -- e isso e fisica  ✅

O pedido era "agora que tenho o angulo, posso achar a reducao mais
facil". A premissa nao fecha, e a conta mostra por que:

    graus da junta = voltas do motor * 360 / reducao

O angulo que o encoder mostra **ja e calculado usando a reducao**. Tirar
a reducao dele seria tirar o numero de uma conta que usa o proprio
numero. Com um sensor so, e antes do redutor, a relacao do redutor e
invisivel -- nenhum programa contorna isso.

O que o encoder da de graca, e com muita precisao, e a contagem de
**voltas do motor**. Falta UMA referencia do lado da junta. Com ela:

    reducao = voltas do motor * 360 / angulo real

E ai vem o ganho de verdade, que e grande: a medida antiga contava
PULSOS COMANDADOS. Ela erra junto com a engrenagem eletronica (se
passosPorVolta estiver errado, a reducao sai errada na mesma proporcao) e
erra junto com perda de passo. Contar voltas reais do motor nao tem
nenhum dos dois problemas.

`U01e` prova a diferenca: mede a reducao certa com o eixo **escorregando
metade do caminho**. A medida por pulsos daria o dobro.

As referencias, da melhor para a pior: esquadro de 90 graus (preciso e
todo mundo tem um), curso entre batentes (maior angulo, menor erro
relativo), volta completa (nao precisa de instrumento nenhum).

## R87 · A area util virou coisa ensinada, nao digitada  ✅

Eram dois numeros: um Y minimo e um raio morto. Descrevem mal uma mesa de
verdade, que e um retangulo, num lugar especifico, e que o operador
conhece pelos cantos e nao por coordenadas.

Agora se leva a ponta a cada canto e grava. Dali para fora o braco nao
anda -- nem por programa, nem pelas setas.

O detalhe que fazia falta pensar: a area entra na mesma conta de
GRAVIDADE dos limites de curso. Sem isso, uma ponta que parasse fora da
area ficaria presa la para sempre -- `posturaValida` bloquearia todo
movimento e o criterio de recuperacao ("nao piorar") nunca teria o que
melhorar. `U03f` prova que sempre ha um lado que melhora.

A conferencia e da PONTA, nao do cotovelo: o cotovelo passa por cima da
mesa o tempo todo e nao solda nada.

O raio morto da base continua em separado -- ele nao e da mesa, e da
mecanica, e vale mesmo com a mesa inteira ensinada.

## R88 · O desenho mostrava a intencao, nao o braco  ✅

O boneco 2D e o 3D usavam o angulo COMANDADO. Isso desenha o que o
firmware acha, nao o que o braco fez: com o eixo escorregado, a tela
continua mostrando tudo no lugar enquanto a peca sai torta.

Agora o boneco e a posicao MEDIDA, e quando as duas discordam de mais de
meio grau o comandado aparece atras como fantasma tracejado. Da para VER
o desvio. Sem leitura confiavel volta a valer o comandado, e a legenda
diz qual das duas esta na tela -- boneco que muda de significado sem
avisar e pior que nenhum.

## R89 · O 3D tinha ordem de desenho fixa  ✅

Era base, elo 1, cotovelo, elo 2 -- sempre nessa ordem. Com o cotovelo
dobrado PARA TRAS, o elo 2 esta atras do elo 1 na cena e mesmo assim era
pintado por cima dele. O braco saia recortado errado em metade das
posturas, e era isso que fazia o desenho "ter problema de design".

Agora cada peca declara a profundidade do seu ponto medio e o conjunto e
pintado do fundo para a frente. As caixas dos elos ganharam tampa nas
pontas (sem elas o elo parecia um tubo aberto visto de enfiada), as duas
laterais tambem sao ordenadas, a base virou pedestal de dois degraus e a
ferramenta virou um cone, que le como tocha.

O achatamento vertical passou de 0,62 para 0,80 -- exagero DECLARADO: um
braco de 850 mm de alcance tem 110 mm de altura, e na proporcao real nao
da para ler qual elo passa por cima de qual. O exagero e so no Z.

## R78 · O som do driver pelo RS485: possivel, mas nao adivinhando  ✅

O pedido era ligar e desligar o bip do driver pela tela em vez de ir ao
painel digitar P098. Da para fazer: Modbus tem escrita (funcao 06 e 16) e
e o mesmo fio que ja le a posicao. O que nao da e supor que "P098" seja o
registrador 98 e mandar escrever la.

Ler no registrador errado da um numero errado na tela. Escrever no
registrador errado num servo drive pode trocar a engrenagem eletronica, o
modo de controle, o sentido do eixo ou o limite de torque -- e o eixo pode
sair andando. O mapa Modbus do T3D nao esta publicado: o registrador da
posicao (90) foi achado procurando.

Entao o caminho comeca por uma ferramenta que **nao escreve**: foto dos
registradores 0 a 255, o operador muda o parametro no painel do driver,
segunda foto, e o que mudou e o endereco. Gravado o endereco, vira um
botao. A escrita avulsa existe, mas atras de modo manual, braco parado,
servos desligados, solda desligada, registrador e valor digitados e
confirmacao.

E toda escrita e conferida **relendo o registrador**. Driver que responde
"aceitei" e guarda outra coisa existe -- e sem a releitura a tela diria
"pronto" com o bip ainda tocando. O mock do banco ganhou esse driver
(`ignoraEscrita`), junto com o que recusa escrita e o que so aceita a
funcao 16, porque um caso que o mock nao sabe encenar e um caso que o
banco nao prova.

O laco de leitura continua so-leitura: a escrita e um caminho avulso, e o
cenario L05c -- que recusa `func=6` na configuracao do encoder -- continua
passando.

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

---

# Rodada 10 — encoder por Modbus

## R22 · Leitura do encoder pelos drivers  `L01`–`L05`  ✅

Aba **Encoder** nova: comandado, medido e **erro** por junta, com
gráfico do erro nos últimos instantes. Linha reta em zero = o braço foi
para onde foi mandado; degrau ou deriva = passo perdido.

Tarefa própria no core 0, mestre Modbus RTU escrito à mão (sem
biblioteca), um barramento RS485 e os dois drivers nele com endereços
diferentes. Pinos 21/22/4/26, livres no mapa.

**O registrador é configurável, e tem de ser.** O mapa Modbus do T3D não
é publicado e muda por versão de firmware; número fixo no código seria
adivinhação, e o segundo driver pode vir diferente. A descoberta se faz
com `ferramentas/teste_rs485` ou no próprio painel, digitando um
endereço e olhando o gráfico.

Três decisões que valem registro:

- **Só leitura.** Nenhuma função escreve registrador, e a rota recusa
  função Modbus que não seja 3 ou 4 (`L05c`). Um defeito que escrevesse
  num parâmetro do servo estragaria a máquina de um jeito que não se
  desfaz pela tela.
- **O ângulo comandado vem do Snapshot, não de `posicaoJ1()`.** Escrevi
  errado na primeira versão: `motores.h` é do core 1, e esta tarefa roda
  no core 0. É exatamente a regra de ouro do projeto, e o Snapshot existe
  para isso.
- **Leitura velha para de valer.** Passado `ENC_IDADE_MAX_MS`, o valor
  fica inválido em vez de a tela mostrar erro calculado em cima de dado
  morto (`L04b`).

## R23 · Espera ocupada no core 0, achada pelo mock  ✅

O laço de recepção do Modbus era `while (millis() < limite)` sem pausa
nenhuma. Em hardware isso queima até 60 ms de core 0 por leitura — o
mesmo core do servidor web.

No banco ficou impossível de ignorar: `millis()` só anda quando alguém
chama `delay()`, então o laço **travou o banco inteiro**. O mock não
"falhou o teste": ele deixou o defeito impossível de despachar.
Corrigido com uma pausa de 50 µs entre olhadas — um caractere a 19200
leva 570 µs, então não se perde byte.

## O mock de UART tem um escravo Modbus dentro

`mocks/HardwareSerial.h` não é um cano mudo: ele entende o quadro que o
firmware manda e responde como um driver responderia, com CRC de
verdade. Dá para encenar driver mudo, exceção, CRC corrompido e ordem de
palavras trocada. É o que permite testar o mestre Modbus inteiro sem
hardware — inclusive o caso da palavra baixa primeiro (`L02`), que é o
defeito clássico e é invisível até você ver os dois valores lado a lado.

## Cobertura

| banco | rodada 9 | agora |
|-------|----------|-------|
| firmware | 141 / 0 | **160 / 0** |
| interface | 80 / 0 | **89 / 0** |

---

# Rodada 11 — o encoder achado, e por que não estava lendo

## R24 · O registrador  ✅  `L06`

O operador rodou o diagnóstico e trouxe os números. Eles respondem tudo:

- **Função 4**, não 3. A função 3 é a tabela de parâmetros — reconhecível
  pelos pares simétricos (+250 / −250, +80 / −80) e pelo `0x5E07`
  repetido trinta vezes como preenchimento. Nada ali muda com o eixo.
- **Registrador 5 (palavra baixa) + 6 (palavra alta)**, palavra baixa
  primeiro. Girando à mão, o par foi de 143 535 para 283 363 —
  139 828 contagens, **1,067 volta** num encoder de 17 bits. É o que uma
  volta à mão parece; nenhuma outra resolução fecha.
- Os registradores 20 e 34 são espelhos do mesmo contador (20 tem
  deslocamento fixo de 28 546; 34 é a mesma leitura um instante depois).

Viraram o padrão de fábrica. E um `0` guardado no NVS por uma versão
anterior passou a ser tratado como "nunca configurado", senão a máquina
do operador continuaria perguntando ao registrador 0 para sempre.

## R25 · Duas corridas entre núcleos que eu tinha deixado passar  ✅

**`encoderReconfigurar()` reabria a UART do core 1.** Ele é chamado pelo
handler de `CMD_APLICAR_ENCODER`, que roda no core 1; a UART pertence à
tarefa do core 0. `rs.end()` / `rs.begin()` por baixo de uma leitura em
andamento corrompe o quadro. Agora o core 1 só levanta
`pedidoReabrir` e a tarefa reabre no começo do próximo ciclo — o mesmo
padrão de `redePedidoReconectar`.

Essa é a segunda vez nesta funcionalidade que escrevi código de core 0
chamando coisa de core 1 (a primeira foi `posicaoJ1()`). A regra existe
justamente porque é fácil de errar.

## R26 · "Não está lendo nada" tinha de dizer o quê  ✅

A célula do medido mostrava `sem leitura` para cinco causas diferentes.
Agora carrega o motivo: `sem resposta`, `quadro corrompido`,
`registrador recusado`, `formato inesperado`. **`registrador recusado` é
notícia boa** — quer dizer que o driver está lá e respondeu; só o
endereço está errado.

Mesma família do botão mudo da rodada 2: um estado que não explica a si
mesmo faz o operador concluir que o sistema está quebrado.

## R27 · As duas rodinhas

A pedido: dois mostradores circulares, um por junta.

- Ponteiro **grosso** = onde o encoder diz que o eixo está.
- Ponteiro **fino** = onde o firmware mandou.
- A abertura entre os dois **é** o erro, sem ler número.
- No centro, um disco gira com a volta do motor (contagem módulo uma
  volta). É a prova visual de que a leitura está viva: braço andando e
  disco parado quer dizer que o dado morreu.

A aba foi para o fim da barra.

## Uma vírgula

A entrada nova de `ABAS` ficou sem vírgula depois da anterior. Em
JavaScript `[...]["enc",...]` não é erro de sintaxe: é **indexação**. O
array virou cinco elementos com o último `undefined`, e a página inteira
morreu em `a[0]`. O banco de interface pegou na primeira execução, antes
de chegar ao operador.

## Cobertura

| banco | rodada 10 | agora |
|-------|-----------|-------|
| firmware | 160 / 0 | **163 / 0** |
| interface | 89 / 0 | **90 / 0** |

---

# Rodada 12 — "0 leituras, 222 falhas"

O operador ligou **um** driver, abriu a aba Encoder e viu as duas juntas
com o mesmo número: *0 leituras, 222 falhas*. O mesmo motor, o mesmo
driver e a mesma fiação respondiam sem erro nenhum ao programa de
bancada `ferramentas/teste_rs485`.

Quando o teste passa e a máquina falha, a diferença não está no motor:
está no que o sistema faz **diferente** do teste. Achei cinco.

## R28 · A pergunta que ninguém nunca tinha feito ao driver  ⚠️  `L08`

> **Corrigido depois.** O operador mandou o código que ele de fato usou:
> os modos 4 e 6 lêem **8 registradores por pergunta**, e funcionam. O
> driver aceita leitura múltipla, então isto **não era** a causa do
> "222 falhas". O recuo automático descrito abaixo ficou no código como
> robustez — não como conserto. A investigação seguiu em R34–R36.


A posição tem 32 bits, dois registradores. O firmware pedia **os dois de
uma vez** — uma pergunta, resposta atômica, mais barata. O programa de
bancada que funcionou **nunca** fez isso: ele lê **um registrador por
pergunta**, sempre.

Ou seja: a única forma de perguntar comprovada neste driver era a que o
sistema não usava. E se o T3D recusa a leitura dupla, o resultado é
exatamente o que o operador viu — falha em cima de driver perfeito.

Não dá para provar num escritório qual das duas o driver aceita. Então o
sistema deixou de apostar:

- começa na **dupla** (atômica, barata);
- se ela levar exceção, formato errado, ou falhar quatro vezes seguidas,
  cai sozinho para **um registrador por vez**;
- se nem essa responder, volta a tentar a dupla — senão uma máquina com
  fio partido ficaria presa na forma cara, triplicando perguntas no
  vazio, e quando o fio voltasse ganharia a forma errada.

Duas perguntas não são atômicas: a palavra baixa pode dar a volta entre
uma e outra e a posição saltaria 65 536 contagens que nunca existiram.
Por isso a forma simples lê **alta, baixa, alta** e só aceita o par
quando a palavra alta não mudou no meio. Quando muda, não conta como
defeito: `MOTIVO_VIRADA`, "contagem virou no meio", e o próximo ciclo
pega o par inteiro.

A tela diz qual das duas está em uso, em texto.

## R29 · A espera curta demais  ✅

`ENC_TIMEOUT_MS` era **60 ms**. Nenhum caminho de leitura do programa de
bancada usa menos que **80**, e o que o operador rodou com sucesso usa
**150**. Uma resposta que chega em 90 ms é resposta boa; com 60 ms de
prazo ela virava "sem resposta". Passou para 150 ms — o número que já
tinha funcionado na máquina dele, não um chute.

E 150 ms de espera **ocupada** seria pior que o defeito: `delayMicroseconds`
não devolve o núcleo, e o núcleo 0 é o mesmo do servidor web — o painel
engasgaria a cada driver lento. Agora, enquanto espera o **primeiro**
byte, a tarefa dorme com `vTaskDelay(1 ms)`; a UART tem fila em hardware
e não perde nada. Depois que o quadro começa, volta a olhar de 50 em 50
µs, que é o que enxerga o silêncio de 3,5 caracteres.

## R30 · Três diferenças menores, todas reais  ✅

- **`abrirLinha()` sem pausas.** O programa de bancada faz `end()`,
  `delay(5)`, `begin()`, `delay(5)`. O firmware fazia `end()` e `begin()`
  colados; o driver de UART do core precisa de tempo para largar e
  retomar os pinos.
- **Prioridade da tarefa igual à do servidor web (1).** Entre
  `rs.flush()` e baixar o DE existe uma janela de menos de um
  milissegundo em que ainda somos nós dirigindo a linha. Com prioridade
  igual, o escalonador troca de tarefa no tique de 1 ms bem dentro dessa
  janela: o driver responde, o nosso DE ainda está alto, e o quadro morre
  na colisão — sempre no mesmo ponto. Subiu para 2.
- **A junta 2 nascia perguntando.** Registrador padrão 5 no endereço 2,
  com um único driver na bancada: metade do barramento gasta em tempo
  esgotado, e a tela cheia de "falha" que não é falha. Agora nasce com
  registrador **0 = não ligada**, e a tela diz *não ligada* em vez de
  contar falha.

## R31 · Contador de falha não é diagnóstico  ✅  `L07`

"222 falhas" não diz nada. O que resolveu o caso na bancada foi **ver os
bytes**. Então a máquina passou a mostrar os mesmos bytes: o último
quadro trocado, em hexadecimal, no painel e em `/api/encoder`.

Com ele, o operador separa sozinho os dois casos que exigem consertos
opostos:

| o que aparece depois da seta de volta | o que é |
|---|---|
| `(silencio)` | ninguém respondeu — fio A/B, DE/RE, endereço |
| bytes, mas sem leitura | respondeu outra coisa — função ou registrador |

## R32 · O HTTP 200 que era recusa  ✅  `K02`

O banco pegou: logo depois de trocar de etapa na calibração, `/api/sentido`
respondia **200** e o comando era recusado pelo núcleo 1 em seguida. O
servidor decide olhando o `Snapshot`, que era publicado a cada 40 ms —
uma cópia velha por uma fração de segundo.

40 ms de atraso é irrelevante para posição e velocidade, que é para o que
esse intervalo existe. Não é irrelevante para **modo** e **etapa**, que
são justamente o que decide se um comando pode entrar. Agora troca de
modo ou de etapa publica na hora; o resto continua a 40 ms.

## R33 · Dois mocks que mentiam  ✅

Da mesma família da rodada 7:

- **`vTaskDelay` era vazio.** No ESP32 ela dorme e o relógio anda. Vazia,
  um laço que espera por tempo giraria para sempre no banco — o mock
  esconderia exatamente o defeito que deveria mostrar.
- **O escravo Modbus não era uma tabela de registradores.** Só sabia
  responder à pergunta que o firmware fazia. Agora tem os dois
  registradores de verdade, atende leitura de um ou de dois, e sabe
  encenar o driver que **recusa** a pergunta dupla — sem isso o `L08`
  não existiria.

E o barramento passou a ser reiniciado entre cenários, como o NVS e o
sistema de arquivos já eram: um cenário que terminava com o driver mudo
deixava o seguinte gastando o tempo esgotado de cada leitura, e o relógio
do banco corria mais rápido que o movimento. Quatro cenários de
calibração falharam por isso, nenhum deles com defeito nenhum.

## Cobertura

| banco | rodada 11 | agora |
|-------|-----------|-------|
| firmware | 163 / 0 | **171 / 0** |
| interface | 90 / 0 | **93 / 0** |

---

# Rodada 13 — "não sei o motivo ainda, pois nesse código lia normalmente"

O operador mandou o programa que de fato usou. Ele **mata a hipótese
R28**: os modos 4 e 6 lêem **8 registradores por pergunta** e funcionam,
então o driver aceita leitura múltipla sem problema.

Isso é um resultado bom, não um desperdício: fecha uma porta e obriga a
procurar no que sobrou. E o que sobrou é a diferença que nenhuma leitura
de código descarta — **o programa de bancada está sozinho na placa; o
sistema não está.**

## R34 · A janela entre o último bit e baixar o DE  ✅  `L09`

Depois de `rs.flush()` o firmware espera dois tempos de caractere
(~1 ms a 19200) antes de baixar o DE — margem para o último bit sair do
registrador de deslocamento. Durante essa espera **o MAX485 continua
dirigindo a linha**. Se o driver responder dentro dela, a resposta
colide com o nosso próprio transmissor e some.

No programa de bancada essa janela é respeitada: nada mais roda na
placa. No sistema há Wi-Fi, servidor web, cartão e as interrupções dos
geradores de pulso — **tudo no mesmo núcleo**. Qualquer um deles pode
esticar a janela, e o Wi-Fi do ESP32 roda em prioridade 23: subir a
tarefa do encoder para 2 (R30) não protege contra ele.

Não dá para consertar isso com prioridade nem com seção crítica. Dá para
consertar tirando o software do caminho: a UART entra em **RS485
meio-duplex**, o DE vira a linha **RTS do periférico**, e o *hardware*
o baixa no fim do bit de parada. Nenhuma tarefa, interrupção ou pausa do
rádio alcança isso.

No modo meio-duplex o periférico desliga a recepção enquanto transmite,
então não há eco para descartar e o RE fica sempre ouvindo. **Não muda
nada na fiação.** A chave na aba Encoder volta ao controle por GPIO sem
regravar firmware, caso alguma montagem não goste.

## R35 · Atualizar o firmware não apaga o NVS  ✅  `L10`

`carregarConfiguracoes()` lê a configuração de encoder do NVS com o
padrão novo apenas como *fallback*. Ou seja: **o que uma versão anterior
gravou continua valendo depois da atualização** e ganha do padrão medido.

Quem rodou uma versão anterior deste projeto — que apontava para a faixa
`0x1000` na função 3 — ficou perguntando no registrador errado para
sempre, sem nada na tela dizendo isso. Falha determinística, 222 de 222,
exatamente o sintoma.

O cenário `L10` encena a atualização: grava a configuração velha no NVS,
recarrega, e confirma que ela ganha. Depois aperta o botão novo,
**"Voltar aos padrões medidos"**, e confere que tudo volta ao que foi
medido nesta máquina — e que fica *gravado*, senão o defeito voltaria no
próximo boot.

## R37 · O autoteste do sketch, por dentro do sistema  ✅  `L11`

O operador gravou e a tela disse **"sem resposta"** — zero bytes de
volta. A essa altura já não dava para continuar deduzindo: o programa de
bancada prova a fiação com o ESP32 **sozinho na placa**, e a pergunta que
importa é se a linha funciona *aqui dentro*, com tudo mais rodando.

Então o autoteste veio para dentro do firmware, no botão **"Testar a
linha agora"**. Três passos, com os bytes crus de cada um:

1. **Eco** — deixa o receptor ligado enquanto transmite e vê se os
   próprios bytes voltam. **Não precisa do driver ligado.** Se voltarem,
   ESP32↔MAX485 está bom dentro do sistema e o que sobra é o barramento
   ou o tempo. Se não, o problema nem chegou no par A/B.
2. **Sondagem** do registrador 0 nas funções 3 e 4 — é como o programa de
   bancada acha o driver. Até **exceção** é prova de vida.
3. A pergunta de verdade, com o registrador configurado.

Roda na tarefa do encoder, no núcleo 0: mexe no modo da UART e nos pinos
do transceptor, e fazer isso de outro núcleo por baixo de uma leitura em
andamento corromperia o quadro. `L11d` confere que, terminado o teste, a
leitura normal volta sozinha — um diagnóstico que deixa a máquina pior
não serve.

### O mock que virou hardware de verdade

A primeira versão do `L11` tinha um `g_uart.eco = true` que o banco
ligava à mão. Ele ficou ligado durante a sondagem, os nossos próprios
bytes voltaram na frente da resposta do escravo, e o cenário reprovou.

Era o banco mentindo, mas apontando para algo real: **o eco não é um
botão, é consequência do que o firmware faz com o pino RE.** O mock
passou a olhar o pino (`g_pinSaida[pinoRe] == LOW`) e a respeitar o modo
RS485 meio-duplex, onde o periférico desliga a recepção ao transmitir e
não há eco por mais que o RE esteja em baixo. Terceira vez que a regra da
rodada 7 se paga.

## R36 · O que ainda decide o caso

O quadro cru (R31) já está na tela. Ele parte o que sobrou em dois, e os
dois pedem consertos opostos:

| na tela | onde está o problema |
|---|---|
| `(silencio)` | ninguém respondeu — R34 (colisão no DE), fio, ou endereço |
| bytes, mas sem leitura | respondeu outra coisa — R35 (registrador velho no NVS) |

## Cobertura

| banco | rodada 12 | agora |
|-------|-----------|-------|
| firmware | 171 / 0 | **187 / 0** |
| interface | 93 / 0 | **93 / 0** |

---

# Rodada 14 — o registrador, achado de verdade

O operador mandou o log completo do programa de bancada, que ele mesmo
ampliou com um modo de **caçada** (lê tudo, gira o eixo, compara). Esse
log fecha a questão do endereço.

## R38 · Registrador 90 na função 3, e não 5 na função 4  ✅  `L06`

A caçada girou o eixo à mão e comparou os 256 registradores da função 3:

```
0x005A (90)   61346 ->  39440   (variou -21906)
0x005B (91)       0 ->      1   (variou +1)
```

Um varia muito, o vizinho varia ±1: é o par de 32 bits. Montando com a
**palavra baixa primeiro**, 61 346 → 104 976 = **+43 630 contagens**, ou
1/3 de volta num encoder de 17 bits. Ao contrário, o número anda 1,4
bilhão para trás — que não é giro nenhum. Então **90 = baixa, 91 = alta,
baixa primeiro**.

**Segunda prova, independente, no mesmo log:** duas varreduras completas
da função 3, uma atrás da outra, sem tocar em nada. Idênticas em 255
registradores; diferem **só no 90** (36 998 → 37 000). É o único que anda
sozinho.

O padrão de fábrica estava em **função 4, registrador 5** — vindo de uma
leitura anterior que eu interpretei mal. Na função 3 esses mesmos
endereços valem 50 e 25, parâmetro parado. Corrigido para função 3,
registrador 90. O `L06c` passou a usar os números exatos da máquina, e o
`L06d` novo confere que a montagem ao contrário dá absurdo — porque é
assim que o operador reconhece o engano na tela.

## R39 · A caçada saiu da bancada e entrou na máquina  ✅  `L12`

O modo de caçada é bom demais para ficar só no programa de bancada: é o
**único jeito honesto** de achar o endereço num driver cujo mapa não está
publicado. Agora está na aba Encoder, em dois botões:

1. **"Procurar o registrador"** — lê a faixa 0..255 e anota.
2. o operador move o braço.
3. **"Comparar agora"** — lista o que mudou e aponta qual é a palavra
   baixa.

`L12d` confere o caso que mais importa para a confiança: **sem mover o
braço, o sistema diz "nenhum registrador mudou"** em vez de chutar um
endereço. E `L12f`, que a leitura normal volta sozinha depois.

## R40 · O mock só servia dois registradores  ✅

A caçada reprovou de cara no banco: o escravo do mock só respondia ao
endereço exato que o firmware costumava pedir. Um driver de verdade
responde a **tabela inteira** — no log do operador, os 256 endereços
responderam, e é justamente por isso que dá para comparar. Sem essa
fidelidade o `L12` não existiria.

Agora o escravo serve qualquer endereço: valor de parâmetro **estável**
(derivado do endereço, para que duas leituras seguidas sejam iguais) em
tudo, menos no par da posição, que anda com o eixo. E `lerRegs` ganhou
buffer de 32 bytes: um bloco de 8 registradores volta com 21, e o buffer
de 16 cortava a resposta boa fazendo-a virar "formato inesperado".

Também: o estado da caçada sobrevivia ao `setup()` no banco, e uma
marcação de um cenário valia no seguinte. Mesma família dos buffers de
programa e trajetória — zerado junto com o resto.

## Cobertura

| banco | rodada 13 | agora |
|-------|-----------|-------|
| firmware | 187 / 0 | **194 / 0** |
| interface | 93 / 0 | **93 / 0** |

---

# Rodada 15 — o log do firmware finalmente falou

O operador mandou um log que comeca com o **firmware rodando**, e nao com
o programa de bancada. Duas coisas ali.

## R41 · A enxurrada de `/connecttest.txt`  ✅  `J04`

```
[WEB] Rota desconhecida: /connecttest.txt      (doze vezes)
[MSG] Conexao perdida: movimento e solda interrompidos
```

Isso e o **Windows perguntando se a rede tem internet**. Todo sistema faz:
Windows pede `/connecttest.txt`, Android `/generate_204`, iPhone
`/hotspot-detect.html`. Como a maquina responde a qualquer nome (o DNS de
captura da rodada 8), a pergunta cai no `onNotFound`.

Responder **404 era o pior dos mundos**: o sistema conclui "esta rede nao
tem internet", repete a pergunta de segundos em segundos — a enxurrada —
e o Windows chega a largar a rede sozinho. O `[MSG] Conexao perdida` logo
depois nao e coincidencia.

Agora essas sondas levam **302 para 192.168.4.1**. O sistema entende que e
uma rede com portal e **abre o painel sozinho** na tela: o operador nao
precisa nem saber o que e um IP.

O destino e o numero, nao `robo2dof.local` — o nome depende de mDNS, que o
Windows so resolve com Bonjour, e e justamente o Windows que mais insiste.
`J04e` confere que rota inexistente que **nao** e sonda continua 404 com
log: esconder tudo seria trocar uma enxurrada por um silencio que engana.

## R42 · O diagnostico estava no lugar errado  ✅

Eu tinha posto o quadro cru e o autoteste **no painel**. Mas o operador
acompanha a maquina pelo **monitor serial** — e la que ele roda o programa
de bancada, e e de la que vem todo log que ele me manda. Pedir que ele
abrisse o painel para ler o diagnostico era pedir que trocasse de
ferramenta no meio da investigacao.

Agora sai nos dois lugares:

```
[ENC] Modbus em 19200 bps, funcao 3, registrador 90, id 1, DE por hardware
[ENC] Junta 2 nao ligada (registrador 0).
[ENC] junta 1 sem leitura -- junta 1  2 registradores  -> 01 03 00 5A 00 02 …   <- (silencio)
[ENC] junta 1 lendo: bruto 104976
```

Uma linha a cada 5 s enquanto falha, uma quando volta a ler. A linha de
boot passou a dizer **registrador e modo do DE**, que sao exatamente os
dois ajustes em disputa.

## R43 · Terceira confirmacao do registrador  ✅

A cacada nova do operador, com o eixo em outra posicao:

```
0x005A (90)   37178 -> 13957   (variou -23221)
0x005B (91)       0 ->     1   (variou +1)
```

Mesmo par, mesmo comportamento. O padrao da rodada 14 esta certo.

## R44 · O mock descartava cabecalho  ✅

`sendHeader` do mock nao guardava nada. Um redirecionamento passaria por
"resposta vazia" e o `J04` nao teria como ver para onde o navegador foi
mandado. Quarta vez que a regra da rodada 7 se paga.

## Cobertura

| banco | rodada 14 | agora |
|-------|-----------|-------|
| firmware | 194 / 0 | **199 / 0** |
| interface | 93 / 0 | **93 / 0** |

---

# Rodada 16 — três caçadas, e o par provado

O operador mandou **três caçadas seguidas**, em posições diferentes do
eixo. Juntas elas fecham a questão de um jeito que nenhuma sozinha fecha.

## R45 · A prova por continuidade  ✅

Montando 90/91 como 32 bits com a palavra baixa primeiro:

| caçada | de | para | variou |
|---|---|---|---|
| A | 9 361 | 15 841 | +6 480 |
| B | 15 842 | 124 571 | +108 729 |
| C | 124 574 | 42 069 | −82 505 |

**As emendas.** A termina em 15 841 e B começa em 15 842. B termina em
124 571 e C começa em 124 574. O valor é **contínuo entre caçadas
separadas** — coincidência não é contínua três vezes.

E o mecanismo aparece: na caçada B o registrador 90 deu a volta (passou
de 65 535) e o 91 subiu de 0 para 1. Na caçada A, que andou pouco, o 91
nem se mexeu. É exatamente o que uma palavra baixa e uma alta fazem.

## R46 · 92 e 93 não são posição, e estavam atrapalhando  ✅  `L12`

Nas caçadas apareceram também 92, 93 e 94. Eles enganam porque mudam
junto com o eixo. Mas o comportamento denuncia: andam **juntos** (os dois
+6, depois os dois −23) e vão para valores negativos (65 530 = −6). É
**erro de seguimento** e velocidade. A diferença prática: voltam para
perto de zero quando o eixo para; a posição não volta.

Listar o que mudou e mandar o operador deduzir era jogar esse problema no
colo dele — e na caçada A o palpite "o que variou mais" teria sido tão
válido para 92 quanto para 90.

Agora a caçada **prova**, no programa de bancada e no painel: monta cada
par `r`/`r+1` das duas maneiras e só aponta quando a montagem certa é
pelo menos **8× mais mansa** que a invertida. Um par que não é de 32 bits
não passa. `L12g` confere o caso que importa: com a posição parada e
outro registrador mexendo, **nenhum par é apontado**.

## R47 · Dois modos que faltavam na ferramenta  ✅

- **`8 90` — contagens por volta.** É o único número do encoder que não
  dá para descobrir olhando, e sem ele a leitura não vira grau. Marca,
  uma volta completa no eixo do motor, marca de novo. Encaixa no valor
  redondo mais próximo dentro de 3%: uma volta à mão nunca fecha exata, e
  é mais provável que a mão tenha ficado torta do que o encoder ter um
  número quebrado.
- **`9 90` — CSV da posição** (`ms,contagem,delta`), para colar numa
  planilha. É assim que se enxerga passo perdido, folga e ruído — coisa
  que número na tela não mostra.

## R48 · O autoteste mentia no menu logo depois  ✅

O modo 1 passeia por todas as velocidades e **não devolvia a linha como
estava**: terminava em 115200 8O1, e o menu logo abaixo de "MODULO OK"
anunciava essa velocidade como se fosse a escolhida. Aparece em todos os
logs do operador e já tinha mandado a investigação para o lado errado uma
vez (rodada 11). Agora guarda e restaura.

## Cobertura

| banco | rodada 15 | agora |
|-------|-----------|-------|
| firmware | 199 / 0 | **200 / 0** |
| interface | 93 / 0 | **93 / 0** |

---

# Rodada 17 — a caçada que eu quebrei, e o painel que ficou aberto

## R49 · Eu troquei um palpite por uma resposta confiante e errada  ✅  `L12`

Na rodada 16 fiz a caçada "provar" o par em vez de listar candidatos.
Na máquina do operador ela apontou **94/95**. Está errado.

O log dele mostra o porquê, duas linhas abaixo:

```
0x005A (90)   62928 -> 52971  (variou  -9957)
0x005E (94)       0 -> 65535  (variou +65535)   <- o maior salto da lista
```

E o monitor logo em seguida, no par apontado:

```
reg 94 = 65535 o tempo todo        (constante = -1)
reg 95 = 0, 5, 42, 0, 65527, ...   (pula para os dois lados)
```

`0 -> 65535` **não é** um salto de +65535: é **−1**. Meu critério
ranqueava pelo maior salto lido **sem sinal**, então a menor variação
possível do barramento virou a maior da lista. Trocar "aqui estão os
candidatos" por "é este aqui" só vale se o "este aqui" estiver certo —
uma resposta confiante e errada é pior que a lista.

**O critério novo é o sentido.** O operador gira sempre para o mesmo
lado, então a posição anda sempre para o mesmo lado — e agora são **dois
giros**, com uma leitura no meio. Um par só é apontado se andou na mesma
direção nas duas vezes. Erro de seguimento e velocidade oscilam e voltam
para perto de zero; posição não volta. A diferença é subtração em
complemento de dois, que ainda resolve a volta da palavra baixa sozinha.

`L12d` encena exatamente o caso do operador: um registrador que vai a
`65535` e depois volta, com a posição parada. Nenhum par é apontado.

A lista também passou a mostrar o valor **com sinal** quando o número
passa de 32768. Só essa coluna já teria evitado o engano.

## R50 · A coluna do Encoder, aberta  ✅

Pedido do operador: *"a aba do encoder deve ser do outro lado da tela de
modo a poder ficar aberta"*. Ele está certo pelo motivo certo — a leitura
do encoder existe para ser acompanhada **enquanto** se mexe no resto, e
trocar de aba para olhar o erro é perder o momento em que ele acontece.

Acima de 1300 px a `.corpo` vira três colunas
(`380px | mesa | 400px`) e o Encoder mora na primeira, sempre aberto. O
botão de aba dele some: a coluna já está na tela. Abaixo disso não há
largura honesta para três colunas, e ele volta a ser aba.

## R51 · Análise detalhada  ✅

O painel guardava só o **erro**. Isso não responde a primeira pergunta de
sempre: *"o erro subiu porque o braço andou ou porque a leitura falhou?"*.
Agora guarda a amostra inteira, e mostra:

- **gráfico da posição medida** — buraco na leitura fica buraco no traço,
  não vira reta;
- **leituras por segundo medidas** na janela, não o período configurado;
- **erro médio** e **oscilação** — média alta com oscilação baixa é
  desalinhamento e se corrige na referência; oscilação alta é folga ou
  ruído, e a referência não resolve;
- tabela das últimas 40 amostras, e **CSV com a janela inteira**.

Junta sem registrador mostra `--`, não zero: estatística inventada sobre
um driver que não existe é pior que espaço em branco.

## R52 · Dois testes que dependiam de posição na tela  ✅

Os testes de interface abriam a seção do Encoder **pelo índice**
(`i === 0`, `i === 1`). Acrescentar seções novas quebrou os dois, e o
segundo derrubou a suíte inteira com um `TimeoutError` que não tinha nada
a ver com o que ele testava. Agora abrem **pelo conteúdo** (a seção que
contém o campo, a seção que contém o botão): mexer no painel não pode
quebrar um teste que não é sobre isso.

E o `.ino` do programa de bancada continua sendo conferido com
`-Wall -Wextra` contra um shim de Arduino antes de sair daqui.

## Cobertura

| banco | rodada 16 | agora |
|-------|-----------|-------|
| firmware | 200 / 0 | **200 / 0** |
| interface | 93 / 0 | **107 / 0** |

---

# Rodada 18 — o monitor do operador, e o nome

O operador escreveu um monitor de encoder proprio, que funciona na
bancada dele, e pediu para trazer para o sistema.

## R53 · Os derivados vao para o FIRMWARE, nao para o navegador  ✅  `L13`

Velocidade, RPM, sentido, delta, passos acumulados, inversoes e faixa
percorrida. O monitor dele calcula tudo no laco principal; aqui isso mora
na tarefa do encoder.

A razao e a regua: a tarefa le a **20 Hz** e o painel consulta a **4 Hz**.
Calcular velocidade no navegador seria medir com uma regua cinco vezes
mais grossa que a disponivel, e perder toda a variacao entre consultas.
Quem tem os instantes de verdade e quem le.

Tres coisas que o monitor dele nao tinha e que mudam o resultado:

- **Zona morta de 3 contagens no sentido.** Um encoder de 17 bits treme um
  ou dois passos parado. Sem zona morta esse tremor vira "inverteu"
  dezenas de vezes por segundo, e o contador de inversoes -- que serve
  para achar folga -- nao vale nada. `L13h` confere.
- **Sem leitura, a velocidade zera.** Manter a ultima faz a tela dizer que
  o eixo continua girando depois que o fio caiu. `L13i`.
- **Velocidade em float.** `(delta * 1000) / dt` em inteiro estoura com
  meio milhao de contagens, que um eixo rapido faz em um segundo.

## R54 · O eixo do mock nao girava entre as leituras  ✅

O `L13` reprovou com `delta 0` e velocidade zero, e o codigo estava
certo: o mock so mudava de posicao quando o teste mandava, e como tres
leituras cabem dentro de um passo, a ultima comparacao dava zero. Um eixo
de verdade gira **entre** as leituras.

O escravo do banco ganhou `girar(contagens_por_segundo)`: a posicao passa
a ser calculada a partir do relogio. Sem isso o banco reprovaria um
calculo correto -- que e o pior tipo de teste.

## R55 · Estaticas dentro da funcao, de novo  ✅

"Passos acumulados" apareceu com **129 milhoes** num teste de 32 mil. Duas
causas, as duas da mesma familia:

1. O instante e o valor da leitura anterior eram `static` **dentro** de
   `publicar()`. No ESP32 o boot zera; no banco elas sobrevivem ao
   `setup()`, e o primeiro delta de um cenario saia medido contra o
   cenario anterior. Foram para escopo de arquivo e entraram no
   `encoderReiniciarTeste()`.
2. O ajudante `prepararEncoder()` aplicava a configuracao **antes** de
   arrumar o escravo, entao o sistema lia outro registrador por um
   instante e o salto entre esse valor e o primeiro de verdade entrava
   nos passos acumulados. Ordem invertida.

E, no firmware, `encoderReconfigurar()` passou a zerar os derivados:
trocar o registrador troca o **significado** do numero.

O terceiro numero errado era do teste, nao do codigo: eu esperava 32 000
passos, e a resposta certa era 70 600 -- o relogio do banco anda mais que
a contagem do laco sugere. O teste agora ancora a expectativa na **faixa
que o proprio encoder registrou** (ida e volta = dois cursos), em vez de
um valor cravado.

## R56 · RoboCNC vira Robo2dof  ✅

A pasta do sketch, o `.ino`, o `#define` do banco (`ROBO2DOF_TESTE`), os
caminhos dos scripts, o manifesto do painel e os documentos. `git mv`
preserva o historico dos arquivos.

O nome do **repositorio** no GitHub e outra coisa, e so o dono muda, nas
configuracoes do repositorio.

## Cobertura

| banco | rodada 17 | agora |
|-------|-----------|-------|
| firmware | 200 / 0 | **210 / 0** |
| interface | 107 / 0 | **112 / 0** |

---

# Rodada 19 — o DE que nunca subia

O operador: *"ate o momento apenas aparece o visual e nada de
funcionamento ou leitura"*. E pediu o modulo do encoder refeito colado no
monitor dele, que funciona.

## R57 · O DE nunca subia  ✅  `L09`

Na rodada 13 eu troquei o controle do DE por **RS485 meio-duplex por
hardware**: o periferico da UART passa a dirigir o DE pela linha RTS e o
baixa no fim exato do ultimo bit. O raciocinio estava certo — a janela
entre `flush()` e baixar o DE e mesmo o ponto fraco num sistema com
Wi-Fi e interrupcoes no mesmo nucleo.

O problema e o que eu fiz com o caminho antigo:

```c
static void modoTransmissao() {
  if (configEncoder.deHardware) return;   // <-- nao levanta o DE
  digitalWrite(PIN_RS485_RE, HIGH);
  digitalWrite(PIN_RS485_DE, HIGH);
}
```

Com o modo por hardware ligado — que era o **padrao que eu deixei** — o
firmware nao levanta o DE, confiando no periferico. Se `uart_set_pin` /
`uart_set_mode` nao pegam naquela placa, o DE fica onde estava: **baixo**.
O MAX485 nunca dirige o barramento, o quadro nao sai no fio, e o driver
nao tem o que responder. Silencio absoluto, deterministico, para sempre.

Um mecanismo mais fino que nao liga vale menos que um grosseiro que
funciona. O controle voltou a ser por GPIO, na sequencia exata do monitor
do operador: `DE+RE alto -> 50 us -> escreve -> flush -> 1000 us -> DE
baixo`.

**Por que nenhum cenario pegou:** nenhum olhava o pino. Todos olhavam a
resposta, e o escravo do banco respondia independentemente do DE — porque
o mock nao tem transceptor. O `L09` novo conta as **subidas do DE** e
reprova se ele parar de subir.

## R58 · Menos mecanismo, mais leitura  ✅  `L08`

Saiu tambem o recuo automatico para "um registrador por vez" (R28). O log
do operador ja tinha desmentido a premissa: os modos 4 e 6 do programa de
bancada leem **oito** registradores por pergunta e funcionam. O recuo so
acrescentava um jeito a mais de dar errado.

Agora e uma pergunta, dois registradores — e isso tambem e **atomico**,
sem o problema da palavra baixa dar a volta entre duas perguntas. Driver
que recuse a pergunta dupla e **reportado**, nao contornado em silencio.

`MOTIVO_VIRADA`, que so existia por causa do recuo, virou estado morto e
saiu junto.

`ENC_TIMEOUT_MS` voltou para **100 ms**, o do monitor dele. Na pratica a
espera acaba muito antes: a leitura sabe quantos bytes a resposta boa tem
e para quando o quadro fecha.

## O que NAO mudou

O registrador padrao continua **90**, nao o 94 do monitor dele. O 94 saiu
da minha cacada quebrada da rodada 16, que imprimia "No painel do robo:
registrador 94" — ele copiou a minha recomendacao errada. As tres cacadas
com continuidade, e o monitor dele mostrando 94 constante em 65535,
apontam 90. E um campo na tela, se a bancada disser o contrario.

## Cobertura

| banco | rodada 18 | agora |
|-------|-----------|-------|
| firmware | 210 / 0 | **209 / 0** |
| interface | 112 / 0 | **112 / 0** |

(um cenario a menos: o `L08` deixou de testar o recuo removido e passou a
testar o contrato de uma pergunta so.)

---

# Rodada 20 — a calibração com encoder, e a coluna que não atualizava

## R59 · Aferir a engrenagem eletrônica sem transferidor  ✅

`passosPorGrau = passosPorVolta × redução / 360`. Dois números, e o
encoder só alcança um: ele conta no eixo do **motor**, antes do redutor,
então a **redução** continua sendo declarada por quem montou a máquina.

Mas a **engrenagem eletrônica** ele mede sozinho — e é justamente a que
mais se erra: troca-se o driver, refaz-se um parâmetro, e o número
declarado deixa de bater com o que o driver faz, sem nada apontar o
culpado.

Marca, gira, `aferirPelosEncoder()`: passos andados ÷ voltas do motor. Um
número a menos para errar. Recusa medida curta (menos de um quarto de
volta mede mais o ruído que a engrenagem) e resultado implausível — gravar
uma resolução absurda estragaria a máquina em silêncio.

## R60 · Travamento: o eixo foi mandado andar e não andou  ✅  `L03`, `M04`

Antes, encostar no batente era invisível para o firmware: ele continuava
contando pulsos e o motor ficava forçando contra o ferro. Agora é
mensurável, e o sistema **para o eixo**.

Denunciar sem parar seria contar o acidente em vez de evitá-lo.

Metade do trabalho foi fazer o vigia **não** disparar à toa — um falso
positivo para o braço no meio de um cordão e estraga a peça. Quatro
condições, e `M04` testa as três negativas: movimento normal não acusa,
eixo parado não acusa (parado não está forçando nada), e **sem leitura o
vigia se cala** (cabo solto no encoder não pode parar o braço).

O `L03c` mudou de expectativa por causa disso, e para melhor: ele
verificava que o erro *crescia* com o eixo preso. Agora o erro cresce
menos, porque o sistema para o eixo antes — e `L03d`/`L03e` verificam
exatamente isso.

## R61 · A coluna sempre aberta nunca atualizava no computador  ✅

Defeito meu, da rodada 17. A consulta ao encoder era:

```js
if(abaAtual==="enc")encAtualizar();
```

Quando a coluna virou fixa no computador, o botão de aba dela sumiu — e
`abaAtual` nunca mais pode valer `"enc"` ali. Resultado: a coluna ficava
aberta na tela mostrando o dado do instante em que a página carregou.

Agora consulta quando o painel **está na tela**, não quando a aba está
escolhida. O teste novo compara o contador de leituras depois de um
segundo: se ele não anda, reprova.

## R62 · As explicações viraram opcionais  ✅

As notas em cinza ensinam quem começa e atrapalham quem opera todo dia:
elas ocupavam mais coluna que os controles. O `?` no cabeçalho esconde
todas de uma vez.

O teste confere as duas metades: as notas somem **e os controles
continuam todos lá**. Esconder texto é uma coisa; esconder botão é outra.

Com elas escondidas, "Ir para um ângulo" passou a caber na tela sem
rolar.

## Cobertura

| banco | rodada 19 | agora |
|-------|-----------|-------|
| firmware | 223 / 0 | **229 / 0** |
| interface | 115 / 0 | **121 / 0** |

---

# Rodada 21 — encoder absoluto: a maquina se localiza sozinha

O operador notou o que muda tudo: **o encoder do servo guarda a posicao
com a maquina desligada**. Empurrar o braco a mao com tudo apagado, e ao
ligar ele sabe. Isso dispensa fim de curso.

## R63 · O zero vira um NUMERO GRAVADO  ✅  `N01`

Antes o zero era "onde o braco estava quando ligou", e o operador tinha
de leva-lo a referencia toda vez. Agora e a contagem crua do encoder que
corresponde a 0 grau, gravada no NVS. Ensina-se uma vez.

No boot, assim que ha leitura boa, a contagem de passos e acertada para
bater com o encoder -- **nenhum pulso sai no fio**, so a conta muda. Dali
em diante tudo o que ja existia (limites, cinematica, programa) funciona
igual, partindo do lugar certo.

`N01d` e o cenario que importa: ensina 30 graus, desliga, **move o braco
a mao**, religa -- e a maquina le 40 graus.

## R64 · O que impede de andar sozinho  ✅  `N02`, `N03`

Mover um braco de solda ao ligar e perigoso. Cinco travas, e cada uma
tem cenario:

- **Zero nao ensinado** e o padrao de fabrica. Uma maquina recem-montada
  acreditaria que a contagem crua 0 e o zero da junta -- um numero
  arbitrario -- e iria para la sozinha. `N03a`.
- **Servos desabilitados.** Este e o bom: habilitar servos e uma acao
  explicita do operador na tela, entao o intertravamento ja existia e nao
  precisou ser inventado. `N02a`/`N02b` provam que ele se localiza e nao
  anda.
- **Sem leitura**, desiste em 5 s e avisa. Maquina que nao liga porque o
  encoder nao respondeu e pior que maquina desorientada. `N03b`.
- **Zero fora do curso calibrado**: nao vai. Furar a protecao seria pior
  que nao ir -- e ela existe justamente porque nao ha fim de curso.
- Solda ligada, ou fora do manual: nao vai.

## R65 · encoderIniciar() apagava o zero que acabara de ser lido  ✅

Defeito real, achado pelo banco. No `setup()`,
`carregarConfiguracoes()` roda **antes** de `encoderIniciar()`, e
`encoderIniciar()` fazia `memset` em `leitura[]` -- levando junto a
referencia absoluta recem-carregada do NVS. A maquina nasceria localizada
em qualquer lugar.

A referencia agora sobrevive ao memset.

## R66 · Tres correcoes de fidelidade no banco  ✅

Todas da mesma familia: o ajudante nao encenava um boot de verdade.

1. `reiniciarSistemaMantendoNvs()` nao reiniciava a maquina de estados do
   zero. Ela le a configuracao no primeiro ciclo e nao volta atras -- o
   que e certo -- mas ali a configuracao chegava **depois** do primeiro
   ciclo, coisa que no ESP32 nao acontece.
2. O escravo Modbus era configurado **depois** do religamento, e nos 50
   ms iniciais o firmware se localizava em cima de valor de outro
   registrador. Virou `religarComEncoder()`, que poe o driver no ar antes
   de a maquina de estados comecar.
3. O espelho do eixo assumia que contagem e pulsos comecam juntos. Depois
   de um boot com encoder absoluto eles nao coincidem mais: a contagem
   nasce onde o encoder disse, os pulsos nascem em zero. Ganhou uma base.

O item 3 apareceu de um jeito bonito: o `N02` reprovou porque o **vigia
de travamento** parou o braco -- corretamente, ja que no cenario o
encoder nao acompanhava o eixo. O vigia estava certo e o teste errado.

## R67 · O braco 3D com volume  ✅

Deixou de ser linha grossa: cada elo e uma caixa de tres faces (topo
claro, laterais escuras), a base e um cilindro, as juntas tem disco, e a
mesa e uma superficie com gradiente em vez de linhas soltas no vazio. A
sombra sob cada junta e o que faz a altura se ler.

## R68 · Seguir o eixo movido a mao -- e a regra que impede o disfarce  ✅

Com os servos desligados o braco fica solto e nenhum pulso sai no fio: a
contagem do firmware ficava para tras e "movi com a mao" nao dava o mesmo
resultado que "mandei ir". `seguirEixoSolto()` acerta a contagem pela
leitura do encoder.

A regra que importa e a negativa: **servo ligado, nao segue**. Com torque
o motor esta segurando a posicao; se o eixo saiu do lugar mesmo assim,
isso e perda de passo, e quem cuida disso e o assentamento. Seguir a
contagem ali esconderia o defeito e o assentamento nunca traria o braco
de volta -- seria trocar uma correcao por um disfarce. `M05a`/`M05b`, e
`M05c` para o caso sem zero ensinado, em que a leitura crua nao e angulo.

## R69 · O botao da ponteira: ensinar o caminho com a mao  ✅

Modulo `aprender.h/.cpp`. Um botao, dois gestos: segurar 1,5 s entra ou
sai do modo aprendizado, toque curto grava o ponto onde a ponta esta.
Dentro do modo o torque cai e o operador leva a ponteira com a mao.

So funciona por cima do R68: sem o seguidor, cada ponto sairia gravado no
mesmo lugar.

Tres decisoes que valem registro:

- **O braco so e solto quando as DUAS juntas sao acompanhadas.** O SON e
  um fio unico para os dois drivers -- nao existe soltar so uma. Uma
  junta solta e nao medida cai pelo proprio peso e grava ponto certo num
  eixo e errado no outro, o que e pior do que errado nos dois, porque
  parece plausivel. Faltando isso, o modo entra com torque em vez de ser
  recusado: com torque ele funciona igual, so muda quem carrega o braco.
  `P04`.
- **O torque nao volta na saida.** Habilitar servo e acao explicita em
  todo o resto do sistema, e aqui -- com a mao do operador dentro da area
  do braco -- mais ainda. `P01j`.
- **Botao preso desde o boot nao vale gesto.** Sem essa guarda um fio em
  curto soltaria o braco na hora de ligar. `P03c`.

O filtro de repique nao e detalhe: contato mecanico repica por alguns
milissegundos, e sem filtro um toque viraria meia duzia de pontos -- que
o operador so descobriria na hora de soldar. `P02` aperta o botao
alternando nivel seis vezes e exige exatamente um ponto.

## R70 · NOME_CMD fora de sincronia com o enum  ✅

Defeito real, meu, achado ao ligar o comando novo. Tres comandos foram
acrescentados a `TipoComando` numa rodada anterior sem os nomes
correspondentes em `NOME_CMD[]` -- que e indexado por `c.tipo` **sem
conferencia de faixa**. Os tres ultimos comandos (os de arquivo) liam
ponteiro fora do vetor no `Serial.printf` do log, e o travamento
apareceria longe da causa: na primeira vez que alguem salvasse um
programa no cartao.

Nenhum cenario pegou porque o log e efeito colateral, nao resultado. A
correcao nao e so completar a lista: um `static_assert` amarra o tamanho
do vetor ao ultimo valor do enum, e agora o erro e de **compilacao**.

## R71 · O guarda do JSON de status media um formato velho  ✅

Mesma familia do R70, no banco. O cenario `A10` guarda o pior caso do
`snprintf` de `/api/status` contra uma **copia** do formato -- e a copia
tinha ficado cinco campos para tras. Um guarda que mede um formato velho
mede folga que nao existe.

Agora `A10b` compara as chaves da copia com as chaves da resposta viva:
campo novo no firmware sem campo novo na copia reprova na hora.

## R72 · A emergencia estava ligada errada, e nao pararia nada  ✅

Defeito real, no unico caminho do sistema que existe para salvar alguem.

O `config.h` mandava ligar o contato NC ao **3V3** e o codigo esperava
**LOW** para acusar emergencia -- com o pull-up interno ligado. Essa
combinacao nao funciona de duas maneiras ao mesmo tempo:

- com o contato no 3V3 e o pull-up ativo, o pino **nunca** chega a LOW.
  Apertar o botao vermelho nao faria nada;
- e um fio partido tambem daria HIGH, ou seja, "esta tudo bem".

A ligacao certa e o contato NC para o **GND**, com pull-up, e
**HIGH = EMERGENCIA**:

| situacao | pino | resultado |
|---|---|---|
| solto (contato fechado) | LOW | opera normalmente |
| apertado (contato abre) | HIGH | emergencia |
| **fio partido / desligado** | HIGH | **emergencia** |

O terceiro caso e o que justifica a ligacao inteira: botao de emergencia
com cabo rompido tem de parar a maquina, nao sumir em silencio.

Nenhum cenario pegava porque o A08 encenava "apertado" com o mesmo nivel
que a ligacao errada esperava -- o teste concordava com o defeito. O novo
`P07` prova o caso que importa: corta o fio, e o torque, o movimento e o
rearme caem.

O padrao do mock (`pinMode(INPUT_PULLUP)` deixa o pino em HIGH) representa
**nada ligado**, que agora e emergencia -- entao cada cenario passa a
comecar declarando o botao instalado e solto. Isso e fidelidade, nao
concessao.

## R73 · O backup da maquina nao guardava a calibracao  ✅

O arquivo `/cfg/*.cfg` guardava velocidades, elos e protecoes, e o curso
medido das juntas ia dentro de um **comentario**, "para conferencia".
Isso fazia o arquivo parecer um backup da maquina sem ser um: restaurar
devolvia os numeros faceis e deixava o operador refazendo o assistente de
calibracao, que e de longe a parte mais demorada de por a maquina de pe.

Agora o arquivo leva `cal=1`, os limites em passos, o angulo da
referencia e o sentido de cada eixo. A marca `cal=1` distingue arquivo
novo de arquivo antigo: um backup gravado pela versao anterior nao traz
calibracao, e por isso **nao pode zerar** a que esta na maquina -- quem
restaura um backup velho espera recuperar o que ele guarda, nao perder o
que ele nao guarda. Cenarios `Q05` e `Q06`.

## R74 · O QR saiu com os bits de formato ao contrario  ✅

Escrevi um gerador de QR proprio -- a maquina nao tem internet, entao
biblioteca de CDN nao e opcao. Ele desenhava um codigo **perfeito aos
olhos** e nenhum leitor abria.

Dois defeitos, os dois invisiveis numa inspecao visual:

1. Os 15 bits de formato eram gravados do menos significativo para o
   mais, e a ordem e a contraria.
2. A segunda copia do formato divide **7 modulos na coluna e 8 na
   linha** -- eu usei 8 e 7, e o modulo `(8, n-8)` ficava sem ser escrito.

O que resolveu nao foi olhar mais: foi arrumar um **oraculo**. Os codigos
passaram a ser renderizados e lidos de volta por um decodificador de
verdade (OpenCV), e os dois defeitos apareceram em minutos. Virou o
guarda permanente `testes/conferir_qr.py`, que roda antes de cada
compilacao e cobre v1 a v10, UTF-8 e o teto de tamanho.

A licao e velha e vale registrar: **para o que nao da para conferir no
olho, arrume um leitor independente antes de escrever o codigo.**

## R75 · O espelho do eixo no banco so existia para a junta 1  ✅

Todo cenario da junta 2 tinha de cravar a posicao do escravo Modbus a
mao, o que nao e o eixo andando -- e o teste fingindo. O primeiro cenario
que exigiu assentamento na junta 2 reprovou por isso, e a reprovacao
estava certa: o banco nao tinha como encenar perda de passo naquele eixo.

O espelho agora cobre as duas juntas, cada uma com sua base e sua perda,
e a junta 2 so e espelhada quando esta no barramento -- entao os
cenarios de um driver so continuam identicos.

## R76 · O botao que apagava a calibracao nao dizia por que estava mudo  ✅

Achado pela propria varredura de interface, que exige que todo botao
desabilitado explique o motivo. O `btCalApagar` era desabilitado por
atribuicao direta, sem passar por `acao()`, e sem elemento de motivo --
entao ficava cinza e calado. Botao apagado e mudo e a reclamacao mais
antiga deste painel.

## R77 · A tela cresceu, e o teto subiu com justificativa  ✅

A pagina comprimida passou de 52 KB para 65 KB e estourou o teto de 64 KB
-- que existe exatamente para o crescimento ser uma decisao, e nao um
acidente. O teto foi para 80 KB, com a lista do que subiu escrita ao lado
dele: gerador de QR, aba Maquina inteira, miniatura de peca, dicionario
de ingles e os controles de producao. No Wi-Fi do proprio robo, 80 KB sao
entre 0,3 e 0,6 s.

## Cobertura

| banco | rodada 20 | rodada 22 | agora |
|-------|-----------|-----------|-------|
| firmware | 229 / 0 | 241 / 0 | **375 / 0** |
| interface | 121 / 0 | 125 / 0 | **186 / 0** |

E o banco inteiro roda limpo sob AddressSanitizer e UndefinedBehaviorSanitizer
(`testes/sanitizar.sh`).

Guardas automaticas antes de cada compilacao: fiacao, rotas, pagina
comprimida e **os codigos QR lidos por um decodificador de verdade**.
