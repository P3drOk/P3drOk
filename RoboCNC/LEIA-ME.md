# RoboCNC 2DOF — braço de solda

Firmware ESP32 para braço planar de 2 graus de liberdade com gravação e
reprodução de trajetória, controle de relé de solda e interface web.

Hardware: ESP32 + 2× driver HLTNC T3D-L20A + servo 80AST-A1C04025.

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
| `servidor_web.*`  | Rotas HTTP                                              |
| `pagina_web.h`    | Interface                                               |
| `RoboCNC.ino`     | Setup, supervisão de segurança, máquina de estados      |

## Ligação elétrica

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

## Primeira partida

1. `Ajustes → Resolução`: informe pulsos por volta (engrenagem eletrônica
   do T3D) e a redução mecânica da junta.
2. `Ajustes → Geometria`: comprimento dos elos, folga de dobra, Y mínimo
   e raio morto da base.
3. `Ajustes → Calibração`: percorra o assistente. Ao final confira se os
   limites em graus batem com a máquina real — se não baterem, o erro
   está na resolução, não na medição.
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

## O que ainda falta

- Sensor de home por junta e homing automático na partida.
- Botão de emergência físico (`ESTOP_FISICO_INSTALADO` em `config.h`).
- Persistir a trajetória em NVS/SPIFFS (hoje ela vive só na RAM).
- Migrar o polling HTTP para WebSocket.
