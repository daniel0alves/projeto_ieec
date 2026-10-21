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

                                 Botão        Arduino 1
                                 -----        ---------
                                 perna 1      2 (e resist. 10k ao GND)
                                 perna 2      5V
                                 (premido = HIGH)
                                 Com o cofre aberto:
                                   1 clique  = registar cartão
                                   2 cliques em menos de 1 s
                                             = remover cartão

                                 Tilt: fica só no Arduino 2 (pino 7).
                                 O Arduino 1 pergunta-lhe o estado por I2C.

                                 Ligação I2C  Arduino 2
                                 -----------  ---------
                                 A4 (SDA)     A4
                                 A5 (SCL)     A5
                                 GND          GND
*/
#include <Arduino.h>

#include <MFRC522.h>
#include <SPI.h>
#include <Servo.h>
#include <Wire.h>

#define RST_PIN 9 // RC522 RST
#define SS_PIN 10 // RC522 SDA (SS)
#define SERVO_PIN 3
#define BUZZER_PIN 5   // buzzer (SOS)
#define LED_PIN 4      // LED fica ligado enquanto o SOS toca
#define ENDERECO_LCD 8 // endereço I2C do Arduino 2
#define BOTAO_PIN 2    // botão para registar/remover cartões (premido = HIGH)
#define MAX_UIDS 10    // máximo de cartões que cabem na lista
#define TEMPO_MODO 10000   // ms à espera do cartão (registar/remover) antes de desistir
#define TEMPO_DUPLO 1000   // ms máximo entre 2 cliques para contar como duplo
#define TEMPO_DEBOUNCE 50  // ms para ignorar os "saltos" do botão

// Tipo de buzzer:
//   0 = passivo (toca com tone())
//   1 = ativo, liga com HIGH
//   2 = ativo/módulo "low trigger", liga com LOW
#define TIPO_BUZZER 0

Servo myservo;
MFRC522 rfid(SS_PIN, RST_PIN);

// Cartões autorizados (cada um tem 4 bytes). Já não é const, porque o botão
// pode acrescentar cartões. Tem lugar para MAX_UIDS; os primeiros 2 vêm já
// preenchidos e os restantes ficam a zeros até serem registados.
byte AUTHORIZED_UID[MAX_UIDS][4] = {{0x17, 0x83, 0xD1, 0x31},
                                    {0x37, 0x0C, 0xD0, 0x31}};

// Quantos cartões estão realmente na lista (começa com os 2 de cima)
byte NUM_UIDS = 2;

bool aberto = false; // guarda se o cofre está aberto ou fechado

// ---- Registar / remover cartões com o botão ----
#define MODO_NORMAL 0   // o cartão abre e fecha o cofre
#define MODO_REGISTAR 1 // o próximo cartão é acrescentado à lista
#define MODO_REMOVER 2  // o próximo cartão é tirado da lista
byte modo = MODO_NORMAL;
unsigned long inicioModo = 0; // quando se entrou no modo atual

byte cliques = 0;                 // cliques contados até agora
unsigned long primeiroClique = 0; // quando foi o 1.º clique
bool botaoAntes = LOW;            // estado do botão na última leitura aceite
unsigned long ultimaMudanca = 0;  // quando o botão mudou pela última vez

// ---- SOS sem bloqueio (o leitor de cartões continua a funcionar) ----
// Duração de cada passo em ms. Posições pares = buzzer ligado, ímpares =
// desligado. S (. . .)   O (- - -)   S (. . .)
const unsigned int PADRAO[] = {
    200, 200, 200, 200, 200, 600, // S
    600, 200, 600, 200, 600, 600, // O
    200, 200, 200, 200, 200, 1000 // S + pausa final
};
const byte PADRAO_N = sizeof(PADRAO) / sizeof(PADRAO[0]);

bool sosAtivo = false;         // o SOS está a tocar?
byte passo = 0;                // passo atual do padrão
unsigned long inicioPasso = 0; // quando começou o passo atual

// Protótipos: num .cpp as funções têm de ser declaradas antes de serem usadas.
bool card_check();
bool lerCartao();
int indiceCartao();
bool cartaoConhecido();
void registarCartao();
void removerCartao();
bool botaoCarregado();
void entrarModo(byte novoModo);
void enviar(char codigo);
bool tiltRemoto();
void buzzerLigar();
void buzzerDesligar();
void aplicarPasso();
void sosIniciar();
void sosAtualizar();
void sosParar();

void setup() {
  Serial.begin(9600);
  while (!Serial)
    ;

  Wire.begin(); // sem número = este Arduino é o "mestre" do I2C

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BOTAO_PIN, INPUT); // resistência pull-down externa: solto = LOW
  buzzerDesligar(); // começa em silêncio

  SPI.begin();
  rfid.PCD_Init();
  myservo.attach(SERVO_PIN);
  myservo.write(0);

  Serial.println("RC522 RFID Reader Initialized");
}

void loop() {
  sosAtualizar(); // faz o SOS avançar (se estiver a tocar)

  // O tilt SÓ é lido quando o SOS não está a tocar
  static unsigned long ultimaLeitura = 0;
  if (!sosAtivo && millis() - ultimaLeitura >= 100) {
    ultimaLeitura = millis();

    if (aberto == false && tiltRemoto()) {
      Serial.println("Tilt - SOS");
      sosIniciar();
    }
  }

  // Botão: só conta com o cofre aberto e em modo normal.
  //   1 clique                  -> registar o próximo cartão
  //   2 cliques em menos de 1 s -> remover o próximo cartão
  bool clique = botaoCarregado();
  if (!aberto || modo != MODO_NORMAL) {
    cliques = 0;
  } else {
    if (clique) {
      cliques++;
      if (cliques == 1) {
        primeiroClique = millis();
      }
    }

    if (cliques >= 2) {
      cliques = 0;
      entrarModo(MODO_REMOVER);
    } else if (cliques == 1 && millis() - primeiroClique >= TEMPO_DUPLO) {
      // passou 1 s e não houve 2.º clique -> era um clique simples
      cliques = 0;
      entrarModo(MODO_REGISTAR);
    }
  }

  // A registar ou a remover: o próximo cartão é tratado em vez de abrir/fechar
  if (modo != MODO_NORMAL) {
    if (millis() - inicioModo >= TEMPO_MODO) {
      Serial.println("Cancelado (tempo esgotado)");
      modo = MODO_NORMAL;
      enviar('A'); // volta a mostrar "Cofre aberto"
    } else if (lerCartao()) {
      if (modo == MODO_REGISTAR) {
        registarCartao();
      } else {
        removerCartao();
      }
      modo = MODO_NORMAL;
      delay(1000); // evita ler o mesmo cartão duas vezes seguidas
    }
    return; // não abre nem fecha enquanto está a registar/remover
  }

  // O leitor de cartões é lido sempre, mesmo com o SOS a tocar
  if (card_check()) {
    sosParar(); // cartão certo -> pára o SOS

    if (aberto == false) {
      Serial.println("Access granted - a abrir");
      myservo.write(90);
      aberto = true;
      enviar('A'); // A = aberto
    } else {
      Serial.println("A fechar");
      myservo.write(0);
      aberto = false;
      enviar('F'); // F = fechado
    }
    delay(1000); // evita ler o mesmo cartão duas vezes seguidas

    if (aberto == false) {
      // O Arduino 2 guarda se o tilt abanou, mesmo com o cofre aberto.
      // Esta leitura deita isso fora, para o que se fez com o cofre aberto
      // (ou ao fechá-lo) não disparar o SOS logo a seguir.
      tiltRemoto();
    }
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
  return false; // sem resposta -> assume normal
}

// Liga o buzzer, conforme o TIPO_BUZZER escolhido no topo
void buzzerLigar() {
#if TIPO_BUZZER == 0
  tone(BUZZER_PIN, 1000); // 1000 Hz
#elif TIPO_BUZZER == 1
  digitalWrite(BUZZER_PIN, HIGH);
#else
  digitalWrite(BUZZER_PIN, LOW);
#endif
}

// Desliga o buzzer (silêncio)
void buzzerDesligar() {
#if TIPO_BUZZER == 0
  noTone(BUZZER_PIN);
#elif TIPO_BUZZER == 1
  digitalWrite(BUZZER_PIN, LOW);
#else
  digitalWrite(BUZZER_PIN, HIGH);
#endif
}

// Liga ou desliga o buzzer conforme o passo atual. O LED fica ligado todo o
// SOS.
void aplicarPasso() {
  if (passo % 2 == 0) {
    buzzerLigar();
  } else {
    buzzerDesligar();
  }
  digitalWrite(LED_PIN, HIGH);
  inicioPasso = millis();
}

// Começa um SOS completo (se já estiver a tocar, não faz nada)
void sosIniciar() {
  if (sosAtivo)
    return;
  sosAtivo = true;
  passo = 0;
  aplicarPasso();
}

// Chamar em todos os loops: passa ao passo seguinte quando o tempo acaba
void sosAtualizar() {
  if (!sosAtivo)
    return;

  if (millis() - inicioPasso >= PADRAO[passo]) {
    passo++;
    if (passo >= PADRAO_N) {
      sosParar(); // o SOS completo acabou
      return;
    }
    aplicarPasso();
  }
}

void sosParar() {
  bool estavaAtivo = sosAtivo;
  sosAtivo = false;
  buzzerDesligar();
  digitalWrite(LED_PIN, LOW);
  if (estavaAtivo) {
    tiltRemoto(); // deita fora o que o tilt fez enquanto o SOS tocava
  }
}

// Lê um cartão (se houver) e mostra o UID no monitor série.
// O UID fica guardado em rfid.uid para as outras funções usarem.
bool lerCartao() {
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

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  return true;
}

// Devolve true uma vez por cada vez que se carrega no botão (LOW -> HIGH).
// Ignora mudanças a menos de TEMPO_DEBOUNCE ms da anterior: ao carregar, o
// contacto "salta" e sem isto um clique podia contar como dois.
bool botaoCarregado() {
  bool agora = digitalRead(BOTAO_PIN);
  if (agora != botaoAntes && millis() - ultimaMudanca >= TEMPO_DEBOUNCE) {
    ultimaMudanca = millis();
    botaoAntes = agora;
    return agora == HIGH;
  }
  return false;
}

// Passa para o modo de registar ou de remover e avisa o Arduino 2
void entrarModo(byte novoModo) {
  modo = novoModo;
  inicioModo = millis();
  if (novoModo == MODO_REGISTAR) {
    Serial.println("Modo registo - passe o cartao novo");
    enviar('R'); // R = à espera de cartão para registar
  } else {
    Serial.println("Modo remover - passe o cartao a remover");
    enviar('D'); // D = à espera de cartão para remover
  }
}

// Procura o cartão lido na lista: devolve a posição, ou -1 se não estiver lá
int indiceCartao() {
  for (byte n = 0; n < NUM_UIDS; n++) {
    if (rfid.uid.size == 4 &&
        memcmp(rfid.uid.uidByte, AUTHORIZED_UID[n], 4) == 0) {
      return n;
    }
  }
  return -1;
}

// Compara o cartão lido com cada cartão da lista
bool cartaoConhecido() {
  return indiceCartao() >= 0;
}

// Acrescenta o cartão lido à lista de autorizados
void registarCartao() {
  if (rfid.uid.size != 4) {
    Serial.println("Cartao nao suportado (UID nao tem 4 bytes)");
    enviar('X'); // X = cartão não suportado
  } else if (cartaoConhecido()) {
    Serial.println("Cartao ja estava na lista");
    enviar('J'); // J = já existe
  } else if (NUM_UIDS >= MAX_UIDS) {
    Serial.println("Lista cheia");
    enviar('C'); // C = lista cheia
  } else {
    memcpy(AUTHORIZED_UID[NUM_UIDS], rfid.uid.uidByte, 4);
    NUM_UIDS++;
    Serial.print("Cartao adicionado. Total: ");
    Serial.println(NUM_UIDS);
    enviar('N'); // N = cartão novo adicionado
  }
}

// Tira o cartão lido da lista de autorizados
void removerCartao() {
  int i = indiceCartao();
  if (i < 0) {
    Serial.println("Cartao nao estava na lista");
    enviar('U'); // U = cartão desconhecido
  } else if (NUM_UIDS <= 1) {
    // sem nenhum cartão ninguém voltava a abrir o cofre
    Serial.println("Nao se pode remover o ultimo cartao");
    enviar('L'); // L = último cartão
  } else {
    // puxa os cartões seguintes uma posição para trás, para não ficar um buraco
    for (byte n = i; n < NUM_UIDS - 1; n++) {
      memcpy(AUTHORIZED_UID[n], AUTHORIZED_UID[n + 1], 4);
    }
    NUM_UIDS--;
    Serial.print("Cartao removido. Total: ");
    Serial.println(NUM_UIDS);
    enviar('M'); // M = cartão removido
  }
}

bool card_check() {
  if (!lerCartao()) {
    return false;
  }

  bool match = cartaoConhecido();

  if (!match) {
    if (aberto) {
      // Com o cofre aberto, um cartão errado não dispara nada
      // (nem buzzer, nem LED, nem alarme no LCD)
      Serial.println("Access denied (cofre aberto, sem alarme)");
    } else {
      Serial.println("Access denied");
      enviar('E');  // E = cartão errado
      sosIniciar(); // buzzer faz SOS (se já estiver a tocar, continua)
    }
  }
  return match;
}

