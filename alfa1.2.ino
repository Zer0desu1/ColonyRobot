#include <WiFi.h>
#include "esp_timer.h"
#include "Arduino.h"
#include "esp_http_server.h"
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
const char* ssid = "";
const char* password = "";

#define WS_SERVER_IP "192.168.1.5"
#define WS_SERVER_PORT 8000
#define WS_SERVER_PATH "/"


#define DEVICE_TYPE "alfaController"
#define DEVICE_ID "cm5tbgiuh0003xhnngr0q42g9"
#define SOCKET_ID "15621321"

#define FRAME_INTERVAL 30  // 30ms between frames
unsigned long lastFrame = 0;
unsigned long lastCommandTime = 0;
const unsigned long COMMAND_TIMEOUT = 100; // 100ms timeout

#define MOTOR_1_PIN_1    26
#define MOTOR_1_PIN_2    25
#define MOTOR_2_PIN_1    32
#define MOTOR_2_PIN_2    33
#define MOTOR_3_PIN_1    27
#define MOTOR_3_PIN_2    14
httpd_handle_t server = NULL;


#define USE_SERIAL Serial


WiFiMulti WiFiMulti;
WebSocketsClient webSocket;

void stopMotors() {
    analogWrite(MOTOR_1_PIN_1, 0);
    analogWrite(MOTOR_1_PIN_2, 0);
    analogWrite(MOTOR_2_PIN_1, 0);
    analogWrite(MOTOR_2_PIN_2, 0);
    analogWrite(MOTOR_3_PIN_1, 0);
    analogWrite(MOTOR_3_PIN_2, 0);
}

void moveForward() {
    analogWrite(MOTOR_1_PIN_1, 255);  // Right motor forward
    analogWrite(MOTOR_1_PIN_2, 0);
    analogWrite(MOTOR_2_PIN_1, 255);  // Left motor forward
    analogWrite(MOTOR_2_PIN_2, 0);
    analogWrite(MOTOR_3_PIN_1, 0);    // Third motor stop
    analogWrite(MOTOR_3_PIN_2, 0);
}

void moveBackward() {
    analogWrite(MOTOR_1_PIN_1, 0);    // Right motor backward
    analogWrite(MOTOR_1_PIN_2, 255);
    analogWrite(MOTOR_2_PIN_1, 0);    // Left motor backward
    analogWrite(MOTOR_2_PIN_2, 255);
    analogWrite(MOTOR_3_PIN_1, 0);    // Third motor stop
    analogWrite(MOTOR_3_PIN_2, 0);
}

void turnLeft() {
    analogWrite(MOTOR_1_PIN_1, 255);  // Right motor forward
    analogWrite(MOTOR_1_PIN_2, 0);
    analogWrite(MOTOR_2_PIN_1, 0);    // Left motor backward
    analogWrite(MOTOR_2_PIN_2, 255);
    analogWrite(MOTOR_3_PIN_1, 0);    // Third motor stop
    analogWrite(MOTOR_3_PIN_2, 0);
}

void turnRight() {
    analogWrite(MOTOR_1_PIN_1, 0);    // Right motor backward
    analogWrite(MOTOR_1_PIN_2, 255);
    analogWrite(MOTOR_2_PIN_1, 255);  // Left motor forward
    analogWrite(MOTOR_2_PIN_2, 0);
    analogWrite(MOTOR_3_PIN_1, 0);    // Third motor stop
    analogWrite(MOTOR_3_PIN_2, 0);
}

// Special movements using all three motors
void move1() {
    analogWrite(MOTOR_1_PIN_1, 0);
    analogWrite(MOTOR_1_PIN_2, 255);
    analogWrite(MOTOR_2_PIN_1, 255);
    analogWrite(MOTOR_2_PIN_2, 0);
    analogWrite(MOTOR_3_PIN_1, 255);
    analogWrite(MOTOR_3_PIN_2, 0);
}

void move2() {
    analogWrite(MOTOR_1_PIN_1, 255);
    analogWrite(MOTOR_1_PIN_2, 0);
    analogWrite(MOTOR_2_PIN_1, 0);
    analogWrite(MOTOR_2_PIN_2, 255);
    analogWrite(MOTOR_3_PIN_1, 0);
    analogWrite(MOTOR_3_PIN_2, 255);
}

void move3() {
    analogWrite(MOTOR_1_PIN_1, 0);
    analogWrite(MOTOR_1_PIN_2, 0);
    analogWrite(MOTOR_2_PIN_1, 255);
    analogWrite(MOTOR_2_PIN_2, 0);
    analogWrite(MOTOR_3_PIN_1, 0);
    analogWrite(MOTOR_3_PIN_2, 255);
}



void sendConnectionMessage() {
    StaticJsonDocument<200> doc;
    doc["type"] = "connection:team";
    doc["robotId"] = DEVICE_ID;
    doc["socket_id"] = SOCKET_ID;
    doc["timestamp"] = millis();

    String jsonString;
    serializeJson(doc, jsonString);
    webSocket.sendTXT(jsonString);
}

void webSocketEvent(WStype_t type, uint8_t *payload, size_t length) {
    String jsonString;  // Moved outside switch for reuse
    
    switch (type) {
        case WStype_DISCONNECTED: {
            USE_SERIAL.printf("[WSc] Disconnected!\n");
            stopMotors(); // Safety: stop motors on disconnection
            break;
        }
            
        case WStype_CONNECTED: {
            USE_SERIAL.printf("[WSc] Connected to url: %s\n", payload);
            // Use smaller JSON document for connection message
            StaticJsonDocument<128> connectDoc;
            connectDoc["type"] = "connection:team";
            connectDoc["robotId"] = DEVICE_ID;
            connectDoc["socket_id"] = SOCKET_ID;
            connectDoc["timestamp"] = millis();

            serializeJson(connectDoc, jsonString);
            webSocket.sendTXT(jsonString);
            break;
        }
            
        case WStype_TEXT: {
            USE_SERIAL.printf("[WSc] Received text: %s\n", payload);
            
            // Use a smaller JSON document size
            StaticJsonDocument<128> doc;
            DeserializationError error = deserializeJson(doc, payload);
            
            if (error) {
                USE_SERIAL.println("Failed to parse JSON");
                return;
            }
            
            // Check if it's a motor command
            const char* msgType = doc["type"];
            if (msgType && strcmp(msgType, "robot:command") == 0) {
                const char* direction = doc["direction"];
                bool isPressed = doc["pressed"]; // New field to check if button is pressed
                
                if (isPressed && direction) {
                    lastCommandTime = millis(); // Update last command time
                    
                    if (strcmp(direction, "forward") == 0) {
                        moveForward();
                    }
                    else if (strcmp(direction, "backward") == 0) {
                        moveBackward();
                    }
                    else if (strcmp(direction, "left") == 0) {
                        turnLeft();
                    }
                    else if (strcmp(direction, "right") == 0) {
                        turnRight();
                    }
                    else if (strcmp(direction, "rotatecw") == 0) {
                        move1();
                    }
                    else if (strcmp(direction, "rotateccw") == 0) {
                        move2();
                    }
                } else {
                    stopMotors();
                }
                
                // Use a separate document for acknowledgment to avoid memory issues
                StaticJsonDocument<128> ack;
                ack["type"] = "robot:command_ack";
                ack["robot_id"] = DEVICE_ID;
                ack["socket_id"] = SOCKET_ID;
                ack["direction"] = direction;
                ack["pressed"] = isPressed;
                ack["timestamp"] = millis();
                
                serializeJson(ack, jsonString);
                webSocket.sendTXT(jsonString);
            }
            break;
        }
            
        case WStype_BIN:
        case WStype_ERROR:
        case WStype_FRAGMENT_TEXT_START:
        case WStype_FRAGMENT_BIN_START:
        case WStype_FRAGMENT:
        case WStype_FRAGMENT_FIN:
        default: {
            break;
        }
    }
}


static const char PROGMEM INDEX_HTML[] = R"rawliteral(
<html>
  <head>
    <title>ESP32 Robot</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
      body { font-family: Arial; text-align: center; margin:0px auto; padding-top: 30px;}
      table { margin-left: auto; margin-right: auto; }
      td { padding: 8px; }
      .button {
        background-color: #2f4468;
        border: none;
        color: white;
        padding: 10px 20px;
        text-align: center;
        text-decoration: none;
        display: inline-block;
        font-size: 18px;
        margin: 6px 3px;
        cursor: pointer;
        -webkit-touch-callout: none;
        -webkit-user-select: none;
        -khtml-user-select: none;
        -moz-user-select: none;
        -ms-user-select: none;
        user-select: none;
        -webkit-tap-highlight-color: rgba(0,0,0,0);
      }
    </style>
  </head>
  <body>
    <h1>ESP32 Robot</h1>
    <table>
      <tr><td colspan="3" align="center"><button class="button" onmousedown="toggleCheckbox('forward');" ontouchstart="toggleCheckbox('forward');" onmouseup="toggleCheckbox('stop');" ontouchend="toggleCheckbox('stop');">Forward</button></td></tr>
      <tr><td align="center"><button class="button" onmousedown="toggleCheckbox('left');" ontouchstart="toggleCheckbox('left');" onmouseup="toggleCheckbox('stop');" ontouchend="toggleCheckbox('stop');">Left</button></td><td align="center"><button class="button" onmousedown="toggleCheckbox('stop');" ontouchstart="toggleCheckbox('stop');">Stop</button></td><td align="center"><button class="button" onmousedown="toggleCheckbox('right');" ontouchstart="toggleCheckbox('right');" onmouseup="toggleCheckbox('stop');" ontouchend="toggleCheckbox('stop');">Right</button></td></tr>
      <tr><td colspan="3" align="center"><button class="button" onmousedown="toggleCheckbox('backward');" ontouchstart="toggleCheckbox('backward');" onmouseup="toggleCheckbox('stop');" ontouchend="toggleCheckbox('stop');">Backward</button></td></tr>
      <tr><td colspan="3" align="center"><button class="button" onmousedown="toggleCheckbox('move1');" ontouchstart="toggleCheckbox('move1');" onmouseup="toggleCheckbox('stop');" ontouchend="toggleCheckbox('stop');">move1</button></td></tr>
      <tr><td colspan="3" align="center"><button class="button" onmousedown="toggleCheckbox('move12');" ontouchstart="toggleCheckbox('move12');" onmouseup="toggleCheckbox('stop');" ontouchend="toggleCheckbox('stop');">move12</button></td></tr>
      <tr><td colspan="3" align="center"><button class="button" onmousedown="toggleCheckbox('move2');" ontouchstart="toggleCheckbox('move2');" onmouseup="toggleCheckbox('stop');" ontouchend="toggleCheckbox('stop');">move2</button></td></tr> 
      <tr><td colspan="3" align="center"><button class="button" onmousedown="toggleCheckbox('move22');" ontouchstart="toggleCheckbox('move22');" onmouseup="toggleCheckbox('stop');" ontouchend="toggleCheckbox('stop');">move22</button></td></tr>
      <tr><td colspan="3" align="center"><button class="button" onmousedown="toggleCheckbox('move3');" ontouchstart="toggleCheckbox('move3');" onmouseup="toggleCheckbox('stop');" ontouchend="toggleCheckbox('stop');">move3</button></td></tr>   
      <tr><td colspan="3" align="center"><button class="button" onmousedown="toggleCheckbox('move32');" ontouchstart="toggleCheckbox('move32');" onmouseup="toggleCheckbox('stop');" ontouchend="toggleCheckbox('stop');">move32</button></td></tr>                              
    </table>
    <script>
      function toggleCheckbox(x) {
        var xhr = new XMLHttpRequest();
        xhr.open("GET", "/action?go=" + x, true);
        xhr.send();
      }
    </script>
  </body>
</html>
)rawliteral";

static esp_err_t index_handler(httpd_req_t *req){
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, (const char *)INDEX_HTML, strlen(INDEX_HTML));
}

static esp_err_t cmd_handler(httpd_req_t *req){
  char* buf;
  size_t buf_len;
  char variable[32] = {0,};
  
  buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1) {
    buf = (char*)malloc(buf_len);
    if(!buf){
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }
    if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
      if (httpd_query_key_value(buf, "go", variable, sizeof(variable)) == ESP_OK) {
      } else {
        free(buf);
        httpd_resp_send_404(req);
        return ESP_FAIL;
      }
    } else {
      free(buf);
      httpd_resp_send_404(req);
      return ESP_FAIL;
    }
    free(buf);
  } else {
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }

  int res = 0;
  
  if(!strcmp(variable, "forward")) {
    Serial.println("Forward");
    analogWrite(33,255);analogWrite(32,0);
    analogWrite(25,255);analogWrite(26,0);
    analogWrite(27,0);analogWrite(14,0);
  }
  else if(!strcmp(variable, "move1")) {
    Serial.println("move1");
    analogWrite(33,255);analogWrite(32,0);
    analogWrite(25,0);analogWrite(26,255);
    analogWrite(27,0);analogWrite(14,255);
  }
  else if(!strcmp(variable, "move12")) {
    Serial.println("move12");
    analogWrite(33,0);analogWrite(32,255);
    analogWrite(25,255);analogWrite(26,0);
    analogWrite(27,255);analogWrite(14,0);
  }
  else if(!strcmp(variable, "move2")) {
    Serial.println("move2");
    analogWrite(33,255);analogWrite(32,0);
    analogWrite(25,0);analogWrite(26,0);
    analogWrite(27,255);analogWrite(14,0);
  }
  else if(!strcmp(variable, "move3")) {
    Serial.println("move3");
    analogWrite(33,0);analogWrite(32,0);
    analogWrite(25,255);analogWrite(26,0);
    analogWrite(27,0);analogWrite(14,255);
  }
    else if(!strcmp(variable, "move32")) {
    Serial.println("move32");
    analogWrite(33,0);analogWrite(32,0);
    analogWrite(25,0);analogWrite(26,255);
    analogWrite(27,255);analogWrite(14,0);
  }
  else if(!strcmp(variable, "move22")) {
    Serial.println("move22");
    analogWrite(33,0);analogWrite(32,255);
    analogWrite(25,0);analogWrite(26,0);
    analogWrite(27,0);analogWrite(14,255);
  }
  else if(!strcmp(variable, "backward")) {
    Serial.println("Backward");
    analogWrite(33,0);analogWrite(32,255);
    analogWrite(25,0);analogWrite(26,255);
    analogWrite(27,0);analogWrite(14,0);
  }
  else if(!strcmp(variable, "stop")) {
    Serial.println("Stop");
    analogWrite(32,0);analogWrite(33,0);
    analogWrite(25,0);analogWrite(26,0);
    analogWrite(27,0);analogWrite(14,0);
  }
  else {
    res = -1;
  }

  if(res){
    return httpd_resp_send_500(req);
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}

void startServer(){
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  httpd_uri_t index_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = index_handler,
    .user_ctx  = NULL
  };

  httpd_uri_t cmd_uri = {
    .uri       = "/action",
    .method    = HTTP_GET,
    .handler   = cmd_handler,
    .user_ctx  = NULL
  };

  if (httpd_start(&server, &config) == ESP_OK) {
    httpd_register_uri_handler(server, &index_uri);
    httpd_register_uri_handler(server, &cmd_uri);
  }
}

void setup() {
  pinMode(MOTOR_1_PIN_1, OUTPUT);
  pinMode(MOTOR_1_PIN_2, OUTPUT);
  pinMode(MOTOR_2_PIN_1, OUTPUT);
  pinMode(MOTOR_2_PIN_2, OUTPUT);
  pinMode(MOTOR_3_PIN_1, OUTPUT);
  pinMode(MOTOR_3_PIN_2, OUTPUT);
  stopMotors();
  Serial.begin(115200);
  
  // Set up the ESP32 as an access point
  WiFiMulti.addAP(ssid, password);
  while (WiFiMulti.run() != WL_CONNECTED) {
      USE_SERIAL.print(".");
      delay(500);
  }
  USE_SERIAL.printf("\n[SETUP] WiFi Connected, IP: %s\n", WiFi.localIP().toString().c_str());
    
  // Initialize WebSocket connection
  webSocket.begin(WS_SERVER_IP, WS_SERVER_PORT, WS_SERVER_PATH);
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);
  //IPAddress IP = WiFi.softAPIP();
  Serial.print("Access Point IP address: ");
  //Serial.println(IP);

  // Start web server
  startServer();
}

void loop() {
  webSocket.loop();
    if (millis() - lastCommandTime > COMMAND_TIMEOUT) {
      stopMotors();
    }
    // Frame rate control
    unsigned long currentMillis = millis();
    
    
}
