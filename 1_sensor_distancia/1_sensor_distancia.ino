#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x20, 16, 2);

int trig = 9;
int echo = 10;
#Linea agregada

void loop() {
  long tiempo;
  int distancia;

  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);

  tiempo = pulseIn(echo, HIGH);

  lcd.setCursor(0,0);
  lcd.print("Distancia:");

  lcd.setCursor(0,1);

  if(tiempo == 0){
    lcd.print("Sin señal   ");
  } else {
    distancia = tiempo * 0.034 / 2;
    lcd.print(distancia);
    lcd.print(" cm   ");
  }

  delay(300);
}