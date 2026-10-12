/*
  Wiring (Arduino Uno)

  RC522 pin     Arduino Uno pin
  ---------     ---------------
  SDA (SS)      10
  SCK           13
  MOSI          11
  MISO          12
  IRQ           not connected
  GND           GND
  RST           9
  3.3V          3.3V   (NOT 5V, it can damage the reader)

  Servo wire    Connection
  ----------    ----------
  Signal        3
  Red (V+)      5V (ideally an external supply)
  Brown (GND)   GND (shared with the Arduino)
*/

#include <SPI.h>
#include <MFRC522.h>
#include <Servo.h>

#define RST_PIN 9   // RC522 RST
#define SS_PIN 10   // RC522 SDA (SS)

Servo myservo;
MFRC522 rfid(SS_PIN, RST_PIN);

const byte AUTHORIZED_UID[4] = {0x17, 0x83, 0xD1, 0x31};

void setup() {
  Serial.begin(9600);
  while (!Serial);

  SPI.begin();
  rfid.PCD_Init();
  myservo.attach(3);
  myservo.write(0);

  Serial.println("RC522 RFID Reader Initialized");
}

void loop() {
  if (card_check()) {
    Serial.println("Access granted");
    myservo.write(90);
    delay(3000);
    myservo.write(0);
  }
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

  bool match = (rfid.uid.size == sizeof(AUTHORIZED_UID)) &&
               (memcmp(rfid.uid.uidByte, AUTHORIZED_UID, sizeof(AUTHORIZED_UID)) == 0);

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  if (!match) {
    Serial.println("Access denied");
  }
  return match;
}