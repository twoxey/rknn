#include <WiFi.h>

#define OUT1_PIN 33
#define OUT2_PIN 25
#define OUT3_PIN 26
#define OUT4_PIN 27

enum Output_Value {
  Output1 = (1<<0),
  Output2 = (1<<1),
  Output3 = (1<<2),
  Output4 = (1<<3),
};


NetworkClient connection;

void setup() {
  Serial.begin(115200);

  pinMode(OUT1_PIN, OUTPUT);
  pinMode(OUT2_PIN, OUTPUT);
  pinMode(OUT3_PIN, OUTPUT);
  pinMode(OUT4_PIN, OUTPUT);
}

void loop() {
  if (Serial.available()) {
    int ch = Serial.read();
    switch (ch) {
      case 'H': {
        String input = Serial.readStringUntil('\n');
        if (input) {
          input.trim();
          host = input;
          Serial.printf("Set host: %s\n", host.c_str());
          try_connect();
        }
        break;
      }
      case '1': digitalWrite(OUT1_PIN, !digitalRead(OUT1_PIN)); break;
      case '2': digitalWrite(OUT2_PIN, !digitalRead(OUT2_PIN)); break;
      case '3': digitalWrite(OUT3_PIN, !digitalRead(OUT3_PIN)); break;
      case '4': digitalWrite(OUT4_PIN, !digitalRead(OUT4_PIN)); break;
    }
  }

  static unsigned long last_try_time; 
  if (!connection.connected()) {
    if (millis() > last_try_time + 2000) {
      try_connect();
      last_try_time = millis();
    }
  }

  while (connection.available()) {
    int output_state = connection.read();
    Serial.printf("Got output command: %d\n", output_state);
    digitalWrite(OUT1_PIN, output_state & Output1);
    digitalWrite(OUT2_PIN, output_state & Output2);
    digitalWrite(OUT3_PIN, output_state & Output3);
    digitalWrite(OUT4_PIN, output_state & Output4);
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
