/*
  ESP32 Domótica HÍBRIDO (Wi-Fi STA + HTTP + mDNS + Bluetooth SPP)
  - Control de 12 luminarias + puerta eléctrica con sensor magnético
  - Endpoints HTTP:
      /light?zona=<nombre>&state=<on|off>
      /door?cmd=open | closed
      /status (retorna JSON)
  - Comandos Bluetooth:
      DOOR_OPEN / STATUS? / WIFI:<SSID>|<PASSWORD>
*/

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <BluetoothSerial.h>

// =====================
// CONFIGURACIÓN WiFi y mDNS
// =====================
const char* STA_SSID  = "MKINF-003-2G";
const char* STA_PASS  = "**L4bH2##";
const char* MDNS_NAME = "casa";  // http://casa.local

// =====================
// BLUETOOTH
// =====================
BluetoothSerial BT;
const char* BT_NAME = "ESP32_DOMOTICA";

// =====================
WebServer server(80);

// =====================
int RELAY_ACTIVE_LEVEL   = LOW;
int RELAY_INACTIVE_LEVEL = HIGH;

// =====================
// ESTRUCTURA DE LUCES
// =====================
struct ZonaLuz {
  const char* nombre;
  int pin;
  bool estado;
};

ZonaLuz luces[] = {
  { "comedor", 23, false },
  { "cuarto1", 22, false },
  { "cuarto2", 21, false },
  { "cuarto3", 19, false },
  { "cuarto4", 18, false },
  { "cuarto5", 17, false },
  { "sala",    16, false },
  { "entrada", 27, false },
  { "bano",    26, false },
  { "admin",   25, false },
  { "pasillo", 33, false },
  { "bano2",   14, false }
};
const int NUM_LUCES = sizeof(luces) / sizeof(luces[0]);

// =====================
// PUERTA Y SENSOR
// =====================
const int PIN_PUERTA = 13;           // Relé de la cerradura
const int PIN_SENSOR_PUERTA = 32;    // Sensor magnético (reed)
const unsigned long PULSO_PUERTA_MS = 500;

bool puertaEnPulso = false;
unsigned long puertaPulsoTermina = 0;
bool puertaAbierta = false;

// Buffer Bluetooth
String btIn;

// =====================
// FUNCIONES BÁSICAS
// =====================
int buscarZonaPorNombre(const String& zona) {
  for (int i = 0; i < NUM_LUCES; i++)
    if (zona.equalsIgnoreCase(luces[i].nombre)) return i;
  return -1;
}

void setLuzEstado(int idx, bool encender) {
  if (idx < 0 || idx >= NUM_LUCES) return;
  luces[idx].estado = encender;
  digitalWrite(luces[idx].pin, encender ? RELAY_ACTIVE_LEVEL : RELAY_INACTIVE_LEVEL);
}

void abrirPuerta() {
  digitalWrite(PIN_PUERTA, RELAY_ACTIVE_LEVEL);
  puertaEnPulso = true;
  puertaPulsoTermina = millis() + PULSO_PUERTA_MS;
}

// =====================
// SENSOR MAGNÉTICO
// =====================
void leerSensorPuerta() {
  int valor = digitalRead(PIN_SENSOR_PUERTA);
  puertaAbierta = (valor == HIGH);  // HIGH = abierta, LOW = cerrada
}

// =====================
//  HTTP: /status
// =====================
void handleStatus() {
  leerSensorPuerta();
  String json = "{";
  for (int i = 0; i < NUM_LUCES; i++) {
    json += "\"" + String(luces[i].nombre) + "\":\"" + (luces[i].estado ? "on" : "off") + "\",";
  }
  json += "\"puerta\":\"" + String(puertaAbierta ? "ABIERTA" : "CERRADA") + "\"}";
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

// =====================
void handleLight() {
  if (!server.hasArg("zona") || !server.hasArg("state")) {
    server.send(400, "text/plain", "FALTAN PARAMETROS");
    return;
  }
  String zona = server.arg("zona");
  String state = server.arg("state");

  int idx = buscarZonaPorNombre(zona);
  if (idx == -1) {
    server.send(404, "text/plain", "ZONA NO ENCONTRADA");
    return;
  }

  setLuzEstado(idx, state.equalsIgnoreCase("on"));
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/plain", "OK");
}

void handleDoor() {
  if (!server.hasArg("cmd")) {
    server.send(400, "text/plain", "FALTA cmd");
    return;
  }

  String cmd = server.arg("cmd");
  if (cmd.equalsIgnoreCase("open")) abrirPuerta();

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/plain", "OK");
}

// =====================
// Bluetooth parser
// =====================
void handleBtCommand(const String& raw) {
  String c = raw; c.trim();

  if (c.startsWith("WIFI:")) {
    int sep = c.indexOf('|');
    if (sep > 0) {
      String ssid = c.substring(5, sep);
      String pass = c.substring(sep + 1);
      BT.println("⚙️ Nueva red: " + ssid);
      WiFi.disconnect();
      WiFi.begin(ssid.c_str(), pass.c_str());
      unsigned long start = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
        delay(500);
        BT.print(".");
      }
      if (WiFi.status() == WL_CONNECTED)
        BT.println("\n✅ WiFi conectado. IP: " + WiFi.localIP().toString());
      else
        BT.println("\n❌ Falló la conexión WiFi");
    }
    return;
  }

  if (c == "door_open") { abrirPuerta(); BT.println("OK"); return; }
  if (c == "status?") {
    leerSensorPuerta();
    BT.println("{\"puerta\":\"" + String(puertaAbierta ? "ABIERTA" : "CERRADA") + "\"}");
    return;
  }
}

// =====================
void wifiConnect() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(STA_SSID, STA_PASS);
  Serial.print("Conectando a WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println("\nWiFi conectado.");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  if (MDNS.begin(MDNS_NAME)) {
    Serial.print("mDNS listo: http://");
    Serial.print(MDNS_NAME);
    Serial.println(".local");
  }
  BT.println("== Iniciando Domótica HÍBRIDO ==");
  BT.println("WiFi conectado.");
  BT.println("IP: " + WiFi.localIP().toString());
  BT.println("HTTP listo en puerto 80.");
}

// =====================
void setup() {
  Serial.begin(115200);
  Serial.println("\n== Iniciando Domótica HÍBRIDO ==");

  for (int i = 0; i < NUM_LUCES; i++) {
    pinMode(luces[i].pin, OUTPUT);
    digitalWrite(luces[i].pin, RELAY_INACTIVE_LEVEL);
  }

  pinMode(PIN_PUERTA, OUTPUT);
  digitalWrite(PIN_PUERTA, RELAY_INACTIVE_LEVEL);

  pinMode(PIN_SENSOR_PUERTA, INPUT_PULLUP);

  BT.begin(BT_NAME);
  Serial.println("Bluetooth listo para emparejar.");
  delay(500);

  wifiConnect();

  server.on("/light", handleLight);
  server.on("/door", handleDoor);
  server.on("/status", handleStatus);

  server.on("/allon", []() {
    for (int i = 0; i < NUM_LUCES; i++) setLuzEstado(i, true);
    server.send(200, "text/plain", "OK - TODAS ENCENDIDAS");
  });

  server.on("/alloff", []() {
    for (int i = 0; i < NUM_LUCES; i++) setLuzEstado(i, false);
    server.send(200, "text/plain", "OK - TODAS APAGADAS");
  });

  server.begin();
  Serial.println("HTTP listo en puerto 80.");
}

// =====================
void loop() {
  server.handleClient();

  leerSensorPuerta();

  if (puertaEnPulso && millis() >= puertaPulsoTermina) {
    digitalWrite(PIN_PUERTA, RELAY_INACTIVE_LEVEL);
    puertaEnPulso = false;
  }

  while (BT.available()) {
    char ch = (char)BT.read();
    if (ch == '\n' || ch == '\r') {
      if (btIn.length()) {
        handleBtCommand(btIn);
        btIn = "";
      }
    } else {
      btIn += ch;
    }
  }
}












































































