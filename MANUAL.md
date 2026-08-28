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
| `correcao.h/.cpp` | assentamento de posição pelo encoder, seguir o eixo solto, zero absoluto |
| `aprender.h/.cpp` | modo aprendizado e o botão físico da ponteira |
| `ota.h/.cpp` | atualização de firmware pela rede |
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

### 5.0 Onde fica cada coisa na tela

A tela de trabalho tem cinco abas, e só elas:

| aba | para quê |
|---|---|
| **Mesa** | traçado, desenho e a vista 2D/3D do braço |
| **Mover** | joystick, ir para ângulo, atalhos |
| **Programa** | ensinar o caminho, ensaiar, soldar, produção |
| **Arquivos** | cartão: peças, trajetórias, backups |
| **Encoder** | a leitura ao vivo, análise e diagnóstico da linha |

Tudo que se ajusta **uma vez** mora atrás da **engrenagem** do cabeçalho,
numa gaveta com três páginas:

| página | o que tem |
|---|---|
| **Máquina** | elos, velocidades, acelerações, resolução, sentido dos eixos, proteções |
| **Calibração** | resolução e redução medidas, curso das juntas, área da mesa |
| **Encoder** | correção de posição, zero absoluto, ligação Modbus |
| **Sistema** | saúde, registro de eventos, QR de conexão, firmware, modo operador, idioma |

A divisão é essa: **operar** fica nas abas, **instalar** fica na gaveta.

> O **cabeçalho e a barra de abas ficam acima da gaveta**, e continuam
> clicáveis com ela aberta. O botão PARAR mora no cabeçalho — parada de
> emergência que exige fechar uma janela antes não é parada de
> emergência.

A gaveta fecha pelo X, pelo **Esc**, tocando fora dela, ou escolhendo uma
aba de trabalho. No modo operador ela abre direto em **Sistema**, com as
duas páginas de instalação escondidas.

### 5.1 Comando manual

**Joystick** dos dois eixos: quanto mais longe do centro, mais rápido.
Zona morta de 12 %. Exige confirmação a cada 350 ms — se o navegador
travar ou a tela apagar, **o eixo para sozinho**.

**Passo a passo**: setas ↺ / ↻ por junta. O sentido de cada eixo é
invertível (Ajustes, ou durante a etapa de referência da calibração):
nem sempre o sentido das setas bate com o do motor.

### 5.2 Calibração

Gaveta da engrenagem, página **Calibração**. Cinco coisas, medidas em
ordem.

#### A conta que a máquina faz

```
passosPorGrau = passosPorVolta × redução ÷ 360
```

São **dois** números, e cada um erra de um jeito diferente. Por isso os
passos 1 e 2 medem um de cada vez.

#### Passo 1 — Engrenagem eletrônica (sem instrumento)

Quantos passos o driver precisa para dar uma volta no motor. É parâmetro
do T3D e é o número que mais se erra: troca-se o driver, refaz-se um
parâmetro, e o declarado deixa de bater. O sintoma é o braço andar menos
(ou mais) do que a tela diz, sem nada apontar para o culpado.

O encoder mede isso **sozinho**: manda-se um tanto conhecido de passos e
pergunta-se quantas voltas o motor deu.

#### Passo 2 — Redução mecânica

> **Leia isto antes de achar que o encoder resolve sozinho.**
>
> O encoder está no eixo do **motor**, antes do redutor. O ângulo que ele
> mostra na tela já é calculado assim:
>
> `graus da junta = voltas do motor × 360 ÷ redução`
>
> Ou seja: **o ângulo lido já depende da redução.** Não dá para tirar a
> redução dele — seria tirar o número de uma conta que usa o próprio
> número. Isso é física, não limitação de programa: com um sensor só, e
> antes do redutor, a relação do redutor é invisível.

O que o encoder dá de graça, e com muita precisão, é a contagem de
**voltas do motor**. Falta **uma** referência do lado da junta. Com ela a
redução sai exata:

```
redução = voltas do motor × 360 ÷ ângulo real da junta
```

**De onde tirar a referência**, da melhor para a pior:

| | método | por quê |
|---|---|---|
| 1 | **Esquadro (90°)** | preciso, e todo mundo tem um. É o recomendado |
| 2 | **Curso entre batentes** | maior ângulo disponível → menor erro relativo |
| 3 | **Volta completa** | se a junta der uma, não precisa de instrumento nenhum |

**Por que isto é melhor do que a medida antiga.** A anterior contava
*pulsos comandados*: ela erra junto com a engrenagem eletrônica (se
`passosPorVolta` estiver errado, a redução sai errada na mesma proporção)
e erra junto com perda de passo (o eixo escorrega e a conta nem fica
sabendo). Contar voltas reais do motor não tem nenhum dos dois problemas
— o encoder mede o eixo, não a intenção. Cenário **U01** prova exatamente
isso: mede certo com o eixo escorregando metade do caminho.

O que a medida **recusa** (cenário **U02**): menos de um quarto de volta
do motor, ângulo de referência menor que 5°, e qualquer resultado fora de
0,5:1 a 1000:1 — que só pode significar que a referência informada não
bate com o que o eixo andou.

#### Passo 3 — Curso das juntas

O assistente de sempre, agora acessível de dentro da aba. Ver §5.2.1.

#### Passo 4 — Área da mesa

A área útil deixou de ser dois números digitados e passou a ser
**ensinada**: leva-se a ponta a cada canto e grava. O retângulo é a caixa
que contém os cantos ensinados (dois opostos bastam; mais cantos só
melhoram).

Dali para fora **o braço não anda** — nem por programa, nem pelas setas.
A checagem é da **ponta**, não do cotovelo: o cotovelo passa por cima da
mesa o tempo todo e não solda nada.

> Se a ponta parar fora da área, só o movimento que a traz de volta é
> liberado. A área entra na mesma conta de gravidade que os limites de
> curso, então o jog de recuperação funciona igual — **o braço nunca se
> prende do lado de fora da própria mesa**. Cenário **U03f**.

Sem área ensinada a máquina se protege como antes, pelo **Y mínimo** e
pelo **raio morto da base** — que continuam valendo em qualquer caso: o
raio da base é mecânica, não mesa.

#### Passo 5 — Conferir

O quadro do topo da página mostra **comandado × medido** para as duas
juntas, ao vivo. Se os dois andarem juntos depois de um movimento, a
resolução está certa. Se o medido andar menos, a redução declarada está
maior que a real.

#### Onde isto fica guardado

Tudo — resolução, redução, curso, referência e a área da mesa — vai para
a memória da máquina assim que se confirma, e sobrevive à queda de
energia. No cartão, o backup em **Arquivos → ajustes** leva a calibração e
a mesa **junto**. Backup gravado por uma versão anterior não apaga
nenhuma das duas: o que o arquivo não traz, ele não mexe.

### 5.2.1 Calibração do curso

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

#### Encoder absoluto: a máquina se localiza sozinha

O encoder do servo **guarda a posição com a máquina desligada**. Se
alguém empurrar o braço à mão com tudo apagado, ao ligar ele sabe. Isso
**dispensa fim de curso**: em vez de procurar batente, a máquina lê onde
está.

Para isso ela precisa saber uma coisa só: **qual contagem crua do encoder
corresponde a 0°**. Ensina-se uma vez, na página avançada (aba Encoder →
*Zero absoluto* → cadeado).

| | |
|---|---|
| **Ensinar** | leve o braço a uma postura que você sabe medir, informe o ângulo real. Não precisa ser 0 |
| **Ao ligar** | a contagem de passos é acertada pelo encoder, e a máquina já nasce sabendo onde está |
| **Ir para 0°** | opcional: depois de se localizar, o braço vai para o zero |

**De fábrica o zero NÃO vem ensinado.** Uma máquina recém-montada
acreditaria que a contagem crua 0 do encoder é o zero da junta — um
número arbitrário — e iria para lá sozinha. Enquanto ninguém ensinar, a
máquina liga exatamente como antes.

**O que impede a ida automática ao zero:**

| | |
|---|---|
| zero não ensinado | não há referência em que acreditar |
| **servos desabilitados** | é o intertravamento: habilitar servos é uma ação sua na tela, e enquanto ninguém habilitar o braço não tem como andar |
| sem leitura do encoder | desiste depois de 5 s e avisa — máquina que não liga é pior que máquina desorientada |
| zero fora do curso calibrado | não vai: furar a proteção seria pior que não ir |
| solda ligada, ou fora do manual | não vai |

> A página fica atrás de um cadeado, e o cadeado **volta a fechar em toda
> visita**. Não é senha: é um tranco para não se mexer sem querer. O que
> está atrás dele é a origem de onde os limites de curso são contados —
> errar ali desloca a área útil inteira.

#### O encoder simplifica duas coisas na calibração

**1. Aferir a engrenagem eletrônica sem transferidor.**

A resolução de uma junta é `passosPorGrau = passosPorVolta × redução / 360`.
São dois números, e cada um erra de um jeito:

| | |
|---|---|
| **redução** | é mecânica, está no redutor, você sabe qual comprou — e o encoder **não consegue medi-la**, porque conta no eixo do *motor*, antes do redutor |
| **engrenagem eletrônica** (`passosPorVolta`) | é parâmetro do driver, e é o que mais se erra: troca-se o driver, refaz-se um parâmetro, e o número declarado deixa de bater |

O sintoma de errar a engrenagem eletrônica é o braço andar menos (ou
mais) do que a tela diz, sem nada apontar o culpado.

Isso o encoder mede sozinho: **Marcar o início → girar bastante → Aferir
pelo encoder**. Ele conta as voltas do motor e a conta sai. Some um dos
dois números da equação; a redução continua sendo declarada por quem
montou a máquina.

**2. Detecção de travamento.**

Antes do encoder, encostar no batente era invisível para o firmware: ele
continuava contando pulsos, o driver continuava recebendo, e o motor
ficava **forçando contra o ferro**.

Agora isso é mensurável: comando andando + medido parado = o eixo
encostou em alguma coisa (ou o acoplamento soltou, ou o driver desarmou).
O sistema **para o eixo** e avisa. O aviso fica na tela até você dizer que
resolveu — aviso que some sozinho é aviso que ninguém leu.

É deliberadamente conservador, porque um falso positivo pararia o braço
no meio de um cordão:

| | |
|---|---|
| só julga com o comando **claramente** andando | perto de zero a conta não distingue parado de travado — e parado não está forçando nada |
| exige o medido **claramente** parado | menos de um quinto do esperado |
| por **meio segundo** | a leitura vem a 20 Hz: menos que isso seria julgar com duas ou três amostras |
| **sem leitura, se cala** | cabo solto no encoder não pode parar o braço no meio de um cordão |

### 5.2.2 O desenho mostra onde o braço ESTÁ

O boneco 2D e o 3D eram desenhados com o ângulo **comandado** — a conta de
pulsos do firmware. Isso desenha a intenção, não o braço: se o eixo
escorregou, a tela continua mostrando tudo no lugar enquanto a peça sai
torta.

Agora o boneco é a posição **medida pelo encoder**. Quando as duas
discordam de mais de meio grau, o comandado aparece por trás como um
**fantasma tracejado**: dá para *ver* o desvio, em vez de só ler um
número. A legenda do rodapé diz qual das duas está sendo desenhada.

Sem leitura confiável (encoder desligado, cabo solto, leitura fora do
curso) volta a valer o comandado — e a legenda diz isso. Um boneco que
muda de significado sem avisar é pior que nenhum.

A área da mesa ensinada aparece nas duas vistas, tracejada.

**O 3D também mudou por dentro.** A ordem de desenho era fixa — base, elo
1, cotovelo, elo 2 — e com o cotovelo dobrado *para trás* o elo 2 era
pintado por cima do elo 1 mesmo estando atrás dele na cena. O braço saía
recortado errado em metade das posturas, e era isso que fazia o desenho
parecer quebrado. Agora cada peça declara a profundidade do seu ponto
médio e o conjunto é pintado do fundo para a frente. As caixas dos elos
ganharam tampa nas pontas e as laterais também são ordenadas.

> O achatamento vertical do 3D é **exagero declarado**: um braço de 850 mm
> de alcance tem 110 mm de altura, e na proporção real ele sai achatado a
> ponto de não se ler qual elo passa por cima de qual. O exagero é só no
> Z — X e Y saem na escala.

### 5.3 Mesa de traçado

Vista de cima, em milímetros de verdade. Mostra o braço, o alcance, o
curso calibrado, o programa e a trajetória gravada. Dá para **desenhar o
caminho com o dedo** e transformar o traço em programa.

**Explicações ocultáveis** (botão `?` no cabeçalho): as notas em cinza
ensinam quem está começando e atrapalham quem opera todo dia — elas
ocupam mais coluna que os controles. O `?` esconde todas de uma vez, os
controles ficam, e a escolha é gravada.

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

### 5.4.1 Modo aprendizado: ensinar o caminho com a mão

Módulo `aprender.h/.cpp`. Um botão físico na ponteira (GPIO 32) com dois
gestos, e o mesmo modo disponível na tela para quem não instalou o botão.

| gesto | o que faz |
|---|---|
| **segurar 1,5 s** | entra ou sai do modo aprendizado |
| **toque curto** | grava o ponto onde a ponta está agora |

Dentro do modo, se der para soltar o braço, o torque cai e você leva a
ponteira com a mão: encosta no início do cordão e toca, leva até o fim e
toca. O programa nasce da peça, não da tela.

**O que faz isso ser possível.** Motor solto anda sem que nenhum pulso
saia no fio — a contagem do firmware ficaria parada e todo ponto sairia
gravado no mesmo lugar. Quem resolve é `seguirEixoSolto()`
(`correcao.h`), que acerta a contagem pelo encoder absoluto enquanto o
braço está solto. É por isso que o modo depende do encoder, e não de
mais um sensor.

**Quando o braço NÃO é solto.** Só quando as duas juntas estão no
barramento com o zero absoluto ensinado. O SON é um fio só para os dois
drivers: soltar por causa da junta 1 solta a 2 junto, e uma junta que
cai sem ninguém medindo grava ponto torto sem avisar. Faltando isso o
modo entra assim mesmo, com torque — você posiciona pelas setas e grava
igual. A tela diz qual dos dois está valendo.

**As regras duras:**

1. Só a partir do modo `MANUAL`, com o braço parado e as juntas
   calibradas — ponto de programa é um par de ângulos, e sem calibração
   não há ângulo.
2. O arco é desligado ao entrar. Ninguém ensina caminho soldando.
3. Sair do modo manual (executar, reproduzir, calibrar, falha) encerra o
   aprendizado. O botão vermelho também.
4. **O torque não volta sozinho na saída.** Habilitar servo é ação
   explícita do operador em todo o resto do sistema, e aqui — com a mão
   dele dentro da área do braço — mais ainda.
5. Botão preso desde o boot não vale como gesto: sem isso um fio em
   curto soltaria o braço na hora de ligar.
6. Contato mecânico repica; o filtro de 40 ms garante **um toque, um
   ponto**. Sem ele um toque viraria meia dúzia de pontos, e o operador
   só descobriria na hora de soldar.

> Com o braço solto ele desce pelo próprio peso. Apoie a ponta antes de
> entrar no modo.

Banco de testes: cenários **P01 a P06** (o gesto, o repique, o toque
fora do modo, o botão preso no boot, o caso sem encoder, o que encerra o
modo, e a mesma coisa pela tela) e **M05** (quando a contagem segue o
eixo movido à mão — e quando não segue).

### 5.4.2 Produção: pausa, repetição, contagem e desfazer

O que separa um braço de bancada de um equipamento de produção não é
precisão — é o que acontece na centésima peça.

**Pausar** (`progPausar`) guarda em que trecho e **a que fração dele** o
cordão parou, e retomar continua dali em vez de refazer por cima do que
já foi soldado. O arco **fecha** na pausa, sempre: arco aberto com o
braço parado fura a chapa em segundos, então não existe pausa "segurando
o arco". Ao retomar ele reabre com o mesmo tempo de abertura do início de
qualquer cordão, porque a poça esfriou. Cenário **Q01**.

**Mais uma peça** (`/api/prog/repetir`) roda o mesmo programa sem
reabrir o arquivo. É o caso normal de produção, e exige a mesma
confirmação do arco.

**Contador de peças** (`Producao`, em `estado.h`), gravado em NVS:

| | |
|---|---|
| peças prontas | execução **com arco** que chegou ao fim |
| interrompidas | execução com arco parada no meio |
| tempo de arco | segundos de arco aberto acumulados — é o número que diz quando trocar bico e difusor |
| desde a manutenção | zerado pelo botão **Registrar manutenção feita** |

Ensaio **não** conta: não gasta consumível nem produz peça. Cenário
**Q02**.

**Desfazer** (`progDesfazer`), um nível, cobre o estrago que não tem
volta pela tela: apagar um programa de trinta pontos ensinados à mão, ou
remover um ponto no meio de um cordão. Desfazer duas vezes volta ao que
estava — um Ctrl+Z apertado sem querer não pode deixar o operador pior do
que começou. Cenário **Q03**.

**Abrir o arco pede dois toques** na tela — *e* `conf=1` na requisição. A
tela pedir confirmação não protege nada se a rota abre o arco para
qualquer chamada, e ela é alcançável por qualquer coisa na rede da
máquina. Cenário **Q04**.

### 5.4.3 Biblioteca de peças, com miniatura

Na aba **Arquivos**, cada programa do cartão tem um botão **ver**: ele lê
o arquivo para uma área de troca e **desenha** a peça — cordões em linha
grossa, deslocamentos tracejados — sem tocar no programa que está na
máquina. Ver a peça errada é barato; carregar a peça errada custa uma
chapa.

A miniatura também acende o **aviso de peça errada**: um programa feito
com outros comprimentos de elo aponta para outro lugar da chapa com os
mesmos ângulos, e o aviso mostra os dois pares de números.

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
| **Tirar a foto / Comparar agora** | acha o endereço de um parâmetro do driver **sem escrever nada** |

O diagnóstico sai **também no monitor serial**, uma linha a cada 5 s
enquanto falha.

### 5.6 SON: o habilita pelo fio, com espelho no RS485

**SON = Servo ON, o habilita do motor.** Nesta máquina ele é um **fio**:
`PIN_SERVO_ON` (GPIO 23), por optoacoplador, no SON dos dois drivers.
`servosHabilitar()` é quem o levanta e o derruba.

É nele que a corrente de segurança inteira se apoia:

| | |
|---|---|
| emergência | derruba o pino **por nível**, a cada ciclo enquanto o botão estiver acionado |
| alarme de driver | `pararTudo()` → servos desligados |
| conexão perdida | idem |
| ESP32 reinicia | o pino nasce em `LOW` — **o drive desabilita sozinho** |

**Fio de SON rompido desabilita o motor. Fio de RS485 rompido não
desabilita nada** — deixa o eixo como estava. Um é falha segura, o outro
não. Por isso o Modbus nunca é o caminho principal do habilita.

#### O espelho

Há drive cuja **fonte do habilita** está em *interna*: nele o pino
sozinho não energiza. Para esse caso existe o espelho — o mesmo
`servosHabilitar()` que mexe no GPIO **também** escreve um registrador
por Modbus. O pino primeiro, sempre; o quadro Modbus depois, e ninguém
espera por ele.

**Não há botão de SON avulso, de propósito.** Quem liga e desliga o
torque continua sendo o botão de servos, que passa por toda a supervisão.
Um botão de SON por fora disso seria um jeito de energizar o eixo sem
nada olhando.

De fábrica o espelho vem **desligado**: só o fio manda.

#### Configurar (coluna Encoder, painel *Espelho do SON*)

1. **Achar o registrador sem escrever.** *Tirar a foto* lê os
   registradores 0 a 255; você muda o parâmetro no painel do driver
   (P098, por exemplo), volta e aperta *Comparar agora*. O que mudou é o
   endereço. Só leitura. Braço **parado**, senão o par da posição muda
   junto.
2. **Experimentar antes de gravar**, pela escrita avulsa: modo manual,
   braço parado, **servos desligados**, solda desligada, registrador e
   valor digitados e confirmação. Toda escrita é **conferida relendo** —
   driver que responde *aceitei* e guarda outra coisa existe, e no SON
   isso significaria a tela dizer "sem torque" com o eixo energizado.
3. **Gravar o espelho.** Registrador **0** desliga o espelho.

O espelho alcança os drivers que estão **no barramento** — junta com
registrador de posição 0 não recebe. Quando o registrador aparece (no
arranque, vindo do NVS, ou porque você acabou de gravá-lo) o espelho
**sincroniza com o pino** em vez de supor: com os servos desligados ele
manda o desabilita, fechando o buraco do religamento; com o eixo já
energizado ele manda o habilita, sem derrubar torque de surpresa.

O estado do espelho fica na tela: `OK` / `FALHOU`, o registrador, se foi
habilita ou desabilita, o motivo e a contagem de falhas. Espelho que falha
não some — "servos ligados" com o espelho falhando seria a tela mentindo
sobre torque.

⚠️ **Se o parâmetro do painel mudar a *fonte* do habilita para interna, o
botão de emergência deixa de desenergizar o driver.** O espelho existe
para conviver com um drive já configurado assim, não para você mudá-lo
para isso.

### 5.7 Correção de posição pelo encoder

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

**Leitura de ângulo na tela.** A régua do topo mostra, para cada junta,
o ângulo **comandado** (a conta de pulsos do firmware) e logo abaixo o
**medido** pelo encoder, com a diferença. Abaixo de 0,5° a diferença fica
discreta — é o tremor normal de um encoder de 17 bits; acima disso fica
vermelha. É o que transforma "acho que está em 30 graus" em "está em
30,12 graus", e o que denuncia um desvio antes de ele virar peça torta.

**Braço movido à mão** (`seguirEixoSolto()`): com o torque **desligado**
o braço está solto e o encoder é a única coisa que sabe onde ele foi
parar. A contagem é acertada pela leitura, e "movi com a mão" passa a
dar o mesmo resultado que "mandei ir".

> **Toda leitura passa por um teste de possibilidade física.** O braço
> não pode estar fora do curso que o próprio operador mediu (com 10° de
> folga para o batente). Leitura dali para fora não vira posição, não é
> seguida e não aparece na tela como medida boa — ela é denunciada, com o
> número, para o operador conferir registrador e contagens por volta.
> Sem isso, uma leitura errada virava a posição oficial da máquina e o
> "ir ao zero" mandava um curso inteiro de pulso contra o batente. Ver
> `ACHADOS.md`, R78, e o cenário **T01**.

> **Só com o torque desligado.** Com servo ligado o motor segura a
> posição: se o eixo saiu do lugar mesmo assim, isso é **perda de
> passo**, não movimento à mão. Seguir a contagem ali esconderia o
> defeito e o assentamento nunca traria o braço de volta — seria trocar
> uma correção por um disfarce. Cenário **M05** do banco.

### 5.8 Solda

Relé com proteções: não liga sem servos, não fica ligado com o braço
parado por engano, e a parada de emergência corta o arco antes de
qualquer outra coisa.

### 5.9 Cartão SD

Programas, trajetórias e cópias de configuração. Tarefa própria no núcleo
0. Ver [`CARTAO_SD.md`](CARTAO_SD.md).

### 5.10 Rede

A máquina **cria** a rede `Robo2dof`. Não entra na rede de ninguém, não
procura roteador, não fala com a internet.

| Como chegar | |
|---|---|
| IP | `192.168.4.1` |
| Nome | `robo2dof.local` (precisa de mDNS/Bonjour no Windows) |

Ao entrar na rede, **o painel abre sozinho**: as sondas de captive portal
do Windows, Android e iPhone levam redirecionamento para 192.168.4.1.

---

### 5.11 Máquina: saúde, registro, conexão, firmware e modo operador

Aba **Máquina**.

### Saúde (`GET /api/saude`)

Uma tela só com tudo que se pergunta quando algo está estranho: há quanto
tempo está ligada, peças prontas e interrompidas, tempo de arco,
memória, ocupação da partição, cartão, alarmes, travamentos, e — o mais
útil — a **taxa de acerto** de cada encoder. 100% é barramento saudável;
60% não é "meio quebrado", é cabo, terminação ou aterramento, e vai
piorar.

Antes disso, a resposta a "está tudo bem?" era abrir o monitor serial com
um cabo — que só existe na bancada, nunca na fábrica.

### Registro de eventos (`GET /api/registro`)

As últimas 24 linhas saem de um **anel na RAM**, então funcionam sem
cartão — que é justamente quando a pergunta "o que aconteceu?" é feita. O
registro completo continua indo para `/log/s####.csv` no cartão.

### Conectar (QR)

Dois códigos: o primeiro entra na rede Wi-Fi da máquina (formato
`WIFI:...`, lido por Android e iPhone), o segundo abre o painel.

O gerador de QR é **próprio** — a máquina não tem internet, então CDN não
é opção — e é conferido por `testes/conferir_qr.py`, que renderiza os
códigos e os **lê de volta com um decodificador de verdade**. Um QR
desenhado errado fica quadradinho e bonito e não abre em celular nenhum;
nenhuma inspeção visual pega isso.

### Atualizar o firmware (OTA)

> **Limite real.** O `partitions.csv` deste projeto dá 3 MB de app e
> **não tem partição de OTA**. Gravado assim, o robô não tem para onde
> escrever a imagem nova, e o painel diz isso em vez de fingir.

Para ter OTA, grave **uma vez pelo USB** com `partitions_ota.csv` (duas
partições de 1,9 MB); daí em diante as atualizações vão pela rede. Não há
meio-termo: a imagem nova é escrita na partição que **não** está rodando.
Com uma partição só, gravar seria escrever por cima do próprio código em
execução — o processador trava no meio, com o relé de solda em estado
indefinido.

O `nvs` fica no mesmo lugar nos dois esquemas, então **a calibração já
salva continua valendo** depois de trocar de partição.

Antes de começar, o módulo desabilita os servos: o ESP32 reinicia no fim,
e um driver habilitado com o gerador de pulso morto é um eixo que ninguém
está comandando.

### Modo operador × técnico

O modo operador **esconde** as abas de instalação (Ajustes e Encoder).
Entrar não pede nada — trancar a máquina para o turno tem de ser rápido.
Sair pede uma senha curta (de fábrica `1234`, trocável na própria tela).

> **Isto não é segurança de rede.** A máquina serve o próprio Wi-Fi, e
> quem estiver nele alcança a API direto, sem passar pela tela. É uma
> trava contra toque errado — útil todo dia, e só isso.

### Idiomas

Traduz o que o operador toca: abas, botões, rótulos, a tela de saúde e a
tira de estado. **As notas longas de explicação continuam em
português** — elas são o manual embutido desta máquina, escritas para
quem a monta, e traduzir mal um texto que explica por que o arco fecha na
pausa é pior do que deixá-lo como está.

---

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
| `POST /api/encoder/diferenca` | acha um parâmetro comparando duas leituras — **não escreve** |
| `POST /api/encoder/escrever` | escreve um registrador; exige manual, parado, servos e solda desligados, `confirmar=1` |
| `GET  /api/encoder/escrita` | como foi a última escrita, **conferida por releitura** |
| `POST /api/son/config` | grava o registrador do espelho do SON e os valores de habilita/desabilita |
| `POST /api/encoder/zerar` | zera a contagem aqui |
| `POST /api/correcao` | assentamento pelo encoder |
| `POST /api/aferir/encoder` | afere a engrenagem eletrônica pelo encoder |
| `POST /api/travamento/ok` | limpa o aviso de travamento |
| `POST /api/zero/config` | o que fazer ao ligar a máquina |
| `POST /api/zero/ensinar` | ensina a referência absoluta de uma junta |
| `POST /api/zero/esquecer` | volta a ligar como antes |
| `POST /api/aprender` | entra/sai do modo aprendizado (`on=1`, `on=0`, `on=-1` alterna) |
| `POST /api/prog/pausar` | pausa (`on=1`) e retoma (`on=0`) o programa |
| `POST /api/prog/repetir` | mais uma peça — exige `conf=1` |
| `POST /api/prog/desfazer` | desfaz a última alteração do programa |
| `POST /api/prog/executar` | com `ensaio=0` **exige `conf=1`** |
| `POST /api/manutencao/ok` | zera o contador de peças desde a manutenção |
| `GET  /api/saude` | tudo que responde "está tudo bem?" |
| `GET  /api/registro` | as últimas linhas do log, da memória |
| `POST /api/painel` | modo operador e senha do técnico |
| `POST /api/ota` | envio do firmware (multipart) |
| `POST /api/sd/prever`, `GET /api/sd/previa` | miniatura de uma peça do cartão |
| `GET  /api/calibracao` | tudo da aba Calibração, num JSON |
| `POST /api/aferir/reducao` | mede a redução pelo encoder (`j`, `g` = ângulo real) |
| `POST /api/mesa/canto` | ensina um canto na posição atual da ponta |
| `POST /api/mesa/limpar` | apaga a área ensinada |
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
