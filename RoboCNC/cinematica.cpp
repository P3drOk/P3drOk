#include "cinematica.h"
#include <math.h>

// ---------------------------------------------------------------------
float passosParaGraus(const Junta& j, long passos) {
  if (j.passosPorGrau <= 0.0f) return 0.0f;
  return (float)passos / j.passosPorGrau;
}

long grausParaPassos(const Junta& j, float graus) {
  if (j.passosPorGrau <= 0.0f) return 0;
  return lroundf(graus * j.passosPorGrau);
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
bool posturaValida(float t1, float t2, const char** motivo) {
  // Sem calibracao nao existe geometria confiavel para proteger nada.
  // Nesse estado apenas o jog manual e liberado (modo de instalacao);
  // todos os modos automaticos exigem calibracao antes de rodar.
  const bool calibrado = J1.calibrada && J2.calibrada;

  // 1) Curso de cada junta - vem da calibracao do proprio operador
  if (protCurso && calibrado) {
    if (t1 < J1.grausMin + MARGEM_LIMITE_GRAUS ||
        t1 > J1.grausMax - MARGEM_LIMITE_GRAUS) {
      if (motivo) *motivo = "junta 1 no fim do curso calibrado";
      return false;
    }
    if (t2 < J2.grausMin + MARGEM_LIMITE_GRAUS ||
        t2 > J2.grausMax - MARGEM_LIMITE_GRAUS) {
      if (motivo) *motivo = "junta 2 no fim do curso calibrado";
      return false;
    }
  }

  // 2) Auto-colisao dos elos: theta2 = 0 e braco esticado (seguro),
  //    theta2 = +/-180 e o elo 2 dobrado sobre o elo 1 (colisao).
  if (protDobra) {
    if (fabsf(t2) > 180.0f - folgaDobra) {
      if (motivo) *motivo = "cotovelo dobrado demais: elo 2 bate no elo 1";
      return false;
    }
  }

  // 3) Envelope cartesiano - depende do comprimento dos elos estar certo
  if (protEnvelope) {
    float xc, yc, xp, yp;
    cinematicaDireta(t1, t2, xc, yc, xp, yp);

    if (yp < envYMin || yc < envYMin) {
      if (motivo) *motivo = "abaixo do Y minimo (mesa)";
      return false;
    }
    if (distanciaOrigemAoSegmento(xc, yc, xp, yp) < envRaioMin) {
      if (motivo) *motivo = "elo 2 passando por cima da base";
      return false;
    }
  }

  return true;
}

// ---------------------------------------------------------------------
// Precisa usar EXATAMENTE os mesmos limites de posturaValida(), margem
// incluida. Quando as duas contas divergiam, existia uma faixa de
// MARGEM_LIMITE_GRAUS onde a postura era invalida e a gravidade era zero:
// posturaValida() bloqueava e o criterio de recuperacao (gAtual > 0)
// nunca liberava, entao o braco entrava na faixa e nao saia mais.
float gravidadeViolacao(float t1, float t2) {
  float g = 0.0f;
  const bool calibrado = J1.calibrada && J2.calibrada;

  if (protCurso && calibrado) {
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
