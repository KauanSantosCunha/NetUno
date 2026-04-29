#include <SPI.h>
#include <Ethernet.h>
#include <utility/w5100.h>

// ----- Configurações de Rede -----
byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};

// IP fixo para fallback (usado se DHCP falhar ou se não conseguir recuperar)
IPAddress ip_fallback(192, 168, 20, 177);
IPAddress meudns(8, 8, 8, 8);
IPAddress gateway_fallback(192, 168, 20, 2);
IPAddress subnet(255, 255, 255, 0);

// Servidor Zabbix
IPAddress zabbixServer(192, 168, 0, 76);
const uint16_t zabbixPort = 10051;
const char* hostname = "Maquina_Injetora";

// ----- Pinos -----
const int pinoSinal      = 4;
const int pinoMotor      = 5;
const int pinoManual     = 6;
const int pinoSemi       = 7;
const int pinoAuto       = 8;
const int pinoRele       = 3;
const int ledPin         = LED_BUILTIN;

EthernetClient client;
unsigned long ciclos = 0;

// Estados anteriores
bool ultimoEstadoSinal = HIGH;
bool ultimoEstadoMotor = HIGH;
bool ultimoEstadoManual = HIGH;
bool ultimoEstadoSemi = HIGH;
bool ultimoEstadoAuto = HIGH;

// ========== FUNÇÕES AUXILIARES ==========

// Envia com retry (igual ao seu original)
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

void iniciar_maquina() {
  Serial.println("Inicializando máquina...");
  send_trapper_with_retry("motor_ligado", 1);
  delay(200);
  send_trapper_with_retry("modo_auto", 1);
  delay(200);
  send_trapper_with_retry("modo_semi", 0);
  delay(200);
  send_trapper_with_retry("modo_manual", 0);
  delay(200);
  ciclos = 0;
  send_trapper_with_retry("ciclos_completos", ciclos);
}

// Função com debounce robusto (já funcionando)
void verificarEEnviar(int pino, const char* key, bool &ultimoEstado) {
  bool leitura = digitalRead(pino);
  if (leitura != ultimoEstado) {
    unsigned long inicio = millis();
    while (millis() - inicio < 100) {
      if (digitalRead(pino) != leitura) {
        inicio = millis();
        leitura = digitalRead(pino);
      }
    }
    if (leitura != ultimoEstado) {
      unsigned long valor = (leitura == LOW) ? 1 : 0;
      send_trapper_with_retry(key, valor, 1);
      ultimoEstado = leitura;
    }
  }
}

// ========== CONFIGURAÇÃO DE REDE COM PERSISTÊNCIA ==========

// Variável global para guardar o último IP obtido (usado na recuperação)
IPAddress ultimoIP;

bool configurarRede() {
  Serial.println("Tentando obter IP via DHCP...");
  if (Ethernet.begin(mac) == 0) {
    Serial.println("DHCP falhou. Usando IP fixo.");
    Ethernet.begin(mac, ip_fallback, meudns, gateway_fallback, subnet);
    ultimoIP = Ethernet.localIP();
    return true;
  } else {
    ultimoIP = Ethernet.localIP();
    Serial.print("DHCP OK. IP: ");
    Serial.println(ultimoIP);
    return true;
  }
}

// Força o uso do último IP conhecido (tentativa de reconexão com o mesmo IP)
bool reconectarComMesmoIP() {
  Serial.print("Recuperando IP salvo: ");
  Serial.println(ultimoIP);
  // Usamos o IP salvo como fixo (mantém gateway e máscara originais, ou usa os de fallback)
  Ethernet.begin(mac, ultimoIP, meudns, gateway_fallback, subnet);
  delay(2000);
  if (Ethernet.localIP() == ultimoIP) {
    Serial.println("Reconexão bem-sucedida com o mesmo IP.");
    return true;
  } else {
    Serial.println("Não foi possível manter o IP, tentando DHCP novamente...");
    return false;
  }
}

// ========== SETUP ==========
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
  digitalWrite(pinoRele, LOW);
  digitalWrite(10, HIGH);

  SPI.begin();
  Ethernet.init(10);
  delay(1000);

  configurarRede();

  Serial.print("IP: "); Serial.println(Ethernet.localIP());
  Serial.print("Gateway: "); Serial.println(Ethernet.gatewayIP());
  Serial.print("Subnet: "); Serial.println(Ethernet.subnetMask());

  delay(5000); // estabilização

  W5100.setRetransmissionTime(0x07D0);
  W5100.setRetransmissionCount(3);

  // Estados iniciais
  ultimoEstadoMotor = digitalRead(pinoMotor);
  ultimoEstadoManual = digitalRead(pinoManual);
  ultimoEstadoSemi = digitalRead(pinoSemi);
  ultimoEstadoAuto = digitalRead(pinoAuto);
  ultimoEstadoSinal = digitalRead(pinoSinal);

  iniciar_maquina();

  Serial.println("Aguardando sinais nos pinos 4,5,6,7,8...");
}

// ========== LOOP ==========
void loop() {
  static unsigned long lastBlink = 0;
  static unsigned long lastCheck = 0;

  // Verifica link a cada 3 segundos
  if (millis() - lastCheck > 3000) {
    lastCheck = millis();
    if (Ethernet.linkStatus() == LinkOFF) {
      Serial.println("Link de rede caiu! Tentando reconectar...");
      int tentativas = 0;
      while (tentativas < 5 && Ethernet.linkStatus() == LinkOFF) {
        delay(2000);
        tentativas++;
        Serial.print("Tentativa "); Serial.print(tentativas); Serial.println(" de reconexão...");
      }
      if (Ethernet.linkStatus() == LinkON) {
        Serial.println("Link restabelecido. Tentando recuperar o IP anterior...");
        if (reconectarComMesmoIP()) {
          Serial.println("IP recuperado com sucesso.");
        } else {
          Serial.println("Falha na recuperação. Obtendo novo IP...");
          configurarRede();
        }
      } else {
        Serial.println("Link continua inativo. Verifique o cabo.");
      }
    }
  }

  // Pisca LED a cada 2s
  if (millis() - lastBlink > 2000) {
    digitalWrite(ledPin, !digitalRead(ledPin));
    lastBlink = millis();
  }

  // Leitura do ciclo (pino 4)
  bool estadoSinal = digitalRead(pinoSinal);
  if (estadoSinal != ultimoEstadoSinal) {
    delay(50);
    estadoSinal = digitalRead(pinoSinal);
    if (estadoSinal == LOW) {
      ciclos++;
      Serial.print("Ciclo detectado #");
      Serial.println(ciclos);
      digitalWrite(pinoRele, HIGH);
      delay(1000);
      digitalWrite(pinoRele, LOW);
      send_trapper_with_retry("ciclos_completos", ciclos, 1);
    }
    ultimoEstadoSinal = estadoSinal;
  }

  // Leitura das outras entradas
  verificarEEnviar(pinoMotor, "motor_ligado", ultimoEstadoMotor);
  verificarEEnviar(pinoManual, "modo_manual", ultimoEstadoManual);
  verificarEEnviar(pinoSemi, "modo_semi", ultimoEstadoSemi);
  verificarEEnviar(pinoAuto, "modo_auto", ultimoEstadoAuto);

  Ethernet.maintain(); // renova DHCP se necessário
}


// #include <SPI.h>
// #include <Ethernet.h>
// #include <utility/w5100.h>

// // ----- Configurações de Rede -----
// byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};

// // IP fixo para fallback
// IPAddress ip_fallback(192, 168, 20, 177);
// IPAddress meudns(8, 8, 8, 8);
// IPAddress gateway_fallback(192, 168, 20, 2);
// IPAddress subnet(255, 255, 255, 0);

// // Servidor Zabbix
// IPAddress zabbixServer(192, 168, 0, 76);
// const uint16_t zabbixPort = 10051;
// const char* hostname = "Maquina_Injetora";

// // Servidor do agente Python (Flask)
// const char* agentHost = "192.168.20.4";
// const uint16_t agentPort = 5000;
// const char* agentToken = "senha_super_secreta";
// const char* clientId = "Arduino_Injetora";

// // ----- Pinos -----
// const int pinoSinal      = 4;
// const int pinoMotor      = 5;
// const int pinoManual     = 6;
// const int pinoSemi       = 7;
// const int pinoAuto       = 8;
// const int pinoRele       = 3;
// const int ledPin         = LED_BUILTIN;

// EthernetClient clientZabbix;
// EthernetClient clientAgent;
// unsigned long ciclos = 0;

// // Estados anteriores
// bool ultimoEstadoSinal = HIGH;
// bool ultimoEstadoMotor = HIGH;
// bool ultimoEstadoManual = HIGH;
// bool ultimoEstadoSemi = HIGH;
// bool ultimoEstadoAuto = HIGH;

// // ========== ZABBIX (original) ==========
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

//     if (clientZabbix.connect(zabbixServer, zabbixPort)) {
//       clientZabbix.write(packet, sizeof(packet));
//       unsigned long start = millis();
//       while (clientZabbix.connected() && millis() - start < 500) {
//         if (clientZabbix.available()) {
//           char c = clientZabbix.read();
//           Serial.print(c);
//         }
//       }
//       clientZabbix.stop();
//       Serial.println();
//       Serial.print(key);
//       Serial.print("=");
//       Serial.println(strVal);
//       return true;
//     } else {
//       Serial.print("Falha (tentativa ");
//       Serial.print(attempt);
//       Serial.print(") ao enviar ");
//       Serial.println(key);
//       if (attempt < maxAttempts) delay(1000);
//     }
//   }
//   return false;
// }

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

// // ========== SERVIDOR AGENTE (Flask) ==========
// bool enviar_status_agent(const String& status, const String& msg, const String& output = "", const String& error = "") {
//   String json = "{";
//   json += "\"client_id\":\"" + String(clientId) + "\",";
//   json += "\"status\":\"" + status + "\",";
//   json += "\"message\":\"" + msg + "\",";
//   json += "\"output\":\"" + output + "\",";
//   json += "\"error\":\"" + error + "\"";
//   json += "}";

//   if (clientAgent.connect(agentHost, agentPort)) {
//     clientAgent.println("POST /report_status HTTP/1.1");
//     clientAgent.print("Host: ");
//     clientAgent.println(agentHost);
//     clientAgent.println("Content-Type: application/json");
//     clientAgent.print("Authorization: Bearer ");
//     clientAgent.println(agentToken);
//     clientAgent.print("Content-Length: ");
//     clientAgent.println(json.length());
//     clientAgent.println();
//     clientAgent.println(json);

//     unsigned long start = millis();
//     while (clientAgent.connected() && millis() - start < 2000) {
//       if (clientAgent.available()) {
//         char c = clientAgent.read();
//         Serial.print(c);
//       }
//     }
//     clientAgent.stop();
//     Serial.println();
//     return true;
//   } else {
//     Serial.println("Erro ao conectar ao servidor agente");
//     return false;
//   }
// }

// String obter_comando_agent() {
//   if (!clientAgent.connect(agentHost, agentPort)) {
//     Serial.println("Falha ao conectar para obter comando");
//     return "";
//   }

//   String request = "GET /get_command?client_id=" + String(clientId) + " HTTP/1.1\r\n";
//   request += "Host: " + String(agentHost) + "\r\n";
//   request += "Authorization: Bearer " + String(agentToken) + "\r\n";
//   request += "Connection: close\r\n\r\n";
//   clientAgent.print(request);

//   unsigned long start = millis();
//   String resposta = "";
//   bool bodyStarted = false;
//   while (clientAgent.connected() && millis() - start < 3000) {
//     if (clientAgent.available()) {
//       char c = clientAgent.read();
//       if (!bodyStarted) {
//         resposta += c;
//         if (resposta.endsWith("\r\n\r\n")) bodyStarted = true;
//       } else {
//         resposta += c;
//       }
//     }
//   }
//   clientAgent.stop();

//   int jsonStart = resposta.indexOf('{');
//   if (jsonStart == -1) return "";
//   String jsonPart = resposta.substring(jsonStart);
//   int cmdStart = jsonPart.indexOf("\"command\":\"");
//   if (cmdStart == -1) return "";
//   cmdStart += 11;
//   int cmdEnd = jsonPart.indexOf('"', cmdStart);
//   if (cmdEnd == -1) return "";
//   return jsonPart.substring(cmdStart, cmdEnd);
// }

// void executar_comando_agent(const String& cmd) {
//   Serial.print("Comando recebido do agente: ");
//   Serial.println(cmd);

//   if (cmd == "reset_ciclos") {
//     ciclos = 0;
//     send_trapper_with_retry("ciclos_completos", ciclos, 1);
//     enviar_status_agent("executed", "Contador de ciclos resetado");
//   }
//   else if (cmd == "ligar_rele") {
//     digitalWrite(pinoRele, HIGH);
//     enviar_status_agent("executed", "Relé ligado permanentemente");
//   }
//   else if (cmd == "desligar_rele") {
//     digitalWrite(pinoRele, LOW);
//     enviar_status_agent("executed", "Relé desligado");
//   }
//   else if (cmd == "reiniciar") {
//     enviar_status_agent("executed", "Reiniciando Arduino...");
//     delay(500);
//     asm volatile ("jmp 0");
//   }
//   else {
//     enviar_status_agent("failed", "Comando desconhecido", "", cmd);
//   }
// }

// // ========== VERIFICAÇÃO DE PINOS (envia para ambos) ==========
// void verificarEEnviarParaAmbos(int pino, const char* key, bool &ultimoEstado) {
//   bool leitura = digitalRead(pino);
//   if (leitura != ultimoEstado) {
//     unsigned long inicio = millis();
//     while (millis() - inicio < 100) {
//       if (digitalRead(pino) != leitura) {
//         inicio = millis();
//         leitura = digitalRead(pino);
//       }
//     }
//     if (leitura != ultimoEstado) {
//       unsigned long valor = (leitura == LOW) ? 1 : 0;
//       send_trapper_with_retry(key, valor, 1);
//       String msg = String(key) + "=" + String(valor);
//       enviar_status_agent("state_change", msg);
//       ultimoEstado = leitura;
//     }
//   }
// }

// // ========== REDE COM PERSISTÊNCIA ==========
// IPAddress ultimoIP;

// bool configurarRede() {
//   Serial.println("Tentando obter IP via DHCP...");
//   if (Ethernet.begin(mac) == 0) {
//     Serial.println("DHCP falhou. Usando IP fixo.");
//     Ethernet.begin(mac, ip_fallback, meudns, gateway_fallback, subnet);
//     ultimoIP = Ethernet.localIP();
//     return true;
//   } else {
//     ultimoIP = Ethernet.localIP();
//     Serial.print("DHCP OK. IP: ");
//     Serial.println(ultimoIP);
//     return true;
//   }
// }

// bool reconectarComMesmoIP() {
//   Serial.print("Recuperando IP salvo: ");
//   Serial.println(ultimoIP);
//   Ethernet.begin(mac, ultimoIP, meudns, gateway_fallback, subnet);
//   delay(2000);
//   if (Ethernet.localIP() == ultimoIP) {
//     Serial.println("Reconexão bem-sucedida com o mesmo IP.");
//     return true;
//   } else {
//     Serial.println("Não foi possível manter o IP, tentando DHCP novamente...");
//     return false;
//   }
// }

// // ========== SETUP ==========
// void setup() {
//   Serial.begin(9600);
//   while (!Serial);
//   Serial.println("Iniciando Ethernet W5100...");

//   pinMode(pinoSinal, INPUT_PULLUP);
//   pinMode(pinoMotor, INPUT_PULLUP);
//   pinMode(pinoManual, INPUT_PULLUP);
//   pinMode(pinoSemi, INPUT_PULLUP);
//   pinMode(pinoAuto, INPUT_PULLUP);
//   pinMode(pinoRele, OUTPUT);
//   pinMode(10, OUTPUT);
//   digitalWrite(pinoRele, LOW);
//   digitalWrite(10, HIGH);

//   SPI.begin();
//   Ethernet.init(10);
//   delay(1000);

//   configurarRede();

//   Serial.print("IP: "); Serial.println(Ethernet.localIP());
//   Serial.print("Gateway: "); Serial.println(Ethernet.gatewayIP());
//   Serial.print("Subnet: "); Serial.println(Ethernet.subnetMask());

//   delay(5000);

//   W5100.setRetransmissionTime(0x07D0);
//   W5100.setRetransmissionCount(3);

//   // Estados iniciais
//   ultimoEstadoMotor = digitalRead(pinoMotor);
//   ultimoEstadoManual = digitalRead(pinoManual);
//   ultimoEstadoSemi = digitalRead(pinoSemi);
//   ultimoEstadoAuto = digitalRead(pinoAuto);
//   ultimoEstadoSinal = digitalRead(pinoSinal);

//   iniciar_maquina();

//   enviar_status_agent("online", "Arduino inicializado");

//   Serial.println("Aguardando sinais nos pinos 4,5,6,7,8...");
// }

// // ========== LOOP ==========
// void loop() {
//   static unsigned long lastBlink = 0;
//   static unsigned long lastCheckLink = 0;
//   static unsigned long lastHeartbeat = 0;
//   static unsigned long lastCommandCheck = 0;

//   // Verifica link de rede a cada 3s
//   if (millis() - lastCheckLink > 3000) {
//     lastCheckLink = millis();
//     if (Ethernet.linkStatus() == LinkOFF) {
//       Serial.println("Link de rede caiu! Tentando reconectar...");
//       int tentativas = 0;
//       while (tentativas < 5 && Ethernet.linkStatus() == LinkOFF) {
//         delay(2000);
//         tentativas++;
//         Serial.print("Tentativa "); Serial.print(tentativas); Serial.println(" de reconexão...");
//       }
//       if (Ethernet.linkStatus() == LinkON) {
//         Serial.println("Link restabelecido. Tentando recuperar o IP anterior...");
//         if (reconectarComMesmoIP()) {
//           Serial.println("IP recuperado com sucesso.");
//         } else {
//           Serial.println("Falha na recuperação. Obtendo novo IP...");
//           configurarRede();
//         }
//       } else {
//         Serial.println("Link continua inativo. Verifique o cabo.");
//       }
//     }
//   }

//   // Pisca LED a cada 2s (indicação de funcionamento)
//   if (millis() - lastBlink > 2000) {
//     digitalWrite(ledPin, !digitalRead(ledPin));
//     lastBlink = millis();
//   }

//   // Heartbeat a cada 30s para o servidor agente
//   if (millis() - lastHeartbeat > 30000) {
//     lastHeartbeat = millis();
//     enviar_status_agent("online", "Heartbeat");
//   }

//   // Verifica comandos a cada 10s
//   if (millis() - lastCommandCheck > 10000) {
//     lastCommandCheck = millis();
//     String comando = obter_comando_agent();
//     if (comando.length() > 0) {
//       executar_comando_agent(comando);
//     }
//   }

//   // Pino de ciclo (sinal) – aciona relé e envia para Zabbix e agente
//   bool estadoSinal = digitalRead(pinoSinal);
//   if (estadoSinal != ultimoEstadoSinal) {
//     delay(50);
//     estadoSinal = digitalRead(pinoSinal);
//     if (estadoSinal == LOW) {
//       ciclos++;
//       Serial.print("Ciclo detectado #");
//       Serial.println(ciclos);
//       digitalWrite(pinoRele, HIGH);
//       delay(1000);
//       digitalWrite(pinoRele, LOW);
//       send_trapper_with_retry("ciclos_completos", ciclos, 1);
//       char msg[30];
//       sprintf(msg, "Ciclo %lu", ciclos);
//       enviar_status_agent("event", msg);
//     }
//     ultimoEstadoSinal = estadoSinal;
//   }

//   // Verifica as entradas do CLP (motor, modos)
//   verificarEEnviarParaAmbos(pinoMotor,   "motor_ligado", ultimoEstadoMotor);
//   verificarEEnviarParaAmbos(pinoManual,  "modo_manual",  ultimoEstadoManual);
//   verificarEEnviarParaAmbos(pinoSemi,    "modo_semi",    ultimoEstadoSemi);
//   verificarEEnviarParaAmbos(pinoAuto,    "modo_auto",    ultimoEstadoAuto);

//   Ethernet.maintain();
// }

