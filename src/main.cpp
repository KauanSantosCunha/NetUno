// #include <SPI.h>
// #include <Ethernet.h>
// #include <utility/w5100.h>

// // ----- Configurações de Rede -----
// byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};
// IPAddress ip(192, 168, 20, 177);
// IPAddress dns(8, 8, 8, 8);
// IPAddress gateway(192, 168, 20, 2);
// IPAddress subnet(255, 255, 255, 0);

// // Servidor Zabbix
// IPAddress zabbixServer(192, 168, 0, 76);
// const uint16_t zabbixPort = 10051;
// const char* hostname = "Maquina_Injetora";

// // ----- Pinos -----
// const int pinoSinal = 4;
// const int pinoRele  = 3;
// const int ledPin    = LED_BUILTIN;

// EthernetClient client;
// unsigned long ciclos = 0;
// bool ultimoEstado = HIGH;

// // ------------------- Função com retry (tenta até 3 vezes) -------------------
// bool send_trapper_with_retry(const char* key, unsigned long value, int maxAttempts = 3) {
//   for (int attempt = 1; attempt <= maxAttempts; attempt++) {
//     char strVal[20];
//     sprintf(strVal, "%lu", value);

//     String json = "{";
//     json += "\"request\":\"sender data\",";
//     json += "\"data\":[{";
//     json += "\"host\":\"" + String(hostname) + "\",";
//     json += "\"key\":\"" + String(key) + "\",";
//     json += "\"value\":\"" + String(strVal) + "\"}";
//     json += "]}";

//     const char header[] = "ZBXD\x01";
//     uint64_t len = json.length();
//     uint8_t packet[13 + len];
//     memcpy(packet, header, 5);
//     for (int i = 0; i < 8; i++) packet[5 + i] = (len >> (8 * i)) & 0xFF;
//     memcpy(packet + 13, json.c_str(), len);

//     if (client.connect(zabbixServer, zabbixPort)) {
//       client.write(packet, sizeof(packet));
//       unsigned long start = millis();
//       while (client.connected() && millis() - start < 500) {
//         if (client.available()) {
//           char c = client.read();
//           Serial.print(c);
//         }
//       }
//       client.stop();
//       Serial.println();
//       Serial.print(key);
//       Serial.print("=");
//       Serial.println(strVal);
//       return true;  // sucesso
//     } else {
//       Serial.print("Falha (tentativa ");
//       Serial.print(attempt);
//       Serial.print(") ao enviar ");
//       Serial.println(key);
//       if (attempt < maxAttempts) delay(1000); // espera antes de tentar novamente
//     }
//   }
//   return false; // todas as tentativas falharam
// }

// // ------------------- Inicialização com retry -------------------
// void iniciar_maquina() {
//   Serial.println("Inicializando máquina...");
//   send_trapper_with_retry("motor_ligado", 1);
//   delay(200);
//   send_trapper_with_retry("modo_auto", 1);
//   delay(200);
//   send_trapper_with_retry("modo_semi", 0);
//   delay(200);
//   send_trapper_with_retry("modo_manual", 0);
//   delay(200);
//   ciclos = 0;
//   send_trapper_with_retry("ciclos_completos", ciclos);
// }

// // ------------------- Setup -------------------
// void setup() {
//   Serial.begin(9600);
//   while (!Serial);
//   Serial.println("Iniciando Ethernet W5100...");

//   pinMode(pinoSinal, INPUT_PULLUP);
//   pinMode(pinoRele, OUTPUT);
//   pinMode(10, OUTPUT);
//   digitalWrite(pinoRele, LOW);
//   digitalWrite(10, HIGH);

//   SPI.begin();
//   Ethernet.init(10);
//   // Ethernet.begin(mac, ip, dns, gateway, subnet);
//   Ethernet.begin(mac);

//   Serial.print("IP do Arduino: ");
//   Serial.println(Ethernet.localIP());
//   Serial.print("Gateway: ");
//   Serial.println(Ethernet.gatewayIP());
//   Serial.print("Subnet: ");
//   Serial.println(Ethernet.subnetMask());

//   // Aguarda estabilização da rede (importante!)
//   delay(5000);

//   // Configura timeout do W5100 (já ok)
//   W5100.setRetransmissionTime(0x07D0);
//   W5100.setRetransmissionCount(3);

//   iniciar_maquina();

//   Serial.println("Aguardando sinal no pino 4 (LOW ativa)...");
// }

// // ------------------- Loop (inalterado, só troquei send_trapper pela versão com retry) -------------------
// void loop() {
//   Ethernet.maintain();

//   static unsigned long lastBlink = 0;
//   if (millis() - lastBlink > 2000) {
//     digitalWrite(ledPin, !digitalRead(ledPin));
//     lastBlink = millis();
//   }

//   bool estadoAtual = digitalRead(pinoSinal);
//   if (estadoAtual != ultimoEstado) {
//     delay(50);
//     estadoAtual = digitalRead(pinoSinal);
//     if (estadoAtual == LOW) {
//       ciclos++;
//       Serial.print("Ciclo detectado #");
//       Serial.println(ciclos);
//       digitalWrite(pinoRele, HIGH);
//       delay(1000);
//       digitalWrite(pinoRele, LOW);
//       send_trapper_with_retry("ciclos_completos", ciclos, 1); // 1 tentativa para ciclos (já funcionava)
//     }
//     ultimoEstado = estadoAtual;
//   }
// }


#include <SPI.h>
#include <Ethernet.h>
#include <utility/w5100.h>

// ----- Configurações de Rede (IP fixo para fallback) -----
byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};
IPAddress ip(192, 168, 20, 177);      // IP fixo (fallback)
IPAddress dns(8, 8, 8, 8);
IPAddress gateway(192, 168, 20, 2);
IPAddress subnet(255, 255, 255, 0);

// Servidor Zabbix
IPAddress zabbixServer(192, 168, 0, 76);
const uint16_t zabbixPort = 10051;
const char* hostname = "Maquina_Injetora";

// ----- Pinos -----
const int pinoSinal      = 4;   // sensor de ciclo (já existente)
const int pinoMotor      = 5;   // motor on/off
const int pinoManual     = 6;   // modo manual
const int pinoSemi       = 7;   // modo semiautomático
const int pinoAuto       = 8;   // modo automático
const int pinoRele       = 3;
const int ledPin         = LED_BUILTIN;

EthernetClient client;
unsigned long ciclos = 0;

// Estados anteriores (para detectar mudanças)
bool ultimoEstadoSinal = HIGH;
bool ultimoEstadoMotor = HIGH;
bool ultimoEstadoManual = HIGH;
bool ultimoEstadoSemi = HIGH;
bool ultimoEstadoAuto = HIGH;

// ------------------- Função com retry (igual) -------------------
bool send_trapper_with_retry(const char* key, unsigned long value, int maxAttempts = 3) {
  for (int attempt = 1; attempt <= maxAttempts; attempt++) {
    char strVal[20];
    sprintf(strVal, "%lu", value);

    String json = "{";
    json += "\"request\":\"sender data\",";
    json += "\"data\":[{";
    json += "\"host\":\"" + String(hostname) + "\",";
    json += "\"key\":\"" + String(key) + "\",";
    json += "\"value\":\"" + String(strVal) + "\"}";
    json += "]}";

    const char header[] = "ZBXD\x01";
    uint64_t len = json.length();
    uint8_t packet[13 + len];
    memcpy(packet, header, 5);
    for (int i = 0; i < 8; i++) packet[5 + i] = (len >> (8 * i)) & 0xFF;
    memcpy(packet + 13, json.c_str(), len);

    if (client.connect(zabbixServer, zabbixPort)) {
      client.write(packet, sizeof(packet));
      unsigned long start = millis();
      while (client.connected() && millis() - start < 500) {
        if (client.available()) {
          char c = client.read();
          Serial.print(c);
        }
      }
      client.stop();
      Serial.println();
      Serial.print(key);
      Serial.print("=");
      Serial.println(strVal);
      return true;
    } else {
      Serial.print("Falha (tentativa ");
      Serial.print(attempt);
      Serial.print(") ao enviar ");
      Serial.println(key);
      if (attempt < maxAttempts) delay(1000);
    }
  }
  return false;
}

// ------------------- Inicialização da máquina (envia estado inicial dos modos) -------------------
void iniciar_maquina() {
  Serial.println("Inicializando máquina...");
  send_trapper_with_retry("motor_ligado", 1);        // assumindo que já está ligado
  send_trapper_with_retry("modo_auto", 1);
  send_trapper_with_retry("modo_semi", 0);
  send_trapper_with_retry("modo_manual", 0);
  delay(200);
  ciclos = 0;
  send_trapper_with_retry("ciclos_completos", ciclos);
}

// ------------------- Função para ler e enviar mudanças de um pino -------------------
void verificarEEnviar(int pino, const char* key, bool &ultimoEstado) {
  bool leitura = digitalRead(pino);
  if (leitura != ultimoEstado) {
    unsigned long inicio = millis();
    bool estavel = false;
    while (millis() - inicio < 100) {  // espera 100ms estabilizando
      if (digitalRead(pino) != leitura) {
        // mudou durante a espera, reinicia
        inicio = millis();
        leitura = digitalRead(pino);
      }
    }
    // após 100ms estável, considera a mudança
    if (leitura != ultimoEstado) {
      unsigned long valor = (leitura == LOW) ? 1 : 0;
      send_trapper_with_retry(key, valor, 1);
      ultimoEstado = leitura;
    }
  }
}
// ------------------- Setup -------------------
void setup() {
  Serial.begin(9600);
  while (!Serial);
  Serial.println("Iniciando Ethernet W5100...");

  pinMode(pinoSinal, INPUT_PULLUP);
  pinMode(pinoMotor, INPUT_PULLUP);
  pinMode(pinoManual, INPUT_PULLUP);
  pinMode(pinoSemi, INPUT_PULLUP);
  pinMode(pinoAuto, INPUT_PULLUP);
  pinMode(pinoRele, OUTPUT);
  pinMode(10, OUTPUT);
  digitalWrite(pinoRele, HIGH); // relé desligado (ativo LOW)
  digitalWrite(10, HIGH);

  SPI.begin();
  Ethernet.init(10);

  Serial.print("Tentando DHCP... ");
  if (Ethernet.begin(mac) == 0) {
    Serial.println("falhou. Usando IP fixo.");
    Ethernet.begin(mac, ip, dns, gateway, subnet);
  } else {
    Serial.println("sucesso!");
  }

  Serial.print("IP do Arduino: ");
  Serial.println(Ethernet.localIP());
  Serial.print("Gateway: ");
  Serial.println(Ethernet.gatewayIP());
  Serial.print("Subnet: ");
  Serial.println(Ethernet.subnetMask());

  delay(5000);

  W5100.setRetransmissionTime(0x07D0);
  W5100.setRetransmissionCount(3);

  // Lê os estados iniciais e armazena nos ultimoEstado
  ultimoEstadoMotor = digitalRead(pinoMotor);
  ultimoEstadoManual = digitalRead(pinoManual);
  ultimoEstadoSemi = digitalRead(pinoSemi);
  ultimoEstadoAuto = digitalRead(pinoAuto);
  ultimoEstadoSinal = digitalRead(pinoSinal);

  iniciar_maquina();

  Serial.println("Aguardando sinais nos pinos 4(ciclo),5(motor),6(manual),7(semi),8(auto)...");
}

// ------------------- Loop -------------------
void loop() {
  Ethernet.maintain();

  static unsigned long lastBlink = 0;
  if (millis() - lastBlink > 2000) {
    digitalWrite(ledPin, !digitalRead(ledPin));
    lastBlink = millis();
  }

  // 1. Verifica o pino de ciclo (sinal) – igual antes, com acionamento do relé
  bool estadoSinal = digitalRead(pinoSinal);
  if (estadoSinal != ultimoEstadoSinal) {
    delay(50);
    estadoSinal = digitalRead(pinoSinal);
    if (estadoSinal == LOW) {
      ciclos++;
      Serial.print("Ciclo detectado #");
      Serial.println(ciclos);
      digitalWrite(pinoRele, LOW);
      delay(1000);
      digitalWrite(pinoRele, HIGH);
      // send_trapper_with_retry("ciclos_completos", ciclos, 1);
    }
    ultimoEstadoSinal = estadoSinal;
  }

  // 2. Verifica as 4 entradas do CLP e envia mudanças
  verificarEEnviar(pinoMotor, "motor_ligado", ultimoEstadoMotor);
  verificarEEnviar(pinoManual, "modo_manual", ultimoEstadoManual);
  verificarEEnviar(pinoSemi, "modo_semi", ultimoEstadoSemi);
  verificarEEnviar(pinoAuto, "modo_auto", ultimoEstadoAuto);
}