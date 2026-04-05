#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <MFRC522.h>

// Definimos los pines que elegimos para el ESP32-S3
#define RST_PIN 9
#define SS_PIN  10

// Creamos los objetos para el Lector y para el Servidor Web (Puerto 80)
MFRC522 mfrc522(SS_PIN, RST_PIN);
WebServer servidor(80);

// Esta variable guardará lo que mostraremos en la página web
String mensajeWeb = "¡Acerca tu tarjeta o llavero al sensor!";

void setup() {
  Serial.begin(115200);

  // 1. Iniciamos el lector de tarjetas
  SPI.begin(12, 13, 11, 10); // Le decimos al ESP32-S3 qué pines usar (SCK, MISO, MOSI, SS)
  mfrc522.PCD_Init();

  // 2. ¡Creamos nuestra propia red Wi-Fi!
  // --- ¡ZONA DE MODIFICACIÓN 1! ---
  WiFi.softAP("Robot-Magico", "12345678"); // Nombre de la red y contraseña
  // --------------------------------
  
  Serial.println("¡Red Wi-Fi creada! Conéctate a ella.");

  // 3. Diseñamos nuestra página web (HTML básico)
  servidor.on("/", []() {
    String html = "<html><head><meta charset='UTF-8'>";
    // Hacemos que la página se actualice solita cada 2 segundos
    html += "<meta http-equiv='refresh' content='2'>"; 
    // Le damos estilo (colores, letra grande)
    html += "<style>body{font-family: Arial; text-align: center; background-color: #E6E6FA; margin-top: 50px;}</style></head><body>";
    
    // --- ¡ZONA DE MODIFICACIÓN 2! ---
    html += "<h1>🤖 Control de Acceso Secreto 🤖</h1>"; // Título
    html += "<h2><strong style='color: blue;'>" + mensajeWeb + "</strong></h2>"; // Muestra la tarjeta
    // --------------------------------
    
    html += "</body></html>";
    servidor.send(200, "text/html", html);
  });

  servidor.begin(); // Encendemos el servidor
}

void loop() {
  // El ESP32 atiende a cualquiera que entre a la página web
  servidor.handleClient(); 

  // Revisamos si alguien puso una tarjeta nueva en el lector
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    
    // Si hay tarjeta, leemos su código secreto (UID)
    String codigoTarjeta = "";
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      codigoTarjeta += String(mfrc522.uid.uidByte[i], HEX);
    }
    codigoTarjeta.toUpperCase(); // Convertimos las letras a mayúsculas

    // Cambiamos el mensaje que se mostrará en la página web
    mensajeWeb = "¡Tarjeta detectada! Su código es: " + codigoTarjeta;
    Serial.println(mensajeWeb); // También lo imprimimos en la compu por si acaso

    mfrc522.PICC_HaltA(); // Le decimos al lector que descanse
    delay(2000); // Esperamos 2 segundos antes de leer otra tarjeta
  }
}