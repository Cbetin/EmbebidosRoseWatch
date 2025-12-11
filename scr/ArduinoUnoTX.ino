#include <SPI.h>
#include <LoRa.h>
#include <DHT.h>
#include <Wire.h>
#include <BH1750.h>

// ——— Configuración de pines ———
// LoRa (Arduino UNO)
const int csPin    = 10;  // CS / NSS
const int resetPin = 9;   // RESET
const int irqPin   = 2;   // DIO0 / IRQ (pin con interrupción)

// DHT11
const int dhtPin = 7;
#define DHTTYPE DHT11
DHT dht(dhtPin, DHTTYPE);

// Sensor de humedad de suelo HW-080 (salida analógica)
const int sensorSueloPin = A2;

// Calibración del sensor de suelo
const int rawSeco   = 900;  
const int rawHumedo = 500;  

// BH1750 (I²C: SDA=A4, SCL=A5 en UNO)
BH1750 lightMeter;

void setup() {
  Serial.begin(9600);
  delay(100);

  // — Inicializar DHT11 —
  dht.begin();
  Serial.println("DHT11: inicializado");

  // — Inicializar BH1750 —
  Wire.begin();
  if (lightMeter.begin()) {
    Serial.println("BH1750: OK");
  } else {
    Serial.println("BH1750: ERROR al iniciar");
  }

  // — Inicializar LoRa —
  LoRa.setPins(csPin, resetPin, irqPin);
  if (!LoRa.begin(433E6)) {
    Serial.println("LoRa: ERROR al iniciar");
    while (true);
  }
  LoRa.setTxPower(14);            // Aumentamos potencia para compensar UNO
  LoRa.setSpreadingFactor(12);    // SF12
  LoRa.setSignalBandwidth(62500); // 62.5 kHz
  LoRa.setCodingRate4(8);         // CR 4/8

  Serial.println("=== Sistema de sensores con LoRa (UNO) listo ===");
}

void loop() {
  // — Lectura DHT11 —
  float temperatura = dht.readTemperature();
  float humedadAmb  = dht.readHumidity();
  if (isnan(temperatura) || isnan(humedadAmb)) {
    Serial.println("DHT11: ERROR al leer");
  }

  // — Lectura y calibración HW-080 —
  int rawSoil = analogRead(sensorSueloPin);
  rawSoil = constrain(rawSoil, rawHumedo, rawSeco);
  int humedadSoilPct = map(rawSoil, rawHumedo, rawSeco, 100, 0);
  
  // — Lectura BH1750 —
  float iluminanciaLux = lightMeter.readLightLevel();
  if (iluminanciaLux < 0) {
    Serial.println("BH1750: ERROR al leer");
  }

  // — Construir mensaje —
  String mensaje = "";
  mensaje = String(temperatura,1) + ",";
  mensaje += String(humedadAmb,1) + ",";
  mensaje += String(humedadSoilPct) + ",";
  mensaje += String(iluminanciaLux,0);

  // — Depuración Serial —
  Serial.println(F("-------------------------------"));
  if (!isnan(temperatura) && !isnan(humedadAmb)) {
    Serial.print("Temperatura: "); Serial.print(temperatura,1); Serial.println(" °C");
    Serial.print("Humedad ambiente: "); Serial.print(humedadAmb,1); Serial.println(" %");
  }
  Serial.print("Humedad suelo: "); Serial.print(rawSoil); Serial.print(" raw → ");
  Serial.print(humedadSoilPct); Serial.println(" %");
  if (iluminanciaLux >= 0) {
    Serial.print("Iluminancia: "); Serial.print(iluminanciaLux,0); Serial.println(" lx");
  }
  Serial.print("Enviando por LoRa: ");
  Serial.println(mensaje);

  // — Transmisión LoRa —
  LoRa.beginPacket();
  LoRa.print(mensaje);
  LoRa.endPacket();

  delay(7000); // Espera 7 s antes de la siguiente lectura
}
