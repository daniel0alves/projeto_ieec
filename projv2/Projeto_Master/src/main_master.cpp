/*
  ARDUINO 1 - RFID + SERVO + BUZZER (envia mensagens ao Arduino 2 por I2C)

  RC522 (RFID)  Arduino 1        Servo        Arduino 1
  ------------  ---------        -----        ---------
  SDA (SS)      10               Sinal        3
  SCK           13               Vermelho     5V
  MOSI          11               Castanho     GND
  MISO          12
  IRQ           nada             Buzzer       Arduino 1
  GND           GND              ------       ---------
  RST           9                +            5
  3.3V          3.3V (NÃO 5V)    -            GND

                                 LED          Arduino 1
                                 ---          ---------
                                 + (resist.)  4
                                 -            GND

                                 Tilt: fica só no Arduino 2 (pino 7).
                                 O Arduino 1 pergunta-lhe o estado por I2C.

                                 Ligação I2C  Arduino 2
                                 -----------  ---------
                                 A4 (SDA)     A4
                                 A5 (SCL)     A5
                                 GND          GND
*/
#include <Arduino.h>

#include <SPI.h>
#include <MFRC522.h>
#include <Servo.h>
#include <Wire.h>

#define RST_PIN 9         // RC522 RST
#define SS_PIN 10         // RC522 SDA (SS)
#define SERVO_PIN 3
#define BUZZER_PIN 5      // buzzer (SOS)
#define LED_PIN 4         // LED fica ligado enquanto o SOS toca
#define INTERVALO_TILT 100  // de quantos em quantos ms pergunta o tilt ao Arduino 2
#define ENDERECO_LCD 8    // endereço I2C do Arduino 2

Servo myservo;
MFRC522 rfid(SS_PIN, RST_PIN);

// Cartões autorizados (cada um tem 4 bytes)
const byte AUTHORIZED_UID[][4] = {
  {0x17, 0x83, 0xD1, 0x31},
  {0x37, 0x0C, 0xD0, 0x31}
};

// Quantos cartões há na lista
const byte NUM_UIDS = sizeof(AUTHORIZED_UID) / sizeof(AUTHORIZED_UID[0]);

bool aberto = false;  // guarda se o cofre está aberto ou fechado

// ---- SOS sem bloqueio ----
// Duração de cada passo em ms. Posições pares = buzzer ligado, ímpares = desligado.
// S (. . .)   O (- - -)   S (. . .)
const unsigned int PADRAO[] = {
  200, 200, 200, 200, 200, 600,    // S
  600, 200, 600, 200, 600, 600,    // O
  200, 200, 200, 200, 200, 1000    // S + pausa final
};
const byte PADRAO_N = sizeof(PADRAO) / sizeof(PADRAO[0]);

bool sosAtivo = false;             // o SOS está a tocar?
bool sosRepetir = false;           // true = repete até ser parado (alarme de tilt)
byte passo = 0;                    // passo atual do padrão
unsigned long inicioPasso = 0;     // quando começou o passo atual

// Protótipos: num .cpp as funções têm de ser declaradas antes de serem usadas.
bool card_check();
void enviar(char codigo);
void sosIniciar(bool repetir);
void sosAtualizar();
void sosParar();
void aplicarPasso();
bool tiltRemoto();

void setup() {
  Serial.begin(9600);
  while (!Serial);

  Wire.begin();  // sem número = este Arduino é o "mestre" do I2C

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);

  SPI.begin();
  rfid.PCD_Init();
  myservo.attach(SERVO_PIN);
  myservo.write(0);

  Serial.println("RC522 RFID Reader Initialized");
}

void loop() {
  // Pergunta ao Arduino 2 se o tilt está inclinado (de 100 em 100 ms)
  static bool inclinado = false;
  static unsigned long ultimaLeitura = 0;
  if (millis() - ultimaLeitura >= INTERVALO_TILT) {
    ultimaLeitura = millis();
    inclinado = tiltRemoto();
  }

  // Cofre fechado e inclinado -> SOS (repete). Se deixar de estar inclinado
  // ou o cofre abrir, o SOS de tilt pára.
  if (aberto == false && inclinado) {
    sosIniciar(true);
  } else if (sosAtivo && sosRepetir) {
    sosParar();
  }

  sosAtualizar();  // faz o SOS avançar sem bloquear o Arduino

  if (card_check()) {
    sosParar();  // cartão certo -> pára o SOS imediatamente

    if (aberto == false) {
      Serial.println("Access granted - a abrir");
      myservo.write(90);
      aberto = true;
      enviar('A');  // A = aberto
    } else {
      Serial.println("A fechar");
      myservo.write(0);
      aberto = false;
      enviar('F');  // F = fechado
    }
    delay(1000);  // evita ler o mesmo cartão duas vezes seguidas
  }
}

// Envia uma letra ao Arduino 2, que mostra a mensagem certa no LCD
void enviar(char codigo) {
  Wire.beginTransmission(ENDERECO_LCD);
  Wire.write(codigo);
  Wire.endTransmission();
}

// Pede 1 byte ao Arduino 2: 1 = tilt inclinado, 0 = normal
bool tiltRemoto() {
  Wire.requestFrom((uint8_t)ENDERECO_LCD, (uint8_t)1);
  if (Wire.available()) {
    return Wire.read() == 1;
  }
  return false;  // sem resposta -> assume normal
}

// Liga ou desliga o buzzer conforme o passo atual. O LED fica ligado todo o SOS.
void aplicarPasso() {
  if (passo % 2 == 0) {
    tone(BUZZER_PIN, 1000);   // 1000 Hz
  } else {
    noTone(BUZZER_PIN);
  }
  digitalWrite(LED_PIN, HIGH);
  inicioPasso = millis();
}

// Começa o SOS. repetir = true -> repete até ser parado; false -> toca uma vez.
void sosIniciar(bool repetir) {
  if (sosAtivo) {
    if (repetir) sosRepetir = true;
    return;
  }
  Serial.println("SOS");
  sosAtivo = true;
  sosRepetir = repetir;
  passo = 0;
  aplicarPasso();
}

// Chamar em todos os loops: passa ao passo seguinte quando o tempo acaba
void sosAtualizar() {
  if (!sosAtivo) return;

  if (millis() - inicioPasso >= PADRAO[passo]) {
    passo++;
    if (passo >= PADRAO_N) {
      if (sosRepetir) {
        passo = 0;       // repete
      } else {
        sosParar();      // tocou uma vez, acabou
        return;
      }
    }
    aplicarPasso();
  }
}

void sosParar() {
  sosAtivo = false;
  sosRepetir = false;
  noTone(BUZZER_PIN);
  digitalWrite(LED_PIN, LOW);
}

bool card_check() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    return false;
  }

  Serial.println("Card detected:");
  Serial.print("UID:");
  for (byte i = 0; i < rfid.uid.size; i++) {
    Serial.print(rfid.uid.uidByte[i] < 0x10 ? " 0" : " ");
    Serial.print(rfid.uid.uidByte[i], HEX);
  }
  Serial.println();

  // Compara o cartão lido com cada cartão da lista
  bool match = false;
  for (byte n = 0; n < NUM_UIDS; n++) {
    if (rfid.uid.size == 4 && memcmp(rfid.uid.uidByte, AUTHORIZED_UID[n], 4) == 0) {
      match = true;
    }
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  if (!match) {
    Serial.println("Access denied");
    enviar('E');         // E = cartão errado
    sosIniciar(false);   // buzzer faz SOS uma vez
  }
  return match;
}