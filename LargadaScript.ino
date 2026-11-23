#include <ESP8266WiFi.h>
#include <espnow.h>
#include <LiquidCrystal.h>

extern "C" {
  #include "user_interface.h"
}

#define PINO_TRIG   5  // D1 (GPIO 5)
#define PINO_ECHO   4  // D2 (GPIO 4)
#define PINO_BOTAO  0  // D3 (GPIO 0)
#define PINO_BUZZER 15 // D8 (GPIO 15)

// Ordem: (RS, E, D4, D5, D6, D7) -> (D0, D4, D5, D6, D7, RX)
LiquidCrystal LCD(16, 2, 14, 12, 13, 3);

#define CANAL_ESPNOW 1

const float DISTANCIA_PROVA_M = 100.0;  // Distância da prova em metros
const float LIMIAR_PRESENCA_CM = 100.0; // Detecção até 1 metro (100 cm)

enum EstadoCorrida {
  AGUARDANDO_ATLETA,
  ATLETA_POSICIONADO,
  LARGADA_DADA,
  ATLETA_EM_PISTA,
  CORRIDA_FINALIZADA,
  FALSA_PARTIDA
};

EstadoCorrida estadoAtual = AGUARDANDO_ATLETA;

unsigned long tempoInicioMs = 0;
unsigned long tempoSaidaBlocoMs = 0;
volatile unsigned long tempoChegadaMs = 0;
volatile bool chegadaRecebidaFlag = false;

float tempoReacaoSeg = 0.0;
float tempoTotalSeg = 0.0;
float velocidadeMs = 0.0;

bool ultimoEstadoBotao = HIGH;
bool telaFinalDesenhada = false;
unsigned long ultimoPingMs = 0;

int leiturasConsecutivasPresenca = 0;
int leiturasConsecutivasAusencia = 0;

typedef struct struct_mensagem {
  unsigned long tempoGatilhoMs;
  bool atletaPassou;
} struct_mensagem;

struct_mensagem dadosRecebidos;

float lerDistanciaCM() {
  digitalWrite(PINO_TRIG, LOW);
  delayMicroseconds(4);
  digitalWrite(PINO_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PINO_TRIG, LOW);

  long duracaoUs = pulseIn(PINO_ECHO, HIGH, 20000);
  if (duracaoUs == 0) return 999.0;
  return (duracaoUs * 0.0343) / 2.0;
}

void OnDataRecv(uint8_t *mac_addr, uint8_t *incomingData, uint8_t len) {
  if (len == sizeof(dadosRecebidos)) {
    memcpy(&dadosRecebidos, incomingData, sizeof(dadosRecebidos));
    if (dadosRecebidos.atletaPassou && !chegadaRecebidaFlag) {
      tempoChegadaMs = millis();
      chegadaRecebidaFlag = true;
    }
  }
}

void resetarSistema();

void setup() {
  Serial.begin(115200);

  pinMode(PINO_TRIG, OUTPUT);
  pinMode(PINO_ECHO, INPUT);
  pinMode(PINO_BOTAO, INPUT_PULLUP);
  pinMode(PINO_BUZZER, OUTPUT);

  // LCD.begin() reconfigura o pino RX (GPIO 3) como saída para o D7 do display
  LCD.begin(16, 2);
  LCD.setCursor(0, 0);
  LCD.print("Iniciando...");

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  wifi_set_channel(CANAL_ESPNOW);

  if (esp_now_init() != 0) {
    LCD.clear();
    LCD.print("Erro ESP-NOW!");
    return;
  }

  esp_now_set_self_role(ESP_NOW_ROLE_SLAVE);
  esp_now_register_recv_cb(OnDataRecv);

  LCD.clear();
  Serial.print("MAC do ESP8266 Largada: ");
  Serial.println(WiFi.macAddress());
}

void loop() {
  unsigned long tempoAtual = millis();

  if (chegadaRecebidaFlag) {
    chegadaRecebidaFlag = false;

    if (estadoAtual == ATLETA_EM_PISTA || estadoAtual == LARGADA_DADA) {
      if (estadoAtual == LARGADA_DADA) {
        tempoReacaoSeg = 0.0;
      }

      tempoTotalSeg = (tempoChegadaMs - tempoInicioMs) / 1000.0;
      if (tempoTotalSeg <= 0.001) tempoTotalSeg = 0.001;
      velocidadeMs = DISTANCIA_PROVA_M / tempoTotalSeg;

      estadoAtual = CORRIDA_FINALIZADA;
      telaFinalDesenhada = false;
      tone(PINO_BUZZER, 1200, 500);

      Serial.println("\n=== RESULTADOS DA PROVA ===");
      Serial.print("Tempo de Reacao:  "); Serial.print(tempoReacaoSeg, 3); Serial.println(" s");
      Serial.print("Tempo Total:      "); Serial.print(tempoTotalSeg, 3); Serial.println(" s");
      Serial.print("Velocidade Media: "); Serial.print(velocidadeMs, 2); Serial.println(" m/s");
    }
  }

  bool estadoBotaoAtual = digitalRead(PINO_BOTAO);
  if (estadoBotaoAtual == LOW && ultimoEstadoBotao == HIGH) {
    delay(50);

    if (estadoAtual == ATLETA_POSICIONADO) {
      estadoAtual = LARGADA_DADA;
      tempoInicioMs = millis();
      tone(PINO_BUZZER, 880, 300);
    } 
    else if (estadoAtual == AGUARDANDO_ATLETA) {
      LCD.setCursor(0, 1);
      LCD.print("Sem Atleta!     ");
      delay(800);
    } 
    else {
      resetarSistema();
    }
  }
  ultimoEstadoBotao = estadoBotaoAtual;

  if (tempoAtual - ultimoPingMs >= 45) {
    ultimoPingMs = tempoAtual;
    float distanciaAtual = lerDistanciaCM();
    
    bool leituraPresenca = (distanciaAtual >= 4.0 && distanciaAtual <= LIMIAR_PRESENCA_CM);

    if (leituraPresenca) {
      leiturasConsecutivasPresenca++;
      leiturasConsecutivasAusencia = 0;
    } else {
      leiturasConsecutivasAusencia++;
      leiturasConsecutivasPresenca = 0;
    }

    switch (estadoAtual) {
      case AGUARDANDO_ATLETA:
        LCD.setCursor(0, 0);
        LCD.print("Prova: "); LCD.print((int)DISTANCIA_PROVA_M); LCD.print("m      ");
        LCD.setCursor(0, 1);
        LCD.print("Aguardando...   ");

        if (leiturasConsecutivasPresenca >= 2) {
          estadoAtual = ATLETA_POSICIONADO;
          tone(PINO_BUZZER, 1000, 100);
        }
        break;

      case ATLETA_POSICIONADO:
        LCD.setCursor(0, 0);
        LCD.print("Atleta na Linha ");
        LCD.setCursor(0, 1);
        LCD.print("Pronto p/ Largar");

        if (leiturasConsecutivasAusencia >= 6) {
          estadoAtual = FALSA_PARTIDA;
        }
        break;

      case FALSA_PARTIDA:
        LCD.setCursor(0, 0);
        LCD.print("!FALSA PARTIDA! ");
        LCD.setCursor(0, 1);
        LCD.print("Pressione Reset ");
        tone(PINO_BUZZER, 250);
        break;

      case LARGADA_DADA:
        LCD.setCursor(0, 0);
        LCD.print("LARGADA DADA!   ");
        LCD.setCursor(0, 1);
        LCD.print("Cronometrando...");

        if (leiturasConsecutivasAusencia >= 3) {
          tempoSaidaBlocoMs = millis();
          tempoReacaoSeg = (tempoSaidaBlocoMs - tempoInicioMs) / 1000.0;
          estadoAtual = ATLETA_EM_PISTA;
        }
        break;

      case ATLETA_EM_PISTA:
        LCD.setCursor(0, 0);
        LCD.print("Reacao: "); LCD.print(tempoReacaoSeg, 3); LCD.print("s ");
        LCD.setCursor(0, 1);
        LCD.print("Atleta em Pista ");
        break;

      case CORRIDA_FINALIZADA:
        if (!telaFinalDesenhada) {
          telaFinalDesenhada = true;
          LCD.clear();
          LCD.setCursor(0, 0);
          LCD.print(tempoTotalSeg, 2); LCD.print("s | ");
          LCD.print(velocidadeMs, 1); LCD.print(" m/s");
          
          LCD.setCursor(0, 1);
          LCD.print("Reacao: "); LCD.print(tempoReacaoSeg, 3); LCD.print("s");
        }
        break;
    }
  }
}

void resetarSistema() {
  noTone(PINO_BUZZER);
  estadoAtual = AGUARDANDO_ATLETA;
  chegadaRecebidaFlag = false;
  telaFinalDesenhada = false;
  tempoInicioMs = 0;
  tempoSaidaBlocoMs = 0;
  tempoChegadaMs = 0;
  tempoReacaoSeg = 0.0;
  tempoTotalSeg = 0.0;
  velocidadeMs = 0.0;
  leiturasConsecutivasPresenca = 0;
  leiturasConsecutivasAusencia = 0;

  tone(PINO_BUZZER, 440, 100);
  delay(150);
  tone(PINO_BUZZER, 440, 100);

  LCD.clear();
}