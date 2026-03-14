#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <ArduinoJson.h>
#include <time.h>
#include <vector>
#include <MPU6050_light.h>
#include <OneButton.h>

const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

const String CLIENT_ID = "YOUR_CLIENT_ID";
const String CLIENT_SECRET = "YOUR_CLIENT_SECRET";
const String REFRESH_TOKEN = "YOUR_REFRESH_TOKEN";
const String LIST_ID = "YOUR_LIST_ID";

#define MPU_SDA 4
#define MPU_SCL 5
#define OLED_SDA 6
#define OLED_SCL 7
#define OLED_RESET -1
#define BUTTON_PIN 10

U8G2_SH1106_128X64_NONAME_F_SW_I2C display(U8G2_R0, OLED_SCL, OLED_SDA, OLED_RESET);

MPU6050 mpu(Wire);
WiFiServer server(80);
OneButton btn(BUTTON_PIN, true);

String display_mode = "clock";
bool is_focus_mode_on = false;
unsigned long focus_start_time = 0;

String music_title = "Not Connected";
String music_artist = "";

unsigned long last_token_update = 0;
const unsigned long TOKEN_UPDATE_INTERVAL = 3000000;

unsigned long last_todo_update = 0;
const unsigned long TODO_UPDATE_INTERVAL = 60000;

String access_token = "";
bool is_token_valid = false;

unsigned long last_gyro_check = 0;
const int GYRO_CHECK_INTERVAL = 10;

unsigned long last_display_update = 0;
const int DISPLAY_UPDATE_INTERVAL = 50;

bool show_done_message = false;
unsigned long done_message_start = 0;

struct Task { String id; String title; };
std::vector<Task> tasks;
int current_task_idx = 0;

int scroll_offset = 0;
unsigned long last_scroll_step = 0;
const int SCROLL_STEP_DELAY = 15;

unsigned long last_task_advance = 0;
const unsigned long TASK_ADVANCE_INTERVAL = 4000; 

void sync_time() {
  configTime(9 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  Serial.println("Waiting for time");
  int retry = 0;
  while (!time(nullptr) && retry < 10) { Serial.print("."); delay(1000); retry++; }
  Serial.println("\nTime synced");
}

void connect_wifi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  int retry_count = 0;
  while (WiFi.status() != WL_CONNECTED && retry_count < 100) {
    delay(500); Serial.print("."); retry_count++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected. IP: " + WiFi.localIP().toString());
    sync_time();
  } else {
    Serial.println("\nWiFi Failed.");
  }
}

String get_new_access_token() {
  if (WiFi.status() != WL_CONNECTED) return "";
  HTTPClient http; WiFiClientSecure client; client.setInsecure();
  String token_url = "https://login.microsoftonline.com/common/oauth2/v2.0/token";
  if (http.begin(client, token_url)) {
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    String cleaned_token = REFRESH_TOKEN;
    cleaned_token.replace(" ", ""); cleaned_token.replace("\n", ""); cleaned_token.replace("\r", "");
    String data = "client_id=" + CLIENT_ID + "&client_secret=" + CLIENT_SECRET +
                  "&grant_type=refresh_token&refresh_token=" + cleaned_token;
    int httpCode = http.POST(data);
    String payload = http.getString();
    http.end();
    if (httpCode == 200) {
      DynamicJsonDocument doc(4096);
      deserializeJson(doc, payload);
      return doc["access_token"].as<String>();
    }
  }
  return "";
}

void get_todo_tasks(String token) {
  if (token == "") return;
  HTTPClient http; WiFiClientSecure client; client.setInsecure();
  String cleaned_list_id = LIST_ID; cleaned_list_id.replace(" ", "");
  String url = "https://graph.microsoft.com/v1.0/me/todo/lists/" + cleaned_list_id +
               "/tasks?$filter=status%20ne%20%27completed%27";
  if (http.begin(client, url)) {
    http.addHeader("Authorization", "Bearer " + token);
    if (http.GET() == 200) {
      DynamicJsonDocument doc(8192);
      deserializeJson(doc, http.getString());
      tasks.clear();
      for (JsonObject task : doc["value"].as<JsonArray>()) {
        tasks.push_back({task["id"].as<String>(), task["title"].as<String>()});
      }
      if (current_task_idx >= (int)tasks.size()) current_task_idx = 0;

      last_task_advance = millis();
      scroll_offset = 0;
    }
    http.end();
  }
}

bool complete_todo_task(String token, String listId, String taskId) {
  HTTPClient http; WiFiClientSecure client; client.setInsecure();
  String url = "https://graph.microsoft.com/v1.0/me/todo/lists/" + listId + "/tasks/" + taskId;
  if (http.begin(client, url)) {
    http.addHeader("Authorization", "Bearer " + token);
    http.addHeader("Content-Type", "application/json");
    int code = http.PATCH("{\"status\": \"completed\"}");
    http.end();
    return code == 200;
  }
  return false;
}

unsigned char h2int(char c) {
    if (c >= '0' && c <= '9') { return((unsigned char)c - '0'); }
    if (c >= 'a' && c <= 'f') { return((unsigned char)c - 'a' + 10); }
    if (c >= 'A' && c <= 'F') { return((unsigned char)c - 'A' + 10); }
    return(0);
}

String urlDecode(String str) {
    String decoded = "";
    int len = str.length();
    for (int i = 0; i < len; i++) {
        char c = str.charAt(i);
        if (c == '+') {
            decoded += ' ';
        } else if (c == '%' && i + 2 < len) {
            unsigned char code0 = str.charAt(++i);
            unsigned char code1 = str.charAt(++i);
            decoded += (char)((h2int(code0) << 4) | h2int(code1));
        } else {
            decoded += c;
        }
    }
    return decoded;
}

void handleLongPressComplete() {
  Serial.println("Double click detected - Complete task");
  if (display_mode == "todo" && tasks.size() > 0) {
    show_done_message = true;
    done_message_start = millis();
    if (complete_todo_task(access_token, LIST_ID, tasks[current_task_idx].id)) {
      get_todo_tasks(access_token);
      scroll_offset = 0;
    }
  }
}

void check_sensor_and_control() {
  mpu.update();
  float ay = mpu.getAccY();
  float threshold_mode = 0.8;
  String new_mode = display_mode;

  if (ay < -threshold_mode) {
      new_mode = "music";
  }
  else if (ay > threshold_mode) {
      new_mode = "todo";
  }
  else  {
      new_mode = "clock";
  }

  if (new_mode != display_mode) {
    display_mode = new_mode;
    scroll_offset = 0;
    Serial.println("Mode Changed: " + display_mode);

    if (display_mode == "todo") last_task_advance = millis();
  }
}

void drawScrollableText(int x_start, int y_pos, String text) {
  int textWidth = display.getUTF8Width(text.c_str());
  int screenWidth = 128;

  if (textWidth <= screenWidth) {
    display.drawUTF8(x_start, y_pos, text.c_str());
  } else {
    int current_x = x_start - scroll_offset;

    display.drawUTF8(current_x, y_pos, text.c_str());

    if (scroll_offset > 0) {
      display.drawUTF8(current_x + textWidth + 30, y_pos, text.c_str());
    }

    if (millis() - last_scroll_step > SCROLL_STEP_DELAY) {
      scroll_offset++;
      if (scroll_offset >= textWidth + 30) {
        scroll_offset = 0;
      }
      last_scroll_step = millis();
    }
  }
}

void advanceTodoTaskIfNeeded() {
  if (btn.isLongPressed()) return;
  if (display_mode != "todo") return;
  if (tasks.size() <= 1) return;

  unsigned long now = millis();
  if (now - last_task_advance >= TASK_ADVANCE_INTERVAL) {
    current_task_idx = (current_task_idx + 1) % tasks.size();
    scroll_offset = 0;          
    last_task_advance = now;
  }
}

void setup() {
  Serial.begin(9600);                    

  Wire.begin(MPU_SDA, MPU_SCL);
  Wire.setClock(400000);
  
  byte status = mpu.begin();
  Serial.print("MPU status: 0x");
  Serial.println(status, HEX);
  if (status != 0) {
    Serial.println("MPU6050 init failed!");
    while (1);  
  }
  Serial.println("MPU6050 OK");

  if (!display.begin()) {
    Serial.println("OLED init failed! Check pins 6,7 wiring.");
    while (1);
  }
  display.enableUTF8Print();
  Serial.println("OLED OK");

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  btn.attachLongPressStart(handleLongPressComplete);

  connect_wifi();
  server.begin();

  Serial.println("Setup complete.");
}

void loop() {
  unsigned long current_time_ms = millis();

  btn.tick();

  if (current_time_ms - last_gyro_check > (unsigned long)GYRO_CHECK_INTERVAL) {
    check_sensor_and_control(); last_gyro_check = current_time_ms;
  }

  WiFiClient client = server.available();

  if (client) {
    String request = "";
    unsigned long timeout = millis() + 20;
    while (client.connected() && millis() < timeout) {
      if (client.available()) {
        char c = client.read();
        request += c;
      }
      yield();
    }

    if (request.indexOf("GET /") != -1) {
        if (request.indexOf("focus?status=on") != -1) {
          is_focus_mode_on = true;
          focus_start_time = millis();
        }
        else if (request.indexOf("focus?status=off") != -1) {
          is_focus_mode_on = false;
        }

        if (request.indexOf("/music") != -1) {
            int tStart = request.indexOf("title=");
            if (tStart != -1) {
                int tEnd = request.indexOf("&", tStart);
                if (tEnd == -1) tEnd = request.indexOf(" ", tStart);
                int aStart = request.indexOf("artist=");
                int aEnd = request.indexOf(" ", aStart);

                music_title = urlDecode(request.substring(tStart + 6, tEnd));
                music_artist = urlDecode(request.substring(aStart + 7, aEnd));

                Serial.println(music_title);
                Serial.println(music_artist);

                if(display_mode == "music") scroll_offset = 0;
            }
        }
        client.println("HTTP/1.1 200 OK\r\n\r\nOK");
    }
    client.stop();
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (!is_token_valid || (current_time_ms - last_token_update > TOKEN_UPDATE_INTERVAL)) {
      access_token = get_new_access_token();
      if (access_token != "") {
        is_token_valid = true;
        last_token_update = current_time_ms;
        get_todo_tasks(access_token);
        last_todo_update = current_time_ms;
      } else {
        last_token_update = current_time_ms - TOKEN_UPDATE_INTERVAL + 10000;
      }
    }

    if (is_token_valid && (current_time_ms - last_todo_update > TODO_UPDATE_INTERVAL)) {
      get_todo_tasks(access_token);
      last_todo_update = current_time_ms;
    }
  }

  advanceTodoTaskIfNeeded();

  if (current_time_ms - last_display_update > DISPLAY_UPDATE_INTERVAL) {
    display.clearBuffer();

    if (show_done_message) {
        display.setDisplayRotation(U8G2_R2);
        display.setFont(u8g2_font_unifont_t_korean2);
        display.drawUTF8(40, 35, "완료!!");
        if (current_time_ms - done_message_start > 1500) show_done_message = false;
    }
    else {
        if (display_mode == "clock") {
          display.setDisplayRotation(U8G2_R3);

          if (is_focus_mode_on) {
            unsigned long elapsed = millis() - focus_start_time;
            int minutes = (elapsed / 60000);
            int seconds = (elapsed / 1000) % 60;
            char timeStr[10];
            sprintf(timeStr, "%02d:%02d", minutes, seconds);

            display.setFont(u8g2_font_logisoso16_tn);
            int strWidth = display.getStrWidth(timeStr);
            display.drawStr((64 - strWidth) / 2, 70, timeStr);

          } else {
            struct tm timeinfo;
            if (getLocalTime(&timeinfo)) {
              char dateStr[20];
              strftime(dateStr, 20, "%m.%d %a", &timeinfo);

              display.setFont(u8g2_font_ncenB08_tr);
              int dWidth = display.getStrWidth(dateStr);
              display.drawStr((64 - dWidth) / 2, 15, dateStr);

              display.setFont(u8g2_font_logisoso32_tf);
              char hourStr[5]; strftime(hourStr, 5, "%H", &timeinfo);
              char minStr[5]; strftime(minStr, 5, "%M", &timeinfo);

              int hWidth = display.getStrWidth(hourStr);
              int mWidth = display.getStrWidth(minStr);

              display.drawStr((64 - hWidth) / 2, 60, hourStr);
              display.drawStr((64 - mWidth) / 2, 100, minStr);
            } else {
              static unsigned long last_ntp_retry = 0;
              if (millis() - last_ntp_retry > 5000) {
                configTime(9 * 3600, 0, "pool.ntp.org", "time.nist.gov");
                last_ntp_retry = millis();
              }
            }
          }
        }
        else if (display_mode == "todo") {
          display.setDisplayRotation(U8G2_R2);
          display.setFont(u8g2_font_unifont_t_korean2);
          display.drawUTF8(0, 12, "To Do");
          display.drawLine(0, 14, 128, 14);

          if (tasks.empty()) display.drawUTF8(0, 35, "");
          else {
            display.setFont(u8g2_font_ncenB08_tr); display.setCursor(92, 12);
            display.print(current_task_idx + 1); display.print("/"); display.print(tasks.size());
            display.setFont(u8g2_font_unifont_t_korean2);
            drawScrollableText(0, 35, tasks[current_task_idx].title);
          }
        }
        else if (display_mode == "music") {
          display.setDisplayRotation(U8G2_R0);
          display.setFont(u8g2_font_unifont_t_korean2);
          drawScrollableText(0, 20, music_title);
          drawScrollableText(0, 50, music_artist);
        }
    }
    display.sendBuffer();

    last_display_update = current_time_ms;
  }
}
