# RoboCNC 2DOF — braço de solda

Firmware ESP32 para braço planar de 2 graus de liberdade com gravação e
reprodução de trajetória, controle de relé de solda e interface web.

Hardware: ESP32 + 2× driver HLTNC T3D-L20A + servo 80AST-A1C04025 +
módulo adaptador microSD (SPI, 6 pinos).

## Arquitetura

Regra única que organiza o projeto inteiro:

- **core 1 (`loop`)** é o dono exclusivo dos motores, do relé e do estado.
- **core 0 (`tarefaRede`)** só enfileira `Comando` e lê `Snapshot`.

Nenhum handler HTTP toca em `motores.h`, `solda.h` ou `trajetoria.h`.
Isso elimina toda a classe de bugs de concorrência entre os núcleos.

| Arquivo           | Responsabilidade                                        |
|-------------------|---------------------------------------------------------|
| `config.h`        | Pinos, limites, constantes, tipos                       |
| `estado.h/.cpp`   | Variáveis globais, NVS, fila de comandos, snapshot      |
| `cinematica.*`    | FK, IK e **validação de postura / anti-colisão**        |
| `motores.*`       | Jog protegido, movimento coordenado, paradas            |
| `trajetoria.*`    | Gravação por amostragem e reprodução por setpoint       |
| `solda.*`         | Relé com intertravamento — única porta de saída         |
| `calibracao.*`    | Assistente de medição de curso                          |
| `armazenamento.*` | Cartão SD — tarefa própria no core 0                    |
| `servidor_web.*`  | Rotas HTTP                                              |
| `pagina_web.h`    | Interface                                               |
| `RoboCNC.ino`     | Setup, supervisão de segurança, máquina de estados      |

O cartão segue a mesma regra: **o core 1 nunca toca no SPI do cartão.**
Gravar num SD leva de dezenas a centenas de milissegundos; dentro do laço
de 1 ms isso pararia a supervisão de segurança junto. Quem faz I/O é uma
tarefa dedicada no core 0, e a conversa com o core 1 acontece por área de
troca mais um `Comando` na fila.

## Ligação elétrica

A referência completa de fiação, com os cuidados de cada pino e a ordem
de energização na primeira vez, está em [`LIGACOES.md`](../LIGACOES.md).
O resumo:

**Antes de energizar, confira:**

1. **Nível de pulso.** O ESP32 sai em 3,3 V. A entrada do T3D espera 5 V.
   Use um buffer (74HCT14 ou 74HCT245 alimentado em 5 V) entre o ESP32 e
   PUL+/DIR+. Para sinal de 24 V, dois resistores de 2 kΩ / 0,25 W em
   série em P+ e D+ — sem eles o driver queima.
2. **Relé de solda.** Pull-down de 10 kΩ do GPIO 26 para GND. O GPIO
   flutua durante o boot; sem o resistor o arco pode abrir sozinho ao
   ligar. Acionamento por optoacoplador, nunca direto.
3. **ALM.** Pull-up de 10 kΩ para 3V3 nos GPIO 34 e 35 (não têm pull-up
   interno). Ajuste `ALARME_ATIVO_EM` conforme a configuração do driver.
4. **Aterramento.** O retorno da solda vai direto para a peça, nunca
   passando pelo chassi da eletrônica.
5. **Cartão microSD.** O módulo pequeno de 6 pinos (o azul com quatro
   resistores 103) é **3,3 V puro** — não tem regulador nem conversor de
   nível. Ligue em 3V3; em 5 V o cartão morre.

| módulo | ESP32   | observação                                    |
|--------|---------|-----------------------------------------------|
| 3V3    | 3V3     | nunca no 5V **deste** módulo                  |
| GND    | GND     |                                               |
| CS     | GPIO 5  | tem pull-up interno, seguro no boot           |
| SCK    | GPIO 14 |                                               |
| MOSI   | GPIO 13 |                                               |
| MISO   | GPIO 25 | **não use o 12** — ver abaixo                 |

   **Cuidado com o GPIO 12.** A pinagem "padrão" do HSPI usa 12 para
   MISO, e o módulo tem pull-up de 10 k nessa linha. O GPIO 12 é
   strapping (MTDI): alto no boot programa o regulador do flash para
   1,8 V e a placa não dá mais boot. O ESP32 remapeia SPI por matriz de
   GPIO, então mover o MISO para o 25 não custa nada.

   Ponha **10 µF cerâmico entre 3V3 e GND junto ao módulo**: o cartão
   puxa picos de ~100 mA na escrita e a queda de tensão derruba a
   montagem.

   Sem cartão no slot a máquina funciona igual — o que se perde é a
   biblioteca de programas e o registro de eventos.

## Como soldar uma chapa (fluxo real)

A interface e uma sequencia numerada. Siga na ordem:

1. **Preparar** — habilite os servos, rode a calibracao das juntas.
2. **Ensinar o caminho** — leve a ponta ate onde o cordao comeca e toque
   em *Gravar ponto aqui*. Leve ate onde ele termina e grave o ponto 2.
   Na lista aparece o trecho `1 -> 2`: ligue a chave de solda dele.
   Um cordao reto sao dois pontos. Um contorno de quatro lados sao cinco
   pontos, com solda ligada nos quatro trechos.
3. **Ensaiar** — roda o percurso inteiro com o rele desligado. Faca isso
   sempre. E o unico jeito barato de descobrir que um ponto esta errado.
4. **Soldar** — mesma sequencia, com o arco abrindo nos trechos marcados.

Cada ponto pode ser removido, revisitado (*ir*) ou ter a solda alternada
sem regravar nada. O desenho a esquerda mostra os pontos numerados, os
trechos de solda em vermelho cheio e os deslocamentos em cinza tracejado.

### Reta de verdade nos cordoes

Trecho com solda ligada e percorrido por **interpolacao cartesiana**: o
firmware divide a reta em passos de 1,5 mm, roda cinematica inversa em
cada um e transmite o resultado como setpoint continuo. A ponta anda em
linha reta na chapa.

Trecho sem solda usa interpolacao nas juntas, que e mais rapida e sai
curva — e nao ha problema nenhum nisso, porque nada esta sendo soldado.
A mesa de tracado mostra os dois de forma diferente: cordao em reta
incandescente, deslocamento em curva tracejada.

Ordem de grandeza do erro que isso corrige: num cordao de 150 mm com
elos de 200 mm, a interpolacao nas juntas desvia ate **11 mm** da reta.

### Por que pontos e nao gravacao a mao livre

Gravar movendo o braco a mao livre nao produz reta, e o estado do arco
fica congelado no instante da gravacao — nao da para corrigir depois.
Robo de solda industrial ensina por pontos justamente por isso. A
gravacao continua continua existindo no firmware (`trajetoria.cpp`) para
percursos organicos, mas nao e o caminho principal da interface.

### Desenhar o caminho com o dedo

Na mesa de traçado, o botão **DES** liga o modo de desenho: você risca o
caminho com o dedo em cima do desenho do braço e o traço vira **programa
de pontos**. Dali em diante ele é um programa como qualquer outro — dá
para ensaiar, executar com arco, corrigir ponto a ponto e salvar no
cartão.

O traço bruto tem centenas de amostras; o navegador o simplifica com
**Douglas-Peucker** antes de mandar, apertando a tolerância até caber nos
40 pontos do programa. A barra mostra as duas contas em tempo real
(`41 amostras → 9 pontos`), então dá para ver o que vai ser enviado.

O corpo do `POST /api/prog/desenho` é uma lista `x,y;x,y;…` em
**milímetros de chapa**, e o firmware roda cinemática inversa em cada
ponto, sempre partindo do anterior — assim o cotovelo não troca de lado
no meio do traço. Se algum ponto não for alcançável, o pedido inteiro é
recusado **dizendo qual** (`ponto 7 do desenho (320, 480 mm): …`) e o
programa que já estava na máquina não é tocado: a validação passa pela
mesma área de troca usada para carregar arquivo do cartão.

A caixa `cordão` decide se os trechos saem marcados para abrir arco. O
último ponto nunca abre: depois dele não há trecho.

### Importar DXF

`Programa → Importar desenho DXF`. Desenhe a peça no CAD, salve como DXF
e traga o arquivo. São aproveitadas as entidades que viram trajeto:
**LINE**, **LWPOLYLINE** (com *bulge*, ou seja, cantos em arco),
**POLYLINE** clássica, **ARC** e **CIRCLE**. Texto, cotas e hachuras são
contados e ignorados — eles não são caminho.

**O arquivo é lido no celular, não no ESP32.** Um DXF de 300 kB não cabe
na RAM dele, e um leitor de DXF em C ocuparia flash que a máquina precisa
para o resto. O robô recebe só a lista de pontos pronta, pela mesma rota
`POST /api/prog/desenho` do traço a dedo — portanto pela mesma validação.

Depois de ler, o importador:

1. **Emenda** contornos cujas pontas se encostam (0,15 mm). Um retângulo
   sai do CAD como quatro `LINE` soltas; sem emendar, viraria quatro
   cordões com deslocamento no meio.
2. **Ordena** os contornos pelo mais próximo do fim do anterior, para
   reduzir deslocamento morto.
3. **Achata** arcos e círculos em cordas com flecha máxima de 0,15 mm.

### Posicionar sobre a mesa

O CAD não sabe onde fica a base do braço, então o desenho entra como um
objeto que você posiciona: arrastar com o dedo sobre a mesa de traçado,
girar de 15° em 15°, espelhar, aumentar e diminuir, ou centralizar na
área útil.

A barra recalcula a cada quadro **quantos pontos caem fora do alcance**,
e eles aparecem em vermelho no desenho. Enquanto houver um ponto fora, o
botão de aplicar fica travado. A conta no navegador espelha
`posturaValidaDet()` do firmware (curso, dobra e envelope) — mas quem
decide continua sendo o robô: isto existe para você não posicionar às
cegas.

Cada ponto vai com o **seu próprio** estado de arco (`x,y,solda`), então
vários contornos viram vários cordões com deslocamento entre eles. O
último ponto de cada contorno nunca abre arco.

O programa guarda **120 pontos** (era 40, que não chega para um contorno
importado). O importador simplifica cada contorno com Douglas-Peucker,
apertando a tolerância até o total caber — e o limite vem do
`/api/status`, não fica escrito na página.

## Gravando no ESP32

A pasta traz um `partitions.csv` com 3 MB de app, para o sketch não
esbarrar nos 1,25 MB do esquema padrão do ESP32. Se a sua IDE ignorá-lo,
escolha `Tools → Partition Scheme → Huge APP (3MB No OTA)`. A calibração
salva sobrevive à troca — o `nvs` fica no mesmo lugar.

Detalhes em [`LIGACOES.md`](../LIGACOES.md), §0.

A interface é servida comprimida (75 kB → 21,8 kB). Você edita
`pagina_web.h`; depois rode `python3 testes/gerar_pagina_gz.py`. O banco
reprova se o gerado ficar velho.

## Rede — Wi-Fi próprio, e só isso

A máquina cria a sua própria rede. Ela **não entra na rede de ninguém,
não procura roteador e não fala com a internet**. O painel não depende de
nada de fora para funcionar.

| Como chegar | Endereço |
|---|---|
| Wi-Fi `Robo2dof` → navegador | `http://192.168.4.1` |
| o mesmo, sem decorar IP | `http://robo2dof.local` |

O IP é **fixado pelo projeto** (`WIFI_AP_IP` em `config.h`), não herdado
do padrão da biblioteca — assim ele não muda quando o core do ESP32 for
atualizado. O `robo2dof.local` vem do mDNS, que funciona em iPhone, Mac,
Windows 10+, Linux e Android recente.

Um **DNS de captura** responde qualquer nome com o IP da máquina. Duas
consequências, as duas boas: ao entrar na rede o celular detecta portal
cativo e costuma oferecer abrir o painel sozinho — em vez de reclamar
que não há internet e pular para os dados móveis — e digitar qualquer
coisa na barra de endereço cai no painel.

> Nome de uma palavra só (`robo2dof`, sem `.local` e sem barra) depende
> do navegador: alguns tratam como busca antes de tentar resolver.
> `robo2dof.local` e `192.168.4.1` funcionam sempre.

### Por que não existe modo estação

Houve aqui um modo de entrar na rede da oficina, com varredura, escolha
de rede e senha pelo painel. Saiu por um motivo técnico, não por gosto.

**O ESP32 tem um rádio só.** Ligado nas duas redes ao mesmo tempo
(`WIFI_AP_STA`), o ponto de acesso é obrigado a acompanhar o canal do
roteador, e o rádio passa a dividir tempo de antena entre as duas. Isso
aparece como atraso e tremor no joystick — e o heartbeat do jog, que
corta o movimento se faltar por 350 ms, é justamente o tráfego que não
pode atrasar.

Rede de terceiro não vale latência no controle de uma máquina que se
move. Em `WIFI_AP` puro o rádio nunca sai do canal.

O código está no histórico do git, se um dia fizer sentido voltar.

## Primeira partida
## Primeira partida

1. `Ajustes → Resolução`: informe pulsos por volta (engrenagem eletrônica
   do T3D) e a redução mecânica da junta.
2. `Ajustes → Geometria`: comprimento dos elos, folga de dobra, Y mínimo
   e raio morto da base.
3. `Ajustes → Calibração`: percorra o assistente. Ele pergunta duas
   coisas que fazem o software concordar com o braço — veja a seção
   abaixo.
4. `Mover → Habilitar servos`, então jog.

## Curso util do jog

A antecipacao de frenagem usa a velocidade **instantanea** do motor. Com
o eixo parado a reserva e praticamente zero, entao da para encostar no
limite passo a passo. Calcular pela velocidade maxima (como faziam as
versoes anteriores) reservava dezenas de graus em cada ponta e reduzia
o curso util sem motivo.

## Resolucao independente por eixo

Cada junta tem seus proprios **pulsos por volta** (engrenagem eletronica
do T3D) e sua propria **reducao mecanica**. Um eixo com redutor 50:1 e
outro com 20:1 e configuracao normal e suportada: `Ajustes > Resolucao
da junta 1` e `Resolucao da junta 2`. O painel mostra o resultado em
pulsos por grau de cada eixo, para conferencia.

## Tema e escala do desenho

Dois temas: prancheta clara (padrao) e oficina escura, no botao TEMA
sobre a mesa de tracado. As zonas proibidas so aparecem desenhadas
quando a protecao correspondente esta ligada — mostrar limite que nao e
aplicado engana o operador.

A mesa de tracado tem largura fixa em milimetros, nao normalizada pelo
alcance. Encurtar um elo encurta o desenho do braco na mesma proporcao,
como deve ser. A grade e a barra de escala no canto mostram a medida
real; os botoes de zoom e o FIT reenquadram.

A espessura de cada elo no desenho e proporcional ao seu comprimento, e
a ponta ganha um rastro proporcional a velocidade instantanea.

## Jog de recuperacao

Se o braco terminar fora da area util por qualquer motivo, o jog nao
trava: enquanto o movimento **nao piorar** a violacao, ele e liberado, e
a interface avisa que esta voltando. Bloquear tudo quando a postura ja
esta invalida prendia o braco sem nenhuma saida — inclusive o movimento
que o traria de volta.

A calibracao tambem confere o que mediu: se os limites sairem trocados
(etapa percorrida no sentido contrario, ou pino DIR invertido) ela
corrige a ordem, garante que o zero fique dentro do curso e descarta a
medicao se o curso for perto de zero.

## O cartão de memória

> Pode o sistema ser instalado no cartão para ampliar a memória? Não — e
> o porquê, mais o que fazer no lugar, está em
> [`CARTAO_SD.md`](../CARTAO_SD.md).

Quatro pastas, cada uma com um propósito:

| pasta   | conteúdo                                                        |
|---------|-----------------------------------------------------------------|
| `/prog` | programas de solda, **em texto e em graus**                     |
| `/traj` | trajetórias gravadas a mão livre, em binário                    |
| `/cfg`  | cópias dos ajustes da máquina                                   |
| `/log`  | um arquivo CSV por partida                                      |

**Programa em graus, não em passos.** Guardar passos amarraria o arquivo
à engrenagem eletrônica em uso: trocar a resolução mudaria de lugar todos
os pontos gravados. Em graus o arquivo continua valendo, e dá para
escrever um programa no computador com um editor de texto comum:

```
ROBOCNC-PROG 1
nome=chapa 30x60
elos=200.000,200.000
pontos=3
# t1(graus) t2(graus) solda_ate_o_proximo
10.0000 -30.0000 1
25.0000 -30.0000 0
40.0000 -50.0000 0
```

Ao carregar, o firmware avisa se os elos do arquivo não batem com os da
máquina — o mesmo par de ângulos aponta para outro lugar da chapa.

**A configuração continua vindo do NVS.** O que a máquina usa ao ligar é
a memória interna; o cartão é backup e transporte, para levar a mesma
configuração para outra máquina. Restaurar de arquivo passa exatamente
pelo mesmo caminho de um POST em `/api/config`, com as mesmas validações:
arquivo do cartão não é mais confiável que requisição de navegador.

**Trajetória em binário, sem cópia na RAM.** São até 1500 waypoints de 16
bytes. Em vez de duplicar 23 kB para entregar à tarefa de SD, o core 1
*empresta* o buffer vivo: enquanto emprestado, gravação e reprodução
ficam recusadas.

**Nada trava o laço de controle.** `logEvento()` só enfileira, com
timeout zero — se a fila estiver cheia a linha é descartada em silêncio.
Um registro nunca pode atrasar a supervisão de segurança.

O cartão pode ser inserido depois de ligar: o módulo de 6 pinos não tem
sinal de detecção, então o firmware tenta montar a cada 3 s enquanto não
houver cartão. Trocar de cartão pede um toque em *Procurar cartão de
novo*, que força a remontagem.

## A interface como aplicativo

No celular a página se comporta como app: abas embaixo no alcance do
polegar, uma tela por vez, sem barra de endereço quando adicionada à tela
inicial (`display: standalone` no manifesto).

Cinco abas: **Mesa** (traçado), **Mover** (joystick, ir para ângulo,
gravar ponto), **Programa** (pontos, ensaio, solda, trajetória a mão
livre), **Arquivos** (cartão) e **Ajustes**.

Nada de botão mudo: quando uma ação está bloqueada, uma linha abaixo dela
diz o que resolver, na ordem em que resolver — *habilite os servos*,
*calibre as juntas*, *robô ocupado*. O joystick apaga e mostra o motivo
em vez de parecer pronto.
O botão PARAR ocupa as duas linhas do cabeçalho — alvo alto, no canto
onde o polegar já está, alcançável de qualquer aba.

No computador nada disso aparece: a mesa de traçado fica sempre visível e
as abas viram um seletor no topo da coluna da direita.

### Joystick

Um disco analógico no lugar dos botões de seta. Horizontal move a junta
1, vertical a junta 2, e a diagonal move as duas ao mesmo tempo — cada
uma na sua fração de velocidade.

Quanto mais longe do centro, mais rápido; na borda o eixo anda na
velocidade de jog configurada, nunca acima. O círculo tracejado é a zona
morta de 12%, e o botão do disco é menor que ela de propósito, para você
ver de onde o movimento começa. A fração é reescalada a partir da borda
da zona morta, então o movimento nasce do zero em vez de dar um salto ao
sair dela.

A zona morta é aplicada **no firmware**, não só no navegador: comando que
chegue de fora passa pela mesma checagem. O disco manda um comando só
para os dois eixos (`/api/jogxy`), o que é metade das requisições de
mandar `/api/jog` por eixo — importa num WebServer que atende uma conexão
por vez.

Soltar o dedo para o braço. Tela apagando, app indo para segundo plano ou
aba perdendo o foco também param, na hora — e mesmo que nada disso
chegue, o heartbeat de 350 ms do firmware para o eixo sozinho.

## Velocidades em graus por segundo

**Hz significa coisas diferentes em cada junta.** Com redução 16,5 na
junta 1 e 4 na junta 2 — que é uma configuração normal — os mesmos
3000 Hz davam:

```
J1   458 pulsos/grau   3000 Hz  =   6,5 °/s
J2   111 pulsos/grau   3000 Hz  =  27,0 °/s      quatro vezes mais rápido
```

Por isso um braço andava muito mais rápido que o outro, e não havia
ajuste que igualasse os dois sem refazer a conta à mão.

Velocidades e rampas passaram a ser especificadas em **°/s** e **°/s²**.
Cada junta converte para Hz com o seu próprio `passosPorGrau`, e
`FREQ_PULSO_MAX_HZ` continua sendo o teto do driver. O movimento
coordenado também mudou: quem manda no tempo é a junta com mais **graus**
a percorrer, não com mais passos.

O painel de ajustes mostra quantos Hz cada junta vai pedir ao driver na
velocidade de jog — é ali que se vê se algum eixo está perto do teto do
T3D.

Os valores antigos ficaram em chaves de NVS separadas, então atualizar o
firmware traz os padrões novos em vez de reinterpretar 3000 Hz como
3000 °/s.

### Tranco na partida — rampa em S

Aceleração constante quer dizer que a aceleração **aparece de uma vez**
no instante da partida. A derivada dela (o *jerk*) é infinita ali, e é
exatamente isso que se sente como tranco — e que faz o eixo mais leve
perder passo justamente no arranque.

`Ajustes → Rampa → Suavidade da partida` liga a rampa em **S** do
FastAccelStepper (`setLinearAcceleration`): em vez de saltar para a
aceleração cheia, ela cresce linearmente. Zero devolve a rampa reta de
antes; 100 a 150 costuma ficar bom. Número muito alto atrasa a chegada na
velocidade cheia, o que só incomoda em movimento curto.

O acompanhamento do setpoint também parou de reprogramar a velocidade a
cada ciclo de 1 ms: `seguirSetpoint()` só chama `setSpeedInHz()` quando o
valor muda de verdade. Reprogramar a cada ciclo reiniciava o cálculo da
rampa o tempo todo, o que aparecia como trepidação no cordão.

### Se o braço estiver perdendo passos

Nesta ordem:

1. **Rampa.** É a causa mais comum. A rampa desigual entre as juntas era
   parte do problema (17 °/s² numa, 72 na outra); agora as duas são
   iguais, mas se ainda perder, baixe `Ajustes → Rampa`.
2. **Buffer de 5 V.** O ESP32 sai em 3,3 V e a entrada do T3D espera 5 V.
   Sem o `74HCT14`/`74HCT245` o driver simplesmente perde pulso. Veja
   [`LIGACOES.md`](../LIGACOES.md) §3.1 — e note que tem de ser da família
   **HCT**, não HC.
3. **Fios ALM.** Ligue-os e mude `ALARME_FISICO_INSTALADO` para `true`:
   um servo drive que perde referência **avisa**, e o firmware leva o
   sistema para FALHA em vez de continuar soldando torto.

## Modo de instalação

Sem calibração válida o robô fica em **modo de instalação**: o jog é
livre, sem limite de curso nenhum, e os modos automáticos ficam
recusados.

Isso não é descuido, é o único jeito de a coisa funcionar. Sem
referência, "graus" é pulso dividido por um número digitado, mais um
offset que pode ser o da calibração anterior. Aplicar a proteção de dobra
ou a de envelope sobre esse ângulo trava justamente o assistente que
existe para estabelecer a referência — e era exatamente o que acontecia:
com a resolução errada, um movimento pequeno lia |θ2| > 160° e o jog era
recusado antes de o operador chegar em qualquer limite.

No modo de instalação quem protege são os batentes da máquina e o
operador. Mova devagar. A interface avisa embaixo do joystick.

`Ajustes → Preparar → Apagar calibração gravada` devolve o robô a esse
estado quando você quer começar do zero sem herdar nada da medição
anterior. A **resolução não é apagada** junto — ela descreve a mecânica,
não a medição.

## Como o firmware sabe em quantos graus a junta está

Ele **conta pulsos**. Não há encoder na malha: o `FastAccelStepper` conta
cada pulso emitido, a calibração mede o curso em pulsos, e a conversão
para graus é uma divisão:

```
passosPorGrau = (passosPorVolta × redução) / 360      ← você digita os dois
ângulo        = pulsos / passosPorGrau + grausHome
```

Ou seja: a **medição** é do robô, mas a **escala** e a **origem** vinham
de números digitados. Errado qualquer um deles, o braço real fica numa
posição e o da tela em outra. O assistente agora fecha os dois.

### A escala — aferir pelo curso que você mediu

Na última etapa o assistente mostra o curso que calculou e pergunta
quanto ele foi **de verdade**. Meça com transferidor ou inclinômetro e
digite. O firmware refaz a conta ao contrário:

```
passosPorGrau = pulsos contados / graus medidos
```

O assistente acabou de varrer o curso inteiro da junta — é a maior base
de medida que a máquina tem, então sai preciso. A `redução` mostrada nos
ajustes é reescrita para explicar essa resolução, para um recálculo
posterior não desfazer a aferição.

Exemplo real do banco de testes: operador digitou `10000` pulsos/volta e
esqueceu o redutor 2:1. O assistente reportou 200° de curso; o braço
girou 100. Informados os 100, a resolução foi de 27,78 para 55,56
pulsos/grau e a redução virou 2,0 — sozinha.

Deixando o campo com o valor que o assistente já sugeriu, nada muda.

### O sentido — para que lado a junta cresce

Se o braço vai para um lado e o desenho na tela vai para o outro, o sinal
do eixo está trocado. **Nenhuma calibração conserta isso**, porque o erro
não é de escala, é de sinal: a aferição só corrigiria a proporção, e o
braço continuaria espelhado.

`Ajustes → Sentido dos eixos` tem uma chave por junta. A cinemática
espera ângulo crescente no sentido **anti-horário**, com a junta 1 em zero
apontando para a **direita**. Marcar a chave inverte o `DIR` no gerador de
pulso — não precisa trocar fio no driver.

### A origem — onde fica o zero

A cinemática chama de zero a postura com o **elo 1 apontando para a
direita, na horizontal**, e o **elo 2 alinhado com ele** (braço
esticado). Se a sua posição de referência for outra, o desenho na tela
sai girado em relação à máquina.

Por isso a etapa de referência pergunta em quantos graus cada junta está
naquela postura. Deixe `0 e 0` se ela for a postura canônica; senão,
informe os ângulos reais. O offset fica guardado em graus, então ele
sobrevive a uma correção de resolução.

### Zerar a máquina na posição atual

`Mover → Zerar a máquina aqui` faz o que a máquina faz ao ligar: declara
que a postura atual é a de referência e zera a contagem de pulsos dos
dois eixos. É o conserto para "o braço perdeu passo e o desenho na tela
ficou deslocado do braço de verdade" — leve o braço de volta à
referência e zere.

Ele **não** substitui a calibração: os limites de curso são contados a
partir da referência, então zerar em outro lugar desloca a área útil
inteira. Se não souber se o braço está na referência, calibre.

O pedido só é aceito com o robô parado em manual, e quem reescreve a
contagem é o core 1 — reescrever posição debaixo do gerador de pulso em
movimento é o jeito mais rápido de mandar o braço para o batente.

### Aferir a redução sem calcular

`Ajustes → Aferir a redução no braço` mede a redução em vez de calculá-la
no papel, que é o que salva quando correia, folga ou engrenagem trocada
fazem a mecânica não bater com o catálogo:

1. **Marcar o início aqui** — guarda a contagem atual daquele eixo.
2. Gire o eixo com o jog o quanto der. A tela mostra os pulsos contados
   e quantos graus o sistema *acha* que isso é.
3. Meça com transferidor quantos graus ele girou **de verdade**, digite
   e grave.

O firmware faz `passosPorGrau = pulsos contados / graus medidos` e
reescreve a redução mecânica a partir disso. Quanto maior o ângulo
medido, melhor: meio grau de erro em 90° pesa dez vezes menos que em 9°.

É a mesma conta da etapa final do assistente de calibração, só que
avulsa — dá para aferir um eixo sem refazer a medição de curso inteira.

### O que você vê depois

Com as juntas calibradas, a mesa de traçado desenha a **área que o braço
alcança de verdade** — o contorno azul. O círculo tracejado continua
sendo o alcance mecânico dos elos, que é maior.

Ela é desenhada em coordenadas polares, e não traçando a borda do
retângulo de limites: a cinemática direta é 2-para-1 (cotovelo para cima
e para baixo dão o mesmo ponto), então aquela borda se cruza sozinha e o
preenchimento saía com buracos — um "yin-yang" que não tinha nada a ver
com a área real. Em polares a conta é direta: cada valor de θ2 dá **um**
raio, e θ1 varre um arco nesse raio. Nos botões de jog, cada
junta ganha uma barra mostrando onde ela está dentro do curso, com as
pontas em vermelho marcando a margem de segurança.

## O cotovelo não vira no meio do cordão

Um braço 2R alcança quase todo ponto de **duas** maneiras: cotovelo para
um lado e para o outro. `resolverXY()` escolhe entre elas pelo critério
"a que exige menos movimento agora", e isso está certo para um comando
avulso de "vá até este ponto".

Dentro de um cordão, não. A reta é percorrida em passos de 1,5 mm, e
reescolher o ramo a cada passo é um convite ao desastre: perto do braço
esticado as duas soluções praticamente coincidem, um arredondamento troca
a escolha, e a troca é uma descontinuidade de até 2 × |θ2| no espaço das
juntas. Na chapa isso é o braço largar a reta e dar uma volta até a
postura espelhada.

Com os elos de 450 e 400 mm e curso de ±120°, o banco de testes acha a
reta de (−360, −770) a (−240, −770): **20,7° de θ2 num único passo de
1,5 mm**, com troca de ramo. Foi isso que apareceu na máquina como "o
braço fugiu da posição e fez uma circunferência" num ziguezague.

O ramo passou a ser **travado no início de cada trecho** (`ramoSeg` em
`prepararReta()`, `resolverXYRamo()` em `cinematica.cpp`). Se o ramo
travado deixar de servir no meio, o cordão **aborta com mensagem** — cair
no outro ramo daria a volta com o arco aberto. A validação prévia trava o
mesmo ramo: antes ela reescolhia igual à execução, as duas erravam junto,
e o cordão passava.

Duas consequências:

- **Recusa nova, "derivada".** Um passo de 1,5 mm que exija mais de 4° de
  qualquer junta é recusado *antes* de o arco abrir. Perto de
  |r| = L1 + L2 a cinemática inversa é mal condicionada — isso é
  geometria do braço, não defeito. O defeito era descobrir com o arco
  aberto.
- **A velocidade de seguimento** passou a ser dimensionada pelo **pior**
  passo da reta, não pela média. Com a média o motor ficava para trás
  exatamente onde a cinemática amplifica, e a ponta cortava caminho.

## Por que um cordão pode ser recusado

Esta é a recusa mais confusa da máquina, e vale entender de uma vez.

Num braço 2R, **aproximar a ponta da base obriga o cotovelo a dobrar**.
Uma reta cartesiana entre dois pontos folgados pode exigir muito mais
curso do que qualquer uma das pontas:

```
cordão de (288, −105) até (−53, 302) mm
   pontas:            θ2 = 80°
   meio do trecho:    θ2 = 135°        curso da junta vai até 90°
```

Os dois pontos estão a 80° num curso de 90°. O cordão é impossível assim
mesmo — e a máquina está certa em recusar.

O que mudou é a **frase**. Antes saía *"junta 2 no fim do curso
calibrado"*, e o operador olhava dois pontos claramente folgados e
concluía que o sistema estava errado. Agora sai:

> cordão 1→2: junta 2 precisa ir a 132.8 graus a 41% do trecho, e o curso
> vai até 89.5

E a recusa relata a **pior** exigência do percurso, não a primeira: a
primeira violação desse cordão é de 89,8° — relatar ela faria você abrir
o limite em 1° e não entender por que continua recusado.

Melhor ainda: **o trecho é conferido enquanto você ensina**. Ao ligar a
chave de solda de um trecho impercorrível, ele fica vermelho na lista com
a frase embaixo, e tracejado em vermelho na mesa de traçado. Você
descobre na hora, não ao apertar Executar.

Saídas: aproxime os pontos, reposicione a peça, ou quebre o cordão em
trechos menores que não passem tão perto da base.

## Proteções ativas

Tres protecoes independentes, ligaveis em `Ajustes > Protecoes ativas`:

| Protecao | Padrao | Depende de |
|----------|--------|------------|
| Fim de curso das juntas | ligada | calibracao |
| Dobra do cotovelo | ligada | folga de dobra |
| Mesa e base (Y minimo / raio morto) | **desligada** | comprimento correto dos elos |

A protecao de mesa e base vem desligada de fabrica porque depende do
comprimento dos elos estar correto — com valores errados ela recusa
posicoes validas. Ligue depois de conferir as medidas.

- Validação de postura em **todo** comando de movimento (jog, IK,
  posicionamento, reprodução).
- Jog antecipa a distância de frenagem e para antes de violar o limite.
- Heartbeat de jog: sem confirmação da interface em 350 ms, o eixo para.
- Sem contato HTTP por 2,5 s: movimento e arco cortados.
- Alarme de driver leva o sistema para `FALHA` e corta o arco.
- Tempo máximo de arco contínuo.
- Relé desligado no boot, no e-stop, na perda de conexão, no fim da
  trajetória e em qualquer falha.

## Testes

Dois bancos, os dois rodando no PC contra o firmware real:

```sh
./testes/compilar.sh          # firmware com mocks de Arduino/SD/NVS/FreeRTOS
./testes/interface/rodar.sh   # a interface num Chromium, contra um ESP32 falso
```

Ver [`testes/LEIA-ME.md`](../testes/LEIA-ME.md).

## O que ainda falta

- Sensor de home por junta e homing automático na partida.
- Botão de emergência físico (`ESTOP_FISICO_INSTALADO` em `config.h`) —
  a lógica já está pronta e testada, falta o botão.
- Migrar o polling HTTP para WebSocket.
- Jog cartesiano (mover a ponta em X/Y direto, não as juntas).
- Relógio de tempo real: sem ele o log marca milissegundos desde o boot,
  e a ordem entre partidas vem de um contador no NVS.
