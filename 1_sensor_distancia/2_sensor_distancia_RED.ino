#include <WiFi.h>
#include <WebServer.h>

// Definimos los pines de nuestro murciélago
int trigPin = 4;
int echoPin = 5;

// Creamos el servidor web en el puerto estándar (80)
WebServer servidor(80);

void setup() {
  Serial.begin(115200);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  // --- ¡ZONA DE MODIFICACIÓN 1: El Wi-Fi! ---
  WiFi.softAP("Radar-Murcielago", "12345678"); // Nombre de red y contraseña
  // -----------------------------------------

  // Diseñamos lo que mostrará la página web cuando alguien entre
  servidor.on("/", []() {
    
    // 1. El ESP32 hace el cálculo de la distancia
    long duracion, distancia;
    digitalWrite(trigPin, LOW); delayMicroseconds(2);
    digitalWrite(trigPin, HIGH); delayMicroseconds(10);
    digitalWrite(trigPin, LOW);
    duracion = pulseIn(echoPin, HIGH);
    distancia = (duracion / 2) / 29.1;

    // 2. Armamos la página web con colores y texto
    String html = "<html><head><meta charset='UTF-8'>";
    
    // ¡Magia! La página se refresca solita cada 1 segundo
    html += "<meta http-equiv='refresh' content='1'>"; 
    
    html += "<style>body{font-family: Arial; text-align: center; background-color: #e0f7fa; margin-top: 50px;}</style></head><body>";
    
    // --- ¡ZONA DE MODIFICACIÓN 2: El diseño! ---
    html += "<h1>🦇 Radar en Vivo 🦇</h1>";
    html += "<h2>Distancia: <strong style='color: red; font-size: 60px;'>" + String(distancia) + " cm</strong></h2>";
    
    // Si algo se acerca mucho, mostramos una alerta en la web
    if (distancia < 10) {
      html += "<h2>🛑 ¡CUIDADO! ¡Vas a chocar! 🛑</h2>";
    }
    // -------------------------------------------

    html += "</body></html>";
    
    // Enviamos la página al celular o compu
    servidor.send(200, "text/html", html);
  });

  // Encendemos el servidor
  servidor.begin();
}

void loop() {
  // El ESP32 siempre está atento a que alguien abra la página
  servidor.handleClient();
}