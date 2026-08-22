# O cartão de memória — o que ele é, e o que ele não é

Documento de referência do RoboCNC 2DOF. Responde à pergunta que motivou
ele: *"seria possível instalar o sistema no SD também, para ampliar a
memória?"*

---

## 1. A resposta curta

**Não.** O ESP32 não consegue executar código a partir do cartão SD, e
nenhum ajuste de configuração muda isso. Não é limitação do firmware — é
de como o processador busca instrução.

Mas há três caminhos reais para o problema de fundo ("meu código vai
crescer e não vai caber"), e nenhum deles é o cartão. Estão na §4.

---

## 2. Por que não dá

O ESP32 executa direto da flash SPI interna porque a MMU **mapeia** essa
flash dentro do espaço de endereços do processador (a região
`0x400D0000` em diante). Do ponto de vista do núcleo, ler uma instrução
da flash é igual a ler da RAM: ele emite um endereço, um cache de 32 kB
resolve, e pronto. Isso se chama *execute in place* (XIP).

O cartão SD é outra coisa: é um **dispositivo de bloco** pendurado num
barramento SPI. Não tem endereço no mapa de memória. Para ler um byte
dele é preciso executar uma sequência de comandos, esperar resposta,
montar o setor em RAM. Um processador não consegue buscar a próxima
instrução assim — a busca de instrução tem que ser resolvida pelo
hardware, em nanossegundos, sem ninguém rodando driver no meio.

O mesmo vale para RAM: o cartão também não vira `malloc()`. Só a PSRAM
(§4.3) faz isso, porque ela **é** mapeada.

> A confusão é razoável: num PC o sistema roda "do HD". Mas ali há um
> sistema operacional que **copia** o programa do disco para a RAM antes
> de executar, e uma MMU com memória virtual que faz paginação sob
> demanda. O ESP32 não tem nem RAM sobrando para copiar 3 MB de programa,
> nem paginação de disco.

---

## 3. Onde você está hoje

Flash de 4 MB no ESP32-WROOM-32, dividida pelo `RoboCNC/partitions.csv`
que está na pasta do sketch:

| Partição | Tamanho | Para quê |
|----------|---------|----------|
| `nvs` | 20 kB | calibração, ajustes — sobrevive a regravação |
| `otadata` | 8 kB | reservado |
| `app0` | **3 MB** | **o programa** |
| `spiffs` | 896 kB | não usado por este projeto |
| `coredump` | 64 kB | despejo de falha |

O erro *"Sketch too big — Maximum is 1310720"* que você viu era o esquema
**padrão** do ESP32 (1,25 MB de app), não o deste projeto. Com o
`partitions.csv` da pasta, o teto é 3 MB. Se a sua IDE ignorar o arquivo:
`Tools → Partition Scheme → Huge APP (3MB No OTA)`.

A ocupação real aparece no monitor serial a cada partida:

```
[FLASH] sketch NNNN kB de 3072 kB de particao (NN% usado)
[RAM]   NNN kB livres
```

Repare que são **dois** limites diferentes, e eles se esgotam por motivos
diferentes:

- **Flash (3 MB)** — tamanho do código. Cresce quando você adiciona
  bibliotecas. Bluetooth sozinho custava ~700 kB, que foi o que estourou
  o esquema padrão.
- **RAM (320 kB, ~230 kB livres)** — o que o programa manipula. Cresce
  com buffers, listas de pontos, o buffer do servidor web.

---

## 4. O que fazer quando faltar espaço de verdade

### 4.1 Sobra flash agora — use antes de comprar hardware

Se `[FLASH]` mostrar menos de 80% usado, não há problema a resolver. O
`spiffs` de 896 kB não é usado por este projeto: dá para removê-lo do
`partitions.csv` e crescer `app0` para ~3,9 MB.

### 4.2 Módulo com mais flash

`ESP32-WROOM-32E-N8` (8 MB) ou `-N16` (16 MB). Mesmo encapsulamento,
mesma pinagem, mesmo preço de ordem de grandeza. Basta ajustar o
`partitions.csv` e regravar — nenhuma linha de código muda.

### 4.3 Módulo com PSRAM — este é o que resolve RAM

`ESP32-WROVER` traz 4 ou 8 MB de **PSRAM**, que *é* mapeada no espaço de
endereços e serve para `ps_malloc()`. É a única maneira de ampliar
memória de verdade neste chip. Continua não servindo para código, mas
resolve buffers grandes.

### 4.4 Streaming — a única forma em que o cartão *amplia* algo

O cartão não amplia memória, mas amplia **capacidade de dados**, desde
que você não carregue tudo de uma vez.

Hoje o programa vive inteiro na RAM (`MAX_PONTOS = 120`). Um contorno
importado de DXF pode passar disso. A saída não é mais RAM: é executar
**direto do cartão**, lendo uma janela de pontos por vez e descartando os
já percorridos. A arquitetura já favorece isso — a tarefa de cartão roda
no core 0, separada do laço de controle, e a área de troca já existe.

Isso está na lista de melhorias, não implementado.

---

## 5. Para que o cartão serve hoje

| Pasta | Conteúdo | Formato |
|-------|----------|---------|
| `/prog` | programas de solda | texto, em **graus** — dá para escrever num editor comum |
| `/traj` | trajetórias a mão livre | binário (magic `TRJ1`) |
| `/cfg` | cópias dos ajustes | texto |
| `/log` | um arquivo por partida | CSV: alarme, emergência, execução |

Três garantias de projeto que valem conhecer:

1. **Nenhum I/O de cartão acontece no laço de controle.** A tarefa de SD
   vive no core 0; o core 1 só enfileira pedido e lê estado publicado.
   Cartão lento, ou ausente, nunca engasga o motor.
2. **Programa é validado antes de substituir o que está na máquina.** O
   arquivo é lido para uma área de troca, cada ponto passa por
   `posturaValida()`, e só então o core 1 troca. Arquivo corrompido não
   derruba o programa que estava rodando.
3. **A máquina funciona inteira sem cartão.** Ele é conveniência e
   backup; o que a máquina usa ao ligar é o NVS interno.

---

## 6. Ligação — e o pino que impede o boot

Detalhe completo em [`LIGACOES.md`](LIGACOES.md) §4. O resumo:

| Módulo TF | ESP32 |
|-----------|-------|
| VCC | **3V3** (nunca 5 V) |
| GND | GND |
| CS | GPIO 5 |
| SCK | GPIO 14 |
| MOSI | GPIO 13 |
| MISO | **GPIO 25** |

**MISO no GPIO 25, não no 12.** O 12 é *strapping pin*: o nível dele no
reset escolhe a tensão do regulador da flash. O módulo TF azul tem um
pull-up de 10 k no MISO, que segura o 12 em alto no boot — e a placa não
liga. Não é defeito do cartão; é a placa recusando iniciar.

## 7. Se o cartão não monta

Na ordem em que resolve mais rápido:

1. **Formato.** FAT32. exFAT e NTFS não montam. Cartão acima de 32 GB
   costuma vir em exFAT de fábrica — reformate como FAT32.
2. **Alimentação.** 3V3, não 5 V. E um capacitor de 10 µF entre 3V3 e GND
   junto ao módulo: o pico de corrente na inicialização do cartão derruba
   a tensão e a montagem falha de forma intermitente.
3. **Fios.** 20 MHz não perdoa rabicho. Menos de 10 cm, e longe dos cabos
   de pulso dos drivers.
4. **Pinos.** Confira contra a tabela acima. `testes/conferir_ligacoes.py`
   reprova se `LIGACOES.md` divergir do `config.h`.
5. **Cartão.** Alguns cartões de má qualidade não respondem em 20 MHz.
   Baixe `SD_FREQ_HZ` em `config.h` para 10000000 e teste.

O painel da aba **Arquivos** mostra o estado real (`SEM_CARTAO`, `ERRO`,
`PRONTO`) e a mensagem da última operação. `Procurar cartão de novo`
força uma nova tentativa de montagem sem reiniciar a máquina.
