#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include "esp_camera.h"
#include "esp_timer.h"
#include "img_converters.h"
#include "fb_gfx.h"
#include "soc/soc.h"             // disable brownout problems
#include "soc/rtc_cntl_reg.h"    // disable brownout problems
#include "esp_http_server.h"
#include <mbedtls/base64.h>
unsigned long lastCommandTime = 0;
const unsigned long COMMAND_TIMEOUT = 100; // 100ms timeout
// Replace with your WiFi network credentials
const char* ssid = "VodafoneNet-V";  // Change to your actual WiFi name
const char* password = "520940330666";  // Change to your actual WiFi password

#define USE_SERIAL Serial

#define WS_SERVER_IP "192.168.1.13"
#define WS_SERVER_PORT 8000
#define WS_SERVER_PATH "/"

#define DEVICE_TYPE "queenController"
#define DEVICE_ID "cm5tbgbtn0002xhnnkjfrkqqq"
#define SOCKET_ID "15621321"

#define MOTOR_1_PIN_1    25
#define MOTOR_1_PIN_2    26
#define MOTOR_2_PIN_1    32
#define MOTOR_2_PIN_2    33
#define MOTOR_3_PIN_1    14
#define MOTOR_3_PIN_2    27
#define MOTOR_4_PIN_1    12
#define MOTOR_4_PIN_2    13

httpd_handle_t server = NULL;
WiFiMulti WiFiMulti;
WebSocketsClient webSocket;

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
      <tr><td colspan="3" align="center"><button class="button" onmousedown="toggleCheckbox('move1');" ontouchstart="toggleCheckbox('move1');" onmouseup="toggleCheckbox('stop');" ontouchend="toggleCheckbox('stop');">L</button></td></tr>
      <tr><td colspan="3" align="center"><button class="button" onmousedown="toggleCheckbox('move12');" ontouchstart="toggleCheckbox('move12');" onmouseup="toggleCheckbox('stop');" ontouchend="toggleCheckbox('stop');">R</button></td></tr>
      <tr><td colspan="3" align="center"><button class="button" onmousedown="toggleCheckbox('move2');" ontouchstart="toggleCheckbox('move2');" onmouseup="toggleCheckbox('stop');" ontouchend="toggleCheckbox('stop');">l180</button></td></tr> 
      <tr><td colspan="3" align="center"><button class="button" onmousedown="toggleCheckbox('move22');" ontouchstart="toggleCheckbox('move22');" onmouseup="toggleCheckbox('stop');" ontouchend="toggleCheckbox('stop');">move22</button></td></tr>
      <tr><td colspan="3" align="center"><button class="button" onmousedown="toggleCheckbox('move3');" ontouchstart="toggleCheckbox('move3');" onmouseup="toggleCheckbox('stop');" ontouchend="toggleCheckbox('stop');">move3</button></td></tr>   
      <tr><td colspan="3" align="center"><button class="button" onmousedown="toggleCheckbox('move32');" ontouchstart="toggleCheckbox('move32');" onmouseup="toggleCheckbox('stop');" ontouchend="toggleCheckbox('stop');">circular</button></td></tr>                              
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
)rawliteral" ;
//"

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
    switch (type) {
        case WStype_DISCONNECTED:
            USE_SERIAL.printf("[WSc] Disconnected!\n");
            break;

        case WStype_CONNECTED:
            USE_SERIAL.printf("[WSc] Connected to url: %s\n", payload);
            sendConnectionMessage();
            break;

        case WStype_TEXT: {
            USE_SERIAL.printf("[WSc] Received text: %s\n", payload);

            // Use a smaller JSON document size
            StaticJsonDocument<512> doc;
            DeserializationError error = deserializeJson(doc, payload);

            if (error) {
                USE_SERIAL.println("Failed to parse JSON");
                return;
            }

            // Check if it's a motor command
            const char* msgType = doc["type"];
            if (msgType && strcmp(msgType, "robot:dijkstra") == 0) {
                // Extract the directions array
                JsonArray directionsArray = doc["directions"].as<JsonArray>();
                bool isPressed = doc["pressed"]; // Check if button is pressed

                if (isPressed && !directionsArray.isNull()) {
                    lastCommandTime = millis(); // Update last command time

                    // Process the first direction in the array
                    if (directionsArray.size() > 0) {
                        for(int i=0;i<directionsArray.size();i++){
                          const char* direction=directionsArray[i];
                          USE_SERIAL.printf("Processing direction: %s\n", direction);

                        if (strcmp(direction, "up") == 0) {
                            moveForward();
                            delay(100);
                            stop();
                        }
                        else if (strcmp(direction, "down") == 0) {
                            moveBackward();
                            delay(100);
                            stop();
                        }
                        else if (strcmp(direction, "left") == 0) {
                            turnRight();
                            delay(100);
                            stop();
                        }
                        else if (strcmp(direction, "right") == 0) {
                            turnLeft();
                            delay(100);
                            stop();
                        }
                        else if (strcmp(direction, "up-left") == 0) {
                            moveForward();
                            delay(50);
                            turnRight();
                            delay(50);
                            stop();
                        }
                        else if (strcmp(direction, "up-right") == 0) {
                            moveForward();
                            delay(50);
                            turnLeft();
                            delay(50);
                            stop();
                        }
                        else if (strcmp(direction, "down-left") == 0) {
                            moveBackward();
                            delay(50);
                            turnRight();
                            delay(50);
                            stop();
                        }
                        else if (strcmp(direction, "down-right") == 0) {
                            moveBackward();
                            delay(50);
                            turnLeft();
                            delay(50);
                            stop();
                        }

                        // Prepare acknowledgment
                        String jsonString;
                        StaticJsonDocument<256> ack;
                        ack["type"] = "robot:command_ack";
                        ack["robot_id"] = DEVICE_ID;
                        ack["socket_id"] = SOCKET_ID;
                        ack["direction"] = direction;
                        ack["pressed"] = isPressed;
                        ack["timestamp"] = millis();

                        serializeJson(ack, jsonString);
                        webSocket.sendTXT(jsonString);
                    }
                        }
                        
                        
                }
            }
            break;  // This break is for the case WStype_TEXT
        }

        case WStype_BIN:
            USE_SERIAL.printf("[WSc] Received binary length: %u\n", length);
            break;

        case WStype_ERROR:
        case WStype_FRAGMENT_TEXT_START:
        case WStype_FRAGMENT_BIN_START:
        case WStype_FRAGMENT:
        case WStype_FRAGMENT_FIN:
            break;
    }
}
void moveForward() {
    Serial.println("Forward");
    analogWrite(MOTOR_1_PIN_1,255);analogWrite(MOTOR_1_PIN_2,0);
    analogWrite(MOTOR_2_PIN_1,255);analogWrite(MOTOR_2_PIN_2,0);
    analogWrite(MOTOR_3_PIN_1,255);analogWrite(MOTOR_3_PIN_2,0);
    analogWrite(MOTOR_4_PIN_1,255);analogWrite(MOTOR_4_PIN_2,0);
  }
 void turnLeft() {
    Serial.println("suppose to be left");
    analogWrite(MOTOR_1_PIN_1,255);analogWrite(MOTOR_1_PIN_2,0);
    analogWrite(MOTOR_2_PIN_1,0);analogWrite(MOTOR_2_PIN_2,255);
    analogWrite(MOTOR_3_PIN_1,0);analogWrite(MOTOR_3_PIN_2,255);
    analogWrite(MOTOR_4_PIN_1,255);analogWrite(MOTOR_4_PIN_2,0);
  }
 void turnRight() {
    Serial.println("suppose to be right");
    analogWrite(MOTOR_1_PIN_1,0);analogWrite(MOTOR_1_PIN_2,255);
    analogWrite(MOTOR_2_PIN_1,255);analogWrite(MOTOR_2_PIN_2,0);
    analogWrite(MOTOR_3_PIN_1,255);analogWrite(MOTOR_3_PIN_2,0);
    analogWrite(MOTOR_4_PIN_1,0);analogWrite(MOTOR_4_PIN_2,255);
  }
  void move1() {
    Serial.println("suppose to be 180");
    analogWrite(MOTOR_1_PIN_1,255);analogWrite(MOTOR_1_PIN_2,0);
    analogWrite(MOTOR_2_PIN_1,0);analogWrite(MOTOR_2_PIN_2,255);
    analogWrite(MOTOR_3_PIN_1,255);analogWrite(MOTOR_3_PIN_2,0);
    analogWrite(MOTOR_4_PIN_1,0);analogWrite(MOTOR_4_PIN_2,255);
  }
  void move2() {
    Serial.println("move3");
    analogWrite(MOTOR_1_PIN_1,50);analogWrite(MOTOR_1_PIN_2,0);
    analogWrite(MOTOR_2_PIN_1,0);analogWrite(MOTOR_2_PIN_2,50);
    analogWrite(MOTOR_3_PIN_1,0);analogWrite(MOTOR_3_PIN_2,255);
    analogWrite(MOTOR_4_PIN_1,255);analogWrite(MOTOR_4_PIN_2,0);
  }
  void move3() {
    Serial.println("move32");
    analogWrite(MOTOR_1_PIN_1,200);analogWrite(MOTOR_1_PIN_2,0);
    analogWrite(MOTOR_2_PIN_1,0);analogWrite(MOTOR_2_PIN_2,200);
    analogWrite(MOTOR_3_PIN_1,0);analogWrite(MOTOR_3_PIN_2,255);
    analogWrite(MOTOR_4_PIN_1,255);analogWrite(MOTOR_4_PIN_2,0);
  }
  void move4() {
    Serial.println("move22");
    analogWrite(MOTOR_1_PIN_1,255);analogWrite(MOTOR_1_PIN_2,0);
    analogWrite(MOTOR_2_PIN_1,255);analogWrite(MOTOR_2_PIN_2,0);
    analogWrite(MOTOR_3_PIN_1,0);analogWrite(MOTOR_3_PIN_2,0);
  }
 void moveBackward(){
    Serial.println("Backward");
    analogWrite(MOTOR_1_PIN_1,0);analogWrite(MOTOR_1_PIN_2,255);
    analogWrite(MOTOR_2_PIN_1,0);analogWrite(MOTOR_2_PIN_2,255);
    analogWrite(MOTOR_3_PIN_1,0);analogWrite(MOTOR_3_PIN_2,255);
    analogWrite(MOTOR_4_PIN_1,0);analogWrite(MOTOR_4_PIN_2,255);
  }
  void stop() {
    Serial.println("Stop");
    analogWrite(MOTOR_2_PIN_1,0);analogWrite(MOTOR_2_PIN_2,0);
    analogWrite(MOTOR_1_PIN_2,0);analogWrite(MOTOR_1_PIN_1,0);
    analogWrite(MOTOR_3_PIN_1,0);analogWrite(MOTOR_3_PIN_2,0);
    analogWrite(MOTOR_4_PIN_1,0);analogWrite(MOTOR_4_PIN_2,0);
  }
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
    analogWrite(MOTOR_1_PIN_1,255);analogWrite(MOTOR_1_PIN_2,0);
    analogWrite(MOTOR_2_PIN_1,255);analogWrite(MOTOR_2_PIN_2,0);
    analogWrite(MOTOR_3_PIN_1,255);analogWrite(MOTOR_3_PIN_2,0);
    analogWrite(MOTOR_4_PIN_1,255);analogWrite(MOTOR_4_PIN_2,0);
  }
  else if(!strcmp(variable, "move1")) {
    Serial.println("suppose to be left");
    analogWrite(MOTOR_1_PIN_1,255);analogWrite(MOTOR_1_PIN_2,0);
    analogWrite(MOTOR_2_PIN_1,0);analogWrite(MOTOR_2_PIN_2,255);
    analogWrite(MOTOR_3_PIN_1,0);analogWrite(MOTOR_3_PIN_2,255);
    analogWrite(MOTOR_4_PIN_1,255);analogWrite(MOTOR_4_PIN_2,0);
  }
  else if(!strcmp(variable, "move12")) {
    Serial.println("suppose to be right");
    analogWrite(MOTOR_1_PIN_1,0);analogWrite(MOTOR_1_PIN_2,255);
    analogWrite(MOTOR_2_PIN_1,255);analogWrite(MOTOR_2_PIN_2,0);
    analogWrite(MOTOR_3_PIN_1,255);analogWrite(MOTOR_3_PIN_2,0);
    analogWrite(MOTOR_4_PIN_1,0);analogWrite(MOTOR_4_PIN_2,255);
  }
  else if(!strcmp(variable, "move2")) {
    Serial.println("suppose to be 180");
    analogWrite(MOTOR_1_PIN_1,255);analogWrite(MOTOR_1_PIN_2,0);
    analogWrite(MOTOR_2_PIN_1,0);analogWrite(MOTOR_2_PIN_2,255);
    analogWrite(MOTOR_3_PIN_1,255);analogWrite(MOTOR_3_PIN_2,0);
    analogWrite(MOTOR_4_PIN_1,0);analogWrite(MOTOR_4_PIN_2,255);
  }
  else if(!strcmp(variable, "move3")) {
    Serial.println("move3");
    analogWrite(MOTOR_1_PIN_1,50);analogWrite(MOTOR_1_PIN_2,0);
    analogWrite(MOTOR_2_PIN_1,0);analogWrite(MOTOR_2_PIN_2,50);
    analogWrite(MOTOR_3_PIN_1,0);analogWrite(MOTOR_3_PIN_2,255);
    analogWrite(MOTOR_4_PIN_1,255);analogWrite(MOTOR_4_PIN_2,0);
  }
  else if(!strcmp(variable, "move32")) {
    Serial.println("move32");
    analogWrite(MOTOR_1_PIN_1,200);analogWrite(MOTOR_1_PIN_2,0);
    analogWrite(MOTOR_2_PIN_1,0);analogWrite(MOTOR_2_PIN_2,200);
    analogWrite(MOTOR_3_PIN_1,0);analogWrite(MOTOR_3_PIN_2,255);
    analogWrite(MOTOR_4_PIN_1,255);analogWrite(MOTOR_4_PIN_2,0);
  }
  else if(!strcmp(variable, "move22")) {
    Serial.println("move22");
    analogWrite(MOTOR_1_PIN_1,255);analogWrite(MOTOR_1_PIN_2,0);
    analogWrite(MOTOR_2_PIN_1,255);analogWrite(MOTOR_2_PIN_2,0);
    analogWrite(MOTOR_3_PIN_1,0);analogWrite(MOTOR_3_PIN_2,0);
  }
  else if(!strcmp(variable, "backward")) {
    Serial.println("Backward");
    analogWrite(MOTOR_1_PIN_1,0);analogWrite(MOTOR_1_PIN_2,255);
    analogWrite(MOTOR_2_PIN_1,0);analogWrite(MOTOR_2_PIN_2,255);
    analogWrite(MOTOR_3_PIN_1,0);analogWrite(MOTOR_3_PIN_2,255);
    analogWrite(MOTOR_4_PIN_1,0);analogWrite(MOTOR_4_PIN_2,255);
  }
  else if(!strcmp(variable, "stop")) {
    Serial.println("Stop");
    analogWrite(MOTOR_2_PIN_1,0);analogWrite(MOTOR_2_PIN_2,0);
    analogWrite(MOTOR_1_PIN_2,0);analogWrite(MOTOR_1_PIN_1,0);
    analogWrite(MOTOR_3_PIN_1,0);analogWrite(MOTOR_3_PIN_2,0);
    analogWrite(MOTOR_4_PIN_1,0);analogWrite(MOTOR_4_PIN_2,0);
  } else {
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
  pinMode(MOTOR_4_PIN_1, OUTPUT);
  pinMode(MOTOR_4_PIN_2, OUTPUT);

  Serial.begin(115200);
  
  // Connect to WiFi network
  Serial.println();
  Serial.println();
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA);
  WiFiMulti.addAP(ssid, password);
  
  // Wait for connection
  while(WiFiMulti.run() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("");
  Serial.print("Connected to WiFi. IP address: ");
  Serial.println(WiFi.localIP());
  
  // Setup WebSocket connection
  webSocket.begin(WS_SERVER_IP, WS_SERVER_PORT, WS_SERVER_PATH);
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);
  
  // Optionally start the local web server
  startServer();
  
  Serial.println("HTTP server started");
}

void loop() {
  webSocket.loop();
}