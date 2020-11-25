#include <ESP8266WiFi.h>

#ifndef STASSID
#define STASSID "????"
#define STAPSK  "????"
#endif

const char* ssid     = STASSID;
const char* password = STAPSK;

// Direct addressing, currently need to hardcode the ATEM's DNS name or IP address
const char* host = "192.168.1.117"; 
const uint16_t port = 9990;

#define CAM_1_LED 16
#define CAM_2_LED  5
#define CAM_3_LED  4
#define CAM_4_LED  0

#define CAM_COUNT  4

const int ledPorts[] = {
  CAM_1_LED,
  CAM_2_LED,
  CAM_3_LED,
  CAM_4_LED
};

// Inputs are wired to pull down when active
#define CAM_1_TRIGGER 14
#define CAM_2_TRIGGER 12
#define CAM_3_TRIGGER 13
#define CAM_4_TRIGGER  2

const int triggerPorts[] = {
  CAM_1_TRIGGER,
  CAM_2_TRIGGER,
  CAM_3_TRIGGER,
  CAM_4_TRIGGER
};

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < CAM_COUNT; i++) {
    pinMode(ledPorts[i], OUTPUT);
    pinMode(triggerPorts[i], INPUT_PULLUP);
  }

  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
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

WiFiClient client;

#define CONNECTING 0
#define CONNECTED  1

int status = CONNECTING;

#define POLL_INTERVAL 1000
unsigned long last = millis();

int channel = 1;

char outbuffer[64];

#define DEBOUNCE_PERIOD 150
unsigned long debouncing = 0;

int readButtonsNow() {
  int key = digitalRead(CAM_1_TRIGGER) ? 0 : 1;
  key = digitalRead(CAM_2_TRIGGER) ? key : 2;
  key = digitalRead(CAM_3_TRIGGER) ? key : 3;
  key = digitalRead(CAM_4_TRIGGER) ? key : 4;
  return key;
}

bool armed = true;
unsigned long deupbounce = 0;
bool rearming = false;

int buttonPressed() {
  unsigned long now = millis();
  if (armed) {
    int key = readButtonsNow();
    if (key != 0) {
      debouncing = now;
      armed = false;
    }
    return key;
  } else {
    if (now - debouncing > DEBOUNCE_PERIOD) {
      if (readButtonsNow() == 0) {
        deupbounce = now;
        rearming = true;
      }
    }
    if (rearming && (now - deupbounce > DEBOUNCE_PERIOD)) {
      rearming = false;
      armed = true;
    }
  }
  return 0;
}

#define IN_BUFFER_SIZE 127

char inputBuffer[IN_BUFFER_SIZE + 1];
int inputCount = 0;

char * readIfLine() {
  while (client.available()) {
    char ch = static_cast<char>(client.read());
    if (ch == '\n') {
      inputBuffer[inputCount] = 0;
      inputCount = 0;
      return inputBuffer;
    } else {
      inputBuffer[inputCount] = ch;
      if (inputCount < IN_BUFFER_SIZE) {
        inputCount++;
      } else {
        Serial.println("**** INPUT OVERFLOW");
      }
    }
  }
  return NULL;
}

byte currentChannel = 0;

#define NO_INPUT 0
#define READING_ROUTES 1

byte inputMode = NO_INPUT;

void loop() {
  if (status == CONNECTING) {
    Serial.print("connecting to ");
    Serial.print(host);
    Serial.print(':');
    Serial.println(port);

    // Use WiFiClient class to create TCP connections
    if (!client.connect(host, port)) {
      Serial.println("connection failed pausing");
      delay(5000);
      return;
    }
    status = CONNECTED;
  }

  if (status == CONNECTED) {
    // look for input lines:
    char * line = readIfLine();
    if (line != NULL) {
      //      Serial.println(line);
      if (inputMode == NO_INPUT) {
        // look for "VIDEO OUTPUT ROUTING:"
        if (strcmp(line, "VIDEO OUTPUT ROUTING:") == 0) {
          Serial.println("found V.O.R:");
          inputMode = READING_ROUTES;
        }
      } else if (inputMode == READING_ROUTES) {
        if (strlen(line) == 0) {
          // found end of block
          inputMode == NO_INPUT;
        } else {
          // should be digit space digit, look for first digit == 1
          if (line[0] == '1') {
            byte channel = line[2] - '0';

            if (channel != currentChannel) {
              currentChannel = channel;
              Serial.print("Channel changed to: "); Serial.println(currentChannel);
              for (int i = 0; i < CAM_COUNT; i++) {
                digitalWrite(ledPorts[i], (i + 1 == currentChannel) ? 0 : 1);
              }
            }
          }
        }
      }
    }

    int channel = buttonPressed();
    if (channel != 0) {
      Serial.print("**** Button pressed "); Serial.println(channel);
      sprintf(outbuffer, "VIDEO OUTPUT ROUTING:\n1 %c\n\n", '0' + channel);
      client.println(outbuffer);
    }

    // kick at intervals, just in case...
    unsigned long now = millis();
    if (now - last >= POLL_INTERVAL) {
      client.println("VIDEO OUTPUT ROUTING:\n\n");
      last = now;
    }
  }
}
