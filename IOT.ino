#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <WebServer.h>

/* ===================== WIFI / WEB ===================== */
const char* AP_SSID = "CREA_CasaInteligente";
const char* AP_PASS = "CREA_CI123";

WebServer server(80);

/* ===================== PINOUT ESP32 ===================== */
// RC522
static const uint8_t PIN_SS   = 5;    // SDA/SS
static const uint8_t PIN_SCK  = 17;   // SCK
static const uint8_t PIN_MOSI = 16;   // MOSI
static const uint8_t PIN_MISO = 4;    // MISO
static const uint8_t PIN_RST  = 15;   // RST
static const uint8_t PIN_IRQ  = 2;    // IRQ opcional

// Salidas
static const uint8_t PIN_LED_UNKNOWN = 13; // Desconocido
static const uint8_t PIN_LED_KNOWN   = 12; // Conocido
static const uint8_t PIN_BUZZ        = 18; // Buzzer pasivo

/* ===================== OBJETOS ===================== */
MFRC522 rfid(PIN_SS, PIN_RST);

/* ===================== CONFIG ===================== */
const uint16_t RFID_POLL_MS         = 80;
const uint16_t LOST_TIMEOUT_MS      = 1200;
const uint32_t BELL_COOLDOWN_MS     = 5000;
const uint32_t LED_ON_MS            = 1200;
const uint32_t SAME_CARD_IGNORE_MS  = 2500;

const size_t MAX_PEOPLE = 30;
const size_t MAX_LOGS   = 80;

/* ===================== ESTRUCTURAS ===================== */
struct Person {
  String uid;
  String name;
};

struct AccessLog {
  String uid;
  String name;
  String timeText;
  bool known;
};

/* ===================== BASE DE DATOS EN RAM ===================== */
Person people[MAX_PEOPLE];
size_t peopleCount = 0;

AccessLog logsDB[MAX_LOGS];
size_t logsCount = 0;

/* ===================== ESTADO ===================== */
String lastScannedUID = "";
String lastScannedTime = "";

String lastProcessedUID = "";
unsigned long lastProcessedMillis = 0;

unsigned long lastSeenCard = 0;
bool cardPresent = false;

unsigned long lastBellMillis = 0;
unsigned long ledOffAt = 0;

/* ===================== HELPERS ===================== */
String uidToString(const MFRC522::Uid &uid) {
  String s = "";
  for (byte i = 0; i < uid.size; i++) {
    if (uid.uidByte[i] < 0x10) s += "0";
    s += String(uid.uidByte[i], HEX);
  }
  s.toUpperCase();
  return s;
}

String formatUptime(unsigned long ms) {
  unsigned long totalSec = ms / 1000UL;
  unsigned long hh = totalSec / 3600UL;
  unsigned long mm = (totalSec % 3600UL) / 60UL;
  unsigned long ss = totalSec % 60UL;

  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%02lu:%02lu:%02lu", hh, mm, ss);
  return String(buffer);
}

String htmlEscape(String s) {
  s.replace("&", "&amp;");
  s.replace("<", "&lt;");
  s.replace(">", "&gt;");
  s.replace("\"", "&quot;");
  s.replace("'", "&#39;");
  return s;
}

int findPersonIndexByUID(const String &uid) {
  for (size_t i = 0; i < peopleCount; i++) {
    if (people[i].uid.equalsIgnoreCase(uid)) {
      return (int)i;
    }
  }
  return -1;
}

bool isKnownUID(const String &uid) {
  return findPersonIndexByUID(uid) >= 0;
}

String getNameByUID(const String &uid) {
  int idx = findPersonIndexByUID(uid);
  if (idx >= 0) return people[idx].name;
  return "DESCONOCIDO";
}

void addOrUpdatePerson(String uid, String name) {
  uid.trim();
  name.trim();
  uid.toUpperCase();

  int idx = findPersonIndexByUID(uid);
  if (idx >= 0) {
    people[idx].name = name;
    return;
  }

  if (peopleCount < MAX_PEOPLE) {
    people[peopleCount].uid = uid;
    people[peopleCount].name = name;
    peopleCount++;
  }
}

void addLog(const String &uid, const String &name, bool known) {
  AccessLog entry;
  entry.uid = uid;
  entry.name = name;
  entry.timeText = formatUptime(millis());
  entry.known = known;

  if (logsCount < MAX_LOGS) {
    logsDB[logsCount++] = entry;
  } else {
    for (size_t i = 1; i < MAX_LOGS; i++) {
      logsDB[i - 1] = logsDB[i];
    }
    logsDB[MAX_LOGS - 1] = entry;
  }
}

void ledsOff() {
  digitalWrite(PIN_LED_KNOWN, LOW);
  digitalWrite(PIN_LED_UNKNOWN, LOW);
}

void indicateKnown() {
  digitalWrite(PIN_LED_KNOWN, HIGH);
  digitalWrite(PIN_LED_UNKNOWN, LOW);
  ledOffAt = millis() + LED_ON_MS;
}

void indicateUnknown() {
  digitalWrite(PIN_LED_KNOWN, LOW);
  digitalWrite(PIN_LED_UNKNOWN, HIGH);
  ledOffAt = millis() + LED_ON_MS;
}

void updateLEDs() {
  if (ledOffAt != 0 && millis() >= ledOffAt) {
    ledsOff();
    ledOffAt = 0;
  }
}

void ringBellIfAllowed() {
  unsigned long now = millis();
  if (now - lastBellMillis < BELL_COOLDOWN_MS) return;

  // Timbre sencillo tipo casa
  tone(PIN_BUZZ, 988);
  delay(120);
  tone(PIN_BUZZ, 1319);
  delay(150);
  tone(PIN_BUZZ, 1568);
  delay(220);
  noTone(PIN_BUZZ);

  lastBellMillis = millis();
}

bool shouldProcessCard(const String &uid) {
  unsigned long now = millis();

  // Si es la misma tarjeta en una ventana corta, ignorar repetición
  if (uid == lastProcessedUID && (now - lastProcessedMillis) < SAME_CARD_IGNORE_MS) {
    return false;
  }

  lastProcessedUID = uid;
  lastProcessedMillis = now;
  return true;
}

/* ===================== RFID ===================== */
bool readCardOnce() {
  if (!rfid.PICC_IsNewCardPresent()) return false;
  if (!rfid.PICC_ReadCardSerial()) return false;
  return true;
}

void processCard(const String &uid) {
  bool known = isKnownUID(uid);
  String personName = getNameByUID(uid);
  String timeNow = formatUptime(millis());

  lastScannedUID = uid;
  lastScannedTime = timeNow;

  addLog(uid, personName, known);
  ringBellIfAllowed();

  if (known) {
    indicateKnown();

    Serial.println("======================================");
    Serial.println("RFID detectado");
    Serial.println("UID: " + uid);
    Serial.println("Estado: CONOCIDO");
    Serial.println("Persona: " + personName);
    Serial.println("Hora (desde encendido): " + timeNow);
    Serial.println("======================================");
  } else {
    indicateUnknown();

    Serial.println("======================================");
    Serial.println("RFID detectado");
    Serial.println("UID: " + uid);
    Serial.println("Estado: DESCONOCIDO");
    Serial.println("Persona: SIN ASIGNAR");
    Serial.println("Hora (desde encendido): " + timeNow);
    Serial.println("======================================");
  }
}

/* ===================== WEB ===================== */
String buildPage() {
  String html;
  html.reserve(18000);

  html += F(
    "<!DOCTYPE html><html lang='es'><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
    "<meta http-equiv='refresh' content='8'>"
    "<title>CREA Casa Inteligente</title>"
    "<style>"
    "body{margin:0;font-family:Arial,Helvetica,sans-serif;background:#eef3f7;color:#1f3340;}"
    ".wrap{max-width:1100px;margin:0 auto;padding:22px;}"
    ".hero{background:linear-gradient(135deg,#00adc8,#5fd0dc);border-radius:24px;padding:26px;color:white;box-shadow:0 10px 24px rgba(0,0,0,.12);}"
    ".hero h1{margin:0;font-size:32px;}"
    ".hero p{margin:10px 0 0 0;line-height:1.5;opacity:.96;}"
    ".chips{margin-top:14px;display:flex;flex-wrap:wrap;gap:10px;}"
    ".chip{background:rgba(255,255,255,.18);padding:9px 14px;border-radius:999px;font-size:14px;font-weight:bold;}"
    ".grid{display:grid;grid-template-columns:1fr 1fr;gap:18px;margin-top:18px;}"
    ".card{background:white;border-radius:18px;padding:18px;box-shadow:0 8px 18px rgba(0,0,0,.08);}"
    ".card h2{margin:0 0 14px 0;color:#0f5d6b;font-size:22px;}"
    "label{display:block;margin:10px 0 6px 0;font-weight:bold;}"
    "input{width:100%;padding:12px;border:1px solid #c9d5dd;border-radius:12px;font-size:15px;box-sizing:border-box;}"
    "button{margin-top:14px;background:#00adc8;color:white;border:none;border-radius:12px;padding:12px 18px;font-size:15px;font-weight:bold;cursor:pointer;}"
    "button:hover{background:#0998ae;}"
    ".pill{display:inline-block;padding:8px 12px;border-radius:999px;font-size:14px;font-weight:bold;}"
    ".ok{background:#dff7e8;color:#17653b;}"
    ".warn{background:#ffe3e3;color:#942020;}"
    ".muted{background:#eef4f8;color:#4d6776;}"
    ".mono{font-family:Consolas,monospace;word-break:break-all;}"
    ".small{font-size:13px;color:#5c7380;}"
    ".section{margin-top:18px;}"
    ".scroll{max-height:360px;overflow:auto;border:1px solid #e8eef2;border-radius:12px;}"
    "table{width:100%;border-collapse:collapse;font-size:14px;}"
    "th,td{padding:10px;border-bottom:1px solid #e8eef2;text-align:left;vertical-align:top;}"
    "th{background:#f5fbfd;color:#2d5d69;position:sticky;top:0;}"
    "@media (max-width:900px){.grid{grid-template-columns:1fr;}}"
    "</style></head><body><div class='wrap'>"
  );

  html += F("<div class='hero'>");
  html += F("<h1>🏠 CREA Casa Inteligente</h1>");
  html += F("<p>Sistema de acceso con RFID sobre ESP32. Administra integrantes del hogar, revisa el historial de ingresos y visualiza el último UID leído desde el hotspot local.</p>");
  html += F("<div class='chips'>");
  html += "<div class='chip'>WiFi: " + String(AP_SSID) + "</div>";
  html += "<div class='chip'>IP: " + WiFi.softAPIP().toString() + "</div>";
  html += "<div class='chip'>Tiempo activo: " + formatUptime(millis()) + "</div>";
  html += F("</div></div>");

  html += F("<div class='grid'>");

  html += F("<div class='card'>");
  html += F("<h2>Última lectura RFID</h2>");
  if (lastScannedUID.length() == 0) {
    html += F("<p class='small'>Aún no se ha leído ninguna tarjeta o pin RFID.</p>");
  } else {
    bool known = isKnownUID(lastScannedUID);
    html += "<p><strong>UID:</strong> <span class='mono'>" + htmlEscape(lastScannedUID) + "</span></p>";
    html += "<p><strong>Hora:</strong> " + htmlEscape(lastScannedTime) + "</p>";
    html += "<p><strong>Estado:</strong> ";
    html += known ? "<span class='pill ok'>Conocido</span>" : "<span class='pill warn'>Desconocido</span>";
    html += "</p>";

    if (known) {
      html += "<p><strong>Persona:</strong> " + htmlEscape(getNameByUID(lastScannedUID)) + "</p>";
    } else {
      html += F("<p><strong>Persona:</strong> SIN ASIGNAR</p>");
    }
  }
  html += F("</div>");

  html += F("<div class='card'>");
  html += F("<h2>Asignar UID a una persona</h2>");
  html += F("<form action='/save' method='POST'>");
  html += F("<label for='uid'>UID</label>");
  html += "<input id='uid' name='uid' type='text' value='" + htmlEscape(lastScannedUID) + "' placeholder='Ej: 04ACDA3DBE2A81'>";
  html += F("<label for='name'>Nombre o rol del hogar</label>");
  html += F("<input id='name' name='name' type='text' placeholder='Ej: Padre, Madre, Hijo, Abuela'>");
  html += F("<button type='submit'>Guardar / Actualizar</button>");
  html += F("</form>");
  html += F("<p class='small' style='margin-top:10px;'>Puedes usar el último UID leído automáticamente o escribir uno manualmente.</p>");
  html += F("</div>");

  html += F("</div>");

  html += F("<div class='card section'>");
  html += F("<h2>Integrantes registrados</h2>");
  if (peopleCount == 0) {
    html += F("<p class='small'>No hay integrantes registrados todavía.</p>");
  } else {
    html += F("<div class='scroll'><table><thead><tr><th>#</th><th>Nombre / Rol</th><th>UID</th></tr></thead><tbody>");
    for (size_t i = 0; i < peopleCount; i++) {
      html += "<tr><td>" + String(i + 1) + "</td><td>" + htmlEscape(people[i].name) + "</td><td class='mono'>" + htmlEscape(people[i].uid) + "</td></tr>";
    }
    html += F("</tbody></table></div>");
  }
  html += F("</div>");

  html += F("<div class='card section'>");
  html += F("<h2>Historial de ingresos</h2>");
  if (logsCount == 0) {
    html += F("<p class='small'>Todavía no hay registros de acceso.</p>");
  } else {
    html += F("<div class='scroll'><table><thead><tr><th>Hora</th><th>Estado</th><th>Persona</th><th>UID</th></tr></thead><tbody>");
    for (int i = (int)logsCount - 1; i >= 0; i--) {
      html += "<tr>";
      html += "<td>" + htmlEscape(logsDB[i].timeText) + "</td>";
      html += "<td>" + String(logsDB[i].known ? "<span class='pill ok'>Conocido</span>" : "<span class='pill warn'>Desconocido</span>") + "</td>";
      html += "<td>" + htmlEscape(logsDB[i].name) + "</td>";
      html += "<td class='mono'>" + htmlEscape(logsDB[i].uid) + "</td>";
      html += "</tr>";
    }
    html += F("</tbody></table></div>");
  }
  html += F("</div>");

  html += F("</div></body></html>");

  return html;
}

void handleRoot() {
  server.send(200, "text/html; charset=utf-8", buildPage());
}

void handleSave() {
  String uid = server.arg("uid");
  String name = server.arg("name");

  uid.trim();
  name.trim();
  uid.toUpperCase();

  if (uid.length() == 0 || name.length() == 0) {
    server.send(400, "text/html; charset=utf-8",
      "<html><body style='font-family:Arial;padding:30px;'><h2>Faltan datos</h2><p>Debes enviar UID y nombre.</p><p><a href='/'>Volver</a></p></body></html>");
    return;
  }

  addOrUpdatePerson(uid, name);

  Serial.println(">>> Registro actualizado desde la interfaz");
  Serial.println("UID: " + uid);
  Serial.println("Nombre/Rol: " + name);

  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

/* ===================== SETUP ===================== */
void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(PIN_LED_KNOWN, OUTPUT);
  pinMode(PIN_LED_UNKNOWN, OUTPUT);
  ledsOff();

  pinMode(PIN_BUZZ, OUTPUT);
  noTone(PIN_BUZZ);

  pinMode(PIN_IRQ, INPUT_PULLUP);

  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_SS);
  rfid.PCD_Init();
  delay(50);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.begin();

  Serial.println();
  Serial.println("======================================");
  Serial.println("CREA Casa Inteligente iniciada");
  Serial.println("Hotspot: CREA_CasaInteligente");
  Serial.println("Clave:   CREA_CI123");
  Serial.println("IP:      " + WiFi.softAPIP().toString());
  Serial.println("Abre en navegador: http://" + WiFi.softAPIP().toString());
  Serial.println("Sistema listo para leer RFID");
  Serial.println("======================================");
}

/* ===================== LOOP ===================== */
void loop() {
  server.handleClient();
  updateLEDs();

  static unsigned long lastPoll = 0;
  unsigned long now = millis();

  if (now - lastPoll >= RFID_POLL_MS) {
    lastPoll = now;

    if (readCardOnce()) {
      String uid = uidToString(rfid.uid);
      lastSeenCard = now;
      cardPresent = true;

      if (shouldProcessCard(uid)) {
        processCard(uid);
      }

      rfid.PICC_HaltA();
      rfid.PCD_StopCrypto1();
    }
  }

  if (cardPresent && (now - lastSeenCard > LOST_TIMEOUT_MS)) {
    cardPresent = false;
  }
}