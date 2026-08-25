# Robo2dof — manual do sistema

Braço de solda de **dois eixos**, comandado por um ESP32 que serve um
painel web pela sua própria rede Wi-Fi. Sem nuvem, sem internet, sem
aplicativo para instalar: entra-se na rede do robô e abre-se o painel.

Este documento é o mapa completo. Para a explicação de *como as coisas
foram descobertas* (e dos defeitos que apareceram no caminho), veja
[`ACHADOS.md`](ACHADOS.md).

---

## 1. O que é a máquina

| | |
|---|---|
| Estrutura | braço planar de 2 graus de liberdade (dois eixos girando no mesmo plano horizontal) |
| Motores | 2 × servo AC **80AST-A1C04025** |
| Drivers | 2 × **HLTNC T3D-L20A** |
| Cérebro | ESP32 (WROOM-32), dois núcleos |
| Realimentação | encoder do próprio servo, lido pelo driver por **Modbus RTU / RS485** |
| Interface | página web servida pelo ESP32, na rede Wi-Fi que ele mesmo cria |
| Armazenamento | NVS interna (configuração) + cartão microSD (programas e trajetórias) |

### A regra de ouro do projeto

> **O núcleo 1 manda no motor. O núcleo 0 nunca toca no motor.**

O núcleo 1 roda o `loop()`: geração de pulso, estado da máquina, relé de
solda. O núcleo 0 roda tudo que espera por alguém: servidor web, Wi-Fi,
cartão, leitura do encoder.

A comunicação entre eles é de mão única e por duas estruturas apenas:

- **`Comando`** — o núcleo 0 enfileira, o núcleo 1 consome.
- **`Snapshot`** — o núcleo 1 publica, o núcleo 0 lê.

Nenhum handler web chama `posicaoJ1()`. Nenhuma tarefa de rede move um
eixo. Quando essa regra foi quebrada (aconteceu duas vezes), o sintoma
foi leitura corrompida e quadro perdido — está registrado em `ACHADOS.md`.

---

## 2. Ligações

A tabela oficial e conferida por teste é [`LIGACOES.md`](LIGACOES.md) —
o script `testes/conferir_ligacoes.py` reprova se ela discordar de
`Robo2dof/config.h`. Resumo:

### Motores

| Sinal | GPIO | Observação |
|---|---|---|
| J1 pulso | 16 | vai para `PUL+` via buffer 5 V |
| J1 direção | 17 | `DIR+` |
| J2 pulso | 18 | |
| J2 direção | 19 | |
| Habilita servos | 23 | `SON` dos dois drivers, via optoacoplador |
| Alarme J1 | 34 | **só entrada**, precisa de pull-up externo de 10 k |
| Alarme J2 | 35 | idem |

> GPIO 34 e 35 não têm pull-up interno. Sem o resistor externo, o pino
> flutua e o firmware lê ruído. O flag `ALARME_FISICO_INSTALADO` fica
> `false` até a fiação existir — foi exatamente esse o travamento da v2.1.

### Solda e emergência

| Sinal | GPIO | Observação |
|---|---|---|
| Relé da solda | 2 | ativo em ALTO, com **pull-down físico de 10 k** para GND |
| Botão de emergência | 27 | contato NC: LOW = emergência |

> O GPIO 2 é o LED da placa — útil na bancada, ruim na máquina: é
> *strapping pin* e pisca sozinho durante o boot, o que num relé de solda
> significa abrir arco na hora de energizar. Na máquina de verdade,
> troque para o 26 (e então `PIN_LED_STATUS` tem de ficar em 255).

### Cartão microSD (SPI)

| Módulo | GPIO | |
|---|---|---|
| 3V3 | 3V3 | **nunca no 5 V** neste módulo: ele não tem regulador |
| CS | 5 | |
| SCK | 14 | |
| MOSI | 13 | |
| MISO | **25** | *não* o 12 |

> O GPIO 12 é o MISO "padrão" do HSPI, e o módulo tem pull-up de 10 k
> nessa linha. O 12 é strapping (MTDI): alto no boot programa o regulador
> do flash para 1,8 V e a placa não dá mais boot. Por isso o MISO vai
> para o 25 — o ESP32 remapeia SPI por matriz, então não se perde nada.
>
> Ponha 10 µF cerâmico entre 3V3 e GND junto ao módulo: o cartão puxa
> picos de ~100 mA na escrita.

### RS485 (leitura do encoder)

| MAX485 | GPIO | |
|---|---|---|
| RO | 22 | via **divisor de 2 resistores**, não conversor de MOSFET |
| DI | 21 | |
| DE | 4 | 1 = transmitindo |
| RE | 26 | 0 = ouvindo |
| A / B | driver | trocados = silêncio absoluto |
| GND | comum | obrigatório |

> **A UART2 do ESP32 tem 16 e 17 como pinos padrão** — que neste projeto
> são o passo e a direção da junta 1. Todo `begin()` deste projeto passa
> RX e TX explicitamente, e o cenário `L01c` reprova se alguém esquecer.

---

## 3. Configuração do driver T3D-L20A

O mapa Modbus do T3D **não é publicado** e muda por versão de firmware.
Os valores abaixo foram **medidos** nesta máquina com
`ferramentas/teste_rs485`.

| | |
|---|---|
| Velocidade | **19200 8N1** |
| Endereço Modbus | **1** (junta 1) |
| Função | **3 — holding registers** |
| Posição | registrador **90** (palavra baixa) e **91** (alta) |
| Ordem | **palavra baixa primeiro** |
| Contagens por volta | **131 072** (encoder de 17 bits) |

### Como esses números foram provados

Três caçadas separadas, em posições diferentes do eixo, montando 90/91
como 32 bits com a baixa primeiro:

| caçada | de | para | variou |
|---|---|---|---|
| A | 9 361 | 15 841 | +6 480 |
| B | 15 842 | 124 571 | +108 729 |
| C | 124 574 | 42 069 | −82 505 |

O valor é **contínuo entre caçadas** — A termina em 15 841 e B começa em
15 842. Coincidência não é contínua três vezes. E na caçada B o
registrador 90 deu a volta e o 91 subiu de 0 para 1, que é exatamente o
que uma palavra baixa e uma alta fazem.

### Registradores que enganam

**92, 93 e 94 não são a posição.** Eles mudam quando o eixo gira e
chegam a dar o maior salto da lista, mas:

- andam **juntos** (os dois +6, depois os dois −23);
- vão para valores negativos (65 530 lidos sem sinal são **−6**);
- **voltam para perto de zero quando o eixo para**.

É erro de seguimento e velocidade. A posição não volta. Por isso a
caçada do sistema exige **dois giros no mesmo sentido** antes de apontar
um par.

### Parâmetros do driver a conferir

Pelo painel do próprio driver (não pelo Modbus — este sistema **nunca**
escreve registrador):

| | |
|---|---|
| Porta RS485 | **habilitada** — muitos T3D saem de fábrica com ela desligada |
| Endereço | 1 na junta 1, 2 na junta 2 (endereços diferentes no mesmo barramento) |
| Velocidade | 19200, 8N1 |
| Engrenagem eletrônica | define os **pulsos por volta** que o firmware precisa saber (`PASSOS_POR_VOLTA_PADRAO`, 10000 de fábrica) |
| Modo de comando | pulso + direção, coletor aberto |

---

## 4. Como o firmware é organizado

| Arquivo | O que faz |
|---|---|
| `Robo2dof.ino` | `setup()`, `loop()`, máquina de estados dos modos, `processarComando()` |
| `config.h` | pinos, limites elétricos e mecânicos, padrões de fábrica |
| `estado.h/.cpp` | `Junta`, `Snapshot`, fila de comandos, configuração e NVS |
| `motores.h/.cpp` | FastAccelStepper, jog, movimento coordenado, parada |
| `cinematica.h/.cpp` | direta e inversa do braço 2R, validação de postura e de caminho |
| `trajetoria.h/.cpp` | gravação e reprodução a mão livre |
| `programa.h/.cpp` | pontos, cordões em reta interpolada, deslocamentos |
| `calibracao.h/.cpp` | assistente de curso das juntas |
| `solda.h/.cpp` | relé, com as proteções |
| `armazenamento.h/.cpp` | cartão SD, tarefa própria no núcleo 0 |
| `rede.h/.cpp` | ponto de acesso, mDNS, DNS de captura |
| `servidor_web.cpp` | todas as rotas HTTP |
| `encoder.h/.cpp` | mestre Modbus, tarefa própria no núcleo 0 |
| `correcao.h/.cpp` | assentamento de posição pelo encoder |
| `pagina_web.h` | a interface inteira, um arquivo — **é aqui que se edita** |
| `pagina_web_gz.h` | gerado; `testes/gerar_pagina_gz.py` refaz |

### Modos da máquina

| Modo | O que acontece |
|---|---|
| `MANUAL` | jog liberado, ajustes permitidos |
| `GRAVANDO` | jog + amostragem da trajetória a mão livre |
| `REPRODUZINDO` | repete a trajetória gravada |
| `EXECUTANDO` | roda o programa de pontos |
| `POSICIONANDO` | indo para um alvo — e, ao chegar, **assentando pelo encoder** |
| `CALIBRANDO` | assistente de curso |
| `FALHA` | parada suave, nada anda |

---

## 5. Funcionalidades

### 5.1 Comando manual

**Joystick** dos dois eixos: quanto mais longe do centro, mais rápido.
Zona morta de 12 %. Exige confirmação a cada 350 ms — se o navegador
travar ou a tela apagar, **o eixo para sozinho**.

**Passo a passo**: setas ↺ / ↻ por junta. O sentido de cada eixo é
invertível (Ajustes, ou durante a etapa de referência da calibração):
nem sempre o sentido das setas bate com o do motor.

### 5.2 Calibração do curso

O braço tem **limite físico**: sem saber onde ele está, o firmware não
tem como impedir uma batida. A calibração ensina isso.

O assistente percorre: referência → limite negativo da J1 → volta →
limite positivo da J1 → o mesmo para a J2. Em cada limite, o operador
leva o eixo com o jog até onde ele *pode* ir e confirma.

Depois disso o firmware conhece `passosMin`/`passosMax` de cada junta, e
**recusa** qualquer movimento — jog, ponto, cordão — que saia daí. A
barra de curso no painel mostra onde o eixo está dentro do que foi
medido.

> Isso é uma **proteção por software**. Ela vale enquanto a calibração
> corresponder à máquina. Trocar a montagem, afrouxar o acoplamento ou
> mexer na redução invalida a calibração — recalibre.

### 5.3 Mesa de traçado

Vista de cima, em milímetros de verdade. Mostra o braço, o alcance, o
curso calibrado, o programa e a trajetória gravada. Dá para **desenhar o
caminho com o dedo** e transformar o traço em programa.

**Vista 3D** (botão `3D`): a mesma máquina de outro ângulo, com a altura
dos elos e a ferramenta descendo até a peça. Serve para enxergar a
máquina; **desenhar e escolher pontos continua na vista de cima**, porque
um traço em perspectiva não tem onde cair na mesa.

### 5.4 Programa de pontos

Até **120** pontos. Entre dois pontos, o trecho é:

- **cordão** — reta cartesiana interpolada a cada 1,5 mm, com a solda
  ligada;
- **deslocamento** — interpolação nas juntas, mais rápida, sem solda.

O sistema **recusa** cordões que o braço não consegue percorrer e diz
qual trecho e por quê — inclusive quando o ponto é alcançável mas o
*caminho até ele* não é.

> O ramo do cotovelo é **travado por segmento**. Sem isso a cinemática
> inversa reescolhia o cotovelo a cada 1,5 mm e o braço fazia uma
> circunferência no meio da reta. Está em `ACHADOS.md`, achado A13.

### 5.5 Leitura do encoder

Aba/coluna **Encoder**. No computador ela fica **aberta o tempo todo** ao
lado da mesa: a leitura existe para ser acompanhada *enquanto* se mexe no
resto. No celular volta a ser aba.

Mostra: comandado, medido, erro, velocidade, RPM, sentido, passos
andados, inversões, faixa percorrida, gráficos de erro e de posição,
tabela das amostras e **CSV** com a janela inteira.

Ferramentas de diagnóstico embutidas:

| | |
|---|---|
| **quadro cru** | os bytes que saíram e voltaram, em hexadecimal |
| **Testar a linha agora** | eco no MAX485 + sondagem do driver, *dentro* do sistema rodando |
| **Procurar o registrador** | acha o par da posição movendo o braço duas vezes no mesmo sentido |
| **Voltar aos padrões medidos** | desfaz configuração antiga herdada do NVS |

O diagnóstico sai **também no monitor serial**, uma linha a cada 5 s
enquanto falha.

### 5.6 Correção de posição pelo encoder

Quando o braço chega, o encoder diz onde ele **realmente** parou e o
sistema dá um retoque curto.

**Não é malha fechada de servo.** Cada leitura Modbus custa 5 a 20 ms com
jitter — corrigir o eixo *enquanto ele anda* faria o braço oscilar. É
assentamento no fim do movimento, e é isso que resolve o incômodo real:
*sair de uma posição e voltar cair no mesmo lugar*.

Duas coisas fazem isso funcionar de verdade:

1. **O erro se mede contra o alvo, não contra o comandado.** O comandado
   sai da contagem de passos, e a contagem anda junto com o retoque:
   medir contra ela daria sempre a mesma diferença e o retoque nunca
   fecharia.
2. **No fim, a contagem volta ao alvo** (sem emitir pulso). Sem isso o
   desvio não some — ele só muda de lugar, e o próximo movimento absoluto
   nasce errado pelo mesmo tanto.

**Seis regras, todas com cenário no banco de testes:**

| | |
|---|---|
| 1 | só com o eixo **parado** |
| 2 | só com leitura **válida e recente** |
| 3 | nunca fora do **curso calibrado** |
| 4 | **nunca com a solda ligada** |
| 5 | erro acima do teto **não se corrige, se denuncia** |
| 6 | número de tentativas limitado |

A regra 5 é a que mais importa: vários graus de erro **não é folga**. É
acoplamento solto, registrador errado ou redução errada — e empurrar o
braço achando que está consertando é a maneira mais rápida de bater a
ferramenta em alguma coisa.

Ajustes: tolerância (0,10° de fábrica), teto do retoque (3°), aviso de
desvio (1°), tentativas (3). Tudo desligável.

**Vigilância**: com o eixo parado, se o erro passar do limite por mais de
um segundo, o painel avisa. Não mexe no motor — só conta e avisa.

### 5.7 Solda

Relé com proteções: não liga sem servos, não fica ligado com o braço
parado por engano, e a parada de emergência corta o arco antes de
qualquer outra coisa.

### 5.8 Cartão SD

Programas, trajetórias e cópias de configuração. Tarefa própria no núcleo
0. Ver [`CARTAO_SD.md`](CARTAO_SD.md).

### 5.9 Rede

A máquina **cria** a rede `Robo2dof`. Não entra na rede de ninguém, não
procura roteador, não fala com a internet.

| Como chegar | |
|---|---|
| IP | `192.168.4.1` |
| Nome | `robo2dof.local` (precisa de mDNS/Bonjour no Windows) |

Ao entrar na rede, **o painel abre sozinho**: as sondas de captive portal
do Windows, Android e iPhone levam redirecionamento para 192.168.4.1.

---

## 6. Rotas HTTP

Conferidas por `testes/conferir_rotas.py`, que reprova rota registrada e
nunca chamada, ou chamada e nunca registrada.

| Rota | |
|---|---|
| `GET /api/status` | tudo que a tela mostra, num JSON |
| `POST /api/jogxy`, `/api/jog` | comando manual |
| `POST /api/mover`, `/api/moverxy` | ir para ângulos / coordenada |
| `POST /api/parar` | parada |
| `POST /api/solda` | relé |
| `POST /api/prog/*` | programa de pontos |
| `POST /api/traj/*` | trajetória a mão livre |
| `POST /api/calib/*` | assistente de calibração |
| `POST /api/config` | parâmetros da máquina |
| `POST /api/sentido` | inverter o sentido de um eixo |
| `POST /api/referenciar` | zerar a máquina na posição atual |
| `POST /api/aferir/*` | aferir a redução mecânica pelo movimento real |
| `GET  /api/encoder` | leitura, derivados, configuração e quadro cru |
| `POST /api/encoder/config` | ligação Modbus |
| `POST /api/encoder/padroes` | volta aos padrões medidos |
| `POST /api/encoder/testar`, `GET /api/encoder/teste` | autoteste da linha |
| `POST /api/encoder/cacar` | caçada do registrador |
| `POST /api/encoder/zerar` | zera a contagem aqui |
| `POST /api/correcao` | assentamento pelo encoder |
| `GET  /api/sd/*`, `POST /api/sd/*` | cartão |
| `GET  /api/rede` | por onde chegar no painel |

---

## 7. Bancos de teste

Duas suítes, ambas rodam no PC, sem hardware.

```sh
./testes/compilar.sh          # firmware de verdade, com mocks só no hardware
./testes/interface/rodar.sh   # a página num Chromium de verdade
```

O firmware **não é reescrito** para testar: os módulos de `Robo2dof/`
entram como estão. O que é substituído por mock é só o que depende de
hardware — e a regra é dura:

> **A assinatura do mock é a assinatura do core, não a conveniente.**

Um mock que aceita mais que a biblioteca de verdade deixa o banco passar
limpo e joga o erro na IDE do operador. Isso aconteceu, está registrado,
e a regra nasceu daí.

O mock do driver Modbus responde de verdade: CRC, ordem das palavras,
exceção, silêncio, driver que recusa pergunta dupla, e um eixo que
**gira entre as leituras**.

Guardas automáticas: `conferir_ligacoes.py`, `conferir_rotas.py`,
`gerar_pagina_gz.py --conferir`, e uma varredura que clica em **todo**
controle da interface.

---

## 8. Ferramentas de bancada

`ferramentas/` tem dois programas independentes, para gravar no lugar do
firmware:

| | |
|---|---|
| [`teste_rs485/`](ferramentas/teste_rs485/) | diagnóstico do barramento: autoteste do módulo, procura o driver, varre registradores, **caça o registrador da posição**, mede as contagens por volta, CSV |
| [`monitor_encoder/`](ferramentas/monitor_encoder/) | monitor ao vivo: posição, velocidade, RPM, sentido, passos, gráfico ASCII, CSV |

Eles rodam com o ESP32 **sozinho** na placa. É isso que separa "o
barramento não presta" de "algo no sistema atrapalha o barramento" — e foi
exatamente assim que se achou o defeito do DE.

---

## 9. Manutenção

### Editar a interface

Edite `Robo2dof/pagina_web.h` e rode:

```sh
python3 testes/gerar_pagina_gz.py
```

O banco reprova se o comprimido ficar velho.

### Gravar

Arduino IDE, placa ESP32 Dev Module, **Partition Scheme: Huge APP
(3 MB No OTA)**. O firmware avisa no boot se a partição estiver errada.

### Depois de gravar uma versão nova

Atualizar o firmware **não apaga o NVS**. Configuração gravada por uma
versão anterior continua valendo e ganha do padrão novo. Se algo parou de
funcionar depois de uma atualização, **"Voltar aos padrões medidos"** é o
primeiro a tentar.

### Quando o encoder não lê

Nesta ordem:

1. **Voltar aos padrões medidos** — o mais barato.
2. **Testar a linha agora** — a linha do `eco` decide: voltou = ESP32↔MAX485
   está bom e o problema é o barramento; não voltou = o problema nem
   chegou no par A/B.
3. Olhe o **quadro cru**: `(silencio)` = ninguém respondeu (fio A/B,
   DE/RE, endereço); bytes sem leitura = respondeu outra coisa (função ou
   registrador).
4. Grave `ferramentas/monitor_encoder` e veja se ele lê. Se ele lê e o
   sistema não, a diferença está no sistema.
