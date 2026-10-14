/*
  ARDUINO 1 - RFID + SERVO (envia mensagens ao Arduino 2 por I2C)

  RC522 (RFID)  Arduino 1        Servo        Arduino 1
  ------------  ---------        -----        ---------
  SDA (SS)      10               Sinal        3
  SCK           13               Vermelho     5V
  MOSI          11               Castanho     GND
  MISO          12
  IRQ           nada             Ligação I2C  Arduino 2
  GND           GND              -----------  ---------
  RST           9                A4 (SDA)     A4
  3.3V          3.3V (NÃO 5V)    A5 (SCL)     A5
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

// Protótipos: num .cpp as funções têm de ser declaradas antes de serem usadas.
bool card_check();
void enviar(char codigo);

void setup() {
  Serial.begin(9600);
  while (!Serial);

  Wire.begin();  // sem número = este Arduino é o "mestre" do I2C

  SPI.begin();
  rfid.PCD_Init();
  myservo.attach(SERVO_PIN);
  myservo.write(0);

  Serial.println("RC522 RFID Reader Initialized");
}

void loop() {
  if (card_check()) {
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
    enviar('E');  // E = cartão errado
  }
  return match;
}
