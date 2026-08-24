# Robo2dof — ligações do ESP32

Referência de bancada. Tudo que está aqui sai de `Robo2dof/config.h` — se
você mudar um `#define` lá, mude aqui também.

**Placa alvo:** ESP32 DevKit v1 (30 ou 38 pinos), chip ESP32-WROOM-32.

---

## 0. Antes de gravar: a partição

Se a IDE reclamar

```
Sketch uses 1721921 bytes (131%) of program storage space. Maximum is 1310720 bytes.
Sketch too big
```

não é o código: é o **mapa de memória do flash**. O esquema padrão do
ESP32 reserva só 1,25 MB para o programa.

A pasta do sketch traz um **`partitions.csv`** com 3 MB de app. O núcleo
Arduino-ESP32 usa esse arquivo quando ele está junto do `.ino`, então na
maioria das instalações basta gravar. Se a IDE continuar dizendo
`Maximum is 1310720 bytes`, ela ignorou o arquivo — escolha na mão:

> **Tools → Partition Scheme → Huge APP (3MB No OTA/1MB SPIFFS)**

**A calibração salva não se perde.** O `partitions.csv` mantém o `nvs` no
mesmo endereço e tamanho do esquema padrão (`0x9000`, `0x5000`).

O firmware sem Bluetooth é bem menor que os 1,7 MB que estouravam, e é
provável que caiba na partição padrão. O `partitions.csv` fica assim
mesmo: ele não custa nada (não usamos OTA nem SPIFFS) e evita a mesma
surpresa se o sketch crescer. Quem manda é o número que a própria IDE
imprime ao compilar.

### Por que a página é servida comprimida

`pagina_web.h` tem 75 kB de HTML. O firmware serve a versão gzip de
`pagina_web_gz.h` — 21,8 kB — com `Content-Encoding: gzip`. São ~53 kB a
menos de flash e uma página que chega no celular umas 3 vezes mais
rápido, o que num ponto de acesso de ESP32 é a diferença entre abrir na
hora e esperar.

Você edita `pagina_web.h`. Depois de mexer, rode:

```sh
python3 testes/gerar_pagina_gz.py
```

O banco de testes reprova se o comprimido ficar velho, então não dá para
esquecer e o robô servir uma interface diferente da do repositório.

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

Conferido contra `Robo2dof/config.h`. O banco de testes reprova se a
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

## 7. LED de status

`PIN_LED_STATUS` vem em `255`, que significa **desligado**. Isso é
necessário enquanto o relé estiver no GPIO 2, senão os dois brigam pelo
mesmo pino.

Depois de mover o relé para o 26, você pode apontar o LED para o 2:

```c
#define PIN_LED_STATUS    2
```

---

## 8. Aterramento

A regra que evita queimar a eletrônica inteira:

> **O retorno da solda vai direto da peça para a máquina de solda. Nunca
> passando pelo chassi da eletrônica, nunca pelo GND do ESP32.**

Corrente de solda procurando caminho de volta pelo GND lógico destrói
tudo que encontrar. Garra de retorno na peça, o mais perto possível do
ponto de soldagem.

O GND do ESP32, o GND dos drivers e o GND da fonte de 5 V do buffer têm
que ser o **mesmo ponto**, ligados em estrela — não em corrente.

---

## 9. Ordem de energização na primeira vez

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

## 10. Se algo não funciona

| Sintoma | Onde olhar |
|---------|------------|
| Placa não dá boot depois de ligar o cartão | MISO no GPIO 12. Mova para o 25. |
| Eixo anda torto, perde passo | Falta buffer 5 V, ou é 74HC em vez de 74HCT. |
| Sistema entra em FALHA e recusa tudo | `ALARME_FISICO_INSTALADO true` sem os fios e pull-ups de ALM. |
| Arco abre sozinho ao energizar | Relé no GPIO 2, ou falta o pull-down de 10 k. |
| Cartão não monta | 5 V em vez de 3V3; falta o capacitor de 10 µF; fio longo demais para 20 MHz. |
| Jog engasga | Wi-Fi fraco. O firmware para o eixo sem heartbeat por 350 ms — é proposital. |
| `robo2dof.local` não abre | mDNS não resolvido no aparelho. Use `192.168.4.1`. |
| O celular sai do Wi-Fi da máquina sozinho | Android trocando para dados móveis por "não há internet". O DNS de captura ajuda, mas se insistir, marque a rede como permanecer conectado. |
| Jog recusado, nada se move | Servos desabilitados. A interface diz o motivo abaixo do joystick. |
| Braço trava e não sai do limite | Calibração com curso curto demais. Refaça movendo até os limites reais, ou use *Apagar calibração gravada*. |
| Braço trava durante a **calibração** | Já corrigido: sem calibração válida o jog é livre (modo de instalação). Se ainda travar, veja se os servos estão habilitados — a interface diz o motivo abaixo do joystick. |
| Braço vai para um lado, desenho vai para o outro | Sinal do eixo trocado. `Ajustes → Sentido dos eixos`, chave da junta. Calibração não conserta: o erro é de sinal, não de escala. |
| Aperto ↻ e a junta gira anti-horário | Mesmo caso. Dá para corrigir sem sair do assistente: a etapa de referência da calibração tem as duas chaves. |
| Ângulo na tela não bate com o transferidor | Resolução digitada errada. Refaça a calibração e informe o **curso real medido** na última etapa. |
| Ângulo na tela não bate com o transferidor, e a calibração não resolve | Redução mecânica diferente do catálogo. `Ajustes → Aferir a redução no braço`: marque, gire, meça e grave. |
| Desenho na tela deslocado do braço depois de perder passo | Leve o braço à posição de referência e use `Mover → Zerar a máquina aqui`. |
| Partida com **tranco**, e o eixo leve perde passo no arranque | `Ajustes → Rampa → Suavidade da partida`. Zero é rampa reta (aceleração entra de uma vez); 100 a 150 costuma resolver. Se persistir, baixe a rampa. |
| *"Sketch too big"*, `Maximum is 1310720` | Partição padrão. Veja a **§0** — `partitions.csv` ou o menu Huge APP. |
| Recusa dizendo que uma junta precisa ir além do curso | Leia a frase inteira: se ela diz *"a N% do trecho"*, o problema é o **meio do cordão**, não as pontas. Reta cartesiana perto da base obriga o cotovelo a dobrar. Aproxime os pontos ou reposicione a peça. |
