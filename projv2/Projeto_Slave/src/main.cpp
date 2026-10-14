/*
  ARDUINO 2 - LCD + TILT + LED (recebe mensagens do Arduino 1 por I2C)

  LCD 16x2              Arduino 2        Outros          Arduino 2
  --------              ---------        ------          ---------
  1  VSS                GND              Tilt            7 (outra perna ao GND)
  2  VDD                5V               LED (+)         8 (com resistência 220 ohm)
  3  V0 (contraste)     6                LED (-)         GND
  4  RS                 12
  5  RW                 GND              Ligação I2C     Arduino 1
  6  E                  11               -----------     ---------
  7-10 D0-D3            nada             A4 (SDA)        A4
  11 D4                 5                A5 (SCL)        A5
  12 D5                 4                GND             GND
  13 D6                 3
  14 D7                 2
  15 A (luz +)          5V com resistência 220 ohm
  16 K (luz -)          GND
*/
#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal.h>

// LiquidCrystal(rs, enable, d4, d5, d6, d7)
LiquidCrystal lcd(12, 11, 5, 4, 3, 2);

const int tiltPin = 7;       // sensor de inclinação
const int ledPin = 8;        // LED de alarme
const int contrastePin = 6;  // V0 do LCD
const int CONTRASTE = 90;    // valor fixo do contraste (0 a 255), em vez do potenciómetro
#define ENDERECO 8           // endereço I2C deste Arduino

volatile char codigo = 0;    // letra recebida do Arduino 1 (0 = nada de novo)
bool aberto = false;         // guarda se o cofre está aberto ou fechado

// Protótipos: num .cpp as funções têm de ser declaradas antes de serem usadas.
void receberDados(int quantos);
void mensagem(String linha1, String linha2);
void mostrarEstado();
void alarme(String motivo);

void setup() {
  Serial.begin(9600);

  Wire.begin(ENDERECO);           // com número = este Arduino é o "escravo" com o endereço 8
  Wire.onReceive(receberDados);   // quando chegar uma mensagem, corre a função receberDados

  pinMode(tiltPin, INPUT_PULLUP); // ativa a resistência pull-up interna
  pinMode(ledPin, OUTPUT);

  analogWrite(contrastePin, CONTRASTE);  // contraste do LCD
  lcd.begin(16, 2);                      // LCD de 16 colunas e 2 linhas

  mostrarEstado();
}

void loop() {
  // 1) Chegou uma letra do Arduino 1?
  if (codigo != 0) {
    char c = codigo;
    codigo = 0;  // marca como tratada

    if (c == 'A') {          // A = cartão certo, cofre aberto
      aberto = true;
      mostrarEstado();
    } else if (c == 'F') {   // F = cofre fechado
      aberto = false;
      mostrarEstado();
    } else if (c == 'E') {   // E = cartão errado
      alarme("Cartao errado");
    }
  }

  // 2) Com o cofre fechado, se o tilt estiver inclinado, é roubo
  if (aberto == false && digitalRead(tiltPin) == LOW) {
    alarme("Cofre mexido");
  }
}

// Corre sozinha sempre que o Arduino 1 envia uma letra
void receberDados(int quantos) {
  codigo = Wire.read();
}

// Escreve a mesma mensagem no monitor série e no LCD
void mensagem(String linha1, String linha2) {
  Serial.println(linha1);
  Serial.println(linha2);

  lcd.clear();
  lcd.setCursor(0, 0);  // linha de cima
  lcd.print(linha1);
  lcd.setCursor(0, 1);  // linha de baixo
  lcd.print(linha2);
}

// Mostra a mensagem normal, conforme o cofre está aberto ou fechado
void mostrarEstado() {
  if (aberto) {
    mensagem("Acesso permitido", "Cofre aberto");
  } else {
    mensagem("Cofre trancado", "Passe o cartao");
  }
}

// Alarme: LED aceso e mensagem de roubo durante 3 segundos
void alarme(String motivo) {
  digitalWrite(ledPin, HIGH);
  mensagem("ALARME! ROUBO!", motivo);
  delay(3000);
  digitalWrite(ledPin, LOW);
  mostrarEstado();
}
