#include <WiFi.h>

#include "haptic-motor-states.h"

#define OUT1_PIN 33
#define OUT2_PIN 25
#define OUT3_PIN 26
#define OUT4_PIN 27

#define LED_PIN 2

typedef struct {
  int pin;
  unsigned long last_interval;
  int interval_ms;
  int duration_ms;
} Ouput_Control;

NetworkServer server(8080);
NetworkClient connection;

Ouput_Control controls[HAPTIC_MOTOR_COUNT] = {};

void set_output_states(uint8_t states) {
  Serial.printf("Got byte value: %u\n", states);

  for (int i = 0; i < HAPTIC_MOTOR_COUNT; ++i) {
    int state = (states >> (i * HAPTIC_STATE_BITS)) & HAPTIC_STATE_BIT_MASK;
    switch (state) {
      case Haptic_Stop:
        controls[i].interval_ms = 0;
        controls[i].duration_ms = 0;
        break;

      case Haptic_Slow:
        controls[i].interval_ms = 2000;
        controls[i].duration_ms = 500;
        break;

      case Haptic_Fast:
        controls[i].interval_ms = 600;
        controls[i].duration_ms = 300;
        break;
    }
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);

  pinMode(OUT1_PIN, OUTPUT);
  pinMode(OUT2_PIN, OUTPUT);
  pinMode(OUT3_PIN, OUTPUT);
  pinMode(OUT4_PIN, OUTPUT);

  controls[0].pin = OUT1_PIN;
  controls[1].pin = OUT2_PIN;
  controls[2].pin = OUT3_PIN;
  controls[3].pin = OUT4_PIN;

  WiFi.softAP("ESP-hapic");

  server.begin();
}

uint8_t soft_ap_station_num = 0;

void loop() {
  unsigned long now = millis();

  uint8_t new_station_num = WiFi.softAPgetStationNum();
  if (new_station_num != soft_ap_station_num) {
    Serial.printf("AP num changed: %u\n", new_station_num);
  }
  soft_ap_station_num = new_station_num;

  for (int i = 0; i < HAPTIC_MOTOR_COUNT; ++i) {
    Ouput_Control* o = &controls[i];
    long dt_ms = now - o->last_interval;
    if (dt_ms > o->interval_ms) {
      o->last_interval = now;
    }
    digitalWrite(o->pin, o->duration_ms > 0 && dt_ms < o->duration_ms);
  }

  if (Serial.available()) {
    int ch = Serial.read();
    switch (ch) {
      case '0': memset(controls, 0, sizeof(controls)); break;
      case '1': digitalWrite(OUT1_PIN, !digitalRead(OUT1_PIN)); break;
      case '2': digitalWrite(OUT2_PIN, !digitalRead(OUT2_PIN)); break;
      case '3': digitalWrite(OUT3_PIN, !digitalRead(OUT3_PIN)); break;
      case '4': digitalWrite(OUT4_PIN, !digitalRead(OUT4_PIN)); break;
    }
  }

  if (!connection.connected()) {
    connection = server.accept();
    if (connection) {
      String ip = connection.remoteIP().toString();
      Serial.printf("Got new connection, IP: %s\n", ip.c_str());
    }
  }

  if (connection.connected()) {
    static unsigned long last_toggle_ms;
    if (millis() > last_toggle_ms + 500) {
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      last_toggle_ms = millis();
    }
  } else {
    digitalWrite(LED_PIN, 0);
  }

  while (connection.available()) {
    int haptic_states = connection.read();
    set_output_states(haptic_states);
  }
}
