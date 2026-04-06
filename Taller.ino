#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <WebServer.h>
#include <pgmspace.h>
#include "LOGOCREA.h"

/* ===================== WIFI / WEB ===================== */
const char* AP_SSID = "CREA_CasaInteligente";
const char* AP_PASS = "CREA_CI123";

WebServer server(80);

/* ===================== PINOUT ===================== */
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
static const uint8_t PIN_BUZZ        = 18; // Buzzer

MFRC522 rfid(PIN_SS, PIN_RST);

/* ===================== CONFIG ===================== */
const uint16_t RFID_POLL_MS       = 80;
const uint16_t LOST_TIMEOUT_MS    = 1200;
const uint32_t BELL_COOLDOWN_MS   = 5000;
const uint32_t ACCESS_LED_MS      = 1200;
const size_t   MAX_PEOPLE         = 30;
const size_t   MAX_LOGS           = 80;

/* ===================== DATA ===================== */
struct Person {
  String uid;
  String role;
};

struct AccessLog {
  String uid;
  String role;
  String timeText;
  bool known;
};

Person people[MAX_PEOPLE];
size_t peopleCount = 0;

AccessLog accessLogs[MAX_LOGS];
size_t logCount = 0;

// UID leído recientemente y pendiente de asignar
String lastScannedUID = "";
String lastScannedTimeText = "";

// Estado de lectura
unsigned long lastSeenCard = 0;
String currentUID = "";
bool currentPresence = false;

// Timbre
unsigned long lastBellMillis = 0;

// LEDs
unsigned long ledOffAt = 0;
bool ledKnownActive = false;
bool ledUnknownActive = false;

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

  char buf[16];
  snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", hh, mm, ss);
  return String(buf);
}

int findPersonIndexByUID(const String &uid) {
  for (size_t i = 0; i < peopleCount; i++) {
    if (people[i].uid.equalsIgnoreCase(uid)) return (int)i;
  }
  return -1;
}

String getRoleByUID(const String &uid) {
  int idx = findPersonIndexByUID(uid);
  if (idx >= 0) return people[idx].role;
  return "DESCONOCIDO";
}

bool isKnownUID(const String &uid) {
  return findPersonIndexByUID(uid) >= 0;
}

void addOrUpdatePerson(const String &uid, const String &role) {
  int idx = findPersonIndexByUID(uid);
  if (idx >= 0) {
    people[idx].role = role;
    return;
  }

  if (peopleCount < MAX_PEOPLE) {
    people[peopleCount].uid = uid;
    people[peopleCount].role = role;
    peopleCount++;
  }
}

void addLog(const String &uid, const String &role, bool known) {
  AccessLog entry;
  entry.uid = uid;
  entry.role = role;
  entry.timeText = formatUptime(millis());
  entry.known = known;

  if (logCount < MAX_LOGS) {
    accessLogs[logCount++] = entry;
  } else {
    for (size_t i = 1; i < MAX_LOGS; i++) {
      accessLogs[i - 1] = accessLogs[i];
    }
    accessLogs[MAX_LOGS - 1] = entry;
  }
}

String htmlEscape(String s) {
  s.replace("&", "&amp;");
  s.replace("<", "&lt;");
  s.replace(">", "&gt;");
  s.replace("\"", "&quot;");
  s.replace("'", "&#39;");
  return s;
}

void setKnownLED() {
  digitalWrite(PIN_LED_KNOWN, HIGH);
  digitalWrite(PIN_LED_UNKNOWN, LOW);
  ledKnownActive = true;
  ledUnknownActive = false;
  ledOffAt = millis() + ACCESS_LED_MS;
}

void setUnknownLED() {
  digitalWrite(PIN_LED_KNOWN, LOW);
  digitalWrite(PIN_LED_UNKNOWN, HIGH);
  ledKnownActive = false;
  ledUnknownActive = true;
  ledOffAt = millis() + ACCESS_LED_MS;
}

void updateLEDTimeout() {
  if ((ledKnownActive || ledUnknownActive) && millis() >= ledOffAt) {
    digitalWrite(PIN_LED_KNOWN, LOW);
    digitalWrite(PIN_LED_UNKNOWN, LOW);
    ledKnownActive = false;
    ledUnknownActive = false;
  }
}

void ringBellIfAllowed() {
  unsigned long now = millis();
  if (now - lastBellMillis < BELL_COOLDOWN_MS) return;

  // Timbre sencillo tipo casa
  tone(PIN_BUZZ, 1047); delay(120);
  tone(PIN_BUZZ, 1319); delay(140);
  tone(PIN_BUZZ, 1568); delay(220);
  noTone(PIN_BUZZ);

  lastBellMillis = millis();
}

/* ===================== RFID ===================== */
bool readCardOnce() {
  if (!rfid.PICC_IsNewCardPresent()) return false;
  if (!rfid.PICC_ReadCardSerial()) return false;
  return true;
}

void processCardRead(const String &uid) {
  String role = getRoleByUID(uid);
  bool known = (role != "DESCONOCIDO");
  String nowText = formatUptime(millis());

  lastScannedUID = uid;
  lastScannedTimeText = nowText;

  addLog(uid, role, known);
  ringBellIfAllowed();

  if (known) {
    setKnownLED();
    Serial.println("======================================");
    Serial.println("RFID detectado");
    Serial.println("UID: " + uid);
    Serial.println("Estado: CONOCIDO");
    Serial.println("Persona: " + role);
    Serial.println("Hora (desde encendido): " + nowText);
    Serial.println("======================================");
  } else {
    setUnknownLED();
    Serial.println("======================================");
    Serial.println("RFID detectado");
    Serial.println("UID: " + uid);
    Serial.println("Estado: DESCONOCIDO");
    Serial.println("Persona: SIN ASIGNAR");
    Serial.println("Hora (desde encendido): " + nowText);
    Serial.println("======================================");
  }
}

/* ===================== LOGO AS BMP ===================== */
// Convierte el array RGB 24-bit del header a un BMP 24-bit servido por HTTP.
// El array recibido en tu archivo es de 225x225 con valores hex RGB. :contentReference[oaicite:2]{index=2}

void writeLE16(WiFiClient &client, uint16_t v) {
  client.write((uint8_t)(v & 0xFF));
  client.write((uint8_t)((v >> 8) & 0xFF));
}

void writeLE32(WiFiClient &client, uint32_t v) {
  client.write((uint8_t)(v & 0xFF));
  client.write((uint8_t)((v >> 8) & 0xFF));
  client.write((uint8_t)((v >> 16) & 0xFF));
  client.write((uint8_t)((v >> 24) & 0xFF));
}

void handleLogoBmp() {
  WiFiClient client = server.client();

  const uint32_t width = LOGOCREA_WIDTH;
  const uint32_t height = LOGOCREA_HEIGHT;
  const uint32_t rowSize = (width * 3 + 3) & ~3;
  const uint32_t pixelDataSize = rowSize * height;
  const uint32_t fileSize = 54 + pixelDataSize;

  server.setContentLength(fileSize);
  server.send(200, "image/bmp", "");

  // BMP Header
  client.write('B');
  client.write('M');
  writeLE32(client, fileSize);
  writeLE16(client, 0);
  writeLE16(client, 0);
  writeLE32(client, 54);

  // DIB Header
  writeLE32(client, 40);
  writeLE32(client, width);
  writeLE32(client, height);
  writeLE16(client, 1);
  writeLE16(client, 24);
  writeLE32(client, 0);
  writeLE32(client, pixelDataSize);
  writeLE32(client, 2835);
  writeLE32(client, 2835);
  writeLE32(client, 0);
  writeLE32(client, 0);

  const uint8_t padding[3] = {0, 0, 0};
  uint8_t padCount = rowSize - width * 3;

  // BMP se envía bottom-up
  for (int y = (int)height - 1; y >= 0; y--) {
    for (uint32_t x = 0; x < width; x++) {
      uint32_t idx = y * width + x;
      uint32_t color = pgm_read_dword(&LOGOCREA[idx]);

      uint8_t r = (color >> 16) & 0xFF;
      uint8_t g = (color >> 8) & 0xFF;
      uint8_t b = color & 0xFF;

      client.write(b);
      client.write(g);
      client.write(r);
    }
    if (padCount) client.write(padding, padCount);
  }
}

/* ===================== WEB UI ===================== */
String buildPage() {
  String html;
  html.reserve(18000);

  html += F(
    "<!DOCTYPE html><html lang='es'><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
    "<title>CREA Casa Inteligente</title>"
    "<style>"
    "body{margin:0;font-family:Arial,Helvetica,sans-serif;background:#eef4f8;color:#203040;}"
    ".wrap{max-width:1100px;margin:0 auto;padding:22px;}"
    ".hero{background:linear-gradient(135deg,#00adc8,#6ad3e5);border-radius:22px;padding:22px;color:white;"
    "box-shadow:0 10px 28px rgba(0,0,0,.12);display:grid;grid-template-columns:160px 1fr;gap:22px;align-items:center;}"
    ".hero img{width:140px;height:140px;object-fit:contain;background:white;border-radius:18px;padding:8px;}"
    ".hero h1{margin:0 0 8px 0;font-size:32px;}"
    ".hero p{margin:0;opacity:.96;line-height:1.5;}"
    ".grid{display:grid;grid-template-columns:1fr 1fr;gap:18px;margin-top:18px;}"
    ".card{background:white;border-radius:18px;padding:18px;box-shadow:0 8px 20px rgba(0,0,0,.08);}"
    ".card h2{margin:0 0 14px 0;font-size:22px;color:#0d5563;}"
    ".pill{display:inline-block;padding:8px 12px;border-radius:999px;font-size:14px;font-weight:bold;margin:4px 8px 0 0;}"
    ".ok{background:#dff7e7;color:#17663b;}"
    ".warn{background:#ffe6e6;color:#8f1e1e;}"
    ".muted{background:#edf4f7;color:#4f6572;}"
    "label{display:block;margin:10px 0 6px 0;font-weight:bold;}"
    "input{width:100%;padding:12px;border:1px solid #c9d8df;border-radius:12px;font-size:15px;box-sizing:border-box;}"
    "button{margin-top:14px;background:#00adc8;color:white;border:none;border-radius:12px;padding:12px 16px;font-size:15px;font-weight:bold;cursor:pointer;}"
    "button:hover{background:#0697ae;}"
    "table{width:100%;border-collapse:collapse;font-size:14px;}"
    "th,td{padding:10px;border-bottom:1px solid #e7eef2;text-align:left;vertical-align:top;}"
    "th{background:#f6fbfd;color:#285b68;position:sticky;top:0;}"
    ".small{font-size:13px;color:#55707f;}"
    ".mono{font-family:Consolas,monospace;word-break:break-all;}"
    ".scroll{max-height:360px;overflow:auto;border:1px solid #ecf1f4;border-radius:12px;}"
    "@media (max-width: 900px){.hero{grid-template-columns:1fr;text-align:center}.grid{grid-template-columns:1fr}}"
    "</style></head><body><div class='wrap'>"
  );

  html += F("<div class='hero'>");
  html += F("<div style='text-align:center;'><img src='/logo.bmp' alt='Logo CREA'></div>");
  html += F("<div><h1>CREA Casa Inteligente</h1>");
  html += F("<p>Control de acceso con RFID, registro local y administración de integrantes del hogar desde el hotspot de la ESP32.</p>");
  html += F("<div style='margin-top:12px;'>");
  html += "<span class='pill muted'>WiFi: " + String(AP_SSID) + "</span>";
  html += "<span class='pill muted'>IP: " + WiFi.softAPIP().toString() + "</span>";
  html += "<span class='pill muted'>Tiempo activo: " + formatUptime(millis()) + "</span>";
  html += F("</div></div></div>");

  html += F("<div class='grid'>");

  // Panel: último UID
  html += F("<div class='card'>");
  html += F("<h2>Última lectura RFID</h2>");
  if (lastScannedUID.length() == 0) {
    html += F("<p class='small'>Aún no se ha leído ninguna tarjeta.</p>");
  } else {
    bool known = isKnownUID(lastScannedUID);
    html += "<p><strong>UID:</strong> <span class='mono'>" + htmlEscape(lastScannedUID) + "</span></p>";
    html += "<p><strong>Hora:</strong> " + htmlEscape(lastScannedTimeText) + "</p>";
    html += "<p><strong>Estado:</strong> ";
    html += known ? "<span class='pill ok'>Conocido</span>" : "<span class='pill warn'>Desconocido</span>";
    html += "</p>";
    if (known) {
      html += "<p><strong>Persona:</strong> " + htmlEscape(getRoleByUID(lastScannedUID)) + "</p>";
    }
  }
  html += F("</div>");

  // Panel: formulario
  html += F("<div class='card'>");
  html += F("<h2>Asignar UID a integrante del hogar</h2>");
  html += F("<form action='/save' method='POST'>");
  html += F("<label for='uid'>UID</label>");
  html += "<input id='uid' name='uid' type='text' value='" + htmlEscape(lastScannedUID) + "' placeholder='Ej: 04ACDA3DBE2A81'>";
  html += F("<label for='role'>Nombre o rol</label>");
  html += F("<input id='role' name='role' type='text' placeholder='Ej: Padre, Madre, Hijo, Abuelo'>");
  html += F("<button type='submit'>Guardar / Actualizar</button>");
  html += F("</form>");
  html += F("<p class='small' style='margin-top:10px;'>Puedes usar el último UID leído automáticamente o escribir uno manualmente.</p>");
  html += F("</div>");

  html += F("</div>");

  // Integrantes
  html += F("<div class='card' style='margin-top:18px;'>");
  html += F("<h2>Integrantes registrados</h2>");
  if (peopleCount == 0) {
    html += F("<p class='small'>No hay integrantes registrados todavía.</p>");
  } else {
    html += F("<div class='scroll'><table><thead><tr><th>#</th><th>Nombre / Rol</th><th>UID</th></tr></thead><tbody>");
    for (size_t i = 0; i < peopleCount; i++) {
      html += "<tr><td>" + String(i + 1) + "</td><td>" + htmlEscape(people[i].role) + "</td><td class='mono'>" + htmlEscape(people[i].uid) + "</td></tr>";
    }
    html += F("</tbody></table></div>");
  }
  html += F("</div>");

  // Historial
  html += F("<div class='card' style='margin-top:18px;'>");
  html += F("<h2>Historial de ingresos</h2>");
  if (logCount == 0) {
    html += F("<p class='small'>Todavía no hay registros de acceso.</p>");
  } else {
    html += F("<div class='scroll'><table><thead><tr><th>Hora</th><th>Estado</th><th>Persona</th><th>UID</th></tr></thead><tbody>");
    for (int i = (int)logCount - 1; i >= 0; i--) {
      html += "<tr>";
      html += "<td>" + htmlEscape(accessLogs[i].timeText) + "</td>";
      html += "<td>" + String(accessLogs[i].known ? "<span class='pill ok'>Conocido</span>" : "<span class='pill warn'>Desconocido</span>") + "</td>";
      html += "<td>" + htmlEscape(accessLogs[i].role) + "</td>";
      html += "<td class='mono'>" + htmlEscape(accessLogs[i].uid) + "</td>";
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
  String role = server.arg("role");

  uid.trim();
  role.trim();
  uid.toUpperCase();

  if (uid.length() == 0 || role.length() == 0) {
    server.send(400, "text/html; charset=utf-8",
      "<html><body><h2>Faltan datos</h2><p>Debes enviar UID y nombre/rol.</p><p><a href='/'>Volver</a></p></body></html>");
    return;
  }

  addOrUpdatePerson(uid, role);

  Serial.println(">>> Registro actualizado desde la interfaz");
  Serial.println("UID: " + uid);
  Serial.println("Persona/Rol: " + role);

  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

/* ===================== SETUP ===================== */
void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(PIN_LED_KNOWN, OUTPUT);
  pinMode(PIN_LED_UNKNOWN, OUTPUT);
  digitalWrite(PIN_LED_KNOWN, LOW);
  digitalWrite(PIN_LED_UNKNOWN, LOW);

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
  server.on("/logo.bmp", HTTP_GET, handleLogoBmp);
  server.begin();

  Serial.println();
  Serial.println("======================================");
  Serial.println("CREA Casa Inteligente iniciada");
  Serial.println("Hotspot: CREA_CasaInteligente");
  Serial.println("Clave:   CREA_CI123");
  Serial.println("IP AP:   " + WiFi.softAPIP().toString());
  Serial.println("Abre en navegador: http://" + WiFi.softAPIP().toString());
  Serial.println("Listo para leer RFID...");
  Serial.println("======================================");
}

/* ===================== LOOP ===================== */
void loop() {
  server.handleClient();
  updateLEDTimeout();

  static unsigned long lastPoll = 0;
  unsigned long now = millis();

  if (now - lastPoll >= RFID_POLL_MS) {
    lastPoll = now;

    if (readCardOnce()) {
      String uid = uidToString(rfid.uid);

      currentUID = uid;
      lastSeenCard = now;

      // Registrar solo cuando aparece una nueva presencia o cambia UID
      if (!currentPresence || uid != lastScannedUID || (now - lastSeenCard < 5)) {
        processCardRead(uid);
      }

      currentPresence = true;

      rfid.PICC_HaltA();
      rfid.PCD_StopCrypto1();
    }
  }

  // Liberar presencia si se pierde lectura
  if (currentPresence && (now - lastSeenCard > LOST_TIMEOUT_MS)) {
    currentPresence = false;
    currentUID = "";
  }
}