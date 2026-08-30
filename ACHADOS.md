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

## R78 · Vinte cartoes, dezoito enfeites diferentes  ✅

A gaveta de configuracao tinha um dingbat por cartao -- ✉ ⚙ ♆ ♥ ☰ ☉ ▣ ⏉
↑ ℹ ✜ -- vindos de blocos Unicode distintos, e dois deles (o cadeado e o
disquete) sao emoji COLORIDO em quase todo sistema. Traco, peso e cor
mudavam de linha para linha, e no celular mudavam de novo. Nao era falta
de capricho num icone: era a maior fonte de poluicao visual da tela.

Junto com isso, o proprio icone da engrenagem era um gear do Feather
editado a mao, com arcos malformados no caminho SVG -- desenhava torto em
todo navegador, e foi o que o operador notou primeiro.

Agora ha um sprite so: mesma grade de 24, mesmo traco, na cor do texto.
Tres testes de interface seguram isso -- nenhum marcador solto de Unicode,
todo <use> apontando para um simbolo que existe, e a engrenagem vindo do
sprite. Sem eles o proximo cartao volta a trazer o seu enfeite.

## R79 · Manual demais entre um campo e o proximo  ✅

A queixa era de proporcao. Na tela de trabalho as notas sao poucas e
curtas, e ensinam enquanto se opera. Na gaveta sao dezenas de paragrafos
de manual: entre um campo e o proximo cabia uma pagina de texto, e quem
so queria mudar a velocidade rolava cinco telas ate achar o campo.

A gaveta ganhou o SEU proprio interruptor de explicacoes, com memoria
propria e nascendo desligado. Medido pelo banco de interface, a pagina
Maquina caiu de 3374 px para 2029 px -- 40% mais curta -- e o manual
continua a um toque de distancia.

## R80 · O botao que apaga a instalacao parecia o que troca o idioma  ✅

"Restaurar padroes" morava no fim dos parametros de mesa e base, com a
mesma aparencia de qualquer outro botao. Agora ele e o botao de apagar
tudo dividem um cartao de zona de perigo, lado a lado e com a tabela do
que cada um leva -- porque a diferenca entre os dois e a instalacao
inteira, e o operador precisa VER isso antes de escolher.

Apagar tudo limpa o espaco de NVS inteiro (nao chave por chave: assim
some tambem o residuo de uma versao anterior do firmware), desliga o
torque e reinicia. Nao toca no cartao SD. Exige a palavra APAGAR digitada,
conferida na tela E na porta.

O banco de interface tambem ficou mais rigoroso de tabela: a excecao da
regra "todo botao dispara uma acao" era uma lista de ids liberados, e
lista de excecao envelhece calada -- o botao para de explicar e continua
passando. Agora a excecao e MERECIDA: vale para quem escreveu o motivo na
tela depois do clique.

## R81 · Escrever no driver saiu do firmware e voltou para a bancada  ✅

A escrita de parametro por RS485 (R78 da rodada anterior) foi retirada do
firmware. O encoder voltou a ser SO LEITURA, como o cabecalho de
encoder.h sempre disse.

O motivo e de lugar, nao de codigo: enquanto nao se souber COM CERTEZA
qual registrador e o que naquele driver, escrever e experimento de
bancada -- com o motor desacoplado da mecanica -- e nao funcao de painel.
Os modos de escrita, comparacao de parametro e teste de SON foram para
ferramentas/teste_rs485, o sketch avulso que ja existia para achar o
registrador da posicao.

E os sketches avulsos ganharam guarda: eles nao entram no banco (nao ha o
que simular num programa que conversa com um driver de verdade), mas
sketch que nao compila e pior que sketch que nao existe -- o operador so
descobre na bancada, com o robo aberto. testes/ferramentas/ troca os
enfeites do Arduino por equivalentes de PC e o compilador confere a
sintaxe dos dois a cada rodada.

## R82 · Puxar para baixo recarregava a pagina no meio do trabalho  ✅

Rolar uma secao ate o topo e continuar puxando disparava o
puxar-para-atualizar do celular. Num painel de maquina isso nao e um
incomodo de navegacao: perde a aba aberta, os campos meio preenchidos e o
heartbeat que segura o movimento.

overscroll-behavior:none no body ja estava la e nao bastava -- quem rola
aqui sao os paineis de dentro, e o excesso deles sobe em cadeia ate o
documento. A trava foi para cada rolagem e para o html. WebView antigo e
iPhone velho ignoram a propriedade, entao ha uma trava de reserva em
JavaScript, estreita de proposito: so barra quando o dedo desce E o
container debaixo dele ja esta no topo.

## R83 · O modo operador saiu  ✅

Ele escondia as abas de instalacao atras de uma senha que nao era
seguranca de rede -- quem esta no Wi-Fi da maquina alcanca a API direto
-- e obrigava toda tela a consultar mais um estado. Nesta maquina ninguem
usava. Saiu inteiro: ConfigPainel, /api/painel, o campo "op" do status, a
chave do NVS, o CSS e o cartao.

## R84 · "Iniciar gravacao" parecia um botao morto  ✅

A trajetoria a mao livre funcionava. O que faltava era dizer isso: o
estado da gravacao aparecia so num rotulo de 9,5 px no cabecalho do
cartao, e o operador aperta, nada visivel muda na tela em que ele esta, e
conclui que o botao nao faz nada. Foi exatamente a queixa -- e o defeito
nao era de firmware.

Agora o cartao tem tarja de estado: parado, GRAVANDO com a contagem de
amostras subindo, ou o que ficou na memoria. E diz o que fazer em
seguida, que era o que faltava. Nao troca de aba sozinho: a tela pular
embaixo do dedo assusta mais do que ajuda.

## R85 · Um seletor de tres posicoes para tres coisas diferentes  ✅

A biblioteca do cartao era uma lista so com um seletor -- programas,
trajetorias, ajustes -- e nunca se sabia qual estava na tela. Agora
programas e trajetorias sao dois cartoes, visiveis ao mesmo tempo. Os
ajustes sairam da biblioteca: eles se copiam sozinhos para o cartao, num
arquivo reservado que nunca colide com backup gravado a mao, e voltam por
um botao na gaveta.

A copia e um ESPELHO, nao um ponto de restauracao -- calibracao refeita
errado e espelhada errada. Esta escrito na tela com essas palavras.

## R86 · Graus por segundo ao quadrado nao sao linguagem de operador  ✅

"Queria algo mais simples, de forma que um operador nao experiente
consiga entender no que ele esta mexendo."

Nenhum numero saiu da maquina. O que mudou foi a ordem: na frente, tres
botoes que qualquer um entende (Lento / Normal / Rapido para velocidade,
Macia / Media / Firme para a partida) e uma linha de resumo em palavras.
Atras de "Ajustar", os campos de sempre. Atras de "Avancado", resolucao,
sentido dos eixos e as margens de seguranca -- com um aviso de que mexer
ali sem medir muda a escala de tudo.

As protecoes viraram uma frase antes de virarem tres chaves, e a frase
diz o que se perde ao desligar cada uma: nao aumenta a area de trabalho,
tira o aviso.

## R87 · Calibracao guiada: a ordem e que era o problema  ✅

A pagina tinha cinco cartoes de calibracao, todos igualmente disponiveis,
e nenhum dizia por onde comecar. Mas a ordem importa e cada passo usa o
anterior: sentido antes de reducao (senao mede-se ao contrario), reducao
antes de curso (o curso e medido em graus), curso antes de mesa (os
cantos sao gravados com a ponta).

O cartao guiado nao refaz nenhum passo -- diz qual e o proximo, marca o
que ja esta pronto e abre o cartao que faz o trabalho. Duplicar a acao
ali seria duplicar a regra, e regra duplicada e regra que diverge.

## R88 · A pagina servia os proprios comentarios  ✅

pagina_web.h tem os comentarios mais longos do projeto -- sao a
documentacao da interface. Eles viajavam pelo Wi-Fi do robo e ocupavam
flash: um quinto da pagina comprimida.

O gerador agora os tira do que VAI PARA A MAQUINA e deixa todos no
arquivo-fonte. A regra e deliberadamente burra, e e isso que a torna
segura: so sai a LINHA INTEIRA que e comentario, dentro de style ou
script. Nenhuma linha com codigo e tocada, entao nao ha como cortar
dentro de uma string ou de uma expressao regular -- que e como
minificador ingenuo quebra uma pagina. O resto do risco esta coberto pelo
banco de interface, que serve exatamente esta saida.

Comprimida: 80.836 -> 61.159 bytes, com a interface maior do que antes.

## R111 · A contagem de passos perdia o sentido, e o erro nascia daí  ✅

O painel mostrava `ERRO +2216,85°` porque o **comandado** não era uma
medida: é o contador de passos do firmware, que só vale enquanto cada
pulso vira movimento. Perdido um destravamento, um religamento ou um eixo
segurado à mão, o contador segue andando sozinho e o "erro" passa a medir
a distância entre o braço e uma ficção.

A resposta não foi esconder o número — foi **reancorar o contador**. Com
o braço parado e a diferença passando de `DIVERGENCIA_MAXIMA_GRAUS`
(45°), o firmware reescreve o contador a partir do encoder, registra o
evento e diz na tela que reancorou. O encoder é a verdade da posição; o
contador é conveniência de quem gera pulso.

## R112 · Ângulo nomeado passou a funcionar sem calibração  ✅

`irParaAngulos()` exigia calibração das duas juntas. Numa máquina em
montagem — um driver ligado, nenhum limite ensinado — digitar 60° não
fazia nada e **nada explicava o silêncio**. Mas ir para um ângulo nomeado
não precisa de limites: precisa de escala, que o encoder já dá. A
exigência caiu; o que continua valendo é a trava de segurança
(`movimentoSeguro`) e ter ao menos uma junta habilitada.

## R113 · O modo "passo" saiu: um gesto, um significado  ✅

Foram acrescentados incrementos fixos de 1°, 5°, 10° e 30° na aba Mover.
Na bancada o gesto que se quer é encostar e ver o braço andar — não
escolher um número antes de tocar. Os incrementos saíram, o jog voltou a
ser gesto único (segurar anda, soltar para) e o valor exato continua indo
pelo campo "ir para o ângulo", que existe exatamente para isso.

No lugar deles entrou o que faltava de verdade na aba: um **controle de
velocidade** e o **teste de relé**, que antes só existiam em outra tela.
"Gravar ponto" virou um botão quadrado com o símbolo do alvo.

## R114 · Os eixos começam vermelhos, não cinza  ✅

`EIXO 1` / `EIXO 2` nasciam cinza — cor de "não sei". Mas o estado
inicial é sabido: **sem torque**. Cinza convidava a supor que talvez
estivessem ligados. O fundo passou a ser o mesmo vermelho de "sem
torque", e o verde só aparece quando o driver confirma o comando de
habilita no barramento.

## R115 · Um `catch` vazio escondeu meia tela  ✅

`encAtualizar()` terminava em `.catch(function(){})` para a rede poder
cair sem encher a tela de erro. Ele engolia também **defeito de código**:
ao fundir as três gavetas do encoder numa seção só, uma variável usada
logo abaixo saiu junto, `encAplicar` passou a lançar `ReferenceError`, e
com ele pararam de atualizar a tabela de amostras, o aviso de travamento
e o estado do zero — **cinco sintomas sem parentesco aparente, um único
defeito**.

Falha de rede segue silenciosa; qualquer outra vai para o console. O
banco de interface ganhou a guarda de console limpo justamente para isso.

## R116 · O braço 2D virou uma máquina, não dois traços  ✅

Cada elo era uma **linha com espessura**. De longe passava por braço; de
perto era uma barra chapada, sem começo nem fim, e no cotovelo as duas se
cruzavam sem dizer qual passa por cima.

Agora cada elo é um **corpo**: cápsula afunilada — grossa no mancal, fina
na ponta —, sombreada ao longo da espessura como um tubo iluminado de
cima. O elo 2 é pintado depois, então encobre o 1: o cotovelo ganhou
ordem, que é o que a máquina faz.

As juntas deixaram de ser bolinha e viraram **mancal**: carcaça, aro,
parafusos de flange e, no centro, o disco que diz se aquele eixo tem
torque. "Onde fica o eixo" e "esse eixo está ligado" passaram a ter cada
uma o seu sinal, no mesmo lugar, sem uma apagar a outra. A base ganhou
flange com chumbadores — o braço deixou de flutuar — e a ponta ganhou o
bico da tocha, apontado para onde o cordão vai.

A vista 3D **não mudou**: as cores dela continuam nas mesmas chaves da
paleta. O único código compartilhado que se mexeu foi a função de
clarear/escurecer cor, que subiu de dentro da 3D para o nível do arquivo
em vez de ser copiada.

## R117 · A Configuração em colunas  ✅

Em tela cheia com uma coluna só sobrava metade do monitor em branco e o
resto ia parar embaixo da dobra: quem procurava um ajuste tinha de rolar
para descobrir se ele existia.

Os cartões entram em **colunas** e **nascem abertos**. A gaveta exclusiva
— abrir uma fecha as outras — existe por causa da coluna estreita do
celular, onde duas gavetas abertas já empurram a terceira para fora da
tela; na largura de um computador ela só atrapalhava. Larga, a gaveta só
alterna.

O número de colunas vem da **contagem de cartões**, não da largura: o
navegador cria quantas couberem e depois reparte o conteúdo, então três
cartões viravam duas colunas cheias e um terço de tela vazio à direita.
E o bloco tem teto de largura e fica centrado — sem ele a coluna se
estica e a linha vira um nome perdido à esquerda com o campo lá na
direita.

## R118 · A lista do programa se lê como uma sequência  ✅

Um programa é uma sequência de pontos ligados por trechos, e a lista
mostrava isso como duas linhas soltas: a pergunta "esse trecho solda?"
pedia leitura de texto em vez de um olhar.

Cada trecho ganhou um **trilho** à esquerda — laranja onde há cordão,
cinza onde só desloca — e a chave foi para a direita, onde estão os
controles do resto da tela; a esquerda ficou com a leitura. No rodapé,
as duas contas que se faz antes de mandar executar: **percurso** total e
**quanto dele é cordão**.

## R119 · Pré-requisito não é erro  ✅

"Entre no modo aprendizado primeiro" saía na mesma cor de "deu errado".
Com a máquina inteira em ordem a aba Programa virava uma coluna de
avisos vermelhos, e o laranja parava de significar alguma coisa. O que a
função de bloqueio escreve é sempre pré-requisito — nunca falha —, então
ele saiu do vermelho e virou nota cinza com um ponto na frente. O
laranja voltou a ser só o que deu errado de verdade.

## R120 · O braço arrancava e parava: duas causas, uma cena  ✅

> "no campo mover, ir para ângulo, quando eu coloco um ângulo pra se
> mover, ele começa a se mover mas daí ele dá tipo uma atualização e daí
> para de se mover"

Eram **duas** coisas, e as duas nasceram de decidir sobre a máquina
inteira o que só se pode decidir por junta ou por medida.

**1. O seguimento de eixo solto valia sobre uma junta com torque.**
Ele existe para o operador empurrar o braço **desenergizado** com a mão e
a contagem acompanhar. Estava travado por `servosLigados`, que só é
verdade com **as duas** juntas energizadas — numa bancada com um driver
só isso nunca acontece. Resultado: a contagem de uma junta *com torque*
era reescrita pelo encoder a cada ciclo, inclusive logo depois de um
destino ter sido calculado a partir dela. A regra passou a ser por junta:
`j.habilitado`. Com torque, divergência é perda de passo, e quem cuida
dela é o assentamento.

**2. O vigia de travamento julgava por número de catálogo.**
Ele comparava a velocidade medida pelo encoder com uma esperada tirada de
`pulsos por volta` × `contagens por volta` — dois números **digitados**,
não medidos. Basta o driver estar configurado com outro número de pulsos
por volta para o esperado sair várias vezes maior que o real: meio
segundo depois de arrancar, um braço andando normalmente é declarado
travado, e o vigia **para o eixo** e escreve na tela — a "atualização" do
relato. Agora, quando há escala ensinada, é ela que serve de régua:
contagens por grau medidas na própria máquina, do mesmo lado de onde vem
a leitura que vai ser comparada. Sem escala ensinada, o caminho antigo
segue valendo; e o travamento de verdade continua sendo pego.

Um terceiro detalhe fechava a fresta: `isRunning()` responde "está saindo
pulso agora?", e entre mandar um destino e o primeiro pulso sair ela
responde **não**. A contagem só é reescrita com a máquina em modo manual
e com o gerador quieto há 300 ms.

## R121 · Velocidade em milímetro por segundo  ✅

A máquina pensa em **graus por segundo** — é o que vira frequência de
pulso, e não há outro jeito. Quem está na bancada pensa em **milímetro
por segundo**: é a velocidade da ponta, a mesma unidade do cordão, a
única que dá para comparar com a solda feita à mão.

A barra da aba Mover passou a ter o número em mm/s, digitável, com a
equivalência em °/s ao lado e três degraus (lento / normal / rápido) para
o dia inteiro. A conversão é a do braço esticado — a ponta a
`R = elo1 + elo2` anda `R × ω` —, e usar o alcance cheio é a escolha
conservadora: em qualquer outra postura a ponta anda mais devagar que o
número pedido, nunca mais rápido.

E o ajuste passou a valer para o jog **e** para "ir para um ângulo". Era
esta a parte "complexa" do relato: os dois são a mão do operador movendo
o braço, mas obedeciam a campos diferentes (`velN` e `velA`), guardados
em telas diferentes — subir a barra do jog e o posicionamento continuar
lerdo. O modo Precisão continua com o valor dele, que é o propósito
daquele botão.

## R122 · Calibrar virou quatro marcas — e virou opcional  ✅

> "quero que vc remova tudo alinhado a calibração do sistema, vamos
> reformular isto, o sistema por si deve funcionar sem a necessidade de
> calibração... agora só preciso deixar livre os motores, mover até o
> ponto máximo no eixo 1 positivo e depois o max negativo, e depois o
> mesmo com o eixo dois, isso é a calibração, cálculo automático daí"

Calibrar eram **onze estados**: declarar o ângulo real na referência, ir
ao limite, **voltar ao zero sozinho**, ir ao outro limite, voltar de
novo, e no fim medir o curso com transferidor para informar a resolução.
Mais três telas de aferição avulsa ao lado — engrenagem eletrônica pelo
encoder, redução contra um esquadro, escala do encoder —, cada uma com
marca, movimento e um número a informar.

Agora são **quatro marcas**: junta 1 no limite positivo, junta 1 no
negativo, e o mesmo na junta 2. Nada digitado. Dali sai:

- o **curso** de cada junta;
- o **zero**, que passa a ser o **meio do curso** — a única escolha que
  não pede número, e a que deixa a área útil centrada;
- a **escala do encoder** em contagens por grau. Ela sai de graça: entre
  as duas marcas há um tanto de contagens e um tanto de graus, e a
  divisão é a escala, com sinal. Era uma tela inteira; virou consequência
  de ter calibrado.

O batente se alcança com o motor **solto**, empurrando o braço com a mão
— que era exatamente o que a exigência antiga (`Habilite os servos antes
de calibrar`) proibia. Enquanto a calibração está aberta, a junta sem
torque tem a contagem puxada pelo encoder. Não é o seguimento de eixo
solto de sempre: aquele escreve posição **absoluta** e por isso exige o
zero absoluto já ensinado; aqui basta o **delta**, e delta não precisa de
origem. A conversão dispensa a redução — contagens por volta e pulsos por
volta são ambos por volta do *motor*, e o redutor cancela na divisão.

Sobrou **um** número declarado: a redução do redutor. Com um sensor só,
antes dele, nenhuma medida a revela — isso é física, não limitação de
software.

## R123 · Nada mais é recusado por falta de calibração  ✅

Programa, trajetória, aprendizado, ir a um ponto gravado e ensinar a mesa
eram todos recusados sem calibração. O argumento era "sem calibração não
há ângulo, há contagem de pulsos". Mas um ponto gravado é um par de
**contagens na mesma régua** com que ele vai ser perseguido: se a régua
não mudou, o braço volta exatamente para onde estava.

O que a calibração acrescenta é a **proteção de curso** — e só ela.
`posturaValidaDet()` já tratava "sem limites, nada é violação". Os
portões saíram; a proteção ficou.

## R124 · O travamento: parar o braço exige régua medida  ✅

> "o sistema está apresentando travamento às vezes"

O vigia de travamento comparava a velocidade medida pelo encoder com uma
esperada calculada de `pulsos por volta × contagens por volta` — dois
números **digitados**. Basta um deles não bater com o driver para o
esperado sair várias vezes maior que o real, e aí um braço andando
normalmente é declarado travado meio segundo depois de arrancar — e o
vigia **corta o movimento**.

Agora ele só **para** quando a escala do encoder foi medida (e a
calibração mede sozinha). Sem ela, avisa e não encosta no braço. Um vigia
que interrompe a operação a partir de um número que ninguém conferiu é
pior que nenhum.

## R125 · A junta muda deixou de roubar o barramento  ✅

O ciclo do encoder alternava as duas juntas sempre. Numa bancada com um
driver só — o caso mais comum durante a montagem — **metade das leituras
era uma espera até o timeout**, e essa espera acontece na mesma tarefa
que divide o núcleo 0 com a rede. O sintoma não era o motor: era a tela
engasgando.

Depois de cinco falhas seguidas a junta muda de regime: continua sendo
perguntada, uma vez a cada dez ciclos — o suficiente para reaparecer
sozinha quando o cabo voltar. Uma leitura boa devolve o ritmo normal na
hora.

## R126 · A rampa passou a acompanhar a velocidade  ✅

A aceleração era um número fixo, então subir a velocidade só alongava a
subida: a 120 °/s com rampa de 60 °/s² a subida levava dois segundos e o
movimento inteiro virava rampa — o braço parecia engasgar em vez de
andar. O controle de velocidade agora manda a rampa junto, calculada para
que o **tempo** de subida fique constante. O movimento tem a mesma cara
em qualquer velocidade.

## R127 · "Anda dois segundos e trava": o contador que ninguém mandava parar  ✅

> "quando clico para ir para o 0 ele anda tipo 2 seg e trava o movimento"

O vigia de travamento tem duas metades: **acusar** e **parar**. Ao separar
as duas (R124) eu deixei o contador `trav.total` subindo nos dois casos —
e é esse contador que o laço principal observa para **interromper o
movimento automático**. Resultado: o vigia "só avisava", mas o movimento
morria do mesmo jeito, sem ninguém ter mandado. Exatamente meio segundo
depois de arrancar, mais a rampa: os dois segundos do relato.

Agora quem conta é quem para. E o critério mudou de forma, o que fecha o
problema pela raiz:

| | |
|---|---|
| **com escala medida** | exige proporção: o eixo entrega menos de um quinto do que deveria. Preciso, pega até escorregão parcial |
| **sem escala medida** | exige o sinal que **não depende de escala nenhuma**: pulso claramente correndo e encoder claramente **parado**. Eixo que gira produz contagem, seja qual for a escala — então aqui não existe falso positivo por número errado |

A proteção voltou a valer em qualquer máquina, calibrada ou não, e o
falso positivo por número de catálogo continua fechado.

## R128 · O relógio da tela empilhava requisições  ✅

`setInterval(tick, 220)` dispara a cada 220 ms **sem esperar a volta
anterior**. O WebServer do ESP32 atende uma conexão por vez: quando a
máquina engasga — Wi-Fi ruim, uma gravação em memória não volátil, o
barramento do encoder esperando um timeout —, as consultas não esperam,
elas se empilham, e a fila cresce enquanto a origem do atraso durar. Dali
em diante tudo chega tarde: o heartbeat do jog, o botão que se aperta, o
próprio estado.

Uma consulta de estado por vez, uma do encoder por vez, com prazo de
escape de 3 s — `fetch` não tem prazo próprio, e uma requisição que nunca
resolve deixaria a página muda para sempre, defeito pior que o que se
estava consertando.

## R129 · Tocar no desenho não manda mais o robô andar  ✅

Era o comportamento padrão da mesa: tocar em qualquer lugar vazio mandava
a ponta para lá. Quem está olhando o desenho toca nele o tempo todo — para
conferir uma cota, para escolher o eixo — e cada toque virava um movimento
que ninguém pediu. Agora há o botão **IR**: ligado, o toque comanda;
desligado, o desenho é só desenho. IR e DES disputam o mesmo toque, então
ligar um desliga o outro.

## R130 · O risquinho azul girando  ✅

As duas rodinhas do encoder tinham um ponteiro **fino azul** com o
**comandado** — a contagem de passos do firmware. Numa máquina em
montagem essa contagem anda sozinha, e o risquinho ficava girando sem
parar em volta do mostrador: o painel parecia estar processando alguma
coisa, ou travado. É o mesmo motivo pelo qual o número comandado já tinha
saído da tela (R108) — faltava tirar o risco. Ficou o ponteiro do que
importa: onde a junta **está**.

Junto saiu o **gráfico do erro**, que era código morto desde R108: o
canvas não existia mais, mas o histórico continuava sendo alimentado a
cada consulta, alimentando um desenho que ninguém veria.

## R131 · A faixa da barra de velocidade é da máquina  ✅

A barra ia de 1 a 120 °/s cravados no código da página. Máquina nenhuma
usa a faixa inteira: uma com redutor grande nunca passa de vinte, outra
com redutor curto só começa a ser útil acima de cinquenta. **Mínimo e
máximo** viraram configuração gravada na máquina — a barra inteira passa
a ser útil, os três degraus repartem a faixa em vez de um teto que ela
nunca alcança, e o máximo vira também um limite de segurança guardado na
máquina em vez de no navegador.

## R132 · O redutor foi para debaixo da medição do encoder  ✅

O encoder conta no eixo do **motor**, antes do redutor: a contagem só vira
grau da junta passando por ele. Os dois números pertencem à mesma conta e
estavam em telas diferentes. Agora o redutor de cada junta fica logo
abaixo do endereço, do registrador e das contagens por volta daquela
junta.

## R133 · A varredura hostil não pode envelhecer calada  ✅

O banco dispara lixo em cada rota de POST — números absurdos, texto onde
se espera número, caminho de arquivo onde se espera índice. A lista de
rotas era mantida à mão, e tinha ficado para trás: citava uma rota que
não existe mais e deixava cinco de fora. `conferir_rotas.py` passou a
comparar as duas pontas antes de cada compilação, com uma lista explícita
de exceções e o motivo de cada uma.

## R134 · Varredura de código morto e do portão de segurança  ✅

A revisão pedida achou, além do que já está acima:

**O gráfico do erro era código morto.** O canvas saiu com R108, mas o
histórico continuava sendo alimentado a cada consulta — memória e
trabalho para desenhar algo que não existia. Junto saiu a única função
que sobrou sem chamador no firmware (`usPorChar`) e uma variável não usada
no vigia. **O firmware compila agora sem um único aviso.**

**O assentamento era o único caminho que move o braço sem vir de um
comando do operador.** `pararTudo()` já o cancela, mas depender disso é
confiar em ordem de chamada: se um dia alguém cortar o movimento por
outro caminho, o retoque daria mais um passo depois. A trava do portão
(`movimentoSeguro`) passou a valer também ali, e ele se declara parado em
vez de ficar tentando.

**Nenhuma rota da tela corta um movimento em curso.** O cenário V21 varre
as dezoito rotas que a interface pode disparar com o braço andando e
denuncia quem encostar. Passou limpo — o que confirmou que a causa do
"anda dois segundos e trava" estava no firmware (R127), não numa rota.

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

## R89 · O habilita saiu do fio e foi para o barramento  ✅

Na bancada, com `ferramentas/teste_rs485`, ficou provado que o **P098** do
painel governa o torque deste driver — e que ele corresponde ao
**registrador Modbus 98**, habilita=1, desabilita=0, função 06. A prova
foi por foto da faixa (`d`), mudança no painel e comparação (`d2`), com um
único registrador mudando; depois `s 98` confirmou o eixo travando e
soltando.

Isso mudou o diagnóstico do fio: com o P098 em 1, o terminal externo não
tinha efeito nenhum. **O SON do GPIO 23 já era decorativo antes de sair.**

O habilita passou a ir por Modbus (`configSon`, `encoderPedirSon()`), o
GPIO 23 ficou livre, e `encoder.h` deixou de ser só-leitura — por uma
porta estreita: um registrador, o configurado, e nenhum outro.

**O que isso custa, e está escrito em três lugares.** Fio de SON rompido
desabilitava o motor; fio de RS485 rompido não desabilita nada. O caminho
do habilita deixou de ser falha segura e não há configuração que traga
isso de volta. O que se pôs no lugar:

1. **Contator** em série com a potência dos drivers, aberto pelo contato
   NC da emergência — deixou de ser recomendação (`LIGACOES.md` §6).
2. Toda escrita é conferida **relendo**, e desabilitar que não confirma
   derruba a máquina em `FALHA` em vez de seguir achando que desligou.

Dois defeitos apareceram enquanto isso era construído, e valem registro:

- **A supervisão comparava o código de resultado**, não o pedido.
  Habilitar que deu certo e desabilitar que deu certo são os dois
  `SON_OK`: um desabilita logo depois de um habilita não teria transição
  nenhuma para ver, e a tela continuaria dizendo "habilitado" com o braço
  já solto. Passou a acompanhar o pedido pendente. Cenário **V01c**.
- **O banco tratava "junta 2 sem posição configurada" como "driver 2 fora
  do barramento".** São coisas diferentes, e a diferença só passou a
  importar quando o habilita virou Modbus — o SON vai para os dois
  drivers, e cortar o torque de um só deixaria meio braço energizado.

Cenários **V01** a **V05**: escrita nos dois drivers, driver que responde
"aceitei" e guarda o valor velho, barramento mudo, registrador 0 (não
configurado, e nada é escrito), driver que só aceita a função 16, e a
recusa de trocar o registrador com o braço energizado.

## R90 · O alarme pelo barramento: ler é uma coisa, rearmar é outra  ✅

O `ALM` continua sendo fio. Pelo Modbus dá para ler o **código** da falha
em vez de só "tem falha" — o fio é um bit, o registrador é o motivo.

Os modos entraram no sketch de bancada, não no firmware, e a separação é
deliberada: `a <reg>` lê ao vivo e não escreve nada; `a <reg> <valor>`
rearma, escreve, e confere relendo. A forma do comando é que diz qual é
qual — a diferença entre olhar e mexer não podia depender de o operador
lembrar de uma letra diferente.

**O firmware não rearma alarme.** Alarme que volta sozinho é a máquina
dizendo que algo está errado; rearmar repetido esconde exatamente o que
precisa ser visto, gasta a EEPROM do driver e deixa o eixo energizando e
desenergizando sem ninguém no controle. Isso é decisão de quem está na
frente da máquina, com os olhos nela.

## R91 · O botão de habilitar sequestrava o barramento  ✅

Reportado como "o sistema parou de fluir": problemas de conexão e de
acionamento depois de o habilita ir para o RS485. Era regressão minha, e
o banco mediu o tamanho dela.

`executarSon()` fazia tudo dentro de um único `ciclo()`: dois drivers,
três tentativas cada, escrita mais releitura — até **doze transações
seguidas**. Com o barramento mudo (cabo solto, driver desligado, endereço
errado — justamente quando o operador mais aperta o botão), cada uma
gastava `ENC_TIMEOUT_MS` inteiro.

**Medido no cenário V06: 822 ms presos num ciclo só.**

A tarefa do encoder roda no core 0 com prioridade 2; a tarefa de rede, que
serve o painel, roda no **mesmo core** com prioridade 1 — menor. Nesse
tempo:

| prazo | o que estourava |
|---|---|
| `TIMEOUT_JOG_MS` = 350 ms | o jog cortava no meio do movimento |
| `ENC_IDADE_MAX_MS` = 1000 ms | a leitura do encoder vencia de idade |

Um botão de habilitar servos derrubando o movimento de quem está
comandando é o oposto do que ele deveria custar.

**Correção: uma tentativa por ciclo.** As tentativas passaram a ser
espalhadas (`sonPasso()`), então cada ciclo custa no máximo duas
transações e entre elas a tarefa de rede roda. A escrita também ganhou
prazo próprio — `SON_TIMEOUT_MS` = 60 ms, seis vezes o round-trip real a
19200 baud, em vez de herdar os 100 ms da leitura, que rodam sozinhos no
ciclo e não custam nada.

**822 ms → 64 ms por tentativa.** V06 guarda os dois lados: cabe no prazo
do jog, e a leitura do encoder volta a valer depois do episódio.

### E um buraco que a assincronia abriu no OTA

`ota.cpp` desabilitava os servos e começava a gravar em seguida. Isso
funcionava quando o habilita era pino: `digitalWrite` cortava na hora e o
nível sobrevivia ao reset. Com Modbus, o pedido ficava só **enfileirado**
— podia nunca chegar ao fio antes do reboot, e o eixo atravessaria a
gravação inteira energizado.

O OTA passou a **esperar** a confirmação (`encoderSonEsperar()`), e a
recusar a atualização se ela não vier. Recusar uma atualização é
reversível; reiniciar com o braço energizado não é.

## R92 · O botão do motor estava enterrado numa gaveta  ✅

Ligar e desligar torque é a coisa que mais se aperta nesta máquina, e
morava em **Ajustes → Preparar a máquina** — duas gavetas de distância de
qualquer aba de trabalho.

Passou a ter botão próprio no cabeçalho, ao lado do PARAR e visível de
qualquer aba. É **o mesmo comando** do botão de Ajustes, de propósito:
dois caminhos para ligar o motor acabam discordando, e aí ninguém sabe
qual é o estado de verdade.

A cor diz o estado **real**, não o que foi pedido:

| | |
|---|---|
| verde, "MOTOR LIGADO" | tem torque |
| cinza, "MOTOR DESLIGADO" | sem torque |
| laranja, "..." | pedido feito, barramento ainda calado |
| vermelho, "FALHOU" / "SEM REG" | não confirmou, ou registrador não configurado |

O estado laranja é o que mais importa e o que não existia: com o habilita
no barramento, "mandei" e "tem torque" deixaram de ser a mesma coisa, e o
operador precisa ver a diferença.

## R93 · Um driver na bancada, e o habilita recusando tudo  ✅

`servosHabilitar()` exigia que **os dois** drivers confirmassem. Com o
segundo ainda fora do barramento — que é o estado da bancada — habilitar
recusava tudo dizendo que o driver 2 não respondeu. Era verdade e não
ajudava ninguém: com o eixo 1 ligado e funcionando, não dava para mexer
nele.

A raiz é que o habilita virou **Modbus**, e cada driver é um escravo
próprio. Um interruptor só descrevia bem um fio ligado nos dois SON; não
descreve dois escravos independentes.

Passou a ser **por junta**: `J1.habilitado` / `J2.habilitado`,
`servosHabilitar(ligar, junta)` com `junta` = 1, 2 ou 0, e dois botões no
cabeçalho — um por eixo.

O portão de movimento se dividiu junto, e a divisão é o que importa:

| | exige |
|---|---|
| `movimentoSeguro` | alarme, emergência, conexão, falha — **sem torque** |
| jog de um eixo | `movimentoSeguro` + o torque **daquele** eixo |
| `movimentoLiberado` (programa, trajetória, ir ao zero) | `movimentoSeguro` + **as duas** juntas |

Travar a junta 1 porque a 2 está sem torque impedia de trabalhar; deixar
o eixo sem torque receber pulso faria o gerador contar passos com o eixo
parado, e **todo limite de curso passaria a apontar para o lugar errado**.
Por isso o jog é por eixo e o movimento coordenado não é.

Um detalhe de lógica que o banco pegou: eu tinha feito o desabilitar que
**falha** deixar `habilitado = true`, raciocinando que o eixo pode
continuar energizado. Está errado — o campo é o **portão de movimento**,
não um sensor de torque. Depois de um desabilita que não confirmou
ninguém pode mover a máquina; que o eixo talvez esteja energizado quem
diz é a `FALHA` e a mensagem, que é onde essa informação serve. Cenário
**V02d**.

Cenário **V07**: com um driver no barramento, a junta 1 habilita sozinha,
o jog dela anda, o da outra não, e desabilitar por junta também funciona.

## R94 · Ir a 0 grau exigia a calibração que só existe depois de mover  ✅

`CMD_IR_HOME` passava por `irParaPassos()`, que recusava com *"Calibre as
juntas antes de usar posicionamento"*. Ciclo fechado: para calibrar é
preciso mover, e para mover era preciso calibrar.

O jog já rodava livre sem calibração — é o modo de instalação, e
`posturaValidaDet()` já trata *"sem calibração nada é violação"*. Ir a 0
grau passou a seguir a mesma regra: é justamente a operação que se faz
para **sair** do estado não calibrado, depois de referenciar o braço a
mão.

**Ir a um ponto gravado continua exigindo calibração** — o ponto foi
gravado num referencial calibrado, e persegui-lo sem ela manda o braço
para um lugar que ninguém escolheu. Cenário **V08c** guarda essa
diferença.

E o zero move só os eixos que têm torque: com um driver no barramento
leva o que responde e deixa o outro quieto, em vez de recusar ou de
mandar pulso para um eixo parado (**V08d**, **V08e**).

## R95 · Um motor ausente derrubava a máquina em FALHA  ✅

Reportado como "não consigo habilitar, e às vezes quero mover para 0 e
ele não deixa porque o sistema vê que só tem um motor conectado".

Com um driver no barramento, `servosHabilitar(false, 0)` — o desabilitar
das duas juntas — falhava na junta 2, e eu tratava esse "não confirmou"
como o caso grave: eixo possivelmente energizado, sem caminho para
cortar, portanto `FALHA`. Em `FALHA` **todo comando é recusado**,
inclusive ir ao zero. O robô travava por causa de um motor que não está
lá.

Faltava uma distinção que agora é explícita (`sonJaEnergizou[2]`):

| a junta | o "não confirmou" significa |
|---|---|
| **nunca teve torque** | o driver não está no barramento — nada a cortar |
| **tinha torque** | pode estar energizada e não há segundo caminho — **FALHA** |

Só a segunda derruba a máquina. A primeira diz o que é e segue.
Cenário **V09**, que também confere que ir ao zero volta a funcionar
depois e que o caso grave continua grave.

Junto, as chaves por eixo em *Ajustes → Preparar a máquina* — uma
`teclinha` para cada torque, ao lado dos dois botões do cabeçalho.

## R96 · Duas calibrações para a mesma coisa  ✅

*Ajustes → Preparar a máquina* tinha um bloco "Curso das juntas" que
abria o **mesmo** assistente do cartão de calibração guiada — os botões
do cartão guiado literalmente clicavam nos de Ajustes. Dois lugares para
a mesma operação só fazem o operador perguntar qual é a diferença, e não
havia nenhuma.

Ficou a guiada, que é onde os passos aparecem na ordem em que dependem
um do outro. O bloco duplicado saiu.

## R97 · O braço parecia travar, e era o desenho  ✅

Reportado como "o movimento do braço parece que está travando".

O `/api/status` chega a cada **220 ms**; o desenho rodava a cada 45 ms.
Resultado: a mesma pose desenhada cinco vezes seguidas e então um salto —
**~4,5 quadros por segundo de movimento real**. O robô não anda aos
saltos; era o desenho que mostrava assim.

Três coisas mudaram, e a primeira é a que resolve:

1. **O braço desenhado glisa entre as amostras.** Aproximação amortecida
   (τ = 90 ms), não extrapolação por velocidade: extrapolar passa do
   ponto toda vez que o eixo para, e um braço que ultrapassa e volta
   mente sobre onde a ponta esteve. Um salto maior que 30° é mudança de
   referencial — zerar a máquina, recuperar posição pelo encoder — e
   pula direto, porque ali o braço não percorreu caminho nenhum.
   Medido: **24 quadros no meio do caminho** onde antes eram zero.

2. **O desenho passou a rodar por quadro do monitor** (`requestAnimation‐
   Frame`) em vez de um `setInterval` de 45 ms, que não se alinhava com a
   tela — um a cada três saía repetido ou pulado, e tremia mesmo com dado
   novo. De brinde, para sozinho quando a aba sai da frente.

3. **Menos disputa pelo servidor durante o jog.** O WebServer do ESP32
   atende **uma conexão por vez**. Status + painel do encoder a cada
   220 ms disputando com o heartbeat do jog de 100 ms atrasava o
   heartbeat — e jog sem heartbeat por 350 ms **para o eixo**. Essa era
   travada de verdade, no motor. O painel do encoder cede a vez enquanto
   alguém está movendo.

## R98 · Aba Mover: texto demais, e um botão para duas juntas  ✅

As notas longas do *zerar aqui*, do joystick e do passo a passo repetiam
o que o próprio botão já diz. Ficou só o que **não** está no botão — que
o curso é contado a partir da referência, e onde trocar o sentido do
eixo.

E o *ir para um ângulo* tinha um botão só para as duas juntas. Agora são
três: **junta 1**, **junta 2** e **as duas**. Levar uma só é mandar a
outra para onde ela já está — o firmware recebe um destino completo, não
precisa de rota nova, e a junta que não se quer mexer não anda um pulso.
Com um motor no barramento dá para levar o eixo que tem torque sem que o
outro entre na conta.

## R99 · Aprendizado guiado, e a origem do desenho marcada com o braço  ✅

O modo aprendizado existia, mas o botão de **gravar ponto** morava na aba
*Mover*: ensinar um cordão obrigava a trocar de aba entre cada ponto, com
a mão no braço. Os três passos passaram a viver no mesmo cartão, na ordem
em que acontecem — soltar, marcar, encerrar — com a lista de passos
dizendo qual está valendo e o que fazer agora, no mesmo formato da
calibração guiada.

E a origem do DXF. Arrastar o desenho na tela pede que o operador saiba
onde a peça está **em milímetros**. Na bancada ele não sabe: ele sabe
onde a peça *está*, porque está olhando para ela. Então o caminho é o
contrário — **origem com o braço**: solta o braço, leva a ponta até onde
o desenho começa, confirma, e o desenho vai para lá.

Usa o modo aprendizado para soltar, e não um desabilita cru, porque é ele
que mantém o encoder acompanhando o braço solto — sem isso a posição lida
seria onde o firmware *acha* que a ponta está.

Um defeito apareceu ao testar: a guarda que cancela a operação quando o
aprendizado cai por fora (emergência, alarme, botão da ponteira)
cancelava **sempre**. O status chega a cada 220 ms, e ela julgava pelo
`apr` ainda falso no intervalo entre pedir e o robô responder. Agora só
se pode dizer que o modo *caiu* depois de tê-lo visto ligado.

## R100 · O 3D cortado nas articulações — diagnosticado, ainda não resolvido  ⚠️

O defeito é real e está localizado: as peças do 3D são ordenadas pela
profundidade do **ponto médio** (algoritmo do pintor), e cada elo é uma
caixa longa com uma ponta na frente e outra atrás. Qualquer profundidade
única está errada em metade do elo. Com o cotovelo dobrado, um elo é
pintado inteiro por cima do outro e o antebraço aparece **cortado em
dois** — confirmado por captura de tela, postura J1 = 45°, J2 = 160°.

**A correção óbvia não serve.** Segmentar os elos e ordenar cada pedaço
resolve a interseção, mas a face lateral de cada pedaço se projeta sobre
o topo do pedaço anterior e o elo sai **serrilhado** — pior que o defeito
original. Testado com sobreposição e sem, com contorno e sem; o serrilhado
é da projeção isométrica, não do contorno. A tentativa foi revertida por
inteiro em vez de trocar um defeito por outro.

O que resolve de verdade é um z-buffer por pixel, que o canvas 2D não
tem, ou recortar cada elo pela silhueta do outro. Fica registrado com o
diagnóstico e a captura para quem pegar.

## R101 · Posicionar por ângulo ignorava o botão Precisão  ✅

Reportado como "o movimento dos ângulos está muito rápido".

Digitar um ângulo e apertar o botão mandava o braço sempre em `velAuto`
(deslocamento, 12 °/s de fábrica) — com o operador olhando de perto e sem
jeito de pedir mais devagar.

O botão **Precisão** já existe e fica **na mesma aba, logo acima desses
campos**. Ele só não valia ali. Agora vale: o mesmo gesto que deixa o jog
fino deixa o posicionamento fino. Cenário **V11** mede os dois e exige
que o preciso ande bem menos no mesmo tempo de relógio.

Um detalhe do próprio banco apareceu junto: `reiniciarSistema()` não zera
o modo precisão, então o cenário novo o deixava ligado e os seguintes
andavam a 2 °/s — esgotavam a espera e reprovavam por um motivo que nada
tinha a ver com o que testam. O cenário passou a devolver a máquina como
a encontrou.

## R102 · Habilitar só a junta 2, e o que dizer quando ela não responde  ✅

Relato: "não estou conseguindo travar o eixo 2, apenas um deles".

O caminho da junta 2 é diferente do da junta 1 na máquina de estados do
habilita — ela começa no índice 1 em vez de 0 — e o banco só exercitava a
junta 1. O cenário **V10** fechou esse buraco: habilita sozinha, o jog
dela anda, o da outra não, desabilita sozinha, e habilitadas uma de cada
vez a máquina se declara pronta. **Passou de primeira**, então o firmware
não é o problema.

O que sobra é o driver não responder naquele endereço Modbus — segundo
driver ainda fora do barramento, ou com o endereço de fábrica igual ao do
primeiro. A varredura da bancada (`teste_rs485`, modo 3) achou **um só
escravo, no endereço 1**, o que é consistente.

Dizer só "não consegui habilitar" mandava o operador adivinhar. As duas
mensagens passaram a apontar o lugar: o endereço do driver, e onde
mudá-lo (**Ajustes → Encoder → Endereço do driver**).

## R103 · Com os dois encoders, o braço desenhado girava sem parar  ✅

Relato: "com os 2 encoders conectados o braço está se movendo muito
rápido no sistema, dando voltas e voltas".

`leituraPlausivel()` conferia o ângulo contra o **curso calibrado** — e
abria mão de conferir quando a junta não estava calibrada:

```c
if (!j.calibrada) return true;      // qualquer numero passava
```

Máquina em comissionamento **nunca** está calibrada. Então, com os dois
encoders no barramento e um deles mal configurado — contagens por volta
erradas, formato de 32 bits errado, registrador do vizinho — o ângulo
saía em dezenas de milhares de graus, era marcado **confiável**, ia para
o `/api/status` como `m2ok:true`, e a tela passava a desenhar o braço
pelo *medido*. O braço desenhado girava atrás de um número que não
existia.

O log da própria bancada já tinha mostrado a assinatura disso antes:
*"Junta 1 fora de posição: −177667.02 graus pelo encoder"*.

**Correção:** um teto absoluto que vale **sempre**, calibrado ou não.
Não existe junta desta máquina em 170 mil graus. `LIMITE_ABSURDO_GRAUS`
= 720° — duas voltas completas, folga generosa para qualquer montagem
real, e ainda pega o lixo por ordens de grandeza. É o que essa guarda
precisa fazer: separar **leitura** de **número**, não medir precisão.

A conferência contra o curso continua exatamente como estava depois de
calibrar. Cenário **V12**: a junta boa segue confiável, a absurda não, e
o status diz `m2ok:false` para a tela voltar a desenhar pelo comandado.

E recusar em silêncio deixaria você sem saber o que consertar — o painel
do encoder passou a dizer, na linha da junta, *"N° — fora de escala.
Confira contagens por volta, o formato de 32 bits e o registrador"*.

## R104 · O ângulo na tela era o da conta, não o do braço  ✅

Relato: o 2D/3D fora de sincronia com o braço — o sistema mede 360° e o
número não bate, mova mais ou mova menos.

O ângulo do encoder saía de **dois números digitados**:

```c
voltasMotor = (bruto − referencia) / contagensPorVolta   // catálogo
graus       = voltasMotor · 360 / reducao + grausHome     // catálogo
```

Errar qualquer um dos dois sai em escala errada, e **nada na tela
denuncia**: o braço em 90° mostra 47, ou 300. Pior, os dois erros se
compensam parcialmente, então acertar um só piora antes de melhorar.

**Correção: uma escala direta, medida pela própria máquina.**
`contagensPorGrau[2]` — contagens do encoder por grau **da junta**. Um
número só, e não se digita: marque, leve a junta até um ângulo que você
**conhece**, diga quantos graus ela andou. A conta é uma divisão.

O **sinal vem junto**: encoder que conta para trás enquanto a junta
avança dá escala negativa, e o ângulo sai certo sem chave de inversão
separada.

`encoderDefinirZero()` passou a usar a mesma conversão — os dois lados da
mesma conta feitos por caminhos diferentes deixariam o zero ensinado
deslocado da leitura que a tela mostra.

**0 = não ensinada**, e nesse estado vale o caminho antigo inteiro: quem
já tinha a máquina andando continua andando. Cenário **V13**: 90° de
braço viram 90° na tela, o sinal é capturado, e movimento curto demais é
recusado porque mediria ruído de leitura em vez de engrenagem.

## R105 · Velocidade por motor  ✅

As duas juntas têm mecânica diferente — redução, massa, braço de alavanca
— e a que carrega mais nem sempre aguenta a velocidade que serve para a
outra. Redução e aceleração já eram por junta; velocidade não era.

`Junta.fatorVel` multiplica a velocidade escolhida em cada motor. Um
fator por junta **compõe** com os três presets em vez de duplicar cada um
deles: escolhe-se o ritmo da máquina num lugar só, e cada motor segue no
que ele aguenta.

O movimento **coordenado** é a exceção, e a razão importa: aplicar um
fator diferente em cada junta faria as duas chegarem em instantes
diferentes, e o caminho deixaria de ser reto no espaço das juntas — que é
o que `moverCoordenado()` existe para garantir. Lá o fator entra pela
junta **mais restrita**: o movimento inteiro anda no que a mais lenta
aguenta, e as duas continuam chegando junto. Cenário **V14** mede os dois
casos.

## R106 · A interface falava demais  ✅

51 notas explicativas longas — cerca de 15 kB de prosa — saíram da tela.
Ficou o que o botão não diz.

Os **seis blocos de perigo ficaram intactos**: braço que desce pelo
próprio peso, arco que abre, máscara e aterramento. Enxugar uma interface
não é tirar aviso de segurança de uma máquina de solda.

Três notas viraram uma linha cada, onde havia restrição real que a tela
não mostra de outro jeito: o braço só solta com zero ensinado nas duas
juntas, registrador errado do habilita estraga o driver, e a chave de
torque é por eixo.

Dois guardas do banco de interface estavam amarrados em **números** em
vez de propriedades — quantas notas reaparecem ao tocar no "?", e quais
palavras aparecem numa nota. Os dois reprovaram quando as notas foram
enxugadas, que era exatamente o que se queria fazer. Passaram a medir a
propriedade: o "?" traz as notas de volta e a gaveta cresce; as notas não
são traduzidas.

A página caiu de 63,5 kB para **57,2 kB** comprimida.

## R107 · Interface de operação: 2D, aba Mover e cabeçalho  ✅

Revisão pedida por itens. O que entrou nesta rodada:

**O tracejado de erro saiu da vista de cima.** Ele desenhava o
*comandado* atrás do *medido* para mostrar o desvio entre os dois — uma
medição de diagnóstico, não de operação. E um tracejado em volta do braço
se confundia com a trajetória de solda, que é outro conceito. A vista 3D
continua com o dela, como pedido.

**Os eixos são selecionáveis no próprio desenho.** Tocar num elo escolhe
aquela junta; o seletor da aba Mover segue. A cor de cada elo diz se ela
tem torque — verde tem, vermelho não, cinza enquanto o barramento não
confirma. Um anel marca a selecionada: **cor diz torque, anel diz foco**,
duas perguntas diferentes com dois sinais diferentes.

Tocar *sobre* o braço escolhe o eixo e **não** manda a ponta para lá — só
o toque que cai fora do braço vira "leve a ponta até aqui". Um toque para
escolher o eixo que fizesse o robô andar seria o oposto do esperado.

**Passo a passo em graus.** As setas ganharam dois modos, ditos na tela
em vez de escondidos no gesto: em **Passo** um toque anda o incremento
escolhido (1°, 5°, 10°, 30°) e para; em **Contínuo** a seta anda enquanto
apertada, como sempre andou. Antes um toque rápido começava e parava o
jog e o eixo andava um tiquinho imprevisível — o mesmo gesto querendo
dizer duas coisas.

O passo reusa o caminho que já existia: leva a junta ao ângulo atual mais
o incremento. Quem calcula velocidade e rampa continua sendo o firmware.
O "ângulo atual" prefere o que o **encoder mediu** — a posição de verdade
do braço — e cai no comandado só quando não há leitura.

**"Ir para um ângulo" virou uma linha:** de onde está → alvo → Levar,
ligado à junta selecionada. Os três botões viraram um.

**"Zerar a máquina" subiu** para junto do passo a passo, que é onde ela
é usada.

**Os status ficaram secundários.** Tinham caixa, borda e halo aceso,
competindo com os controles — e controle do robô tem de ganhar de
diagnóstico. Viraram texto apagado com um ponto pequeno. **A falha é a
exceção e continua furando a discrição**, em vermelho piscando: o que
precisa chamar continua chamando.

**"Robô 2DOF | Configuração"** no cabeçalho, do tamanho de um link. E a
configuração deixou de ser um popup de 760 px sobre véu escurecido:
ocupa a tela inteira abaixo do cabeçalho, como seção.

O joystick saiu **no computador**, onde as setas de passo fazem o mesmo
com mais precisão. No celular ele fica: ali não há setas confortáveis e
arrastar o polegar continua sendo o gesto natural.

Três guardas do banco pegaram defeitos meus nesta rodada — id duplicado
(`btRefer` em dois lugares), botão sem ação, e o botão de levar saindo
calado quando o alvo estava vazio.

## R108 · O erro do encoder saiu da tela  ✅

O painel mostrava três números por junta: **comandado**, **medido** e
**erro**. Numa máquina em montagem a contagem de pulsos do firmware anda
sozinha, e o painel chegou a mostrar:

```
JUNTA 1 COMANDADO   1986,79°
JUNTA 1 MEDIDO      −230,05°
JUNTA 1 ERRO      +2216,85°
```

Nenhum dos três ajudava a operar, e **o do meio — o único que descreve o
braço de verdade — ficava perdido entre dois que não descrevem nada**.
O gráfico do erro, a linha "Avisos de desvio" na Saúde e a legenda do
desvio saíram junto.

Ficou o **medido**: onde a junta está. O firmware continua calculando o
erro internamente — é dele que vivem o assentamento e o vigia de
travamento —, ele só deixou de disputar espaço na tela.

## R109 · A cor sai do braço e vai para a bolinha da junta  ✅

Pintar o braço inteiro de verde ou vermelho tirava a leitura da
**postura** — que é o assunto do desenho — e punha um estado de energia
no lugar dela. O elo voltou à cor dele; quem carrega o torque agora é a
**bolinha** de cada junta: pequena, no ponto exato do eixo, e sem apagar
o resto. O anel da junta selecionada continua marcando o foco.

## R110 · A configuração ganhou a gramática da Saúde da Máquina  ✅

Ela já ocupava a tela; faltava o conteúdo. Cada ajuste virou uma
**linha**: nome à esquerda, valor à direita, alternadas para o olho não
se perder — exatamente como a Saúde da Máquina. A diferença é que aqui o
valor da direita **se edita**. Mesma gramática visual, então quem sabe
ler uma sabe ler a outra.

Títulos de grupo separam as linhas em assuntos; botões e notas continuam
soltos, porque não são valores, são ações.

Um defeito de ordem de CSS apareceu: a regra que esconde o joystick no
computador estava **antes** da que o abre. Mesma especificidade, ganha a
última — e a última era a errada. O joystick continuava na tela apesar da
regra existir.

## R135 · A régua do rodapé virou o número que se lê de pé  ✅

Corpo 16 é tamanho de texto, não de leitura de processo. Quem opera está
**de pé, a um metro da bancada**, de máscara — e para conferir o ângulo
tinha de se abaixar. Os cinco valores passaram a corpo 28, tabulares. O
rótulo em cima e o medido embaixo continuam pequenos **de propósito**: se
tudo crescesse junto, a faixa voltaria a ser um bloco cinza uniforme e a
hierarquia se perderia outra vez.

## R136 · "Zero" queria dizer duas coisas, a um botão de distância  ✅

Na aba Mover conviviam **"Ir para o zero da máquina"** e **"Zerar a
máquina aqui"**. O primeiro anda até a referência; o segundo *muda onde
ela fica* — e desloca a área útil inteira. Nomes parecidos, consequências
opostas, e o destrutivo com o nome mais curto.

O que muda a origem virou **"Declarar esta posição como referência"**,
atrás do mesmo cadeado que já guardava o zero absoluto na gaveta. Não é
senha: é um tranco para não se mexer sem querer.

## R137 · A ajuda voltou, no lugar da dúvida  ✅

O "?" do cabeçalho tinha saído, e por um bom motivo: ele *escondia* as
notas, e um interruptor para o que nunca se esconde é só mais um botão.
Voltou com outro papel — ele **acrescenta** uma frase sobre a aba em que
a pessoa está: o que ela é e qual o primeiro passo. Nasce ligado; quem já
sabe operar desliga uma vez e nunca mais vê. As notas curtas de cada
painel seguem sempre visíveis, com ele ligado ou desligado — e o banco
compara a contagem antes e depois para provar isso.

## R138 · O desenho estava dominado pelo alcance, não pelo braço  ✅

O contorno da região alcançável era azul cheio, 1,5 px — a coisa mais
forte da tela. Só que **o alcance não muda nunca**: é cenário, não
informação. Virou um risco cinza fino, do mesmo peso da grade. O azul
volta a significar uma coisa só: o braço e a ponta que ele carrega.

## R139 · Duas bibliotecas onde cabia uma  ✅

A aba Arquivos tinha dois cartões lado a lado — "Programas salvos" e
"Trajetórias salvas" —, cada um com o seu campo de nome, o seu botão
Salvar e a sua lista. Para usar era preciso saber **antes** em qual das
duas palavras o que você acabou de fazer se encaixa. Quem nunca operou
não sabe, e o segundo cartão ainda nascia fechado.

Agora é um só: um campo de nome, um Salvar, uma lista. O tipo aparece
como **etiqueta em cada linha** — depois de salvo, quando já não é uma
decisão. E a escolha de tipo só aparece na tela quando a máquina tem
mesmo as duas coisas para guardar; nos outros casos ela se faz sozinha e
o operador nem vê que existia. Sem nenhuma das duas, o texto volta a
falar de programa: dizer "não há trajetória" a quem acabou de desenhar
uma peça só confunde.

"Carregar" virou **"abrir"**.

## R140 · A gaveta ganhou um começo  ✅

Quinze cartões e nenhuma ordem. Quem monta a máquina pela primeira vez
não sabe o que vem antes do quê, e nada na tela dizia — descobria-se
abrindo cartão por cartão.

O cartão **"Por onde começar"** põe os cinco passos em ordem (medidas,
torque, calibração, mesa, zero absoluto), cada um lendo do **estado real
da máquina** se já está feito — não de um "já marquei essa" guardado no
navegador — e levando ao lugar onde se faz, trocando de página da gaveta
quando é preciso.

Defeito encontrado ao escrever o teste: o roteiro se redesenhava de meio
em meio segundo, trocando os botões por outros iguais o tempo todo. Um
clique que caísse entre a destruição e a criação se perderia. Agora só
mexe no DOM quando alguma coisa mudou de verdade.

## R141 · Procurar um ajuste pelo nome  ✅

Lembrar em qual dos quinze cartões mora "aceleração" é trabalho que a
máquina pode fazer. A busca casa o que foi digitado com o **texto inteiro**
de cada cartão — título, rótulos dos campos, notas —, de todas as quatro
páginas ao mesmo tempo, e abre os que casam. Ignora acento dos dois
lados, porque quem procura no celular raramente acentua. Enquanto ela
está valendo, as abas ficam apagadas: o que está na tela vem de todas
elas, e deixar uma marcada seria mentir sobre isso.

## R142 · O desenho ainda tinha uma porta para o comandado  ✅

Relato: "o desenho está fora de posição, a posição dele deve ser
influenciada apenas pelos encoders ou a redução, apenas isso".

`postura()` — a função única que alimenta o braço 2D, o 3D e a deteção
de clique nos dois — já preferia o ângulo medido pelo encoder quando ele
existia. Mas por junta, na falta de leitura confiável, ela caía de volta
no **comandado** (a conta de pulsos):

```js
const b1 = tem1 ? (D.m1 || 0) : c1;   // c1 = D.t1, contagem de passos
```

Isso foi decisão deliberada em R103/R104, para uma bancada com um
driver só no barramento continuar desenhando alguma coisa. Só que
comandado e medido são **duas fontes diferentes de verdade** — um diz
para onde o firmware mandou o motor ir, o outro diz onde a redução e o
encoder dizem que a junta está de fato. Um passo perdido, uma folga no
redutor ou `contagensPorGrau` mal medido abrem uma diferença entre os
dois, e o desenho, ao usar o comandado, escondia justo o caso em que
mostrar a diferença importava.

**Correção:** o desenho passou a depender só do encoder, sem exceção.
Sem leitura confiável, a junta **congela** na última postura que de fato
foi medida — nunca volta a seguir o comandado. `ultimoMedido` guarda
esse último ângulo por junta; `postura()` só o atualiza quando `m1ok`/
`m2ok` está de pé.

```js
let ultimoMedido = {t1:0, t2:0, tem1:false, tem2:false};
function postura(){
  const tem1 = !!D.m1ok, tem2 = !!D.m2ok;
  if(tem1){ ultimoMedido.t1 = D.m1 || 0; ultimoMedido.tem1 = true; }
  if(tem2){ ultimoMedido.t2 = D.m2 || 0; ultimoMedido.tem2 = true; }
  const sv = suavizar(ultimoMedido.t1, ultimoMedido.t2);
  ...
}
```

A legenda sob o desenho ganhou um terceiro estado, porque "sem leitura"
deixou de significar "mostrando o comandado":

| estado | frase |
|---|---|
| encoder respondendo | "posição medida pelo encoder" |
| nunca teve leitura | "sem leitura do encoder ainda" |
| já teve, perdeu agora | "última posição medida (sem leitura agora)" |

O comandado (`c1`/`c2`) continua existindo dentro de `postura()` só para
o **fantasma tracejado** (R104-ish): a linha que aparece quando medido e
comandado divergem mais de 0,5°. Isso é um alerta *sobre* a posição, não
a posição — continua correto mostrar para onde o firmware mandou o motor
ir, ao lado de onde ele de fato está.

Dois cenários de banco cobrem o congelamento: perder a leitura no meio
de um movimento não move o desenho nem um grau na direção do comandado;
e quando a leitura volta, o desenho retoma exatamente do valor medido,
ignorando tudo que o comandado fez enquanto a leitura estava fora.

## R143 · O curso medido calava o encoder da máquina inteira  `V24`  ✅

Relato: "movo o motor e o braço não acompanha o movimento".

Na foto, a junta 1 mostrava um curso medido de **cerca de quatro graus**
— uma calibração que parou no meio — e o braço parado em **−65,9°**.
`leituraPlausivel()` recusava qualquer leitura fora do curso medido:

```c
if (!j.calibrada) return true;
return graus >= j.grausMin - FOLGA_PLAUSIVEL_GRAUS &&
       graus <= j.grausMax + FOLGA_PLAUSIVEL_GRAUS;
```

Com o braço a 65 graus de uma faixa de quatro, **toda** leitura do
encoder era descartada. E como `leituraConfiavel()` é o portão de tudo, o
efeito foi total:

| deixou de funcionar | consequência |
|---|---|
| reancoragem da contagem | a conta de pulsos nunca mais se corrigia |
| seguimento de eixo solto | mover o braço na mão não movia a contagem |
| assentamento pós-movimento | nenhuma correção depois de posicionar |
| `m1ok`/`m2ok` no status | e o desenho, que desde R142 só obedece ao encoder, **congelava** |

A causa raiz é uma premissa que envelheceu. Quando essa conferência foi
escrita, o curso medido *era* uma afirmação sobre onde o braço pode
estar. Desde que o limite virou **opção** (R-limites, tarefa 29), não é
mais: o braço anda livre pela mesa por padrão, e o operador liga o limite
quando quiser. Leitura fora da faixa passou a ser leitura **boa** de um
lugar onde o braço legitimamente está.

**Correção:** a conferência contra o curso vale quando o limite está
**ligado** — que é o operador dizendo "este curso é real, respeite-o". O
que separa leitura de lixo em qualquer caso é o teto absoluto de 720°
(R103), e esse vale sempre.

`V24` prende os quatro lados: com o limite ligado a recusa continua; com
ele desligado a leitura volta a valer; o status volta a dizer
`m1ok:true`; e um ângulo absurdo segue recusado com o limite desligado.

## R144 · A coluna mexia debaixo do dedo e matava o jog  ✅

Relato: "esta aba lateral deve ser fixa sem arrastar, pois quando faço
algum ajuste nela, ao clicar nas setas de atalho o motor não se move
corretamente".

As setas de jog escutavam **`pointerleave`** para parar:

```js
["pointerup","pointerleave","pointercancel"].forEach(...)
```

Sem `setPointerCapture`, qualquer coisa que mudasse a altura da coluna —
a barra de estado ganhando uma linha, o botão de próximo passo
aparecendo, uma dica surgindo sob outro botão — tirava a seta de baixo do
dedo. `pointerleave` disparava, o jog morria no meio e o eixo parava
sozinho, sem ninguém ter soltado nada.

O joystick, ao lado, já fazia certo desde sempre — inclusive com o
comentário explicando por que `pointerleave` não podia estar na lista. As
setas nunca ganharam o mesmo tratamento.

**Correção**, na mesma forma do joystick: `setPointerCapture` no
`pointerdown`, e a parada por `pointerup` / `pointercancel` /
`lostpointercapture`. Com a captura, o botão continua recebendo o fluxo
do ponteiro onde quer que ele vá, e o `pointerup` chega sempre.

`jogOff()` passou a mandar o zero só quando havia jog desta página: a
captura entrega `pointerup` e `lostpointercapture` em sequência pelo
mesmo gesto, e dois zeros seguidos só ocupariam a única conexão do
servidor. Se ainda assim ele se perder, o firmware corta o jog em 350 ms
sem heartbeat (`TIMEOUT_JOG_MS`).

**E a coluna parou de pular** onde dava para parar sem custo:

- `.teMsg` reserva **duas linhas**. Era a oscilação mais frequente: a
  mensagem de estado troca a cada leitura e cada linha a mais empurrava
  tudo abaixo.
- `.agora` virou uma linha só, com reticências: "(medido)" e
  "(comandado)" têm larguras diferentes e a troca quebrava a linha em
  duas.
- `.eixo .fx` ganhou altura fixa: "sem curso" é uma linha de texto,
  curso medido é número **mais** barrinha — calibrar uma junta empurrava
  as setas da outra.
- O botão de próximo passo só mexe no DOM quando o passo **muda**, em
  vez de reescrever `display` e texto 4,5 vezes por segundo.

O banco reproduz o defeito antes de provar a correção: com a captura
removida, o cenário acusa `parada precoce: true`.

## R145 · "Levar a um ângulo" sacudia o eixo que ninguém pediu  ✅

Relato: "quando clico levar a tal ângulo não está indo corretamente".

`/api/mover` recebe um destino **absoluto para as duas juntas** e o
converte em pulsos pela contagem que o próprio firmware mantém. A tela
preenchia o eixo não selecionado com `anguloAtual()`, que **prefere o
ângulo medido pelo encoder**:

```js
const t1=(j===1)?alvo:anguloAtual(1);
const t2=(j===2)?alvo:anguloAtual(2);
```

Duas contas diferentes no mesmo pedido. Onde elas divergem — perda de
passo, folga do redutor, escala recém-medida — o firmware via um destino
diferente da posição atual e mexia num eixo que ninguém mandou mexer:
levar a junta 1 a zero sacudia a junta 2 junto.

**Correção:** o eixo parado recebe a **própria contagem do firmware**
(`D.t1`/`D.t2`), então a diferença é exatamente zero e ele não recebe
pulso nenhum. E o "de" da linha ("−65,9° → 0") passou a mostrar essa
mesma conta, para a linha descrever o que vai de fato acontecer — onde o
braço está de verdade continua na linha de cima ("medido") e na régua do
rodapé.

`anguloAtual()` saiu: era exatamente a função que escolhia a conta por
todos, e não sobrou nenhum uso dela.

Não havia **nenhum** cenário cobrindo o botão "Levar" — por isso o
defeito atravessou tantas rodadas. Agora há três.

## R146 · O painel de jog virou quadro fixo  ✅

Pedido: "a aba da direita do jog deve ser um quadro fixo, não de
rolagem; os restos devem sumir".

Rolar é o defeito, não o sintoma: se a coluna rola, a seta que estava sob
o dedo saiu dali. R144 já tinha feito o **gesto** sobreviver a isso
(captura de ponteiro); faltava a coluna parar de precisar rolar.

Medido a 1280×800, o conteúdo passava **119 px** do quadro. De onde ele
vinha:

| bloco | px | destino |
|---|---|---|
| ajuda por aba (`ajudaAba`) | 97 | **nasce fechada**, atrás do "?" |
| "Mudar a origem" + botão | ~90 | foi para a gaveta, no cartão "Zero absoluto" |
| rodapé do joystick | ~20 | saiu: repetia o motivo |
| folgas do painel | ~25 | menores que as do resto da página, de propósito |

**A ajuda por aba nasce fechada.** Aberta, ela ocupava quase cem pixels
bem em cima das setas — sozinha, era 80% do estouro. Continua a um toque
no "?", e a escolha fica gravada no navegador.

**"Mudar a origem" foi para a gaveta.** Ela mudou de lugar, não sumiu:
mora no cartão *Zero absoluto*, que já é sobre origem e já tem o mesmo
cadeado. Este painel é o de **mexer** no braço; o que se ajusta uma vez
não disputa espaço com ele.

**"Habilite os servos" aparecia três vezes na mesma tela** — na tarja de
estado, no subtítulo do cartão e no rodapé do joystick. Ficou só na
tarja, que é onde ela vem em corpo maior e com o botão do próximo passo.
O joystick continua dizendo que está bloqueado do jeito dele: apagado. E
o texto apontava para uma "aba Ajustes, etapa 1" que **não existe mais**
— quem leva até os servos agora é o botão da tarja.

Resultado, com a máquina pronta:

| | |
|---|---|
| 1440×900 | fixo |
| 1366×768 | fixo |
| 1280×800 | fixo |

Bloqueada, a tarja cresce o botão de próximo passo e a coluna pode rolar
nas telas mais baixas — mas ali o jog está desligado de qualquer jeito, e
o que a tela precisa mostrar é **como destravar**. No celular ela
continua rolando: joystick, setas, velocidade, ir-para-ângulo e atalhos
não cabem em 844 px, e rolar é melhor que esconder um botão.

## R147 · Quem chegava ao ângulo era o fantasma, não o braço  `V25`  ✅

Relato: "quando clico para ir ao ponto zero ou a um ângulo ele está se
baseando no erro para a chegada; o braço não chega ao ângulo, só o erro".

O destino de "ir para o zero" é **absoluto em pulsos**, calculado sobre a
contagem que o firmware mantém:

```c
irParaPassos(grausParaPassos(J1, t1), grausParaPassos(J2, t2));
```

Com a contagem adiantada do braço — perda de passo, folga, escala
recém-medida — mover a **contagem** até o alvo deixa o **braço** parado no
tanto do erro. Na tela isso é literal, e é o que o relato descreve: o
fantasma tracejado vermelho *é* a contagem, e era ele que chegava ao
ângulo pedido.

O banco reproduz o número exato. Contagem em 5°, braço em 12°, "ir ao
zero":

| | contagem | braço |
|---|---|---|
| antes | 0,00 | **6,98** |
| depois | 0,00 | **0,00** |

**Correção:** `ancorarNoEncoder()`, chamada em `irParaAngulos()` **antes**
de converter o ângulo em pulsos. Ela reescreve a contagem pelo que o
encoder mede, em cada junta com leitura confiável e com o braço parado.
Dali em diante a contagem descreve o braço, e o destino calculado sobre
ela leva o **braço** ao lugar.

Não é o assentamento: aquele conserta *depois* de chegar, e só se estiver
ligado. Este arruma o **ponto de partida**, que é de onde o erro vinha.
Também não é o reancoramento de 45°, que existe para a contagem que
perdeu o sentido — este vale para qualquer diferença acima de 0,05°,
porque na hora de calcular um destino cada décimo importa.

Reescrever a posição da máquina em silêncio seria a tela mudando de
número sem ninguém entender por quê, então acima de meio grau — o mesmo
limite em que o fantasma aparece — a mensagem diz: *"Indo para 0,0 / 0,0
graus (a contagem estava 6,98 graus fora e foi acertada pelo encoder)"*.

> Escrever o cenário achou um segundo defeito, no próprio banco: a
> primeira versão colava o encoder na contagem durante o movimento, o que
> apagava de graça o erro que ela media — e passava com ou sem a
> correção. O eixo agora anda o mesmo que a contagem andou, **guardando**
> a diferença que já existia, que é o que o ferro faz.

## R148 · IR e DES saíram da mesa de traçado  ✅

Pedido: "remova o DES/desenho, no caso trajetória a mão livre; remova
também o IR".

Dois botões na barra da mesa disputavam o mesmo toque:

| | o que fazia |
|---|---|
| **IR** | ligado, tocar na mesa mandava a **ponta** até o ponto tocado |
| **DES** | riscar o caminho com o dedo; o traço virava programa de pontos |

Saíram os dois. O toque na mesa passou a ter **um propósito só**:
escolher o eixo, tocando num elo. Levar o braço a um lugar se faz pelas
setas e por "ir para um ângulo", que dizem para onde vão **antes** de ir;
o caminho se ensina por pontos gravados ou importando um DXF.

Saiu com eles a barra `#barraDes`, o modo de desenho inteiro
(`desOn`, o traço, o simplificador `enxugar()`, o cursor de mira) e a
regra de exclusão mútua entre os dois modos — que só existia porque os
dois consumiam o mesmo toque.

**O que ficou, de propósito:**

- A **gravação da trajetória a mão livre** (mover o braço com a mão e
  reproduzir) — é outra coisa, mora na aba Programa e não foi pedida.
  O cartão dela perdeu só a nota que mandava usar o DES.
- `POST /api/prog/desenho` — quem a usa agora é o **DXF importado**.
- `POST /api/mover_xy` — ficou sem chamador no painel.

Essa última virou um aviso solto na compilação (*"rota registrada e nunca
chamada pela página"*), e aviso que ninguém sabe explicar acaba ignorado
— inclusive quando for de verdade. Então `conferir_rotas.py` ganhou um
`SEM_PAGINA` com o **motivo escrito**, e duas guardas para a lista não
envelhecer calada: se a página voltar a chamar a rota, ou se o firmware
deixar de registrá-la, a compilação reprova dizendo qual das duas.

## R149 · O braço não chegava: três portões fechados de uma vez  `M06`  ✅

Relato, depois de R147: "ainda não chega; quem chega é apenas o
tracejado. Para movimento para ângulo o braço deve se basear no
**encoder**, não no erro, com suavidade na chegada."

R147 consertou o **ponto de partida** (ancorar a contagem no encoder
antes de calcular o destino). Faltava a **chegada**. Quem fecha a conta
depois que o eixo para é o assentamento — e ele estava barrado por três
portões independentes, cada um bastando sozinho para o braço ficar onde
estava:

| # | portão | efeito na máquina do relato |
|---|---|---|
| 1 | `faltaPara()` exigia **curso medido** | junta sem calibração não recebia assentamento nenhum |
| 2 | erro acima de `maxCorrecaoGraus` (3°) era **RECUSADO** | os ~7° de erro caíam direto em "erro grande demais" |
| 3 | o retoque era preso ao **curso calibrado** mesmo com o limite desligado | num curso medido pela metade ele não cabia, e virava "retoque cairia fora do curso" |

**1 — o encoder mede a junta com ou sem calibração.** Mesmo defeito de
R143, noutro lugar: exigir curso medido para *ler* o encoder. Agora
`faltaPara()` usa `leituraConfiavel()`, que confere registrador,
validade, idade e possibilidade física — e nada mais.

**2 — o teto virou o tamanho do PASSO, não um motivo para desistir.** A
intenção da regra original era boa: nunca lançar o braço vários graus de
uma vez achando que está consertando. Mas *recusar* deixava o braço
parado — que era exatamente o sintoma. Agora cada retoque anda no máximo
`maxCorrecaoGraus`, lê o encoder de novo e repete: 7° fecham em três
passos, sempre olhando o encoder, e nenhum deles é um pulo.

**3 — o curso só prende o retoque quando o limite está ligado.** Com o
limite desligado o braço anda livre pela mesa (R143); prender o retoque
num curso que não está em vigor — pior, num curso medido pela metade —
era mais um jeito de não chegar. Com o limite **ligado** ele volta a
valer, e `M06e` prende isso.

**E a regra das tentativas virou progresso.** Contar tentativas
absolutas desistia no meio de uma convergência saudável. O que denuncia
acoplamento solto não é o número de retoques, é o retoque **não diminuir
o erro**. Agora: enquanto cada passo aproxima pelo menos 15%, continua;
parou de aproximar, desiste e diz. Sobra um teto absoluto de 40 só para
nunca existir laço infinito no núcleo 1.

**Suavidade na chegada.** A velocidade do retoque era fixa em ¼ da
normal — boa para décimos de grau, dura para graus inteiros, e sempre a
mesma no último passo, que é onde passar do ponto custa outra viagem.
Agora ela acompanha o que falta: meia velocidade longe, afinando até o
mínimo da máquina. O último décimo é um encosto, não um tranco.

> **E se o eixo simplesmente não seguir?** Soltar o número de tentativas
> poderia virar licença para martelar o ferro. Não vira: o **vigia de
> travamento** pega antes — comando andando e medido parado é a
> definição de travamento — para o eixo e diz qual junta. `M07` prende
> essa ordem; na prática o assentamento nem chega a rodar.

Os três portões foram verificados um a um: reintroduzido cada um
sozinho, `M06` reprova; restaurados, passa.

## R150 · A aba de comando virou um painel de comando  ✅

Seis pedidos numa tacada, todos na mesma direção: **a aba de jog é para
comandar, e só**.

**Os comandos desceram para o painel.** `EIXO 1`, `EIXO 2` e `PARAR`
moravam no cabeçalho; foram para uma linha no alto da aba Mover, junto
das setas que a mão já está usando. No lugar deles, no cabeçalho, entrou
o atalho **Arquivos** — a aba que se abre de qualquer lugar.

> **O que isso custa, dito na cara.** O PARAR deixou de estar em toda
> tela: não aparece na aba Programa nem com a gaveta aberta. O que para a
> máquina de qualquer lugar passou a ser a **tecla de espaço** — e é ela
> que o banco agora prende, no lugar do teste antigo de "PARAR clicável
> com a gaveta aberta". Nada disso é a parada de emergência: essa sempre
> foi o **contator no fio**, o único corte que funciona com o ESP32
> travado.

**Saiu a tarja de estado de Mover e Programa.** "PRONTA / UM EIXO COM
TORQUE" era um cartaz que mudava sozinho em cima dos controles. Nas duas
abas de comando ela some: os próprios botões dizem o que dá e o que não
dá (cada um com o seu motivo embaixo), as lâmpadas do cabeçalho dizem o
estado, e o botão de torque — que era o que a tarja mandava apertar —
agora está ali dentro. Nas outras abas ela fica, porque ali não há botão
de torque à mão.

**Saíram o seletor "Junta" e a linha "Eixo 1: −65,92° (medido)".** Os
dois repetiam o que já está em outro lugar: o eixo se escolhe tocando no
elo do desenho ou na própria seta, e o ângulo está na régua do rodapé em
corpo 28, comandado e medido lado a lado.

**A velocidade virou cinco degraus.** Era uma barra contínua — mira fina
para acertar um número que ninguém sabe de cor. Os cinco degraus
repartem a faixa configurada da máquina, e o 1 e o 5 são exatamente o
mínimo e o máximo dela: nenhum degrau é inalcançável. **Lento, normal e
rápido viraram apelidos dos degraus 1, 3 e 5** — os mesmos degraus, não
uma segunda escala, e o degrau aceso é o mesmo tanto faz por onde se
escolha. O mm/s continua na tela, pequeno e ao lado: deixou de ser o que
se escolhe para ser o que se confere.

**Precisão virou o quarto atalho**, ao lado de lento/normal/rápido — é um
jeito de andar, como eles. Perdeu o texto "Precisao: desligada" dentro do
botão: o estado é o botão aceso.

## R151 · Programa: quadro fixo por dentro, não por corte  ✅

Mover coube enxugando (R146). Programa não cabia de jeito nenhum: a
**lista de pontos é o programa** e cresce com a peça — cortar seria
esconder ponto, e esconder ponto é pior que rolar. Medido a 1280×800, o
painel passava **652 px** do quadro, e o cartão aberto sozinho ocupava
1005 px.

A saída foi mudar o que rola. O painel virou um acordeão em flex: os
**cabeçalhos** dos cartões e as bordas ficam parados, e quem rola é o
**miolo do cartão aberto**, e só ele. O dedo procura "Ensaiar sem arco"
sempre no mesmo lugar, com um programa de três pontos ou de quarenta.

O `.rol` continua com `overflow-y:auto` de reserva: se a tela for tão
baixa que nem o miolo mínimo caiba, é melhor rolar do que cortar um
botão.

| | Mover | Programa |
|---|---|---|
| 1440×900 | fixo | fixo |
| 1366×768 | fixo | fixo |
| 1280×800 | fixo | fixo |

## R152 · A máquina não dizia qual firmware estava rodando  `V27`  ✅

A foto do relato mostra contagem **−3,3°**, encoder **63,00°**, erro
**+66,31°** — e a mensagem *"Indo para 0.0 / 0.0 graus"*. Essa frase é a
**anterior** ao ancoramento de R147: com 66° de correção, o firmware
corrigido diria *"a contagem estava 66,31 graus fora e foi acertada pelo
encoder"*. A placa estava rodando um binário sem as correções.

E não havia **nada na tela** que dissesse isso. Diagnosticar o fonte
enquanto a máquina roda outro binário é trabalho jogado fora, e foi o que
aconteceu: uma rodada inteira olhando para o lado errado.

A saúde da máquina passou a publicar o **MD5 do binário gravado**, na
primeira linha. `__DATE__` não serviria: é do momento em que *aquele
arquivo* foi compilado, e uma correção noutro `.cpp` não mexe nele — o
carimbo ficaria velho justamente na hora em que precisa estar certo. O
MD5 é calculado sobre o que está na flash: muda quando o binário muda, e
só quando ele muda.

## R153 · Movimento pela contagem não podia sair calado  `V26`  ✅

Pedido: "quando o usuário pedir para ir a tal ângulo, quero que mova com
base na medida do encoder, não de leitura acumulada de erro".

Olhando o próprio ancoramento com essa frase na mão, apareceu uma brecha
real: `ancorarNoEncoder()` devolvia **só um número**, e devolvia zero em
três situações diferentes:

| situação | o que acontecia |
|---|---|
| não havia o que corrigir | move pelo encoder ✔ |
| **não deu para ler o encoder** | move pela contagem, **calado** |
| **o eixo ainda estava andando** | move pela contagem, **calado** |

Nos dois últimos casos o movimento saía exatamente com o defeito do
relato — o braço parando longe do ângulo pedido — e a tela dizia a mesma
frase de sempre. Para quem pediu um movimento baseado no encoder, essas
três coisas não podem ter a mesma cara.

Agora a função devolve **por quê**, e a mensagem diz em que conta o
movimento saiu:

| | mensagem |
|---|---|
| leitura boa, contagem já certa | "Indo para X / Y graus, medido pelo encoder" |
| leitura boa, contagem fora | "…(a contagem estava N graus fora e foi acertada pelo encoder)" |
| tem encoder, sem leitura agora | **"Indo para X / Y graus PELA CONTAGEM: sem leitura confiável do encoder"** |
| máquina sem encoder nenhum | "Indo para X / Y graus" |

O último caso é deliberado: operar pela contagem numa máquina sem encoder
é escolha da instalação, não falha, e ali não há o que avisar. E o
movimento **não é recusado** quando falta leitura — recusar deixaria o
operador sem tirar o braço do lugar. Ele acontece, e a tela diz que
aquele não foi pelo encoder.

## R154 · O fantasma tracejado saiu  ✅

Pedido: "remova ele do sistema, pois só está atrapalhando".

Ele desenhava o braço onde a **contagem de pulsos** achava que ele
estava, em vermelho tracejado, sempre que contagem e encoder discordavam
de mais de meio grau. A ideia era boa — dar a *ver* o desvio em vez de
obrigar a ler um número — e foi ele que tornou o defeito de R147 visível
na foto do relato.

Mas o critério de aparecer estava errado por construção: **durante todo
movimento a contagem vai à frente do braço por causa da rampa**. O
firmware já sabe disso e ignora esse período no seu próprio alerta de
fora-de-posição; o desenho não ignorava. Resultado: o tracejado piscava a
cada viagem, em condição perfeitamente normal. Alarme que toca em
condição normal ensina a ignorar alarme — e aí ele não serve nem quando
o desvio é de verdade.

Saiu junto tudo que só existia para ele: `DESVIO_VISIVEL`, e os campos
`c1`, `c2` e `desvio` de `postura()`. A postura agora carrega **só** o
que se desenha, e o que se desenha vem só do encoder.

**Quem compara comandado com medido continua existindo, em dois lugares
melhores:** a **régua do rodapé**, que mostra os dois números lado a lado
em corpo 28, e o **aviso de desvio** da saúde da máquina, que conta e
denuncia sem depender de alguém estar olhando o desenho na hora certa.

O cenário novo prende a ausência com uma divergência enorme — comandado
0°, medido 60° — e confirma que o desenho continua mostrando **um braço
só**, o medido.

## R155 · Arquivos virou gaveta de tela cheia  ✅

Pedido: "a aba Arquivos deve abrir um novo sistema, igual à Configuração
que abre por inteiro na tela; e o Files do lado direito deve ser
removido dali".

Arquivos era um terço de coluna ao lado do desenho do braço — e ali uma
lista de trabalhos nunca coube. Guardar e abrir trabalho é uma
**biblioteca**: quer largura, e não se olha para o braço enquanto se
escolhe arquivo.

Virou gaveta de tela cheia no **mesmo molde da Configuração** —
`veu cfgVeu` + `cx cfgCx` + `cfgRol` —, e se abre pelo atalho **Arquivos**
do cabeçalho, que já estava lá desde R150. Duas gavetas com a mesma forma
são uma coisa só de aprender: mesmo lugar do título, mesmo botão Fechar,
**Esc** fecha, tocar fora fecha, e **só uma fica aberta por vez**.

A aba saiu das **duas** barras (a de baixo, do celular, e a de cima, do
computador), do mapa `PANES` e da ajuda por aba. A tela de trabalho ficou
com quatro abas: Mesa, Mover, Programa, Encoder.

Duas coisas que só apareceram ao mexer:

- **A prévia da peça ficava atrás da gaveta.** `#veuPeca` e a gaveta
  têm o mesmo `z-index`, e com empate ganha quem vem depois no documento
  — a gaveta. Só que a prévia abre *de dentro* dela, então tem de ficar
  por cima: `#veuPeca` subiu para 80.
- **A varredura hostil de botões** percorria os painéis de trabalho e
  depois as páginas da engrenagem. Arquivos saiu dos primeiros, então
  entrou junto das gavetas — botão mudo atrás de uma gaveta continua
  sendo botão mudo, e deixá-lo fora da varredura seria perder a cobertura
  em silêncio.

## R156 · Trava no meio e passa do ponto: uma régua, dois sintomas  `M08` `M09`  ✅

Relato: "começa bem, mas no meio do caminho dá alguns travamentos e nunca
chega; e quando o caminho é curto ele passa e não tem ajuste que o faça
ir para o ponto certo".

Parecem dois defeitos. São **a mesma condição**: `passosPorGrau` (do
catálogo — pulsos por volta × redução) discordando de
`contagensPorGrau` (medido na máquina). Discordar é o normal antes de
calibrar, e a máquina do relato discordava muito.

**1 — a trava no meio do caminho.** O vigia de travamento tinha dois
critérios, e escolhia entre eles:

```c
if (reguaMedida) { /* proporcional */ } else { /* sem escala */ }
```

O ramo proporcional divide por `passosPorGrau` e compara com o encoder.
Com as duas réguas discordando, o **esperado** sai várias vezes maior que
o real, o eixo entrega menos de um quinto do previsto — e um braço
andando perfeitamente é declarado travado meio segundo depois de
arrancar. O banco reproduz o número: pedindo 60°, o eixo andava **1,9°** e
parava com *"Junta 1 travada: o comando anda e o eixo não."*

O próprio comentário do código já dizia qual dos dois não pode mentir:
*"eixo que gira produz contagem, seja qual for a escala"*. Então o teste
sem escala virou **condição necessária** — pulso correndo **e** encoder
parado. O proporcional continua, mas só para **refinar** (pega
escorregão parcial): ele estreita o critério, nunca inventa um
travamento sozinho.

**2 — passa do ponto e não volta.** O retoque anda em **pulsos**: graus
de erro viram passos por `passosPorGrau`. O erro, porém, é medido em
graus do **encoder**. Com as réguas discordando, pedir "ande 2 graus" faz
o eixo andar 4 — passa, volta passando de novo, e o assentamento desiste
dizendo que não aproxima. Reproduzido: pedindo 20°, parava em **18,14°**;
caminho curto de 2°, parava a meio caminho.

**Não há por que adivinhar essa razão — o retoque anterior a mede.** Foi
comandado tanto, o encoder andou tanto: a divisão é o ganho real da
máquina, do jeito que ela está agora. O retoque seguinte já sai dividido
por ele e cai no ponto.

- Enquanto o ganho não foi medido, o retoque sai **amortecido** (70%):
  passar do ponto custa outra viagem.
- Ganho fora de `[0,15 … 6]` não vira régua — ali a medida veio de ruído.
- O ganho é propriedade da **máquina**, não de um movimento: sobrevive de
  um posicionamento para o outro, então o segundo já nasce certo. `M08d`
  prende isso.

Os dois lados foram verificados desligando cada correção sozinha: sem o
critério sem escala, `M09` acusa a trava a 1,9°; sem o ganho aprendido,
`M08` acusa a parada em 18,14° e o caminho curto errando o alvo.

## Cobertura

| banco | rodada 20 | rodada 22 | rodada 24 | agora |
|-------|-----------|-----------|-----------|-------|
| firmware | 229 / 0 | 241 / 0 | 367 / 0 | **477 / 0** |
| interface | 121 / 0 | 125 / 0 | 209 / 0 | **290 / 0** |

E o banco inteiro roda limpo sob AddressSanitizer e UndefinedBehaviorSanitizer
(`testes/sanitizar.sh`).

Guardas automaticas antes de cada compilacao: fiacao, rotas, pagina
comprimida, **os codigos QR lidos por um decodificador de verdade** e a
sintaxe dos sketches avulsos de ferramentas/.
