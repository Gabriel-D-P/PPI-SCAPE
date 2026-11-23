#include <ESP8266WiFi.h>
#include <espnow.h>

extern "C" {
  #include "user_interface.h"
}

// MAC do ESP8266 de LARGADA (confira se bate com a placa da largada!)
uint8_t macLargada[] = {0xC8, 0xC9, 0xA3, 0x69, 0xB9, 0x33}; 

// --- MAPEAMENTO DOS PINOS ---
#define PINO_TRIG   14 // D5 (GPIO 14)
#define PINO_ECHO   12 // D6 (GPIO 12)
#define PINO_BUZZER 13 // D7 (GPIO 13)

#define CANAL_ESPNOW 1

const float LIMIAR_CHEGADA_CM = 100.0;
const unsigned long TEMPO_COOLDOWN_MS = 1500;

typedef struct struct_mensagem {
  unsigned long tempoGatilhoMs;
  bool atletaPassou;
} struct_mensagem;

struct_mensagem dadosEnvio;

bool jaDetectou = false;
int leiturasConsecutivas = 0;

unsigned long tempoUltimaDetec = 0;
unsigned long tempoBuzzerDesliga = 0;
unsigned long ultimoPingMs = 0;
unsigned long ultimoPrintSerial = 0;

float lerDistanciaCM() {
  digitalWrite(PINO_TRIG, LOW);
  delayMicroseconds(4);
  digitalWrite(PINO_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PINO_TRIG, LOW);

  long duracaoUs = pulseIn(PINO_ECHO, HIGH, 25000);
  if (duracaoUs == 0) return 999.0;
  return (duracaoUs * 0.0343) / 2.0;
}

void setup() {
  Serial.begin(115200);

  pinMode(PINO_TRIG, OUTPUT);
  pinMode(PINO_ECHO, INPUT);
  pinMode(PINO_BUZZER, OUTPUT);

  // Modo Station puro, desconectado de roteadores e travado no canal 1
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  wifi_set_channel(CANAL_ESPNOW);

  if (esp_now_init() != 0) {
    Serial.println("Erro ao inicializar ESP-NOW!");
    return;
  }

  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
  
  if (esp_now_add_peer(macLargada, ESP_NOW_ROLE_SLAVE, CANAL_ESPNOW, NULL, 0) != 0) {
    Serial.println("Erro ao parear com o ESP da Largada!");
    return;
  }

  tone(PINO_BUZZER, 1000, 100);
  Serial.println("--- CHEGADA PRONTA (ESP-NOW DIRETO) ---");
}

void loop() {
  unsigned long tempoAtual = millis();

  if (tempoBuzzerDesliga > 0 && tempoAtual >= tempoBuzzerDesliga) {
    noTone(PINO_BUZZER);
    tempoBuzzerDesliga = 0;
  }

  if (tempoAtual - ultimoPingMs >= 45) {
    ultimoPingMs = tempoAtual;

    float distanciaAtual = lerDistanciaCM();

    if (tempoAtual - ultimoPrintSerial >= 300) {
      ultimoPrintSerial = tempoAtual;
      Serial.print("Distancia: ");
      Serial.print(distanciaAtual, 1);
      Serial.print(" cm | Confirmacoes: ");
      Serial.println(leiturasConsecutivas);
    }

    bool leituraValida = (distanciaAtual >= 4.0 && distanciaAtual <= LIMIAR_CHEGADA_CM);

    if (leituraValida) {
      leiturasConsecutivas++;
    } else {
      leiturasConsecutivas = 0;
    }

    if (leiturasConsecutivas >= 2 && !jaDetectou) {
      jaDetectou = true;
      tempoUltimaDetec = tempoAtual;

      dadosEnvio.tempoGatilhoMs = tempoAtual;
      dadosEnvio.atletaPassou = true;

      int result = esp_now_send(macLargada, (uint8_t *) &dadosEnvio, sizeof(dadosEnvio));

      tone(PINO_BUZZER, 1800);
      tempoBuzzerDesliga = tempoAtual + 200;

      if (result == 0) {
        Serial.println("\n>>> CRUZAMENTO CONFIRMADO - Sinal enviado diretamente! <<<\n");
      } else {
        Serial.println("\n[X] Falha no envio ESP-NOW!\n");
      }
    }

    if (jaDetectou && !leituraValida && (tempoAtual - tempoUltimaDetec >= TEMPO_COOLDOWN_MS)) {
      jaDetectou = false;
      leiturasConsecutivas = 0;
    }
  }
}