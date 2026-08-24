# Banco de testes do Robo2dof

Roda o firmware **real** no PC. Os módulos de `Robo2dof/` são compilados sem
nenhuma alteração; o que é substituído por mock é só o que depende do hardware:

| Mock | O que simula |
|------|--------------|
| `mocks/Arduino.h` | `millis()`, GPIO, `Serial` (com contagem de bytes e de mensagens) |
| `mocks/FastAccelStepper.h` | rampa trapezoidal, posição acumulada, `isRunning()` |
| `mocks/Preferences.h` | NVS em RAM |
| `mocks/freertos/` | fila de comandos com capacidade real e contagem de descartes |
| `mocks/SD.h`, `mocks/FS.h`, `mocks/SPI.h` | sistema de arquivos em memória, com cartão ausente e escrita falhando |
| `mocks/WiFi.h` | só para o `.ino` compilar |
| `mocks/driver/uart.h` | as chamadas do IDF que pedem RS485 meio-duplex por hardware, guardando o que foi pedido |
| `mocks/HardwareSerial.h` | UART **com um escravo Modbus dentro** cujo eixo **gira entre as leituras** (e o eco do MAX485, que segue o pino RE de verdade): tabela de registradores de verdade (um ou dois por pergunta), encena mudo, exceção, CRC ruim e driver que **recusa** a leitura dupla |
| `mocks/WebServer.h` | registra as rotas de verdade e despacha um pedido direto no handler |

`servidor_web.cpp` **entra** no banco. Os dois defeitos que o operador sentiu na
mão — botão que não fazia nada e velocidade de cordão que não salvava — moravam
ali, e nenhum teste de motor os pegaria. O que continua faltando de propósito no
mock é socket, concorrência e HTTP de verdade: o banco chama o handler na mesma
thread.

**A assinatura do mock é a assinatura do core, não a conveniente.** Um mock
que aceita mais que a biblioteca de verdade deixa o banco passar limpo e joga
o erro na IDE do operador. Ver [`mocks/LEIA-ME.md`](mocks/LEIA-ME.md).

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
| C01 | a recusa diz onde, qual junta e quanto faltou |
| C02 | braço parado fora da área útil: a recusa aponta para ele |
| C03 | cordão que cabe no curso continua passando |
| E01 | resolução digitada errada é corrigida pelo curso medido |
| E02 | referência gravada fora do zero: o desenho acompanha |
| E03 | quem não preencher nada mantém o comportamento anterior |
| F01 | sem calibração o jog fica livre (era o que impedia calibrar) |
| F02 | apagar a calibração gravada volta ao modo de instalação |
| F03 | braço indo para um lado e o desenho para o outro |
| G01 | engrenagens diferentes, mesma velocidade angular |
| H01 | a velocidade de cordão salva quando muda na tela |
| H02 | suavidade da partida chega nos geradores de pulso |
| H03 | zerar a máquina na posição atual |
| H04 | aferir a redução mecânica pelo movimento real |
| H05 | desenhar na mesa vira programa de pontos |
| H06 | rota inexistente responde 404 em vez de sumir em silêncio |
| I01 | ziguezague de 11 vértices seguido milímetro a milímetro |
| I02 | ziguezague raspando o limite do alcance |
| I03 | a velocidade programada não fica velha entre trechos |
| J01 | Wi-Fi próprio, sem modo estação e sem procurar rede de terceiro |
| J02 | o painel diz por onde se chega nele, e as rotas removidas somem |
| J03 | qualquer endereço digitado cai no painel (DNS de captura) |
| K01 | trocar o sentido do eixo chega ao gerador de pulso e ao NVS |
| K02 | dá para inverter na etapa de referência; depois de medir, não |
| K03 | com o eixo andando a troca de sentido é recusada, com motivo |
| L01 | lê a posição do encoder e não encosta nos pinos de passo/direção |
| L02 | palavra baixa primeiro: o erro que faz a posição saltar |
| L03 | erro comandado − medido, inclusive com o eixo preso |
| L04 | driver mudo, exceção e CRC ruim não viram posição |
| L05 | o módulo nunca escreve, e só se configura em manual |
| L06 | os números medidos na máquina do operador são remontados iguais |
| L07 | falhou: o quadro cru na tela diz o porquê, e junta não ligada não é falha |
| L08 | os 32 bits vêm de **uma** pergunta; driver que recusa é reportado, não contornado |
| L09 | o **DE sobe** a cada pergunta e desce depois — sem isso nada sai no barramento |
| L10 | configuração de encoder de uma versão anterior ganha do padrão novo — e o botão que desfaz |
| L11 | autoteste da linha RS485 dentro do sistema rodando: eco, sondagem e a pergunta de verdade |
| J04 | sondas de captive portal do Windows/Android/iPhone abrem o painel em vez de dar 404 |
| L13 | velocidade, RPM, sentido, inversões e passos andados, com o eixo girando de verdade |
| L12 | achar o registrador movendo o braço **duas vezes no mesmo sentido**; registrador que oscila não é apontado |

Os resultados estão interpretados em [`../ACHADOS.md`](../ACHADOS.md).

## A tarefa de cartão

`armazenamento.cpp` roda numa tarefa própria no core 0. No banco não há
thread: a tarefa é bombeada a mão, um ciclo por milissegundo de simulação
(`armCicloTeste()`, compilado só com `-DROBO2DOF_TESTE`). O mock de
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

Clica **todo** botão de **toda** seção, uma seção aberta por vez, e
reprova se algum estiver invisível dentro da própria seção, não disparar
requisição nenhuma, ou estiver desabilitado sem motivo escrito na tela.
Também confere ids repetidos, botão sem handler e sanfona vazando entre
abas.

Além disso verifica que a página carrega sem erro de JavaScript, que as cinco abas
mostram o conteúdo certo, que o joystick manda `/api/jogxy` proporcional
nos dois eixos, que o botão acompanha o dedo, que soltar e mandar o app
para segundo plano param o jog, que arrastar para fora do disco satura em
vez de soltar o comando, e que a aba Arquivos lista e aciona o cartão.
Deixa as capturas em `testes/saida/ui/`.

O servidor lê `pagina_web.h` ao subir, então ele **precisa** subir junto
do teste — um servidor deixado de pé serve a página antiga em memória.

## Página comprimida

```sh
python3 testes/gerar_pagina_gz.py            # regenera
python3 testes/gerar_pagina_gz.py --conferir # roda junto com compilar.sh
```

O firmware serve `pagina_web_gz.h`, não o HTML cru. A conferência compara
o sha256 do HTML de `pagina_web.h` com o registrado no gerado: se você
editar a interface e esquecer de regenerar, os dois bancos reprovam antes
de o robô servir uma versão diferente da do repositório.

O servidor falso também entrega os bytes comprimidos com
`Content-Encoding: gzip` — o mesmo caminho do ESP32, não o HTML cru.

## Conferência da fiação

```sh
python3 testes/conferir_ligacoes.py     # roda junto com compilar.sh
```

Reprova se `LIGACOES.md` divergir dos pinos de `Robo2dof/config.h`.
Documento de fiação que mente é pior que documento nenhum: o operador
liga o fio no pino errado.

## Conferência das rotas

```sh
python3 testes/conferir_rotas.py        # roda junto com compilar.sh
```

Compara as rotas que `pagina_web.h` chama com as que `servidor_web.cpp`
registra. Rota chamada e não registrada é 404 silencioso — o operador
aperta o botão e nada acontece, sem nenhuma mensagem. Foi exatamente esse
o defeito de "gravar ponto não faz nada". Rota registrada e nunca chamada
sai só como aviso.

## Como o simulador anda

`rodar(ms)` avança `millis()`, integra a rampa dos dois steppers e chama
`loop()` uma vez por milissegundo — a mesma cadência do firmware.
`rodarComWeb(ms)` faz o mesmo mantendo o heartbeat HTTP de 200 ms, para separar
o que é comportamento normal do que é reação à perda de conexão.
