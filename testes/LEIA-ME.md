# Banco de testes do RoboCNC

Roda o firmware **real** no PC. Os módulos de `RoboCNC/` são compilados sem
nenhuma alteração; o que é substituído por mock é só o que depende do hardware:

| Mock | O que simula |
|------|--------------|
| `mocks/Arduino.h` | `millis()`, GPIO, `Serial` (com contagem de bytes e de mensagens) |
| `mocks/FastAccelStepper.h` | rampa trapezoidal, posição acumulada, `isRunning()` |
| `mocks/Preferences.h` | NVS em RAM |
| `mocks/freertos/` | fila de comandos com capacidade real e contagem de descartes |
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

Os resultados estão interpretados em [`../ACHADOS.md`](../ACHADOS.md).

## Como o simulador anda

`rodar(ms)` avança `millis()`, integra a rampa dos dois steppers e chama
`loop()` uma vez por milissegundo — a mesma cadência do firmware.
`rodarComWeb(ms)` faz o mesmo mantendo o heartbeat HTTP de 200 ms, para separar
o que é comportamento normal do que é reação à perda de conexão.
