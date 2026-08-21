# Banco de testes do RoboCNC

Roda o firmware **real** no PC. Os módulos de `RoboCNC/` são compilados sem
nenhuma alteração; o que é substituído por mock é só o que depende do hardware:

| Mock | O que simula |
|------|--------------|
| `mocks/Arduino.h` | `millis()`, GPIO, `Serial` (com contagem de bytes e de mensagens) |
| `mocks/FastAccelStepper.h` | rampa trapezoidal, posição acumulada, `isRunning()` |
| `mocks/Preferences.h` | NVS em RAM |
| `mocks/freertos/` | fila de comandos com capacidade real e contagem de descartes |
| `mocks/SD.h`, `mocks/FS.h`, `mocks/SPI.h` | sistema de arquivos em memória, com cartão ausente e escrita falhando |
| `mocks/WiFi.h`, `mocks/WebServer.h` | só para o `.ino` compilar |

`servidor_web.cpp` fica de fora da execução (roda no core 0 e depende de rede),
mas passa por conferência de compilação no mesmo script; quando um cenário
precisa dele, reproduz o que o handler faz.

O banco compila com `-DESTOP_FISICO_INSTALADO=true` para exercitar o ramo da
emergência física. O `config.h` de produção mantém `false` até o botão existir.

## Rodar

```sh
./testes/compilar.sh
```

Cada cenário imprime `PASSA` ou `ANOMALIA`. O critério de cada um está escrito
no próprio `checar(...)`.

## Cenários

| id | O que exercita |
|----|----------------|
| A01 | prioridade da parada de emergência na fila de comandos |
| A02 | jog com os drivers desabilitados |
| A03 | jog de recuperação dentro da faixa da margem de segurança |
| A04 | calibração com curso menor que a margem |
| A05 | queda de Wi-Fi com programa de solda em execução |
| A06 | "ir para o ponto" depois de mudar as proteções |
| A07 | programa cujo deslocamento atravessa zona proibida é recusado |
| A08 | emergência por nível: rearme e jog com o botão acionado |
| A09 | taxa de `definirMensagem()` dentro do laço de controle |
| A10 | pior caso do buffer do JSON de `/api/status` |
| A11 | cancelar a calibração restaura a origem anterior |
| A12 | reprodução de trajetória sem servos |
| A13 | condicionamento da cinemática inversa perto do braço esticado |
| A14 | configuração aplicada só pelo core 1 e só em modo manual |
| A15 | **regressão**: calibrar, ensinar, ensaiar, soldar e reproduzir, ponta a ponta |
| A16 | joystick: zona morta, velocidade proporcional, diagonal, servos desligados |
| B01 | cartão removido e recolocado; a máquina inteira sem cartão |
| B02 | programa: salvar e carregar de volta |
| B03 | programa em graus sobrevive à troca de resolução |
| B04 | arquivo corrompido não derruba o programa da máquina |
| B05 | nome de arquivo vindo de HTTP não escapa da pasta |
| B06 | trajetória binária ida e volta; buffer emprestado bloqueia gravação |
| B07 | backup e restauração de ajustes, com validação de faixa |
| B08 | carregar arquivo não troca o programa em execução |
| B09 | eventos de segurança chegam ao arquivo de log |

Os resultados estão interpretados em [`../ACHADOS.md`](../ACHADOS.md).

## A tarefa de cartão

`armazenamento.cpp` roda numa tarefa própria no core 0. No banco não há
thread: a tarefa é bombeada a mão, um ciclo por milissegundo de simulação
(`armCicloTeste()`, compilado só com `-DROBOCNC_TESTE`). O mock de
sistema de arquivos é instantâneo, então o que se testa é a **lógica** —
o protocolo de troca entre os núcleos, a validação e a degradação sem
cartão — e não a latência real de um SD.

## Banco da interface

```sh
./testes/interface/rodar.sh
```

Sobe um servidor que finge ser o ESP32 (`servidor_falso.py`), serve a
página extraída de `pagina_web.h` e roda a interface num Chromium de
verdade via Playwright, em viewport de celular e de computador.

Verifica que a página carrega sem erro de JavaScript, que as cinco abas
mostram o conteúdo certo, que o joystick manda `/api/jogxy` proporcional
nos dois eixos, que o botão acompanha o dedo, que soltar e mandar o app
para segundo plano param o jog, que arrastar para fora do disco satura em
vez de soltar o comando, e que a aba Arquivos lista e aciona o cartão.
Deixa as capturas em `testes/saida/ui/`.

O servidor lê `pagina_web.h` ao subir, então ele **precisa** subir junto
do teste — um servidor deixado de pé serve a página antiga em memória.

## Como o simulador anda

`rodar(ms)` avança `millis()`, integra a rampa dos dois steppers e chama
`loop()` uma vez por milissegundo — a mesma cadência do firmware.
`rodarComWeb(ms)` faz o mesmo mantendo o heartbeat HTTP de 200 ms, para separar
o que é comportamento normal do que é reação à perda de conexão.
