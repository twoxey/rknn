#include <WiFi.h>

#define OUT1_PIN 33
#define OUT2_PIN 25
#define OUT3_PIN 26
#define OUT4_PIN 27

#define LED_PIN 2

enum Output_Value {
  Output1 = (1<<0),
  Output2 = (1<<1),
  Output3 = (1<<2),
  Output4 = (1<<3),
};

NetworkServer server(8080);
NetworkClient connection;

void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);

  pinMode(OUT1_PIN, OUTPUT);
  pinMode(OUT2_PIN, OUTPUT);
  pinMode(OUT3_PIN, OUTPUT);
  pinMode(OUT4_PIN, OUTPUT);

  WiFi.softAP("ESP-hapic");

  server.begin();
}

void loop() {
  if (Serial.available()) {
    int ch = Serial.read();
    switch (ch) {
      case '1': digitalWrite(OUT1_PIN, !digitalRead(OUT1_PIN)); break;
      case '2': digitalWrite(OUT2_PIN, !digitalRead(OUT2_PIN)); break;
      case '3': digitalWrite(OUT3_PIN, !digitalRead(OUT3_PIN)); break;
      case '4': digitalWrite(OUT4_PIN, !digitalRead(OUT4_PIN)); break;
    }
  }

  if (!connection.connected()) {
    connection = server.accept();
    if (connection) {
      Serial.println("Got new connection\n");
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
    // int output_state = connection.read();
    // Serial.printf("Got output command: %d\n", output_state);
    // digitalWrite(OUT1_PIN, output_state & Output1);
    // digitalWrite(OUT2_PIN, output_state & Output2);
    // digitalWrite(OUT3_PIN, output_state & Output3);
    // digitalWrite(OUT4_PIN, output_state & Output4);

    int ch = connection.read();
    switch (ch) {
      case '1': digitalWrite(OUT1_PIN, !digitalRead(OUT1_PIN)); break;
      case '2': digitalWrite(OUT2_PIN, !digitalRead(OUT2_PIN)); break;
      case '3': digitalWrite(OUT3_PIN, !digitalRead(OUT3_PIN)); break;
      case '4': digitalWrite(OUT4_PIN, !digitalRead(OUT4_PIN)); break;
    }
  }
}

/*

// Old code for testing 

const int port = 8080;
String host = "";

void connect_wifi() {
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void try_connect() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (host == "") return;
  Serial.printf("Try to connect to host: %s\n", host.c_str());
  if (connection.connect(host.c_str(), port)) {
    Serial.println("connected");
    connection.print("ESP hello from esp");
  } else {
    Serial.println("connect failed");
  }
}

*/
