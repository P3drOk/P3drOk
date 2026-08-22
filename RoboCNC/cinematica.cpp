#include "cinematica.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------
// Ponto unico de conversao entre o que o motor conta e o que a maquina
// e. Todo o resto do firmware -- validacao, cinematica, arquivos, tela --
// passa por aqui, entao basta o offset viver neste par de funcoes para o
// sistema inteiro falar o mesmo idioma que o braco de verdade.
float passosParaGraus(const Junta& j, long passos) {
  if (j.passosPorGrau <= 0.0f) return j.grausHome;
  return (float)passos / j.passosPorGrau + j.grausHome;
}

long grausParaPassos(const Junta& j, float graus) {
  if (j.passosPorGrau <= 0.0f) return 0;
  return lroundf((graus - j.grausHome) * j.passosPorGrau);
}

// ---------------------------------------------------------------------
void cinematicaDireta(float t1, float t2,
                      float& xCotovelo, float& yCotovelo,
                      float& xPonta,    float& yPonta) {
  const float r1 = t1 * (float)M_PI / 180.0f;
  const float r2 = (t1 + t2) * (float)M_PI / 180.0f;
  xCotovelo = elo1Mm * cosf(r1);
  yCotovelo = elo1Mm * sinf(r1);
  xPonta    = xCotovelo + elo2Mm * cosf(r2);
  yPonta    = yCotovelo + elo2Mm * sinf(r2);
}

// ---------------------------------------------------------------------
bool cinematicaInversa(float x, float y, bool cotoveloCima,
                       float& t1, float& t2) {
  const float L1 = elo1Mm;
  const float L2 = elo2Mm;
  if (L1 <= 0.0f || L2 <= 0.0f) return false;

  const float r2 = x * x + y * y;
  float c2 = (r2 - L1 * L1 - L2 * L2) / (2.0f * L1 * L2);

  // Tolerancia para pontos exatamente na borda do alcance
  if (c2 >  1.0f) { if (c2 >  1.0005f) return false; c2 =  1.0f; }
  if (c2 < -1.0f) { if (c2 < -1.0005f) return false; c2 = -1.0f; }

  float t2r = acosf(c2);
  if (!cotoveloCima) t2r = -t2r;

  const float t1r = atan2f(y, x) - atan2f(L2 * sinf(t2r), L1 + L2 * cosf(t2r));

  t1 = t1r * 180.0f / (float)M_PI;
  t2 = t2r * 180.0f / (float)M_PI;
  return true;
}

// ---------------------------------------------------------------------
// Menor distancia entre a origem (eixo da base) e o segmento AB.
// Usado para impedir que o antebraco varra por cima da coluna da base.
// ---------------------------------------------------------------------
static float distanciaOrigemAoSegmento(float ax, float ay, float bx, float by) {
  const float vx = bx - ax;
  const float vy = by - ay;
  const float comp2 = vx * vx + vy * vy;
  if (comp2 < 1e-6f) return sqrtf(ax * ax + ay * ay);

  float t = -(ax * vx + ay * vy) / comp2;
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;

  const float px = ax + t * vx;
  const float py = ay + t * vy;
  return sqrtf(px * px + py * py);
}

// ---------------------------------------------------------------------
void violacaoLimpar(Violacao& v) {
  v.causa = nullptr; v.junta = 0; v.valor = 0; v.limite = 0; v.fracao = -1.0f;
}

void violacaoTexto(const Violacao& v, char* destino, size_t tam) {
  if (!destino || tam == 0) return;
  if (!v.causa) { snprintf(destino, tam, "postura invalida"); return; }

  char onde[40] = "";
  if (v.fracao >= 0.0f) {
    snprintf(onde, sizeof(onde), " a %.0f%% do trecho", v.fracao * 100.0f);
  }

  if (!strcmp(v.causa, "curso")) {
    snprintf(destino, tam,
             "junta %u precisa ir a %.1f graus%s, e o curso vai ate %.1f",
             (unsigned)v.junta, v.valor, onde, v.limite);
  } else if (!strcmp(v.causa, "dobra")) {
    snprintf(destino, tam,
             "cotovelo dobraria %.1f graus%s: o elo 2 bate no elo 1 acima de %.1f",
             v.valor, onde, v.limite);
  } else if (!strcmp(v.causa, "mesa")) {
    snprintf(destino, tam,
             "o braco desceria a Y=%.0f mm%s, abaixo do Y minimo de %.0f",
             v.valor, onde, v.limite);
  } else if (!strcmp(v.causa, "base")) {
    snprintf(destino, tam,
             "o elo 2 passaria a %.0f mm da base%s, dentro do raio morto de %.0f",
             v.valor, onde, v.limite);
  } else if (!strcmp(v.causa, "alcance")) {
    snprintf(destino, tam, "ponto fora do alcance do braco%s", onde);
  } else {
    snprintf(destino, tam, "%s%s", v.causa, onde);
  }
}

// ---------------------------------------------------------------------
bool posturaValidaDet(float t1, float t2, Violacao& v) {
  violacaoLimpar(v);

  // MODO DE INSTALACAO.
  //
  // Sem calibracao valida nao existe geometria confiavel para proteger
  // nada: "graus" ainda e pulsos divididos por um numero digitado, mais
  // um offset que pode ser o da calibracao antiga. Aplicar a protecao de
  // dobra ou a de envelope sobre esse angulo trava o braco justamente
  // durante o assistente que existe para estabelecer a referencia -- e
  // era exatamente isso que acontecia: com a resolucao errada, um
  // movimento pequeno lia |theta2| > 160 e o jog era recusado.
  //
  // Sem referencia, quem protege sao os batentes da maquina e o
  // operador. Os modos automaticos continuam exigindo calibracao, entao
  // nada roda sozinho neste estado.
  const bool calibrado = J1.calibrada && J2.calibrada;
  if (!calibrado) return true;

  // 1) Curso de cada junta - vem da calibracao do proprio operador
  if (protCurso) {
    const float min1 = J1.grausMin + MARGEM_LIMITE_GRAUS;
    const float max1 = J1.grausMax - MARGEM_LIMITE_GRAUS;
    const float min2 = J2.grausMin + MARGEM_LIMITE_GRAUS;
    const float max2 = J2.grausMax - MARGEM_LIMITE_GRAUS;
    if (t1 < min1) { v.causa="curso"; v.junta=1; v.valor=t1; v.limite=min1; return false; }
    if (t1 > max1) { v.causa="curso"; v.junta=1; v.valor=t1; v.limite=max1; return false; }
    if (t2 < min2) { v.causa="curso"; v.junta=2; v.valor=t2; v.limite=min2; return false; }
    if (t2 > max2) { v.causa="curso"; v.junta=2; v.valor=t2; v.limite=max2; return false; }
  }

  // 2) Auto-colisao dos elos: theta2 = 0 e braco esticado (seguro),
  //    theta2 = +/-180 e o elo 2 dobrado sobre o elo 1 (colisao).
  if (protDobra && fabsf(t2) > 180.0f - folgaDobra) {
    v.causa="dobra"; v.junta=2; v.valor=fabsf(t2); v.limite=180.0f - folgaDobra;
    return false;
  }

  // 3) Envelope cartesiano - depende do comprimento dos elos estar certo
  if (protEnvelope) {
    float xc, yc, xp, yp;
    cinematicaDireta(t1, t2, xc, yc, xp, yp);
    const float menorY = (yp < yc) ? yp : yc;
    if (menorY < envYMin) {
      v.causa="mesa"; v.valor=menorY; v.limite=envYMin; return false;
    }
    const float d = distanciaOrigemAoSegmento(xc, yc, xp, yp);
    if (d < envRaioMin) {
      v.causa="base"; v.valor=d; v.limite=envRaioMin; return false;
    }
  }
  return true;
}

// Casca antiga: mesma checagem, texto curto. Continua valendo para todo
// caminho de codigo que so quer saber se pode mover.
bool posturaValida(float t1, float t2, const char** motivo) {
  Violacao v;
  if (posturaValidaDet(t1, t2, v)) return true;
  if (motivo) {
    static char curto[96];
    violacaoTexto(v, curto, sizeof(curto));
    *motivo = curto;
  }
  return false;
}

// ---------------------------------------------------------------------
// Precisa usar EXATAMENTE os mesmos limites de posturaValida(), margem
// incluida. Quando as duas contas divergiam, existia uma faixa de
// MARGEM_LIMITE_GRAUS onde a postura era invalida e a gravidade era zero:
// posturaValida() bloqueava e o criterio de recuperacao (gAtual > 0)
// nunca liberava, entao o braco entrava na faixa e nao saia mais.
float gravidadeViolacao(float t1, float t2) {
  float g = 0.0f;
  // Mesmo criterio de posturaValidaDet: sem calibracao nada e violacao.
  if (!(J1.calibrada && J2.calibrada)) return 0.0f;

  if (protCurso) {
    const float min1 = J1.grausMin + MARGEM_LIMITE_GRAUS;
    const float max1 = J1.grausMax - MARGEM_LIMITE_GRAUS;
    const float min2 = J2.grausMin + MARGEM_LIMITE_GRAUS;
    const float max2 = J2.grausMax - MARGEM_LIMITE_GRAUS;
    if (t1 < min1) g += min1 - t1;
    if (t1 > max1) g += t1 - max1;
    if (t2 < min2) g += min2 - t2;
    if (t2 > max2) g += t2 - max2;
  }
  if (protDobra) {
    const float excesso = fabsf(t2) - (180.0f - folgaDobra);
    if (excesso > 0.0f) g += excesso;
  }
  if (protEnvelope) {
    float xc, yc, xp, yp;
    cinematicaDireta(t1, t2, xc, yc, xp, yp);
    if (yp < envYMin) g += (envYMin - yp) * 0.1f;
    if (yc < envYMin) g += (envYMin - yc) * 0.1f;
    const float d = distanciaOrigemAoSegmento(xc, yc, xp, yp);
    if (d < envRaioMin) g += (envRaioMin - d) * 0.1f;
  }
  return g;
}

float gravidadeViolacaoPassos(long p1, long p2) {
  return gravidadeViolacao(passosParaGraus(J1, p1), passosParaGraus(J2, p2));
}

// ---------------------------------------------------------------------
bool caminhoJuntasValido(float t1a, float t2a, float t1b, float t2b,
                         const char** motivo) {
  const float d1 = fabsf(t1b - t1a);
  const float d2 = fabsf(t2b - t2a);
  const float maior = (d1 > d2) ? d1 : d2;

  int n = (int)(maior / PASSO_VALIDACAO_GRAUS) + 1;
  if (n < 4)   n = 4;      // trecho curto ainda merece alguns pontos
  if (n > 360) n = 360;    // teto de custo: ~360 checagens por trecho

  for (int k = 0; k <= n; k++) {
    const float a = (float)k / (float)n;
    if (!posturaValida(t1a + (t1b - t1a) * a,
                       t2a + (t2b - t2a) * a, motivo)) {
      return false;
    }
  }
  return true;
}

bool caminhoJuntasValidoDet(float t1a, float t2a, float t1b, float t2b,
                            Violacao& v) {
  violacaoLimpar(v);
  const float d1 = fabsf(t1b - t1a);
  const float d2 = fabsf(t2b - t2a);
  const float maior = (d1 > d2) ? d1 : d2;

  int n = (int)(maior / PASSO_VALIDACAO_GRAUS) + 1;
  if (n < 4)   n = 4;
  if (n > 360) n = 360;

  // Varre o caminho INTEIRO e guarda a PIOR violacao, nao a primeira.
  // Relatar a primeira engana: num cordao que estoura 0,3 grau no comeco
  // e 45 graus no meio, o operador tenta abrir o limite em 1 grau e nao
  // entende por que continua recusado.
  bool achou = false;
  float piorExcesso = 0.0f;
  Violacao pior;
  violacaoLimpar(pior);

  for (int k = 0; k <= n; k++) {
    const float a = (float)k / (float)n;
    Violacao atual;
    if (!posturaValidaDet(t1a + (t1b - t1a) * a,
                          t2a + (t2b - t2a) * a, atual)) {
      const float excesso = fabsf(atual.valor - atual.limite);
      if (!achou || excesso > piorExcesso) {
        achou = true; piorExcesso = excesso; pior = atual; pior.fracao = a;
      }
    }
  }
  if (achou) { v = pior; return false; }
  return true;
}

// ---------------------------------------------------------------------
// A reta cartesiana e o unico caminho em que o operador pensa ("do canto
// da chapa ate o outro"), mas quem tem de percorrer sao as juntas. Num
// braco 2R, aproximar a ponta da base obriga o cotovelo a dobrar: uma
// reta entre dois pontos folgados pode exigir muito mais curso do que
// qualquer uma das pontas. E por isso que a recusa precisa dizer ONDE.
// ---------------------------------------------------------------------
bool retaCartesianaValida(float x0, float y0, float x1, float y1,
                          float refT1, float refT2, Violacao& v) {
  violacaoLimpar(v);

  const float dist = sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
  int n = (int)(dist / PASSO_INTERP_MM) + 2;
  if (n > 900) n = 900;          // teto de custo para cordao muito longo

  bool achou = false;
  float piorExcesso = 0.0f;
  Violacao pior;
  violacaoLimpar(pior);

  float r1 = refT1, r2 = refT2;
  for (int k = 0; k <= n; k++) {
    const float a = (float)k / (float)n;
    const float x = x0 + (x1 - x0) * a;
    const float y = y0 + (y1 - y0) * a;

    float ca1, ca2, cb1, cb2;
    const bool okA = cinematicaInversa(x, y, true,  ca1, ca2);
    const bool okB = cinematicaInversa(x, y, false, cb1, cb2);
    if (!okA && !okB) {
      // Fora de alcance nao tem meio termo: e o pior caso possivel.
      violacaoLimpar(v); v.causa = "alcance"; v.fracao = a; return false;
    }

    Violacao va, vb;
    const bool validaA = okA && posturaValidaDet(ca1, ca2, va);
    const bool validaB = okB && posturaValidaDet(cb1, cb2, vb);

    if (!validaA && !validaB) {
      // Entre as duas solucoes, reporta a que chegou MAIS PERTO de
      // servir. Entre os pontos da reta, guarda a PIOR: relatar a
      // primeira faria o cordao parecer quase viavel quando o meio dele
      // precisa de dezenas de graus a mais.
      const Violacao& melhorAqui =
          (okA && (!okB || fabsf(va.valor - va.limite) <= fabsf(vb.valor - vb.limite)))
          ? va : vb;
      const float excesso = fabsf(melhorAqui.valor - melhorAqui.limite);
      if (!achou || excesso > piorExcesso) {
        achou = true; piorExcesso = excesso; pior = melhorAqui; pior.fracao = a;
      }
      continue;   // segue varrendo: interessa o pior ponto do cordao
    }

    // Segue pelo ramo de cotovelo que exige menos deslocamento, igual ao
    // que resolverXY faz na execucao.
    if (validaA && validaB) {
      const float custoA = fabsf(ca1 - r1) + fabsf(ca2 - r2);
      const float custoB = fabsf(cb1 - r1) + fabsf(cb2 - r2);
      if (custoA <= custoB) { r1 = ca1; r2 = ca2; } else { r1 = cb1; r2 = cb2; }
    } else if (validaA) { r1 = ca1; r2 = ca2; }
    else                { r1 = cb1; r2 = cb2; }
  }
  if (achou) { v = pior; return false; }
  return true;
}

bool caminhoJuntasValidoPassos(long p1a, long p2a, long p1b, long p2b,
                               const char** motivo) {
  return caminhoJuntasValido(passosParaGraus(J1, p1a), passosParaGraus(J2, p2a),
                             passosParaGraus(J1, p1b), passosParaGraus(J2, p2b),
                             motivo);
}

// ---------------------------------------------------------------------
bool posturaValidaPassos(long p1, long p2, const char** motivo) {
  return posturaValida(passosParaGraus(J1, p1),
                       passosParaGraus(J2, p2), motivo);
}

// ---------------------------------------------------------------------
bool resolverXY(float x, float y, float t1Atual, float t2Atual,
                float& t1, float& t2, const char** motivo) {
  float a1, a2, b1, b2;
  const bool okA = cinematicaInversa(x, y, true,  a1, a2);
  const bool okB = cinematicaInversa(x, y, false, b1, b2);

  if (!okA && !okB) {
    if (motivo) *motivo = "ponto fora do alcance do braco";
    return false;
  }

  const char* mA = "postura invalida";
  const char* mB = "postura invalida";
  const bool validaA = okA && posturaValida(a1, a2, &mA);
  const bool validaB = okB && posturaValida(b1, b2, &mB);

  if (validaA && !validaB) { t1 = a1; t2 = a2; return true; }
  if (validaB && !validaA) { t1 = b1; t2 = b2; return true; }
  if (!validaA && !validaB) {
    if (motivo) *motivo = okA ? mA : mB;
    return false;
  }

  // As duas solucoes servem: escolhe a que exige menos movimento.
  const float custoA = fabsf(a1 - t1Atual) + fabsf(a2 - t2Atual);
  const float custoB = fabsf(b1 - t1Atual) + fabsf(b2 - t2Atual);
  if (custoA <= custoB) { t1 = a1; t2 = a2; }
  else                  { t1 = b1; t2 = b2; }
  return true;
}
