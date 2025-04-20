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
// WiFi credentials
#define WIFI_SSID "VodafoneNet-V"
#define WIFI_PASSWORD "520940330666"

// WebSocket server details
#define WS_SERVER_IP "192.168.1.5"
#define WS_SERVER_PORT 8000
#define WS_SERVER_PATH "/"

#define DEVICE_TYPE "alpha"
#define DEVICE_ID "cm5tbgiuh0003xhnngr0q42g9"
#define SOCKET_ID "15621321"

#define FRAME_INTERVAL 30  // 30ms between frames
unsigned long lastFrame = 0;


#define PART_BOUNDARY "123456789000000000000987654321"

// Camera pins for AI Thinker
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

WiFiMulti WiFiMulti;
WebSocketsClient webSocket;

#define USE_SERIAL Serial

static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t camera_httpd = NULL;
httpd_handle_t stream_httpd = NULL;

// HTML content for web interface
static const char PROGMEM INDEX_HTML[] = R"rawliteral(
<html>
  <head>
    <title>ESP32-CAM Robot</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
      body { font-family: Arial; text-align: center; margin:0px auto; padding-top: 30px;}
      table { margin-left: auto; margin-right: auto; }
      td { padding: 8 px; }
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
      img { width: auto; max-width: 100%; height: auto; }
    </style>
  </head>
  <body>
    <h1>ESP32-CAM Robot</h1>
    <img src="" id="photo">
    <script>
      function toggleCheckbox(x) {
        var xhr = new XMLHttpRequest();
        xhr.open("GET", "/action?go=" + x, true);
        xhr.send();
      }
      window.onload = document.getElementById("photo").src = window.location.href.slice(0, -1) + ":81/stream";
    </script>
  </body>
</html>
)rawliteral";

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

void sendPosition(float x, float y) {
    StaticJsonDocument<200> doc;
    doc["type"] = "robot:position";
    doc["robot_id"] = DEVICE_ID;
    doc["socket_id"] = SOCKET_ID;
    
    JsonObject position = doc.createNestedObject("position");
    position["x"] = x;
    position["y"] = y;
    
    doc["timestamp"] = millis();

    String jsonString;
    serializeJson(doc, jsonString);
    webSocket.sendTXT(jsonString);
}

void sendWebcamFrame(uint8_t* frame_buffer, size_t length) {
    // Calculate base64 encoded length
    size_t base64_length;
    mbedtls_base64_encode(NULL, 0, &base64_length, frame_buffer, length);
    
    // Allocate memory for base64 buffer
    char* base64_buffer = (char*)malloc(base64_length + 1);
    if (!base64_buffer) {
        USE_SERIAL.println("Failed to allocate memory for base64 buffer");
        return;
    }
    
    // Encode the frame to base64
    size_t written_bytes;
    int result = mbedtls_base64_encode((unsigned char*)base64_buffer, base64_length, &written_bytes, frame_buffer, length);
    
    if (result != 0) {
        USE_SERIAL.println("Base64 encoding failed");
        free(base64_buffer);
        return;
    }
    
    // Null terminate the string
    base64_buffer[written_bytes] = '\0';
    
    // Create JSON document
    StaticJsonDocument<32768> doc; // Increased size to handle larger base64 strings
    doc["type"] = "robot:webcam_frame";
    doc["robot_id"] = DEVICE_ID;
    doc["socket_id"] = SOCKET_ID;
    doc["frame"] = base64_buffer;
    doc["timestamp"] = millis();

    // Serialize and send
    String jsonString;
    serializeJson(doc, jsonString);
    webSocket.sendTXT(jsonString);
    
    // Clean up
    free(base64_buffer);
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
        case WStype_TEXT:
            USE_SERIAL.printf("[WSc] Received text: %s\n", payload);
            break;
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

static esp_err_t index_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, (const char *)INDEX_HTML, strlen(INDEX_HTML));
}

static esp_err_t stream_handler(httpd_req_t *req) {
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t *_jpg_buf = NULL;
    char *part_buf[64];

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK) {
        return res;
    }

    while (true) {
        fb = esp_camera_fb_get();
        if (!fb) {
            USE_SERIAL.println("Camera capture failed");
            res = ESP_FAIL;
        } else {
            if (fb->format != PIXFORMAT_JPEG) {
                bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
                esp_camera_fb_return(fb);
                fb = NULL;
                if (!jpeg_converted) {
                    USE_SERIAL.println("JPEG compression failed");
                    res = ESP_FAIL;
                }
            } else {
                _jpg_buf_len = fb->len;
                _jpg_buf = fb->buf;
            }
        }

        if (res == ESP_OK) {
            size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
            res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        }
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
        }
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        }

        if (fb) {
            esp_camera_fb_return(fb);
            fb = NULL;
            _jpg_buf = NULL;
        } else if (_jpg_buf) {
            free(_jpg_buf);
            _jpg_buf = NULL;
        }

        if (res != ESP_OK) {
            break;
        }
    }
    return res;
}

static esp_err_t cmd_handler(httpd_req_t *req) {
    char *buf;
    size_t buf_len;
    char variable[32] = {0,};
    
    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = (char*)malloc(buf_len);
        if (!buf) {
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

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, NULL, 0);
}

void startCameraServer() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

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

    httpd_uri_t stream_uri = {
        .uri       = "/stream",
        .method    = HTTP_GET,
        .handler   = stream_handler,
        .user_ctx  = NULL
    };

    if (httpd_start(&camera_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(camera_httpd, &index_uri);
        httpd_register_uri_handler(camera_httpd, &cmd_uri);
    }

    config.server_port += 1;
    config.ctrl_port += 1;
    if (httpd_start(&stream_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
    }
}

void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
    
    Serial.begin(115200);
    Serial.setDebugOutput(true);
    
    // Camera configuration
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    
    if (psramFound()) {
        config.frame_size = FRAMESIZE_VGA;
        config.jpeg_quality = 10;
        config.fb_count = 2;
    } else {
        config.frame_size = FRAMESIZE_SVGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
    }
    
    // Initialize camera
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        USE_SERIAL.printf("Camera init failed with error 0x%x", err);
        return;
    }

    // Connect to WiFi
    WiFiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);
    while (WiFiMulti.run() != WL_CONNECTED) {
        USE_SERIAL.print(".");
        delay(500);
    }
    USE_SERIAL.printf("\n[SETUP] WiFi Connected, IP: %s\n", WiFi.localIP().toString().c_str());

    // Start camera server
    startCameraServer();
    
    // Initialize WebSocket connection
    webSocket.begin(WS_SERVER_IP, WS_SERVER_PORT, WS_SERVER_PATH);
    webSocket.onEvent(webSocketEvent);
    webSocket.setReconnectInterval(5000);
}

void loop() {
    webSocket.loop();
    
    // Frame rate control
    unsigned long currentMillis = millis();
    
    // Check if it's time to send a new frame
    if (currentMillis - lastFrame >= FRAME_INTERVAL) {
        camera_fb_t * fb = esp_camera_fb_get();
        if (fb) {
            // Send the frame over WebSocket
            sendWebcamFrame(fb->buf, fb->len);
            esp_camera_fb_return(fb);
            lastFrame = currentMillis;
        }
    }
    
    // Position update (every 5 seconds)
    static unsigned long lastPosition = 0;
    if (currentMillis - lastPosition > 5000) {
        float x = random(0, 100) / 10.0;
        float y = random(0, 100) / 10.0;
        sendPosition(x, y);
        lastPosition = currentMillis;
    }
}