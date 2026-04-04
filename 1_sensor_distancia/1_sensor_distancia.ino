#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ¡Configuramos nuestra pantalla!
LiquidCrystal_I2C lcd(0x20, 16, 2);

// Los pines del sensor (nuestros "ojos" de murciélago)
int trigPin = A0; // La boca que "grita" el sonido
int echoPin = A1; // La oreja que "escucha" el eco

void setup() {
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  lcd.init();
  lcd.backlight(); // Enciende la luz de la pantalla

  // --- ¡ZONA DE MODIFICACIÓN 1! ---
  lcd.setCursor(0,0);
  lcd.print("Hola Clase!"); // <- ¡Cambia este mensaje por tu nombre!
  delay(3000);              // <- Cambia el 3000 (3 segundos) por otro número
  // --------------------------------

  lcd.clear(); // Limpia la pantalla
}

void loop() {
  long duracion, distancia;

  // 1. El sensor lanza un sonido invisible (grito)
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  // 2. El sensor escucha cuánto tarda en regresar el eco
  duracion = pulseIn(echoPin, HIGH);

  // 3. Calculamos la distancia en centímetros
  distancia = (duracion / 2) / 29.1;

  // 4. Mostramos la distancia en la pantalla
  lcd.setCursor(0,0);
  lcd.print("Distancia:");

  lcd.setCursor(0,1);
  lcd.print(distancia);
  lcd.print(" cm   "); // Los espacios extra borran los números viejos

  // --- ¡ZONA DE MODIFICACIÓN 2! ---
  delay(500); // <- Cambia el 500 para que la pantalla se actualice más rápido o más lento
  // --------------------------------
}