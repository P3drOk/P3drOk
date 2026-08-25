# Monitor do encoder — ESP32 sozinho na placa

Programa **independente do sistema**. Grave-o no ESP32 no lugar do
firmware, abra o monitor serial em **115200**, e ele mostra a posição do
encoder ao vivo com velocidade, RPM, sentido e passos acumulados.

Foi escrito pelo operador e é o código que **provou que a leitura
funciona** nesta máquina. Está aqui por três motivos:

1. É a referência viva de como falar com este driver. Quando o sistema
   não lê e este lê, a diferença está no sistema — e foi assim que se
   achou o defeito do DE (ver `ACHADOS.md`, rodada 19).
2. Roda com o ESP32 **sozinho**: sem Wi-Fi, sem servidor web, sem cartão,
   sem interrupção de motor. Separa "o barramento não presta" de "algo no
   sistema atrapalha o barramento".
3. Mede coisas que o painel não mede sem estar montado na máquina.

## Antes de gravar, confira estas quatro linhas

```c
#define ENCODER_MODBUS_BAUD    19200
#define ENCODER_MODBUS_ID      1
#define ENCODER_MODBUS_FUNC    3      // 3 = holding, 4 = input
#define ENCODER_REG_BASE       90     // palavra BAIXA; a alta é a seguinte
#define CONTAGENS_POR_VOLTA    131072 // encoder de 17 bits
```

`ENCODER_REG_BASE` é o número que muda de máquina para máquina. Se não
souber o seu, use `ferramentas/teste_rs485` — o modo 7 acha, e o modo 8
mede as contagens por volta.

## Ligação

Igual à do sistema (ver `LIGACOES.md`):

| MAX485 | ESP32 | |
|---|---|---|
| `RO` | GPIO 22 | via divisor 2 resistores, **não** conversor de MOSFET |
| `DI` | GPIO 21 | |
| `DE` | GPIO 4 | 1 = transmitindo |
| `RE` | GPIO 26 | 0 = ouvindo |
| `A` / `B` | driver | trocados = silêncio absoluto |
| `GND` | comum | obrigatório |

## Comandos

| | |
|---|---|
| `r` | zera passos e estatísticas |
| `p` | pausa / continua |
| `s` | estatísticas acumuladas |
| `d` | resumo detalhado da leitura atual |
| `g` | gráfico ASCII da posição |
| `e` | despeja tudo em CSV |
| `c` | mostra a configuração em uso |
| `8` | mede as contagens por volta (dê **uma volta** no eixo do motor) |
| `9` | grava CSV contínuo, para colar numa planilha |
| `?` | ajuda |

## O que cada número quer dizer

| | |
|---|---|
| **Posição** | contagem crua do encoder, 32 bits |
| **Delta** | quanto andou desde a leitura anterior |
| **Veloc** | contagens por segundo — do eixo, não a comandada |
| **RPM** | a mesma coisa em voltas por minuto do **motor** |
| **Sentido** | cresce, decresce, parado |
| **Passos** | soma do caminho andado: ir e voltar **não** dá zero, dá o dobro |

Comparar a velocidade **medida** com a comandada é o jeito de ver
escorregamento.

## Diferenças em relação ao sistema

O sistema faz duas coisas a mais, e por bons motivos:

- **Zona morta no sentido.** Um encoder de 17 bits treme um ou dois
  passos parado. Sem zona morta esse tremor vira "inverteu" dezenas de
  vezes por segundo, e o contador de inversões — que serve para achar
  folga — não vale nada.
- **Velocidade zera quando a leitura falha.** Manter a última faria a
  tela dizer que o eixo continua girando depois que o fio caiu.

E o sistema **dorme** enquanto espera a resposta, em vez de girar em
espera ocupada: lá o núcleo é compartilhado com o servidor web. Aqui não
faz diferença, porque o ESP32 está sozinho.
