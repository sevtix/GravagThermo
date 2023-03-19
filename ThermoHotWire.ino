#include <EEPROM.h>
#include <Wire.h>                  // I2C -> 7-Segment-Anzeigen
#include <Adafruit_GFX.h>          // 7-Segment-Anzeigen
#include "Adafruit_LEDBackpack.h"  // 7-Segment-Anzeigen

// Severin Schmid
// 19.11.2022 - Basis
// 23.01.2023 - Tests
// 27.01.2023 - Hinzufügen von Input Pullup
// 09.03.2023 - Highscore
// 19.03.2023 - Highscore Reset über Counter-Trigger

int MaxZeit = 120;
int MaxFehler = 100;
int MaxPunkte = 1000;
int Highscore = 0;

Adafruit_7segment zeit_segment = Adafruit_7segment();
Adafruit_7segment fehler_segment = Adafruit_7segment();
Adafruit_7segment punkte_segment = Adafruit_7segment();
Adafruit_7segment highscore_segment = Adafruit_7segment();

byte pos1_pin = 2;  // START HALL-SENSOR-PIN
byte pos2_pin = 3;  // ZIEL HALL-SENSOR-PIN
byte error_input_pin = 5; // KURZSCHLUSS FEHLER COUNTER
byte buzzer_output_pin = 4;

byte pos1_last_status = 0;  // START ( 0 = abgelegt | 1 = angehoben )
byte pos2_last_status = 0;  // ENDE ( 0 = abgelegt | 1 = angehoben )

bool error_input_toggle_state = false;
bool error_input_validation_pending = false; // Wird aktuell ein EIV-Check durchgeführt?
int error_input_validation_signal = 0; // Logik-Signal des aktuellen EIV-Check
unsigned long error_input_validation_end_time = 0L; // End-Zeit des aktullen EIV-Check
int error_input_validation_delay = 10; // Dauer für für EIV (Validierung)

unsigned long timer_end = 0L; // Timer-Ende (Zeitstempel)
int timer_duration = 5000; // 10 sec
bool timer_active = false;
int timer_counter = 0; // Counter
int timer_counter_trigger = 5; // Counter for trigger

byte startupAnimationDelay = 45;

// SPIEL INFORMATIONEN

byte SpielStatus = 0; // ( 0 = nicht gestartet | 1 = läuft )
int FehlerCounter = 0;

bool FehlerTimerAktiv = false;
unsigned long FehlerTimerTriggerZeitstempel = 0L;
int FehlerTimerInterval = 50;
int FehlerTimerStrafe = 1;

unsigned long SpielStartZeitstempel = 0L;
unsigned long SpielEndeZeitstempel = 0L;

unsigned long LastAnzeigenUpdateZeitstempel = 0L;
int AnzeigenUpdateInterval = 100; // ms

void setup() {
  pinMode(pos1_pin, INPUT);  // START
  pinMode(pos2_pin, INPUT);  // ENDE
  pinMode(error_input_pin, INPUT_PULLUP);
  pinMode(buzzer_output_pin, OUTPUT);
  Serial.begin(115200);

  // 7-Segment-Anzeigen
  punkte_segment.begin(0x70);
  fehler_segment.begin(0x71);
  zeit_segment.begin(0x72);
  highscore_segment.begin(0x73);

  // Aktueller Zustand der Hallsensoren abfragen
  byte pos1_current_status = digitalRead(2);
  byte pos2_current_status = digitalRead(3);

  // Um Fehlfunktion der Events beim Start zu verhindern.
  // Letzte Position auf Aktuelle Position setzten
  pos1_last_status = pos1_current_status;
  pos2_last_status = pos2_current_status;

  playStartAnimation();
  HighscoreLaden();
  highscore_segment.println(Highscore);
  highscore_segment.writeDisplay();

}

void loop() {
  if (digitalRead(pos1_pin) == HIGH && pos1_last_status == 0) {
    // Vom Start angehoben
    BeiAngehobenStart();
    pos1_last_status = 1;
  }

  if (digitalRead(pos1_pin) == LOW && pos1_last_status == 1) {
    // Am Start abgelegt
    BeiAbgelegtStart();
    pos1_last_status = 0;
  }

  if (digitalRead(pos2_pin) == HIGH && pos2_last_status == 0) {
    // Vom Ziel angehoben
    BeiAngehobenZiel();
    pos2_last_status = 1;
  }

  if (digitalRead(pos2_pin) == LOW && pos2_last_status == 1) {
    // Am Ziel abgelegt
    BeiAbgelegtZiel();
    pos2_last_status = 0;
  }

  // Offene EIV-Check(s) durchführen
  BeiErrorInputValidationCheck();
  if (digitalRead(error_input_pin) == LOW && !error_input_toggle_state) {
    // Signal fallend
    BeiErrorInputChange(LOW); // Error Input
    error_input_toggle_state = true; // Für Toggling
  }
  if (digitalRead(error_input_pin) == HIGH && error_input_toggle_state) {
    // Signal steigend
    BeiErrorInputChange(HIGH); // Error Input
    error_input_toggle_state = false; // Für Toggling
  }

  // Wenn Timer End-Zeit kleiner als aktuelle Zeit ist
  if (timer_active && timer_end <= millis()) {
    // Timer Reset
    timer_active = false;
    timer_counter = 0;
    timer_end = 0L;
    Serial.println("Timer Reset");
  }

  BeiAnzeigenAktualisierenCheck();
  BeiFehlerTimerCheck();

}

// EVENTS

void BeiAngehobenStart() {
  Serial.println("BeiAngehobenStart()");
  SpielStatus = 1;
  SpielStartZeitstempel = millis();
  BeiSpielStart();
}

void BeiAbgelegtStart() {
  Serial.println("BeiAbgelegtStart()");
  if (timer_active) {
    // Timer bereits aktiv
    // Counter dazuzählen
    timer_counter++;
    Serial.println("timer_counter++");

    // Wenn Counter Trigger-Anzahl erreicht hat
    if (timer_counter == timer_counter_trigger) {
      // Event auslösen
      BeiCounterTrigger();

      // Timer Reset
      timer_active = false;
      timer_counter = 0;
      timer_end = 0L;
    }
  } else {

    // Timer noch nicht aktiv
    // Timer starten und Counter dazuzählen
    timer_active = true;
    Serial.println("Timer gestartet");
    timer_end = millis() + timer_duration;
    timer_counter++;
    Serial.println("timer_counter++");
  }
  if (SpielStatus == 1) {
    SpielStatus = 0;
    SpielEndeZeitstempel = millis();
    BeiSpielAbbruch();
  }
}

void BeiAngehobenZiel() {
  Serial.println("BeiAngehobenZiel()");
}

void BeiAbgelegtZiel() {
  Serial.println("BeiAbgelegtZiel()");
  if (SpielStatus == 1) {
    SpielStatus = 0;
    SpielEndeZeitstempel = millis();
    BeiSpielEnde();
  }
}

void BeiBehruehrungDraht() {
  Serial.println("BeiBehruehrungDraht()");
  if (SpielStatus == 1) {
    FehlerCounter++;
    digitalWrite(buzzer_output_pin, HIGH);

    FehlerTimerAktiv = true;
    FehlerTimerTriggerZeitstempel = millis() + FehlerTimerInterval;

  }
}

void BeiLoslassenDraht() {
  Serial.println("BeiLoslassenDraht()");
  digitalWrite(buzzer_output_pin, LOW);
  FehlerTimerAktiv = false;
}

void BeiSpielStart() {
  Serial.println("BeiSpielStart()");
  FehlerCounter = 0;
  LastAnzeigenUpdateZeitstempel = millis();
}

void BeiSpielEnde() {
  Serial.println("BeiSpielEnde()");
  int Zeit = SpielEndeZeitstempel - SpielStartZeitstempel;
  float ZeitSekunden_Float = (float)(millis() - SpielStartZeitstempel) / 1000.0f;
  int Punkte = CalculatePunkte(FehlerCounter, ZeitSekunden_Float);

  // Highscores
  if (Punkte > Highscore) {
    Highscore = Punkte;
    HighscoreSpeichern();
    BeiAnzeigenAktualisierenInterval();
    playHighscoreSound();
  } else {
    BeiAnzeigenAktualisierenInterval();
    playWinSound();
  }

  digitalWrite(buzzer_output_pin, LOW);
}

void BeiSpielAbbruch() {
  Serial.println("BeiSpielAbbruch()");
}

void BeiFehlerTimerCheck() {
  if (FehlerTimerAktiv) {
    if (SpielStatus == 1) {
      if (FehlerTimerTriggerZeitstempel <= millis()) {
        FehlerTimerTriggerZeitstempel = millis() + FehlerTimerInterval;
        BeiFehlerTimerTrigger();
      }
    } else {
      FehlerTimerAktiv = false;
    }
  }
}

void BeiCounterTrigger() {
  Serial.println("BeiCounterTrigger()");

  if (Highscore != 0) {
    Highscore = 0;
    HighscoreSpeichern();
  }

  digitalWrite(buzzer_output_pin, HIGH);
  delay(50);
  digitalWrite(buzzer_output_pin, LOW);
  delay(50);
  digitalWrite(buzzer_output_pin, HIGH);
  delay(50);
  digitalWrite(buzzer_output_pin, LOW);
  delay(50);
  digitalWrite(buzzer_output_pin, HIGH);
  delay(50);
  digitalWrite(buzzer_output_pin, LOW);
  highscore_segment.println(Highscore);
  highscore_segment.writeDisplay();
}

void BeiFehlerTimerTrigger() {
  // Spieler mit FehlerTimerStrafe strafen!
  FehlerCounter = FehlerCounter + FehlerTimerStrafe;
}

// EIV EVENTS
void BeiErrorInputChange(int state) {
  //Serial.print("BeiErrorInputChange()");
  if (error_input_validation_pending) {
    error_input_validation_pending = false;
    BeiErrorInputValidationCheckAbort(state);
    // Aktueller EIV-Check abbrechen da Statusänderung!
  } else {
    // Starte neuen EIV-Check
    error_input_validation_pending = true;
    error_input_validation_signal = state;
    error_input_validation_end_time = millis() + error_input_validation_delay;
  }
}

void BeiErrorInputValidationCheck() {
  if (error_input_validation_pending) {
    if (error_input_validation_end_time <= millis()) {
      // EIV Zeit erreicht
      error_input_validation_pending = false;
      error_input_validation_end_time = 0L;
      BeiErrorInputValidationCheckSuccess(error_input_validation_signal);
    }
  }
}

void BeiErrorInputValidationCheckAbort(int state) {
  //Serial.println("BeiErrorInputValidationCheckAbort()");
  if (state == LOW) {
    //Serial.println("EIV RESET: Zu LOW gewechselt bevor Validation-Ende!");
  }

  if (state == HIGH) {
    //Serial.println("EIV RESET: Zu HIGH gewechselt bevor Validation-Ende!");
  }
}

void BeiErrorInputValidationCheckSuccess(int state) {
  //Serial.println("BeiErrorInputValidationCheckSuccess()");
  if (state == LOW) {
    //Serial.println("EIV SUCCESS: Signal LOW wurde validiert!");
    BeiBehruehrungDraht();
  }
  if (state == HIGH) {
    //Serial.println("EIV SUCCESS: Signal HIGH wurde validiert!");
    BeiLoslassenDraht();
  }
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

void BeiAnzeigenAktualisierenCheck() {
  if (SpielStatus == 1) {
    long AktuelleZeit = millis();
    if (AktuelleZeit > LastAnzeigenUpdateZeitstempel) {
      BeiAnzeigenAktualisierenInterval();
      LastAnzeigenUpdateZeitstempel = AktuelleZeit + AnzeigenUpdateInterval;
    }
  }
}

void BeiAnzeigenAktualisierenInterval() {
  //Serial.println("BeiAnzeigenAktualisierenInterval()");
  int ZeitSekunden = (millis() - SpielStartZeitstempel) / 1000;
  float ZeitSekunden_Float = (float)(millis() - SpielStartZeitstempel) / 1000.0f;

  int m;
  int s;
  SekundenZuMinutenSekunden(ZeitSekunden, m, s);
  String lcdText = ZeitFormattiert(m, s);

  if ((s % 2) == 0) {
    // Sekunden gerade
    zeit_segment.drawColon(true);
  } else {
    zeit_segment.drawColon(false);
  }

  zeit_segment.println(lcdText);
  zeit_segment.writeDisplay();

  Serial.println(ZeitSekunden_Float);

  int Punkte = CalculatePunkte(FehlerCounter, ZeitSekunden_Float);
  punkte_segment.print(Punkte);
  punkte_segment.writeDisplay();

  fehler_segment.print(FehlerCounter);
  fehler_segment.writeDisplay();

  highscore_segment.println(Highscore);
  highscore_segment.writeDisplay();
}

void writeDigitsRaw(Adafruit_7segment display, int digit0, int digit1, int digit3, int digit4) {
  display.writeDigitRaw(0, digit0);
  display.writeDigitRaw(1, digit1);
  display.writeDigitRaw(3, digit3);
  display.writeDigitRaw(4, digit4);
  display.writeDisplay();
}

void HighscoreSpeichern() {
  EEPROM.put(0, Highscore);
}

void HighscoreLaden() {
  EEPROM.get(0, Highscore);
}

void HighscoreLoeschen() {
  EEPROM.put(0, 0);
  EEPROM.put(1, 0);
}

void playStartAnimation() {
  writeDigitsRaw(punkte_segment, 32, 0, 0, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(punkte_segment, 48, 0, 0, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(punkte_segment, 56, 0, 0, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(punkte_segment, 60, 0, 0, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(punkte_segment, 62, 0, 0, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(punkte_segment, 63, 0, 0, 0);
  delay(startupAnimationDelay);

  writeDigitsRaw(punkte_segment, 63, 32, 0, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(punkte_segment, 63, 48, 0, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(punkte_segment, 63, 56, 0, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(punkte_segment, 63, 60, 0, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(punkte_segment, 63, 62, 0, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(punkte_segment, 63, 63, 0, 0);
  delay(startupAnimationDelay);

  writeDigitsRaw(punkte_segment, 63, 63, 32, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(punkte_segment, 63, 63, 48, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(punkte_segment, 63, 63, 56, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(punkte_segment, 63, 63, 60, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(punkte_segment, 63, 63, 62, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(punkte_segment, 63, 63, 63, 0);
  delay(startupAnimationDelay);

  writeDigitsRaw(punkte_segment, 63, 63, 63, 32);
  delay(startupAnimationDelay);
  writeDigitsRaw(punkte_segment, 63, 63, 63, 48);
  delay(startupAnimationDelay);
  writeDigitsRaw(punkte_segment, 63, 63, 63, 56);
  delay(startupAnimationDelay);
  writeDigitsRaw(punkte_segment, 63, 63, 63, 60);
  delay(startupAnimationDelay);
  writeDigitsRaw(punkte_segment, 63, 63, 63, 62);
  delay(startupAnimationDelay);
  writeDigitsRaw(punkte_segment, 63, 63, 63, 63);
  delay(startupAnimationDelay);

  // -----------------------------------

  writeDigitsRaw(highscore_segment, 32, 0, 0, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(highscore_segment, 48, 0, 0, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(highscore_segment, 56, 0, 0, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(highscore_segment, 60, 0, 0, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(highscore_segment, 62, 0, 0, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(highscore_segment, 63, 0, 0, 0);
  delay(startupAnimationDelay);

  writeDigitsRaw(highscore_segment, 63, 32, 0, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(highscore_segment, 63, 48, 0, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(highscore_segment, 63, 56, 0, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(highscore_segment, 63, 60, 0, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(highscore_segment, 63, 62, 0, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(highscore_segment, 63, 63, 0, 0);
  delay(startupAnimationDelay);

  writeDigitsRaw(highscore_segment, 63, 63, 32, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(highscore_segment, 63, 63, 48, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(highscore_segment, 63, 63, 56, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(highscore_segment, 63, 63, 60, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(highscore_segment, 63, 63, 62, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(highscore_segment, 63, 63, 63, 0);
  delay(startupAnimationDelay);

  writeDigitsRaw(highscore_segment, 63, 63, 63, 32);
  delay(startupAnimationDelay);
  writeDigitsRaw(highscore_segment, 63, 63, 63, 48);
  delay(startupAnimationDelay);
  writeDigitsRaw(highscore_segment, 63, 63, 63, 56);
  delay(startupAnimationDelay);
  writeDigitsRaw(highscore_segment, 63, 63, 63, 60);
  delay(startupAnimationDelay);
  writeDigitsRaw(highscore_segment, 63, 63, 63, 62);
  delay(startupAnimationDelay);
  writeDigitsRaw(highscore_segment, 63, 63, 63, 63);
  delay(startupAnimationDelay);

  // -----------------------------------

  writeDigitsRaw(zeit_segment, 32, 0, 0, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(zeit_segment, 48, 0, 0, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(zeit_segment, 56, 0, 0, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(zeit_segment, 60, 0, 0, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(zeit_segment, 62, 0, 0, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(zeit_segment, 63, 0, 0, 0);
  delay(startupAnimationDelay);

  writeDigitsRaw(zeit_segment, 63, 32, 0, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(zeit_segment, 63, 48, 0, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(zeit_segment, 63, 56, 0, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(zeit_segment, 63, 60, 0, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(zeit_segment, 63, 62, 0, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(zeit_segment, 63, 63, 0, 0);
  delay(startupAnimationDelay);

  writeDigitsRaw(zeit_segment, 63, 63, 32, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(zeit_segment, 63, 63, 48, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(zeit_segment, 63, 63, 56, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(zeit_segment, 63, 63, 60, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(zeit_segment, 63, 63, 62, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(zeit_segment, 63, 63, 63, 0);
  delay(startupAnimationDelay);

  writeDigitsRaw(zeit_segment, 63, 63, 63, 32);
  delay(startupAnimationDelay);
  writeDigitsRaw(zeit_segment, 63, 63, 63, 48);
  delay(startupAnimationDelay);
  writeDigitsRaw(zeit_segment, 63, 63, 63, 56);
  delay(startupAnimationDelay);
  writeDigitsRaw(zeit_segment, 63, 63, 63, 60);
  delay(startupAnimationDelay);
  writeDigitsRaw(zeit_segment, 63, 63, 63, 62);
  delay(startupAnimationDelay);
  writeDigitsRaw(zeit_segment, 63, 63, 63, 63);
  delay(startupAnimationDelay);

  // -----------------------------------

  writeDigitsRaw(fehler_segment, 32, 0, 0, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(fehler_segment, 48, 0, 0, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(fehler_segment, 56, 0, 0, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(fehler_segment, 60, 0, 0, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(fehler_segment, 62, 0, 0, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(fehler_segment, 63, 0, 0, 0);
  delay(startupAnimationDelay);

  writeDigitsRaw(fehler_segment, 63, 32, 0, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(fehler_segment, 63, 48, 0, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(fehler_segment, 63, 56, 0, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(fehler_segment, 63, 60, 0, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(fehler_segment, 63, 62, 0, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(fehler_segment, 63, 63, 0, 0);
  delay(startupAnimationDelay);

  writeDigitsRaw(fehler_segment, 63, 63, 32, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(fehler_segment, 63, 63, 48, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(fehler_segment, 63, 63, 56, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(fehler_segment, 63, 63, 60, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(fehler_segment, 63, 63, 62, 0);
  delay(startupAnimationDelay);
  writeDigitsRaw(fehler_segment, 63, 63, 63, 0);
  delay(startupAnimationDelay);

  writeDigitsRaw(fehler_segment, 63, 63, 63, 32);
  delay(startupAnimationDelay);
  writeDigitsRaw(fehler_segment, 63, 63, 63, 48);
  delay(startupAnimationDelay);
  writeDigitsRaw(fehler_segment, 63, 63, 63, 56);
  delay(startupAnimationDelay);
  writeDigitsRaw(fehler_segment, 63, 63, 63, 60);
  delay(startupAnimationDelay);
  writeDigitsRaw(fehler_segment, 63, 63, 63, 62);
  delay(startupAnimationDelay);
  writeDigitsRaw(fehler_segment, 63, 63, 63, 63);

  delay(2000);

  // -----------------------------------

  writeDigitsRaw(punkte_segment, 0, 0, 0, 0);
  writeDigitsRaw(highscore_segment, 0, 0, 0, 0);
  writeDigitsRaw(zeit_segment, 0, 0, 0, 0);
  writeDigitsRaw(fehler_segment, 0, 0, 0, 0);
}

void playWinSound() {
  // TODO
  pinMode(buzzer_output_pin, OUTPUT);
  // put your setup code here, to run once:
  tone(buzzer_output_pin, 261);
  delay(750);
  tone(buzzer_output_pin, 329, 1500);
  delay(750);
  tone(buzzer_output_pin, 523, 1500);
  delay(1000);
  noTone(buzzer_output_pin);
}

void playHighscoreSound() {
  // TODO
  pinMode(buzzer_output_pin, OUTPUT);
  tone(buzzer_output_pin, 261 * 2);
  delay(750);
  tone(buzzer_output_pin, 329 * 2);
  delay(750);
  
  tone(buzzer_output_pin, 523 * 2);
  delay(250);
  noTone(buzzer_output_pin);
  delay(50);

  tone(buzzer_output_pin, 523 * 2);
  delay(250);
  noTone(buzzer_output_pin);
  delay(50);

  tone(buzzer_output_pin, 523 * 2);
  delay(250);
  noTone(buzzer_output_pin);
  delay(50);

  tone(buzzer_output_pin, 523 * 2);
  delay(750);
    
  noTone(buzzer_output_pin);
}

String ZeitFormattiert(int m, int s) {
  String myString;

  if (m <= 9) {
    myString.concat("0");
    myString.concat(m);
  }
  if (m > 9 && m <= 99) {
    myString.concat(m);
  }

  if (s <= 9) {
    myString.concat("0");
    myString.concat(s);
  }
  if (s > 9 && s <= 99) {
    myString.concat(s);
  }

  return myString;
}

void SekundenZuMinutenSekunden( const uint32_t seconds, int &m, int &s )
{
  uint32_t t = seconds;
  s = t % 60;
  t = (t - s) / 60;
  m = t % 60;
  t = (t - m) / 60;
}
