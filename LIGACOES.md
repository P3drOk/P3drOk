# RoboCNC 2DOF — ligações do ESP32

Referência de bancada. Tudo que está aqui sai de `RoboCNC/config.h` — se
você mudar um `#define` lá, mude aqui também.

**Placa alvo:** ESP32 DevKit v1 (30 ou 38 pinos), chip ESP32-WROOM-32.

---

## 1. Mapa de pinos, de uma olhada

| GPIO | Direção | Vai para | Observação |
|------|---------|----------|------------|
| 16 | saída | Driver J1 · PUL+ | via buffer 5 V |
| 17 | saída | Driver J1 · DIR+ | via buffer 5 V |
| 18 | saída | Driver J2 · PUL+ | via buffer 5 V |
| 19 | saída | Driver J2 · DIR+ | via buffer 5 V |
| 23 | saída | SON dos dois drivers | via optoacoplador |
| 34 | **entrada** | Driver J1 · ALM | pull-up externo 10 k obrigatório |
| 35 | **entrada** | Driver J2 · ALM | pull-up externo 10 k obrigatório |
| **2** → 26 | saída | Relé de solda | vem de fábrica no **2** (LED, bancada). Troque para **26** na máquina real · **§5** |
| 27 | entrada | Botão de emergência | `INPUT_PULLUP`, contato NC |
| 5  | saída | microSD · CS | pull-up interno, seguro no boot |
| 14 | saída | microSD · SCK | |
| 13 | saída | microSD · MOSI | |
| 25 | entrada | microSD · MISO | **não use o 12** · ver §4 |

Pinos deliberadamente **livres**: 4, 15, 21, 22, 32, 33.
Pinos que o firmware **não usa e você também não deve**: 0, 12, 6–11.

Conferido contra `RoboCNC/config.h`. O banco de testes reprova se a
tabela e o `config.h` divergirem (`./testes/compilar.sh`).

---

## 2. Pinos do ESP32 em que não se mexe

Vale ler antes de improvisar qualquer mudança.

| GPIO | Por quê |
|------|---------|
| 6, 7, 8, 9, 10, 11 | ligados à memória flash interna. Usar = placa não liga. |
| 0 | strapping: baixo no boot entra em modo de gravação. |
| 2 | strapping + LED da placa. Pisca sozinho durante o boot. |
| 12 | strapping (MTDI): **alto no boot programa o flash para 1,8 V e a placa não dá mais boot.** |
| 15 | strapping: baixo no boot silencia o log serial. Usável com cuidado. |
| 34, 35, 36, 39 | **somente entrada** e **sem pull-up/pull-down interno**. |

---

## 3. Drivers dos servos (2× HLTNC T3D-L20A)

### 3.1 Nível de pulso — não pule esta parte

O ESP32 sai em **3,3 V**. A entrada do T3D espera **5 V**. Ligar direto
funciona "às vezes", perde passo sob ruído e é a causa mais comum de eixo
que anda torto.

Use um buffer alimentado em 5 V entre o ESP32 e o driver:

```
ESP32 GPIO16 ──► [74HCT14 ou 74HCT245, Vcc = 5 V] ──► PUL+ do J1
ESP32 GPIO17 ──►                                  ──► DIR+ do J1
ESP32 GPIO18 ──►                                  ──► PUL+ do J2
ESP32 GPIO19 ──►                                  ──► DIR+ do J2
```

O `74HCT` é o que importa: a família **HCT** reconhece 3,3 V como nível
alto; a família **HC** não. Um 74HC14 alimentado em 5 V pode simplesmente
não comutar.

Os terminais PUL− e DIR− dos drivers vão ao GND comum.

### 3.2 Se o driver estiver configurado para 24 V

Alguns T3D vêm configurados para entrada de 24 V. Nesse caso, em vez do
buffer, use **dois resistores de 2 kΩ / 0,25 W** em série, um em P+ e um
em D+. Sem eles a entrada do driver queima.

Confira em qual modo o seu driver está antes de energizar.

### 3.3 Habilitação (SON)

```
ESP32 GPIO23 ──► [optoacoplador] ──► SON+ dos dois drivers
                                     SON− ──► GND dos drivers
```

Nunca direto. O GPIO23 comanda os dois drivers ao mesmo tempo:
`servosHabilitar()` em `motores.cpp` é a única coisa que escreve nele.

### 3.4 Alarme (ALM)

```
3V3 ──[10 kΩ]──┬── ESP32 GPIO34 ──► ALM do driver J1
               │
3V3 ──[10 kΩ]──┴── ESP32 GPIO35 ──► ALM do driver J2
```

Os GPIO 34 e 35 são **somente entrada e não têm pull-up interno**. Sem os
resistores externos eles ficam flutuando e leem ruído.

O firmware nasce com `ALARME_FISICO_INSTALADO false` em `config.h`, e
nesse estado ignora os dois pinos. **Só mude para `true` depois de ligar
os fios e os pull-ups** — com o flag ligado e o pino solto, o ESP32 lê
ruído, o sistema entra em `FALHA` e recusa todo comando.

Ajuste `ALARME_ATIVO_EM` conforme a saída do seu driver (padrão `LOW`).

---

## 4. Cartão microSD (módulo adaptador de 6 pinos)

O módulo pequeno azul, com quatro resistores marcados `103`, é
**3,3 V puro**: não tem regulador nem conversor de nível. Em 5 V o cartão
morre.

| Módulo | ESP32 |
|--------|-------|
| 3V3 | 3V3 |
| GND | GND |
| CS | GPIO 5 |
| SCK | GPIO 14 |
| MOSI | GPIO 13 |
| MISO | **GPIO 25** |

### O GPIO 12 é a armadilha

A pinagem "padrão" do HSPI usa **GPIO 12 para MISO**, e o módulo tem
pull-up de 10 k nessa linha. O GPIO 12 é strapping (MTDI): alto no boot
programa o regulador do flash para 1,8 V e **a placa não dá mais boot**.

Todo tutorial de microSD com ESP32 que você achar na internet vai mandar
usar o 12. Não use. O ESP32 remapeia SPI por matriz de GPIO, então mover
o MISO para o 25 não custa desempenho nenhum.

### Alimentação

Ponha **10 µF cerâmico entre 3V3 e GND colado no módulo**. O cartão puxa
picos de ~100 mA na escrita; sem o capacitor a tensão cai, a montagem
falha e o cartão "some" no meio da gravação.

Se estiver em protoboard e o cartão não montar, tente baixar
`SD_FREQ_HZ` em `config.h` de 20 MHz para 10 MHz ou 4 MHz. Fio comprido
não sustenta 20 MHz.

Formate o cartão em **FAT32** (cartões de até 32 GB). Sem cartão no slot
a máquina funciona igual — só perde a biblioteca de programas e o log.

---

## 5. Relé de solda

```
ESP32 GPIO26 ──┬──► [optoacoplador] ──► bobina do relé ──► arco
               │
             [10 kΩ]
               │
              GND
```

Três coisas, todas obrigatórias:

1. **Pull-down externo de 10 kΩ para GND.** O GPIO flutua durante o boot
   do ESP32. Sem o resistor, o arco pode abrir sozinho na hora de ligar.
2. **Optoacoplador, nunca direto.** O ESP32 não aciona bobina.
3. **`PIN_RELE_SOLDA` vem de fábrica em `2`, que é o LED da placa.** Isso
   é para bancada: dá para ver o relé "acionando" sem nada ligado.
   **Para a máquina de verdade, troque para 26** em `config.h`. O GPIO 2
   é strapping e pisca sozinho durante o boot — num relé de solda isso
   significa abrir arco ao energizar.

```c
// config.h — máquina de verdade
#define PIN_RELE_SOLDA    26
```

---

## 6. Botão de emergência

```
3V3 ──► [contato NC do botão] ──► ESP32 GPIO27
                                  (INPUT_PULLUP interno)
```

Contato **NC** (normalmente fechado): apertar o botão abre o contato, o
pino vai a LOW pelo pull-up interno e o firmware entende emergência. Fio
partido também dá LOW — falha para o lado seguro.

Vem desligado em `config.h`:

```c
#define ESTOP_FISICO_INSTALADO  false   // mude para true após instalar
```

A lógica já está pronta e testada (banco de testes, cenário A08): com o
botão acionado, o torque cai a cada ciclo, rearmar servos é recusado e o
jog fica bloqueado até soltar.

---

## 7. Controle por Bluetooth (aplicativo Dabble)

Não tem fiação: o Bluetooth é o rádio interno do ESP32. O que tem é
consequência disso.

**Biblioteca.** `DabbleESP32`, instalável pelo gerenciador de bibliotecas
da IDE. Sem ela, ponha `BLUETOOTH_INSTALADO false` em `config.h` e o
firmware compila sem nada de Bluetooth.

**Particionamento.** BLE + Wi-Fi + servidor web não cabem na partição
padrão. Na IDE:

> Tools → Partition Scheme → **Huge APP (3MB No OTA/1MB SPIFFS)**

Sem isso o upload falha com *"Sketch too big"*.

**Rádio compartilhado.** BLE e Wi-Fi dividem a mesma antena. Funcionam
juntos — é o caso de uso normal do ESP32 — mas com o gamepad conectado a
interface web fica um pouco mais lenta. A pilha BLE também come ~60 kB de
RAM.

**Conectar.** Aplicativo Dabble → módulo **GamePad** → conectar em
`RoboCNC-2DOF`.

| Controle | O que faz |
|----------|-----------|
| analógico / direcional | jog das duas juntas, proporcional ao quanto você empurra |
| **X** (cross) | **PARADA** — mesmo caminho do botão PARAR da tela, fora da fila de comandos |
| triângulo | liga/desliga o modo precisão |
| quadrado | grava ponto na posição atual |
| círculo | vai para o zero da máquina |
| start | executa o **ensaio** (sem arco) |
| select | habilita/desabilita os servos |

**O gamepad não abre arco.** Executar com solda exige a confirmação da
tela. Botão de controle não é lugar de comandar arco elétrico — e por
isso `start` roda sempre o ensaio, nunca a execução com solda.

O gamepad conectado conta como operador presente: sem isso o supervisor
cortaria o movimento em 2,5 s por "conexão perdida" para quem usa só o
Bluetooth, sem navegador aberto.

## 8. LED de status

`PIN_LED_STATUS` vem em `255`, que significa **desligado**. Isso é
necessário enquanto o relé estiver no GPIO 2, senão os dois brigam pelo
mesmo pino.

Depois de mover o relé para o 26, você pode apontar o LED para o 2:

```c
#define PIN_LED_STATUS    2
```

---

## 9. Aterramento

A regra que evita queimar a eletrônica inteira:

> **O retorno da solda vai direto da peça para a máquina de solda. Nunca
> passando pelo chassi da eletrônica, nunca pelo GND do ESP32.**

Corrente de solda procurando caminho de volta pelo GND lógico destrói
tudo que encontrar. Garra de retorno na peça, o mais perto possível do
ponto de soldagem.

O GND do ESP32, o GND dos drivers e o GND da fonte de 5 V do buffer têm
que ser o **mesmo ponto**, ligados em estrela — não em corrente.

---

## 10. Ordem de energização na primeira vez

1. Só o ESP32, sem drivers e sem solda. Confira no monitor serial
   (115200) que o Wi-Fi subiu:
   ```
   [WIFI] Access Point ativo.
   [WIFI]   SSID : Robo2dof
   [WIFI]   Abra no navegador: http://192.168.4.1
   ```
2. Conecte o celular na rede `Robo2dof`, senha `12345678`, e abra
   `http://192.168.4.1`.
3. **Antes de ligar a solda:** aba Ajustes → *Pulsar relé por 2 segundos*.
   Confirme que o relé (ou o LED) responde. É o teste de fiação sem arco.
4. Ligue os drivers, ainda sem a solda. Aba Ajustes → *Habilitar servos*
   → volte à aba Mover e teste o joystick devagar.
5. Aba Ajustes → *Abrir assistente de calibração*. Ao final confira se os
   limites em graus batem com a máquina real. Se não baterem, o erro está
   na resolução, não na medição.
6. Aba Programa → grave dois pontos → *Executar ensaio*. Só depois disso
   ligue a solda.

---

## 11. Se algo não funciona

| Sintoma | Onde olhar |
|---------|------------|
| Placa não dá boot depois de ligar o cartão | MISO no GPIO 12. Mova para o 25. |
| Eixo anda torto, perde passo | Falta buffer 5 V, ou é 74HC em vez de 74HCT. |
| Sistema entra em FALHA e recusa tudo | `ALARME_FISICO_INSTALADO true` sem os fios e pull-ups de ALM. |
| Arco abre sozinho ao energizar | Relé no GPIO 2, ou falta o pull-down de 10 k. |
| Cartão não monta | 5 V em vez de 3V3; falta o capacitor de 10 µF; fio longo demais para 20 MHz. |
| Jog engasga | Wi-Fi fraco. O firmware para o eixo sem heartbeat por 350 ms — é proposital. |
| Jog recusado, nada se move | Servos desabilitados. A interface diz o motivo abaixo do joystick. |
| Braço trava e não sai do limite | Calibração com curso curto demais. Refaça movendo até os limites reais. |
| *"Sketch too big"* ao gravar | Partição padrão com o Bluetooth ligado. Use **Huge APP** (§7). |
| Recusa dizendo que uma junta precisa ir além do curso | Leia a frase inteira: se ela diz *"a N% do trecho"*, o problema é o **meio do cordão**, não as pontas. Reta cartesiana perto da base obriga o cotovelo a dobrar. Aproxime os pontos ou reposicione a peça. |
| Gamepad não aparece no Dabble | `BLUETOOTH_INSTALADO` em false, ou partição sem espaço. Confira o log serial em 115200. |
