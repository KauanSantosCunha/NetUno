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
bool ultimoEstadoSinal = LOW;
bool ultimoEstadoMotor = LOW;
bool ultimoEstadoManual = LOW;
bool ultimoEstadoSemi = LOW;
bool ultimoEstadoAuto = LOW;

// ========== FUNÇÕES AUXILIARES ==========

// Envia com retry (igual ao seu original)
bool send_trapper_with_retry(const char* key, unsigned long value, int maxAttempts = 3) {
  for (int attempt = 1; attempt <= maxAttempts; attempt++) {
    char strVal[20];
    if (strcmp(key, "motor_ligado") == 0 ||
        strcmp(key, "modo_manual") == 0 ||
        strcmp(key, "modo_semi") == 0 ||
        strcmp(key, "modo_auto") == 0) {
        if (value == 1)
            strcpy(strVal, "LIGADO");
        else
            strcpy(strVal, "DESLIGADO");
    } else {
        // Para outras chaves (ex: ciclos_completos) mantém número
        sprintf(strVal, "%lu", value);
    }

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
      // unsigned long valor = (leitura == LOW) ? 1 : 0;
      unsigned long valor = (leitura == LOW) ? 0 : 1;   // LOW → 0, HIGH → 1
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

  // iniciar_maquina();

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
        delay(100);
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
    delay(30);
    estadoSinal = digitalRead(pinoSinal);
    if (estadoSinal == LOW) {
      ciclos++;
      Serial.print("Ciclo detectado #");
      Serial.println(ciclos);
      digitalWrite(pinoRele, LOW);
      delay(30);
      digitalWrite(pinoRele, HIGH);
      send_trapper_with_retry("i_producao_hora", 1, 1);  
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

