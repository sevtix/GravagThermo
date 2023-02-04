#include "Volume.h"                // Buzzer
#include <Wire.h>                  // I2C -> 7-Segment-Anzeigen
#include <Adafruit_GFX.h>          // 7-Segment-Anzeigen
#include "Adafruit_LEDBackpack.h"  // 7-Segment-Anzeigen

// Severin Schmid
// 19.11.2022 - Basis
// 23.01.2023 - Tests
// 27.01.2023 - Hinzufügen von Input Pullup
// Gravag-Thermo Erdgas

int MaxZeit = 120;
int MaxFehler = 100;
int MaxPunkte = 1000;

Volume vol;  // https://github.com/connornishijima/arduino-volume1#supported-pins
Adafruit_7segment zeit_segment = Adafruit_7segment();
Adafruit_7segment fehler_segment = Adafruit_7segment();
Adafruit_7segment punkte_segment = Adafruit_7segment();

byte pos1_pin = 2;  // START HALL-SENSOR-PIN
byte pos2_pin = 3;  // ZIEL HALL-SENSOR-PIN
byte error_input_pin = 5; // KURZSCHLUSS FEHLER COUNTER

byte pos1_last_status = 0;  // START ( 0 = abgelegt | 1 = angehoben )
byte pos2_last_status = 0;  // ENDE ( 0 = abgelegt | 1 = angehoben )

bool error_input_toggle_state = false;

void setup() {
  pinMode(pos1_pin, INPUT);  // START
  pinMode(pos2_pin, INPUT);  // ENDE
  pinMode(error_input_pin, INPUT_PULLUP);
  Serial.begin(115200);

  // 7-Segment-Anzeigen
  zeit_segment.begin(0x72);
  fehler_segment.begin(0x71);
  punkte_segment.begin(0x70);

  // Aktueller Zustand der Hallsensoren abfragen
  byte pos1_current_status = digitalRead(2);
  byte pos2_current_status = digitalRead(3);

  // Um Fehlfunktion der Events beim Start zu verhindern.
  pos1_last_status = pos1_current_status;
  pos2_last_status = pos2_current_status;

  Serial.print("pos1_last_status=");
  Serial.println(pos1_current_status);
  Serial.print("pos2_last_status=");
  Serial.println(pos2_current_status);

  //vol.begin();

}

void loop() {
  if (digitalRead(pos1_pin) == HIGH && pos1_last_status == 0) {
    // Vom Start angehoben
    OnAngehobenStart();
    pos1_last_status = 1;
  }

  if (digitalRead(pos1_pin) == LOW && pos1_last_status == 1) {
    // Am Start abgelegt
    OnAbgelegtStart();
    pos1_last_status = 0;
  }

  if (digitalRead(pos2_pin) == HIGH && pos2_last_status == 0) {
    // Vom Ziel angehoben
    OnAngehobenZiel();
    pos2_last_status = 1;
  }

  if (digitalRead(pos2_pin) == LOW && pos2_last_status == 1) {
    // Am Ziel abgelegt
    OnAbgelegtZiel();
    pos2_last_status = 0;
  }

  if (digitalRead(error_input_pin) == LOW && !error_input_toggle_state) {
    // Draht berührt
    OnBehruehrungDraht();
    error_input_toggle_state = true;
  }

  if (digitalRead(error_input_pin) == HIGH && error_input_toggle_state) {
    // Draht losgelassen
    OnLoslassenDraht();
    error_input_toggle_state = false;
  }

  OnAnzeigenAktualisierenCheck();

}

// SPIEL INFORMATIONEN

byte SpielStatus = 0; // ( 0 = nicht gestartet | 1 = läuft )
int FehlerCounter = 0;
long SpielStartZeitstempel = 0L;
long SpielEndeZeitstempel = 0L;

long LastAnzeigenUpdateZeitstempel = 0L;
int AnzeigenUpdateInterval = 100;

// EVENTS

void OnAngehobenStart() {
  Serial.println("OnAngehobenStart()");
  SpielStatus = 1;
  SpielStartZeitstempel = millis();
  OnSpielStart();
}

void OnAbgelegtStart() {
  Serial.println("OnAbgelegtStart()");
  if (SpielStatus == 1) {
    SpielStatus = 0;
    SpielEndeZeitstempel = millis();
    OnSpielAbbruch();
  }
}

void OnAngehobenZiel() {
  Serial.println("OnAngehobenZiel()");
}

void OnAbgelegtZiel() {
  Serial.println("OnAbgelegtZiel()");
  if (SpielStatus == 1) {
    SpielStatus = 0;
    SpielEndeZeitstempel = millis();
    OnSpielEnde();
  }
}

void OnBehruehrungDraht() {
  if (SpielStatus == 1) {
    vol.tone(261 * 2, 255);
    FehlerCounter++;
  }
  Serial.println("OnBehruehrungDraht()");
}


void OnLoslassenDraht() {
  Serial.println("OnLoslassenDraht()");
  vol.tone(1000, 0);
}

void OnSpielStart() {
  Serial.println("OnSpielStart()");
  FehlerCounter = 0;
  LastAnzeigenUpdateZeitstempel = millis();
}

void OnSpielEnde() {
  Serial.println("OnSpielEnde()");
  int Zeit = SpielEndeZeitstempel - SpielStartZeitstempel;
  int ZeitSekunden = (SpielEndeZeitstempel - SpielStartZeitstempel) / 1000;
  Serial.print(Zeit);
  Serial.println(" ms");

  //int Minuten = 0;
  //int Sekunden = 0;
  //SekundenZuMinutenSekunden(ZeitSekunden, Minuten, Sekunden);
  //Serial.println(ZweistellenFormattiert(Minuten) + ZweistellenFormattiert(Sekunden));
  zeit_segment.print(ZeitSekunden);
  zeit_segment.writeDisplay();

}

void OnSpielAbbruch() {
  Serial.println("OnSpielAbbruch()");
}

int CalculatePunkte(float Zeit, float Fehler) {
  if (Zeit >= MaxZeit || Fehler >= MaxFehler) {
    return 0;
  } else {
    float ZeitFaktor = 1 - (Zeit / MaxZeit);
    float FehlerFaktor = 1 - (Fehler / MaxFehler);
    float EndFaktor = ZeitFaktor * FehlerFaktor;
    float Punkte = MaxPunkte * EndFaktor;
    int PunkteInteger = (int)(Punkte + .5);
    return PunkteInteger;
  }
}

void OnAnzeigenAktualisierenCheck() {
  if (SpielStatus == 1) {
    long AktuelleZeit = millis();
    if (AktuelleZeit > LastAnzeigenUpdateZeitstempel) {
      OnAnzeigenAktualisierenInterval();
      LastAnzeigenUpdateZeitstempel = AktuelleZeit + AnzeigenUpdateInterval;
    }
  }
}

void OnAnzeigenAktualisierenInterval() {
  //Serial.println("OnAnzeigenAktualisierenInterval()");
  int ZeitSekunden = (millis() - SpielStartZeitstempel) / 1000;
  float ZeitSekunden_Float = (float)(millis() - SpielStartZeitstempel) / 1000.0f;
  zeit_segment.print(ZeitSekunden);
  zeit_segment.writeDisplay();

  Serial.println(ZeitSekunden_Float);

  int Punkte = CalculatePunkte(FehlerCounter, ZeitSekunden_Float);
  punkte_segment.print(Punkte);
  punkte_segment.writeDisplay();

  fehler_segment.print(FehlerCounter);
  fehler_segment.writeDisplay();


}

String ZweistellenFormattiert(int n) {
  if (n <= 9) {
    return String("0") + String(n);
  }
  if (n > 9 && n <= 99) {
    return String(n);
  }
}

void SekundenZuMinutenSekunden( const uint32_t seconds, int &m, int &s )
{
  uint32_t t = seconds;
  s = t % 60;
  t = (t - s) / 60;
  m = t % 60;
  t = (t - m) / 60;
}

void playWinSound() {
  for (int i = 0; i <= 8; i++) {
    vol.tone(261 * 2, 255);
    vol.delay(50);
    vol.tone(261 * 2, 255);
    vol.delay(50);
  }

  for (int i = 0; i <= 8; i++) {
    vol.tone(261 * 3, 255);
    vol.delay(50);
    vol.tone(261 * 3, 255);
    vol.delay(50);
  }


  for (int i = 0; i <= 8; i++) {
    vol.tone(440 * 4, 255);
    vol.delay(50);
    vol.tone(440 * 4, 255);
    vol.delay(50);
  }

  vol.tone(1000, 0);
}
