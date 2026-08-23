# Regra dos mocks

Um mock que aceita **mais** que a biblioteca de verdade não é um mock: é
uma armadilha. Ele deixa o banco passar limpo e joga o erro na IDE do
operador, que é o pior lugar possível para descobrir.

Isso já aconteceu duas vezes, as duas por conveniência na hora de
escrever o mock:

| Mock devolvia | O core do ESP32 devolve | Como apareceu |
|---|---|---|
| `WiFi.SSID(i)` → `const char*` | `String` | `cannot convert 'String' to 'const char*'` |
| `WiFi.localIP()` → `const char*` | `IPAddress` | `invalid user-defined conversion` |
| `server.arg()` sem decodificar | decodifica `%20` e `+` | o banco lia `Oficina%202G` |

## A regra

**A assinatura do mock é a assinatura do core, não a conveniente.** Tipo
de retorno, tipo dos parâmetros e efeitos observáveis. Se o core devolve
um objeto que não converte implicitamente para o que você quer, o mock
também não pode converter — é justamente essa recusa que o banco precisa
reproduzir.

Quando a assinatura não for óbvia, deixe escrito ao lado:

```cpp
String  SSID(int i) { ... }   // core: String WiFiScanClass::SSID(uint8_t)
bool    setHostname(const char*) { ... }   // core: bool
int16_t scanComplete() { ... }             // core: int16_t
```

## O que os mocks podem simplificar

Comportamento, não interface. É legítimo o cartão SD responder na hora em
vez de em milissegundos reais, e a rede conectar em 500 ms encenados. O
que não é legítimo é mudar o **tipo** de `SD.open()` ou o formato de um
argumento HTTP.

## Ao adicionar uma chamada nova de biblioteca

1. Abra o cabeçalho do core e copie a assinatura exata.
2. Se ela devolver `String` ou `IPAddress`, o mock devolve o mesmo.
3. Deixe o comentário `// core: ...` quando houver qualquer dúvida.
