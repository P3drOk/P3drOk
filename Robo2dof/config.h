#pragma once
#include <Arduino.h>

// =====================================================================
//  Robo2dof - Configuracao central
//  Hardware alvo: ESP32 + 2x driver HLTNC T3D-L20A + servo 80AST-A1C04025
// =====================================================================

// ---------------------------------------------------------------------
// PINOS
// ---------------------------------------------------------------------
// Saidas de pulso/direcao (vao para PUL+/DIR+ dos drivers via buffer 5V)
#define PIN_J1_PULSO      16
#define PIN_J1_DIR        17
#define PIN_J2_PULSO      18
#define PIN_J2_DIR        19

// O habilita dos servos NAO tem pino. Ele vai por Modbus/RS485, no
// registrador provado na bancada -- ver SON_REG_PADRAO mais abaixo e a
// secao "Habilita (SON)" do LIGACOES.md. O GPIO 23 fica LIVRE.

// Alarme dos drivers (ALM). GPIO 34/35 sao SOMENTE ENTRADA e nao possuem
// pull-up interno: use pull-up externo de 10k para 3V3.
// O ALM DOS DRIVERS SAIU DO FIRMWARE.
//
// Ele lia dois pinos que ninguem nunca ligou, e o codigo carregava um
// caminho de falha inteiro -- entrar em FALHA, recusar comando, exigir
// rearme -- disparado por uma leitura que na pratica era ruido de pino
// solto. Enquanto nao houver um jeito conferido de ler o alarme deste
// driver, esse caminho nao existe: quem corta de verdade continua sendo
// o CONTATOR da emergencia, que e hardware e nao depende de software
// nenhum. GPIO 34 e 35 voltaram a ficar livres.

// Rele da solda. Ativo em nivel ALTO, com pull-down fisico de 10k para GND
// (o GPIO flutua durante o boot do ESP32).
//
// GPIO 2 = LED da propria placa. Util para BANCADA: da para ver o rele
// "acionando" sem nada ligado. Para a maquina de verdade troque para 26:
// o GPIO 2 e strapping pin e pisca sozinho durante o boot, o que num
// rele de solda significa abrir arco na hora de energizar.
#define PIN_RELE_SOLDA    2

// ---------------------------------------------------------------------
// BOTAO DE EMERGENCIA fisico.
//
// Ligacao a prova de falha, e a polaridade nao e detalhe:
//
//   GND ──► [contato NC do botao] ──► GPIO27  (INPUT_PULLUP interno)
//
//   solto (contato FECHADO)  -> o pino ve GND        -> LOW  -> normal
//   apertado (contato ABRE)  -> o pull-up puxa       -> HIGH -> EMERGENCIA
//   fio partido / desligado  -> o pull-up puxa       -> HIGH -> EMERGENCIA
//
// O terceiro caso e a razao de ser desta ligacao: botao de emergencia
// com fio rompido tem de PARAR a maquina, nao passar despercebido. Com
// o contato NA (normalmente aberto), ou com o pull-up do lado errado,
// um fio solto vira "esta tudo bem" -- e o botao vermelho deixa de
// existir sem ninguem notar.
//
// Havia aqui a combinacao errada: o comentario mandava ligar o contato
// no 3V3 e o codigo esperava LOW. Com pull-up interno e o outro lado no
// 3V3 o pino nunca chega a LOW, entao o botao nao faria nada -- e um
// fio partido tambem nao faria nada. Ver ACHADOS.md, R72.
#define PIN_ESTOP         27
// Nivel logico que significa EMERGENCIA no pino acima.
#define ESTOP_NIVEL_ATIVO HIGH

// ---------------------------------------------------------------------
// BOTAO DE APRENDIZADO (o botao da ponteira)
//
// Um botao so, com dois gestos:
//
//   TOQUE CURTO   grava o ponto onde a ponta esta agora.
//   SEGURAR 1,5 s entra ou sai do modo aprendizado.
//
// No modo aprendizado os servos ficam DESLIGADOS: o braco fica solto e o
// operador leva a ponteira com a mao. O encoder acompanha, entao o ponto
// gravado e onde a ponta REALMENTE esta -- nao onde o firmware acha.
//
// Por isso o aprendizado exige o zero absoluto ensinado: sem ele o
// encoder nao tem do que a posicao ser medida, e o botao gravaria pontos
// baseados numa contagem que ninguem acertou.
//
// GPIO 32: entrada comum, sem funcao de strapping, sem briga com nada
// deste projeto. Ligue o botao entre o pino e o GND -- o pull-up e
// interno, e nivel BAIXO quer dizer apertado.
#ifndef PIN_APRENDER
#define PIN_APRENDER      32
#endif
// Mude para true depois de instalar o botao. Com false o firmware nem
// olha o pino -- entrada solta le ruido, e ruido aqui gravaria ponto
// sozinho no meio de um programa.
#ifndef APRENDER_BOTAO_INSTALADO
#define APRENDER_BOTAO_INSTALADO  false
#endif
static const uint16_t APRENDER_SEGURAR_MS = 1500;  // segurar = trocar de modo
static const uint16_t APRENDER_DEBOUNCE_MS = 40;

// ---------------------------------------------------------------------
// CARTAO MICRO SD (modulo adaptador TF de 6 pinos, SPI)
// ---------------------------------------------------------------------
// O modulo pequeno de 6 pinos (o azul com os quatro resistores 103) e
// 3,3 V PURO: nao tem regulador nem conversor de nivel. Ligue em 3V3.
// Em 5 V o cartao morre.
//
//   modulo      ESP32          observacao
//   -------     -----------    --------------------------------------
//   3V3         3V3            NUNCA no 5V deste modulo
//   GND         GND
//   CS          GPIO 5         tem pull-up interno, seguro no boot
//   SCK         GPIO 14
//   MOSI        GPIO 13
//   MISO        GPIO 12 NAO!   -> GPIO 25 (ver abaixo)
//
// ATENCAO AO GPIO 12. A pinagem "padrao" do HSPI usa 12 para MISO, e o
// modulo tem pull-up de 10k nessa linha. O GPIO 12 e strapping (MTDI):
// alto no boot programa o regulador do flash para 1,8 V e a placa nao
// da mais boot. Por isso o MISO vai para o 25. O ESP32 remapeia SPI por
// matriz de GPIO, entao nao ha perda nenhuma.
//
// Ponha 10 uF ceramico entre 3V3 e GND junto ao modulo: o cartao puxa
// picos de ~100 mA na escrita e a queda de tensao derruba a montagem.
// ---------------------------------------------------------------------
// RS485 -- leitura do encoder pelos drivers, por Modbus RTU
//
// ATENCAO: a UART2 do ESP32 tem como pinos PADRAO o 16 e o 17, que neste
// projeto sao o PASSO e a DIRECAO da junta 1. Chamar begin() sem passar
// os pinos faria a UART tomar conta deles e mandar lixo para o driver.
// Todo begin() deste projeto passa RX e TX explicitamente.
// ---------------------------------------------------------------------
#ifndef PIN_RS485_RX
#define PIN_RS485_RX  22    // vem do RO do MAX485, ja em 3,3 V
#endif
#ifndef PIN_RS485_TX
#define PIN_RS485_TX  21    // vai para o DI
#endif
#ifndef PIN_RS485_DE
#define PIN_RS485_DE   4    // driver enable
#endif
#ifndef PIN_RS485_RE
#define PIN_RS485_RE  26    // receiver enable (ativo em baixo)
#endif

// O DE e o RE do MAX485 sao controlados por GPIO, a mao, exatamente
// como no monitor que funciona na maquina do operador. Houve aqui um
// modo em que o periferico da UART dirigia o DE sozinho (RS485
// meio-duplex por hardware): em teoria e melhor, porque baixa o DE no
// fim exato do ultimo bit. Na maquina do operador o DE simplesmente nao
// subia -- o quadro nao saia no barramento e nao havia o que responder.
// Nao vale a pena um mecanismo mais fino que nao liga.

// Um barramento, os dois drivers: multiponto, enderecos diferentes.
static const uint32_t ENC_BAUD_PADRAO    = 19200;
static const uint8_t  ENC_PARIDADE_PADRAO = 0;      // 0=8N1 1=8E1 2=8O1
// Padroes MEDIDOS na maquina do operador com ferramentas/teste_rs485.
//
// A cacada (modo 7) girou o eixo a mao e comparou os 256 registradores
// da funcao 3 antes e depois. Mudaram cinco, e dois deles sao o par da
// posicao:
//
//   registrador 90 (0x5A): 61346 -> 39440   (a parte BAIXA, varia muito)
//   registrador 91 (0x5B):     0 ->     1   (a parte ALTA, varia +1)
//
// Montando com a palavra baixa primeiro: 61346 -> 104976, ou seja
// +43630 contagens numa girada a mao. Com a palavra alta primeiro o
// numero anda 1,4 bilhao para tras, que nao e giro nenhum. Entao e
// BAIXA PRIMEIRO, e o par e 90/91.
//
// Confirmacao independente, no mesmo log: duas varreduras completas da
// funcao 3, uma atras da outra, sao identicas em 255 registradores e
// diferem so no 90 (36998 -> 37000). E o unico que anda sozinho.
//
// FUNCAO 3, nao 4. Nesta maquina a posicao viva esta na tabela de
// holding registers. A funcao 4 tambem responde, mas o par 5/6 dela nao
// e a posicao -- na funcao 3 esses mesmos enderecos valem 50 e 25, que
// sao parametro parado.
//
// Registrador 0 nunca e posicao: e onde comeca a tabela de parametros.
// Por isso um 0 guardado no NVS e tratado como "nao configurado" e cai
// nestes padroes.
static const uint8_t  ENC_FUNCAO_PADRAO  = 3;       // 3=holding 4=input
static const uint16_t ENC_REG_PADRAO     = 90;      // palavra baixa da posicao
static const bool     ENC_BAIXA_PRIMEIRO = true;
// 43630 contagens numa girada a mao da 1/3 de volta num encoder de 17
// bits, que e o que estes servos usam. Se a leitura andar rapido ou
// devagar demais em graus, e este numero que se ajusta na tela.
static const float    ENC_CONTAGENS_PADRAO = 131072.0f;   // encoder de 17 bits
// ---------------------------------------------------------------------
// HABILITA (SON) PELO MODBUS -- O UNICO CAMINHO
//
// Ate a versao anterior o habilita era um fio (GPIO 23, por
// optoacoplador, no SON dos dois drivers). Ele saiu: nesta maquina o
// P098 do painel governa o torque, e com ele em 1 o terminal externo nao
// tinha efeito nenhum -- o fio ja era decorativo antes de sair.
//
// O QUE ISSO CUSTA, DITO SEM RODEIO
//
// Fio de SON rompido desabilitava o motor. Fio de RS485 rompido NAO
// desabilita nada: deixa o eixo como estava. ESP32 travado idem. O
// caminho do habilita deixou de ser falha segura, e nao ha configuracao
// que o traga de volta.
//
// O QUE SOBRA NO LUGAR, E QUE PASSOU A SER OBRIGATORIO
//
//   1. CONTATOR em serie com a potencia dos drivers, aberto pelo contato
//      NC do botao de emergencia. E o unico corte que funciona com o
//      ESP32 morto. Ver LIGACOES.md secao 6.
//   2. Escrita de desabilita que nao confirma na releitura derruba a
//      maquina em MODO_FALHA e recusa comando. Melhor parar dizendo que
//      nao sabe do que seguir achando que desligou.
//
// O REGISTRADOR FOI PROVADO, NAO CHUTADO
//
// Bancada com ferramentas/teste_rs485, modos d / d2 / s: o P098 do
// painel e o registrador Modbus 98, habilita=1, desabilita=0, funcao 06,
// id 1, 19200 8N1. reg = 0 significa NAO CONFIGURADO e nada e escrito --
// registrador 0 e onde comeca a tabela de parametros do driver.
static const uint16_t SON_REG_PADRAO      = 98;
static const uint16_t SON_VAL_LIGA_PADRAO = 1;
static const uint16_t SON_VAL_DESL_PADRAO = 0;
// Prazo de resposta da escrita do habilita, separado do prazo da leitura.
//
// A leitura usa 100 ms porque e o numero que ja funcionou na bancada e
// nao custa nada: ela roda sozinha no ciclo. A escrita e outra historia
// -- ela acontece quando o operador aperta um botao, possivelmente com o
// jog em curso, e cada milissegundo aqui e um milissegundo em que a
// tarefa de rede (core 0, prioridade MENOR que a do encoder) nao roda.
//
// A 19200 baud um quadro de 8 bytes leva ~4 ms e a resposta outro tanto.
// 60 ms e seis vezes o necessario e ainda cabe com folga dentro do
// TIMEOUT_JOG_MS: o botao nao pode cortar o movimento de quem esta
// comandando. Ver o cenario V06 do banco, que mede isso.
static const uint32_t SON_TIMEOUT_MS      = 60;

// Quantas vezes insistir numa escrita que nao confirmou na releitura.
// Desabilitar que se perde no fio e a falha que importa, entao a
// insistencia existe para ela. Curta de proposito: ninguem pode ficar
// preso esperando o barramento com o eixo energizado.
static const uint8_t  SON_TENTATIVAS      = 3;
// Prazo para a tarefa do core 0 confirmar o que o core 1 pediu.
//
// Isto NAO e o detector de falha do dia a dia: quem descobre que a
// escrita nao pegou e a propria maquina de estados, que esgota as
// tentativas e diz. Este prazo existe so para o caso em que a tarefa do
// core 0 morreu e nunca vai responder -- "pendente para sempre" seria a
// tela dizendo "calma" com o eixo energizado.
//
// Por isso e generoso: as tentativas sao espalhadas UMA POR CICLO para
// nao prender o barramento, e o pior caso realista (dois drivers mudos,
// tres tentativas cada) leva ~400 ms. 2 s deixa esse caminho terminar
// sozinho e ainda assim pega tarefa morta em tempo util.
static const uint32_t SON_PRAZO_MS        = 2000;
// Quanto o OTA espera pela confirmacao do desabilita antes de desistir da
// atualizacao. Recusar uma atualizacao e reversivel; reiniciar com o
// braco energizado nao e.
static const uint32_t OTA_PRAZO_SON_MS    = 1500;

// A partir daqui a contagem de passos DEIXOU de descrever o braco.
//
// Com servo ligado, uma divergencia entre o comandado e o medido e perda
// de passo, e quem cuida dela e o assentamento -- que retoca ate
// maxCorrecaoGraus (teto de 15) e acima disso denuncia em vez de mexer.
//
// Existe um terceiro caso que nao e nem um nem outro: o painel do
// operador chegou a mostrar "comandado 1986,79 graus, medido -230,05,
// erro +2216,85". Isso nao e perda de passo -- nenhum braco perde dois
// mil graus. E a contagem tendo perdido o sentido, porque o motor nao
// seguiu os pulsos (engrenagem eletronica errada, driver em falha, eixo
// preso). Continuar confiando nela e pior do que jogar fora: todo limite
// de curso, todo destino e todo erro passam a ser calculados sobre um
// numero que nao existe.
//
// 45 graus e muito acima de qualquer erro de seguimento legitimo e muito
// abaixo do disparate. Passando disso com o eixo PARADO e a leitura boa,
// a contagem e reescrita pelo encoder -- e a maquina avisa.
static const float DIVERGENCIA_MAXIMA_GRAUS = 45.0f;

static const uint16_t ENC_PERIODO_MIN_MS = 20;      // teto de 50 leituras/s
static const uint16_t ENC_PERIODO_PADRAO = 50;
// Tempo maximo esperando a resposta do driver. 100 ms e o que o monitor
// do operador usa, e com ele o HL-T3DL20A responde. Nao e chute nem
// folga inventada: e o numero que ja funcionou na maquina dele.
//
// Na pratica a espera acaba muito antes: a leitura sabe quantos bytes a
// resposta boa tem (3 + 2*N + 2) e para assim que o quadro fecha.
static const uint32_t ENC_TIMEOUT_MS     = 100;     // resposta do driver
// Sem leitura por este tempo, o valor deixa de ser confiavel e a
// interface para de mostrar erro calculado em cima de dado velho.
static const uint32_t ENC_IDADE_MAX_MS   = 1000;
// Zona morta do SENTIDO, em contagens. Um encoder de 17 bits treme um ou
// dois passos com o eixo parado; sem zona morta esse tremor viraria
// "inverteu de sentido" dezenas de vezes por segundo, e o contador de
// inversoes -- que serve para achar folga de verdade -- nao valeria nada.
static const int32_t  ENC_PARADO_CONTAGENS = 3;

// Vigia de travamento, quando NAO ha escala do encoder medida.
//
// O criterio proporcional -- "o medido esta abaixo de um quinto do
// esperado" -- precisa saber quanto o eixo DEVERIA andar, e esse numero
// so existe com escala medida. Sem ela sobra um criterio que nao depende
// de escala nenhuma: o gerador de pulso claramente correndo e o encoder
// claramente PARADO. Eixo que anda produz contagem, qualquer que seja a
// escala; entao este criterio nao da falso positivo por numero errado.
// SALTO IMPOSSIVEL ENTRE DUAS LEITURAS.
//
// Uma leitura pode ser numericamente possivel e ainda assim nao ser o
// eixo: quadro corrompido que passou no CRC, palavra baixa de um
// instante casada com a alta de outro, contador dando a volta. O sinal
// disso e sempre o mesmo -- a posicao PULA, tipicamente meia volta ou
// uma volta inteira do motor, de uma amostra para a outra.
//
// Nenhum eixo desta maquina gira tres voltas de motor por segundo. Acima
// disso nao e movimento: e defeito de leitura, e obedecer a ele teleporta
// a posicao oficial da maquina -- foi o "braco pulando angulo".
//
// O piso existe para a amostra que chega logo depois da anterior: com dt
// de poucos milissegundos, a conta proporcional daria uma tolerancia
// menor que o proprio tremor do encoder.
// O teto fixo virou PISO: ele cobre o braco empurrado a mao, que nao
// aparece em frequencia de pulso nenhuma. Quando ha pulso saindo, o limite
// e a propria velocidade comandada com esta margem -- num eixo rapido ela
// e muito maior que tres voltas, e num eixo lento, muito menor.
static const float ENC_SALTO_VOLTAS_POR_S = 3.0f;
static const float ENC_SALTO_VOLTAS_MIN   = 0.10f;
static const float ENC_SALTO_MARGEM       = 3.0f;

static const float TRAV_HZ_MINIMO       = 200.0f;  // pulso claramente correndo
static const float TRAV_CONTAGENS_QUIETO = 20.0f;  // contagens/s: ruido, nao movimento

// AMOSTRAS BOAS que o vigia precisa ver antes de parar o braco.
//
// A janela de meio segundo so vale se ela estiver CHEIA DE MEDIDA. Leitura
// que falhou tem a velocidade zerada de proposito (a tela nao pode dizer
// que o eixo gira depois que o fio caiu), e um zero desses nao e o eixo
// parado: e o encoder calado. Num barramento ruim uma rajada de falhas
// enchia a janela sozinha e o braco parava com "Junta travada" no meio do
// cordao. Exigindo amostras que chegaram, silencio nunca mais acusa.
static const uint8_t TRAV_AMOSTRAS_MINIMAS = 3;

// AFERICAO DA ENGRENAGEM ELETRONICA pelo proprio movimento.
//
// Medida curta mede ruido, nao engrenagem -- mas o que conta como "curta"
// aqui e menos do que parece, e vale entender por que.
//
// O que se mede e a engrenagem ELETRONICA do drive: pulso entra, eixo do
// motor gira. O encoder esta nesse mesmo eixo, antes do redutor. Entre o
// pulso e a contagem nao ha mecanica nenhuma -- nem folga, nem correia,
// nem redutor --, entao nao ha o erro que obrigaria uma medida longa. Sobra
// a quantizacao, e um decimo de volta ja sao treze mil contagens num
// encoder de 17 bits: o erro de uma contagem fica quatro ordens de
// grandeza abaixo do que se mede.
//
// Meia volta era o minimo herdado da viagem ao zero da calibracao, onde o
// motor dava voltas inteiras de qualquer jeito. Como minimo geral ele
// EXCLUIA a maquina de acionamento direto: com reducao 1, meia volta do
// motor e meia volta da junta, e uma maquina dessas nunca aferiria a regua
// num movimento de trabalho.
static const float    AFERIR_VOLTAS_MIN = 0.10f;
static const long     AFERIR_PASSOS_MIN = 200;
// Abaixo disto a diferenca nao paga uma escrita na flash. A regua converge
// em dois ou tres movimentos e para de mudar.
static const float    AFERIR_GRAVAR_REL = 0.005f;

#ifndef PIN_SD_CS
#define PIN_SD_CS    5
#endif
#ifndef PIN_SD_SCK
#define PIN_SD_SCK   14
#endif
#ifndef PIN_SD_MOSI
#define PIN_SD_MOSI  13
#endif
#ifndef PIN_SD_MISO
#define PIN_SD_MISO  25
#endif

// Deixe false para compilar sem nenhum codigo de cartao (a maquina
// continua funcionando exatamente como antes, so em NVS).
#ifndef CARTAO_INSTALADO
#define CARTAO_INSTALADO  true
#endif


// 20 MHz e conservador e funciona com cabo de protoboard. Suba para
// 40000000 so com fiacao curta e soldada.
static const uint32_t SD_FREQ_HZ = 20000000;

// LED de status. Deixe 255 para desligar (necessario quando o rele esta
// usando o GPIO 2, senao os dois brigam pelo mesmo pino).
#define PIN_LED_STATUS    255

// Mude para true depois de instalar o botao fisico de emergencia.
// (o #ifndef permite o banco de testes compilar com o botao "instalado"
// para exercitar esse ramo sem mexer no valor de producao)
#ifndef ESTOP_FISICO_INSTALADO
#define ESTOP_FISICO_INSTALADO  false
#endif

// ---------------------------------------------------------------------
// LIMITES ELETRICOS / MECANICOS
// ---------------------------------------------------------------------
// Frequencia maxima de pulso aceita pelo driver em modo coletor aberto.
// Mantenha folga: nunca comande acima disso.
static const uint32_t FREQ_PULSO_MAX_HZ = 180000;

static const uint32_t PASSOS_POR_VOLTA_PADRAO = 10000;  // engrenagem eletronica do T3D
static const float    REDUCAO_PADRAO          = 1.0f;   // reducao mecanica da junta

// VELOCIDADES EM GRAUS POR SEGUNDO, nao em Hz.
//
// Hz significa coisas diferentes em cada junta. Com reducao 16,5 na
// junta 1 e 4 na junta 2, os mesmos 3000 Hz davam 6,5 graus/s numa e 27
// na outra -- a junta 2 andava quatro vezes mais rapido que a junta 1, e
// nao havia ajuste que igualasse as duas sem recalcular a mao.
//
// Em graus por segundo o comportamento da maquina para de depender da
// engrenagem de cada eixo. Cada junta converte para Hz com o seu proprio
// passosPorGrau, e FREQ_PULSO_MAX_HZ continua sendo o teto do driver.
static const float VEL_NORMAL_PADRAO   = 20.0f;   // graus/s
static const float VEL_AUTO_PADRAO     = 12.0f;   // graus/s

// FAIXA DA BARRA DE VELOCIDADE, em graus/s.
//
// A barra da aba Mover ia de 1 a 120 graus/s cravados no codigo da
// pagina. Maquina nenhuma usa a faixa inteira: uma com redutor grande
// nunca passa de vinte, outra com redutor curto so comeca a ser util
// acima de cinquenta. Deixando a faixa configuravel, a barra inteira
// passa a ser util na maquina de quem a esta usando -- e o teto vira
// tambem um limite de seguranca, guardado na maquina em vez de no
// navegador.
static const float VEL_MIN_PADRAO      =  2.0f;   // graus/s
static const float VEL_MAX_PADRAO      = 60.0f;   // graus/s
// Velocidade do cordao em mm/s: e assim que se especifica solda, nao em
// pulsos. O firmware converte para pulsos resolvendo a cinematica.
static const float    VEL_CORDAO_PADRAO   = 5.0f;
static const float    PASSO_INTERP_MM     = 1.5f;  // resolucao da reta

// Perto do braco esticado (|r| -> L1+L2) e perto do braco totalmente
// dobrado (|r| -> |L1-L2|) a cinematica inversa e mal condicionada:
// milimetros de chapa viram dezenas de graus de junta. Nenhum motor
// acompanha isso, e a ponta corta caminho -- e justamente ali que o
// cordao deixa de ser reto. Um passo de PASSO_INTERP_MM que exija mais
// do que isto de qualquer junta faz o cordao ser recusado ANTES de o
// arco abrir, em vez de sair torto.
static const float    SALTO_MAX_GRAUS     = 4.0f;
// Rampa tambem em graus por segundo ao quadrado, pelo mesmo motivo:
// 8000 passos/s2 eram 17 graus/s2 numa junta e 72 na outra.
static const float ACEL_PADRAO         = 60.0f;   // graus/s2

// SUAVIDADE DA PARTIDA (limite de jerk).
//
// Uma rampa trapezoidal muda a aceleracao de zero para o valor cheio de
// um ciclo para o outro. Isso e um degrau de torque, e e o "tranco" que
// se sente no comeco do movimento. O FastAccelStepper sabe subir a
// aceleracao gradualmente ao longo dos primeiros passos: e o que tira o
// solavanco sem deixar o movimento lento.
//
// 0 desliga (rampa reta). O maximo util fica em torno de 200.
// Se a sua versao da biblioteca nao tiver setLinearAcceleration, ponha
// RAMPA_SUAVE_DISPONIVEL como false.
#ifndef RAMPA_SUAVE_DISPONIVEL
#define RAMPA_SUAVE_DISPONIVEL true
#endif
static const uint8_t SUAVIDADE_PADRAO = 120;

// ---------------------------------------------------------------------
// GEOMETRIA E ANTI-COLISAO
// ---------------------------------------------------------------------
static const float ELO1_PADRAO_MM = 200.0f;
static const float ELO2_PADRAO_MM = 200.0f;

// Folga angular antes da dobra total do cotovelo.
// theta2 = 0 -> braco esticado (sem colisao).
// theta2 = +/-180 -> elo 2 dobrado sobre o elo 1 (colisao).
// Postura invalida quando |theta2| > 180 - FOLGA_DOBRA.
static const float FOLGA_DOBRA_PADRAO = 20.0f;

// Envelope cartesiano permitido (mm, origem no eixo da junta 1).
// Y_MIN protege a mesa / bancada.
static const float ENV_Y_MIN_PADRAO   = -150.0f;
static const float ENV_RAIO_MIN_PADRAO = 40.0f;   // zona morta em volta da base

// Margem de seguranca aplicada aos limites de curso calibrados.
// gravidadeViolacao() usa EXATAMENTE esta mesma margem: se as duas contas
// divergirem, existe uma faixa onde a postura e invalida e a gravidade e
// zero, e o jog de recuperacao nunca libera o movimento de volta.
static const float MARGEM_LIMITE_GRAUS = 0.5f;

// Curso minimo que a calibracao aceita por junta. Precisa ser bem maior
// que 2 x MARGEM_LIMITE_GRAUS, senao a calibracao "valida" produz um
// intervalo util negativo e tranca o eixo.
// Abaixo disto o movimento e curto demais para ter sentido definido, e o
// freio do encoder nao arma: quem fecha e o assentamento. Serve tambem
// para nao frear um movimento que ja nasceu no alvo.
static const float FREIO_ENC_MINIMO_GRAUS = 0.5f;

// O encosto final da afinacao. Abaixo disto o eixo praticamente nao anda
// entre duas leituras do barramento, e a chegada ficaria eterna.
static const float FREIO_ENC_VEL_MINIMA = 1.5f;   // graus/s

// ATE QUE RITMO DE LEITURA VALE FECHAR A MALHA NO ENCODER.
//
// Corrigir o eixo a partir de uma medida so funciona se a proxima medida
// chegar a tempo de mostrar o resultado da correcao. Abaixo desse ritmo
// a maquina age, o eixo anda, e so muito depois ela ve onde parou -- e
// age de novo em cima de um numero velho. Isso nao converge: fica
// caçando. Da bancada, com 4,6 leituras por segundo: "micro variacao e
// nunca fica no ponto setado, ele fica tentando acertar".
//
// Acima do limite o encoder guia o movimento (afina ao chegar, freia no
// alvo, assenta no fim). Abaixo dele o encoder continua VALENDO para
// tudo o mais -- dizer onde o braco esta, ancorar a partida, calibrar,
// avisar de travamento --, mas quem leva o eixo e a rampa do gerador de
// pulso, que desacelera e para exatamente onde foi mandada. Que e como a
// maquina se comportava antes de existir correcao nenhuma.
//
// 100 ms = 10 leituras por segundo. Com passos de retoque de ate 3 graus
// a uns 10 graus/s, cada retoque dura ~300 ms: com 10 Hz ainda chegam
// tres medidas por retoque, o bastante para enxergar o resultado.
static const uint32_t CORR_INTERVALO_MAX_MS = 100;

// Acima disto o intervalo nao e ritmo, e LACUNA: cabo que caiu e voltou,
// reconfiguracao, a maquina parada. Nao entra na media -- so recomeca a
// contagem. Sem isso qualquer pausa deixava o encoder marcado como lento
// por segundos depois.
static const uint32_t CORR_LACUNA_MS = 2000;

// MARGEM DA DESACELERACAO.
//
// A conta raiz(2.a.falta) usa a aceleracao em graus DA REGUA. Se a regua
// estiver errada por um fator, a aceleracao real e menor na mesma
// proporcao -- o eixo freia menos do que a conta promete e passa do
// ponto. Planejar a frenagem com uma fracao da aceleracao e a folga que
// cobre isso: com 0,25, uma regua ate quatro vezes errada ainda para no
// angulo.
static const float FREIO_ENC_MARGEM = 0.25f;

static const float CURSO_MINIMO_GRAUS = 5.0f;

// Depois de gravar os limites a maquina religa o torque e volta ao zero.
// Entre uma coisa e outra ela ESPERA: o driver confirma o habilita pelo
// barramento e o rotor leva um instante para segurar de verdade. Mandar
// pulso nesse intervalo e mandar pulso para um eixo que ainda esta solto
// -- a contagem anda e o braco nao. Era um numero solto no meio do
// switch; agora tem nome e mora aqui.
static const uint32_t CAL_ESPERA_RELIGAR_MS = 400;

// Resolucao da validacao de um caminho interpolado nas juntas.
// Os limites de curso sao caixas no espaco das juntas (a reta entre dois
// pontos validos fica valida), mas o envelope cartesiano NAO e convexo
// nesse espaco: o interior do caminho precisa ser verificado.
static const float PASSO_VALIDACAO_GRAUS = 2.0f;

// TODAS AS PROTECOES NASCEM DESLIGADAS.
//
// O braco se move livre pela mesa; limite e coisa que o operador LIGA,
// depois de conferir que os numeros descrevem a maquina dele.
//
// A regra vale para as tres pelo mesmo motivo: cada uma depende de um
// numero que pode estar errado -- o curso depende da calibracao, a dobra
// e o envelope dependem do comprimento dos elos. Protecao ligada sobre
// numero errado nao protege nada: ela recusa movimento legitimo e o
// operador fica olhando um braco que nao anda, sem saber por que. Pior
// ainda numa maquina em montagem, que e onde os numeros ainda nao
// existem.
//
// Ligadas, elas sao boas e valem: quem liga sabe o que esta protegendo.
static const bool PROT_CURSO_PADRAO    = false;
static const bool PROT_DOBRA_PADRAO    = false;
static const bool PROT_ENVELOPE_PADRAO = false;

// ---------------------------------------------------------------------
// TEMPOS
// ---------------------------------------------------------------------
static const uint32_t TIMEOUT_JOG_MS      = 350;    // sem heartbeat -> para o jog
static const uint32_t TIMEOUT_CONEXAO_MS  = 2500;   // sem contato HTTP -> parada segura
static const uint32_t TIMEOUT_ARCO_MS     = 60000;  // arco continuo maximo
static const uint32_t PERIODO_AMOSTRA_MS  = 25;     // taxa de gravacao (40 Hz)
static const uint16_t MAX_WAYPOINTS       = 1500;   // ~37 s de trajetoria continua
// Pontos do programa de solda.
//
// Eram 40, que basta para cordao ensinado a mao mas nao para um contorno
// importado de DXF: um retangulo com cantos arredondados ja passa disso.
// Cada Ponto ocupa 12 bytes com alinhamento, e ha duas listas (o programa
// vivo e a area de troca do cartao): 120 pontos custam 2,9 kB de RAM num
// ESP32 que fecha o boot com mais de 200 kB livres.
static const uint8_t  MAX_PONTOS          = 120;
static const uint32_t DWELL_ABRE_ARCO_MS  = 400;    // espera o arco estabilizar
static const uint32_t DWELL_FECHA_ARCO_MS = 250;    // fecha a cratera no fim

// ---------------------------------------------------------------------
// ARQUIVOS
// ---------------------------------------------------------------------
static const uint8_t  MAX_NOME_ARQ    = 24;   // sem extensao, sem caminho
static const uint8_t  MAX_ARQ_LISTA   = 40;   // arquivos mostrados por pasta
static const uint8_t  MAX_LOG_SESSOES = 40;   // arquivos de log antes de girar

// Zona morta do joystick: abaixo disso o eixo fica parado. Sem ela o
// dedo tremendo no centro do disco manda pulso o tempo todo.
static const float JOY_ZONA_MORTA = 0.12f;
// Fracao minima de velocidade quando o joystick sai da zona morta: o
// movimento comeca perceptivel em vez de "quase parado".
static const float JOY_FRACAO_MIN = 0.05f;

// ---------------------------------------------------------------------
// WIFI
// ---------------------------------------------------------------------
// Wi-Fi proprio da maquina: o unico que existe. O painel nao depende de
// roteador, de senha de terceiro nem de internet.
static const char* const WIFI_AP_SSID  = "Robo2dof";
static const char* const WIFI_AP_SENHA = "12345678";

// IP fixo do ponto de acesso. Declarado aqui em vez de herdado do padrao
// da biblioteca: o endereco do painel nao pode mudar entre versoes do
// core do ESP32.
static const uint8_t WIFI_AP_IP[4] = {192, 168, 4, 1};

// Nome na rede: http://robo2dof.local abre o painel sem decorar IP.
// So letras minusculas, numeros e hifen.
static const char* const WIFI_NOME_LOCAL = "robo2dof";

// A maquina NAO entra na rede de ninguem. Houve aqui um modo estacao;
// ele saiu porque o ESP32 tem um radio so: em AP+STA o ponto de acesso
// acompanha o canal do roteador e o radio divide tempo entre as duas
// redes, o que aparece como atraso no heartbeat do jog.

// ---------------------------------------------------------------------
// TIPOS
// ---------------------------------------------------------------------
enum Modo : uint8_t {
  MODO_MANUAL,
  MODO_GRAVANDO,
  MODO_REPRODUZINDO,
  MODO_EXECUTANDO,
  MODO_POSICIONANDO,
  MODO_CALIBRANDO,
  MODO_FALHA
};

// CALIBRAR SAO DOIS GESTOS, E A MAQUINA FAZ O RESTO.
//
//   1. O operador toca em Calibrar. A maquina leva os dois eixos ao ZERO
//      e SOLTA os motores.
//   2. Ele empurra o braco com a mao ate o extremo de UM lado -- os dois
//      eixos de uma vez, que e o que da para fazer com o braco solto --
//      e toca de novo.
//   3. A maquina liga os motores, volta os dois ao zero e solta outra vez.
//   4. Ele empurra para o outro extremo e toca. Acabou.
//
// Dali sai: o CURSO de cada junta (as duas marcas), a ESCALA DO ENCODER
// em contagens por grau, e os PULSOS POR VOLTA de cada driver -- este
// ultimo medido de graca durante as voltas ao zero, que sao movimentos
// com pulso contado e encoder olhando.
//
// O ZERO nao se move. Ele e o ponto ao qual o operador voltou duas vezes
// e viu o braco parar; deslocar isso no fim seria trocar debaixo dele o
// unico ponto que ele conhece da maquina.
enum EstadoCalib : uint8_t {
  CAL_INATIVO,
  // AS VIAGENS AO ZERO SAIRAM. Eram tres, e a primeira delas LIGAVA o
  // torque assim que o operador tocava em "Calibrar" -- o contrario do
  // que a tela promete e do que a mao dele espera. Elas tambem eram a
  // ocasiao em que a maquina remedia a propria escala e reescrevia os
  // angulos por baixo dos limites recem-marcados.
  CAL_SOLTANDO,   // pedido de SOLTAR o torque, esperando o barramento
  CAL_LADO_A,     // motores soltos: os dois eixos ao extremo NEGATIVO
  CAL_LADO_B,     // motores soltos: agora o extremo POSITIVO
  CAL_RELIGANDO   // limites gravados: torque de volta, espera, e ao zero
};

// Copia da configuracao no cartao: nome fixo e intervalo minimo entre
// gravacoes. Ver o bloco em estado.h.
// Nome RESERVADO: nunca colide com um backup que o operador tenha
// gravado a mao. A copia automatica e um espelho do estado atual, nao um
// ponto de restauracao -- calibracao refeita errado e espelhada errada.
#define CFG_CARTAO_NOME          "maquina-atual"
#define CFG_CARTAO_INTERVALO_MS  15000UL

enum TipoComando : uint8_t {
  CMD_JOG,              // a = junta (1|2), b = direcao (-1|0|1)
  CMD_PARAR,
  CMD_SERVOS,           // a = 0|1 (desliga/liga), b = junta (1, 2, ou 0 = as duas)
  CMD_GRAVAR_INICIAR,
  CMD_GRAVAR_PARAR,
  CMD_REPRODUZIR,
  CMD_TRAJ_LIMPAR,
  CMD_SOLDA,            // a = 0|1
  CMD_TESTE_RELE,       // pulso curto de teste de bancada
  CMD_PONTO_GRAVAR,
  CMD_PONTO_REMOVER,    // a = indice
  CMD_PONTO_SOLDA,      // a = indice, b = 0|1
  CMD_PROG_LIMPAR,
  CMD_PROG_EXECUTAR,    // a = 1 para ensaio sem arco
  CMD_PROG_PARAR,
  CMD_IR_PARA_PONTO,    // a = indice
  CMD_APLICAR_CONFIG,   // salva em NVS e reaplica apos mudanca via web
  CMD_RESTAURAR_PADROES,
  CMD_MOVER_ANGULOS,    // f1 = theta1, f2 = theta2
  CMD_IR_HOME,
  CMD_CALIB_INICIAR,
  CMD_CALIB_CONFIRMAR,
  CMD_CALIB_CANCELAR,
  CMD_CALIB_APAGAR,     // esquece a calibracao gravada e volta ao modo de instalacao
  CMD_REFERENCIAR,      // o braco esta na posicao de referencia: sincroniza a contagem
  // As quatro afericoes avulsas sairam: engrenagem eletronica pelo
  // encoder, reducao contra um esquadro, escala do encoder e resolucao
  // por transferidor. Todas existiam porque a calibracao media so os
  // limites; agora ela mede a escala sozinha, das proprias marcas.
  // Ensina a referencia absoluta: "esta junta esta AGORA em f1 graus".
  // Com encoder absoluto e a unica calibracao que sobra.
  CMD_ENSINAR_ZERO,     // a = junta, f1 = graus
  CMD_ESQUECER_ZERO,    // a = junta (0 = as duas)
  CMD_INVERTER_EIXO,    // a = junta, b = 0/1: para que lado o eixo gira
  CMD_APLICAR_ENCODER,  // grava encoderPendente e reconfigura o Modbus
  CMD_ENCODER_ZERAR,    // a = junta (0 = as duas): marca a contagem atual

  // Modo aprendizado: o braco solto e o botao da ponteira gravando
  // pontos. a = 0 sai, 1 entra, -1 alterna. Ver aprender.h.
  CMD_APRENDER,

  // Producao e edicao do programa.
  CMD_PROG_PAUSAR,      // a = 0 retoma, 1 pausa, -1 alterna
  CMD_PROG_DESFAZER,
  CMD_PROG_REPETIR,     // roda de novo o ultimo programa executado
  CMD_MANUTENCAO_OK,    // zera o contador de ciclos desde a manutencao

  // Area util ensinada.
  CMD_MESA_CANTO,       // ensina um canto na posicao atual da ponta
  CMD_MESA_LIMPAR,

  // Joystick: f1 e f2 sao a fracao de velocidade de cada junta, de -1 a
  // +1. Um comando so para os dois eixos - metade das requisicoes HTTP
  // do heartbeat comparado a mandar CMD_JOG por eixo.
  CMD_JOG_XY,

  // Arquivos. O 'nome' do Comando carrega o nome do arquivo.
  CMD_ARQ_SALVAR_PROG,     // core 1: copia pontos -> staging e pede a gravacao
  CMD_ARQ_APLICAR_PROG,    // core 1: staging -> pontos (postado pela tarefa SD)
  CMD_ARQ_SALVAR_TRAJ,     // core 1: empresta o buffer e pede a gravacao
  CMD_ARQ_CARREGAR_TRAJ,   // core 1: empresta o buffer e pede a leitura
  CMD_ARQ_LIBERAR_TRAJ,    // core 1: devolve o buffer (postado pela tarefa SD)
  CMD_ARQ_SALVAR_CONFIG,   // core 1: prepara a area e pede a gravacao

  // APAGAR TUDO e reiniciar. Nao e "restaurar padroes": este limpa o NVS
  // inteiro -- calibracao, mesa, zero ensinado, encoder, contadores -- e
  // reinicia. Ver apagarTudo() em estado.h.
  CMD_APAGAR_TUDO
};

struct Comando {
  TipoComando tipo;
  int32_t a;
  int32_t b;
  float   f1;
  float   f2;
  // Nome de arquivo, para os comandos de armazenamento. Vazio nos demais.
  char    nome[MAX_NOME_ARQ + 1];
};

struct Waypoint {
  uint32_t tMs;    // tempo relativo ao inicio da gravacao
  int32_t  p1;     // passos da junta 1
  int32_t  p2;     // passos da junta 2
  uint8_t  solda;  // estado do rele neste ponto
};
