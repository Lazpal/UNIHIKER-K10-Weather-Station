  /*
    UNIHIKER K10 Weather Station for Mind+ Arduino C++ mode.
    A: refresh outside weather now   Day/night theme: room light sensor
  */

  #include "unihiker_k10.h"
  #include <WiFi.h>
  #include <HTTPClient.h>
  #include <WiFiClientSecure.h>
  #include <time.h>
  #include "AIRecognition.h"
  #include <Preferences.h>

  String wifiSSID = "";
  String wifiPassword = "";
  float latitude = 0.0;
  float longitude = 0.0;
  String locationName = "LOCATING...";

  const uint32_t NAVY = 0x071426;
  const uint32_t NIGHT = 0x0B1F3A;
  const uint32_t DAY = 0x1976B9;
  const uint32_t WHITE = 0xFFFFFF;
  const uint32_t MUTED = 0xB5D3E8;
  const uint32_t CYAN = 0x4DDAFF;
  const uint32_t YELLOW = 0xFFD54A;
  const uint32_t RAIN = 0x67CEFF;
  const uint32_t GREY = 0xB7C6D4;
  const uint32_t DARK_GREY = 0x647B91;
  const uint32_t PANEL = 0x102D4B;
  const uint32_t PANEL_ALT = 0x123A59;
  const uint32_t GREEN = 0x51E39A;
  const uint16_t NIGHT_LIGHT_THRESHOLD = 80;
  const uint16_t DAY_LIGHT_THRESHOLD = 150;
  const uint32_t WIFI_RECONNECT_INTERVAL = 30000;
  const uint32_t LOCATION_RETRY_INTERVAL = 60000;
  const uint8_t WIFI_FAILURES_BEFORE_QR = 3;

  UNIHIKER_K10 k10;
  AHT20 indoorSensor;

  bool themeDay = true;
  bool screenThemeDay = false;
  bool weatherOK = false;
  bool locationOK = false;
  bool wifiWasConnected = false;
  bool lastButtonA = false;
  uint8_t animationPhase = 0;
  uint32_t lastWeatherUpdate = 0;
  uint32_t lastWifiReconnect = 0;
  uint32_t lastLocationAttempt = 0;

  float outsideTemp = 0;
  float outsideFeels = 0;
  float outsideHumidity = 0;
  float outsideWind = 0;
  int outsideCode = -1;
  int outsideDay = 1;
  float forecastTemp[4] = {0, 0, 0, 0};
  int forecastRain[4] = {0, 0, 0, 0};
  String forecastTime[4] = {"--", "--", "--", "--"};
  bool forecastOK = false;
  float dayMinTemp = 0;
  float dayMaxTemp = 0;
  bool dailyOK = false;
  Music music;
  AIRecognition ai;
  Preferences wifiPreferences;
  bool rainAlertInitialized = false;
  bool rainingNow = false;
  bool rainSoonAlerted = false;
  String serialCommand;

  String weatherCondition(int code) {
    if (code == 0) return "CLEAR";
    if (code == 1 || code == 2) return "PARTLY CLOUDY";
    if (code == 3) return "OVERCAST";
    if (code == 45 || code == 48) return "FOG";
    if (code >= 51 && code <= 57) return "DRIZZLE";
    if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return "RAIN";
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return "SNOW";
    if (code >= 95 && code <= 99) return "THUNDER";
    return "UNKNOWN";
  }

  String currentTimeText() {
    struct tm timeInfo;
    if (!getLocalTime(&timeInfo, 50)) return "--:--";

    char timeText[6];
    strftime(timeText, sizeof(timeText), "%H:%M", &timeInfo);
    return String(timeText);
  }

  String locationDisplayName() {
    if (locationName.length() <= 10) return locationName;
    return locationName.substring(0, 9) + ".";
  }

  uint32_t connectionStatusColour() {
    if (WiFi.status() != WL_CONNECTED) return 0xFF3232;
    if (!weatherOK) return YELLOW;
    return GREEN;
  }

  void updateThemeFromLight(uint16_t indoorLux) {
    if (themeDay && indoorLux <= NIGHT_LIGHT_THRESHOLD) themeDay = false;
    if (!themeDay && indoorLux >= DAY_LIGHT_THRESHOLD) themeDay = true;
  }

  String weatherKind(int code) {
    if (code == 0) return "sun";
    if (code >= 1 && code <= 3) return "cloud";
    if (code == 45 || code == 48) return "fog";
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return "rain";
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return "snow";
    if (code >= 95 && code <= 99) return "storm";
    return "cloud";
  }

  bool isRainCondition(int code) {
    return (code >= 51 && code <= 67) || (code >= 80 && code <= 82) ||
          (code >= 95 && code <= 99);
  }

  void playRainSoonAlert() {
    music.playTone(880, 1000);
    delay(120);
    music.playTone(660, 1400);
  }

  void playRainStartedAlert() {
    music.playTone(660, 1800);
  }

  void checkRainAlerts() {
    bool currentRain = isRainCondition(outsideCode);
    bool rainSoon = forecastOK && forecastRain[0] >= 35 && !currentRain;

    if (!rainAlertInitialized) {
      rainingNow = currentRain;
      rainSoonAlerted = false;
      rainAlertInitialized = true;
      if (rainSoon) {
        playRainSoonAlert();
        rainSoonAlerted = true;
      }
      return;
    }

    if (currentRain && !rainingNow) playRainStartedAlert();
    if (rainSoon && !rainSoonAlerted) {
      playRainSoonAlert();
      rainSoonAlerted = true;
    }
    if (!rainSoon) rainSoonAlerted = false;
    rainingNow = currentRain;
  }

  float jsonNumber(const String &body, const char *key, float fallback) {
    int currentStart = body.indexOf("\"current\"");
    if (currentStart < 0) return fallback;
    String token = String("\"") + key + "\":";
    int start = body.indexOf(token, currentStart);
    if (start < 0) return fallback;
    start += token.length();
    int end = start;
    while (end < body.length()) {
      char c = body.charAt(end);
      if (!((c >= '0' && c <= '9') || c == '-' || c == '.')) break;
      end++;
    }
    return body.substring(start, end).toFloat();
  }

  float jsonTopLevelNumber(const String &body, const char *key, float fallback) {
    String token = String("\"") + key + "\":";
    int start = body.indexOf(token);
    if (start < 0) return fallback;
    start += token.length();
    while (start < body.length() && body.charAt(start) == ' ') start++;
    int end = start;
    while (end < body.length()) {
      char c = body.charAt(end);
      if (!((c >= '0' && c <= '9') || c == '-' || c == '.')) break;
      end++;
    }
    return body.substring(start, end).toFloat();
  }

  String jsonString(const String &body, const char *key, const String &fallback) {
    String token = String("\"") + key + "\"";
    int start = body.indexOf(token);
    if (start < 0) return fallback;
    start = body.indexOf(':', start + token.length());
    if (start < 0) return fallback;
    start++;
    while (start < body.length() && body.charAt(start) == ' ') start++;
    if (start >= body.length() || body.charAt(start) != '"') return fallback;
    start++;
    int end = body.indexOf('"', start);
    return end < 0 ? fallback : body.substring(start, end);
  }

  String qrWifiField(const String &payload, const char *field) {
    String marker = String(field) + ":";
    int start = payload.indexOf(marker);
    if (start < 0) return "";
    start += marker.length();
    String value;
    for (int i = start; i < payload.length(); i++) {
      char current = payload.charAt(i);
      if (current == ';') break;
      if (current == '\\' && i + 1 < payload.length()) {
        i++;
        current = payload.charAt(i);
      }
      value += current;
    }
    return value;
  }

  bool parseWifiQr(const String &payload) {
    if (!payload.startsWith("WIFI:")) return false;
    String detectedSSID = qrWifiField(payload, "S");
    String detectedPassword = qrWifiField(payload, "P");
    if (detectedSSID.length() == 0) return false;

    wifiSSID = detectedSSID;
    wifiPassword = detectedPassword;
    Serial.print("[QR] WiFi SSID received: ");
    Serial.println(wifiSSID);
    return true;
  }

  void loadSavedWifiCredentials() {
    wifiPreferences.begin("weather", true);
    String savedSSID = wifiPreferences.getString("ssid", "");
    String savedPassword = wifiPreferences.getString("password", "");
    wifiPreferences.end();
    if (savedSSID.length() > 0) {
      wifiSSID = savedSSID;
      wifiPassword = savedPassword;
      Serial.print("[WiFi] loaded saved SSID: ");
      Serial.println(wifiSSID);
    } else {
      Serial.println("[WiFi] no saved credentials, using configured SSID");
    }
  }

  void saveWifiCredentials() {
    wifiPreferences.begin("weather", false);
    wifiPreferences.putString("ssid", wifiSSID);
    wifiPreferences.putString("password", wifiPassword);
    wifiPreferences.end();
    Serial.println("[WiFi] credentials saved");
  }

  String getPublicIP(const char *endpoint, const char *label) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(8000);
    if (!http.begin(client, endpoint)) return "";

    int responseCode = http.GET();
    String publicIP = responseCode == HTTP_CODE_OK ? http.getString() : "";
    http.end();
    publicIP.trim();
    Serial.print("[Location] ");
    Serial.print(label);
    Serial.print(" HTTP=");
    Serial.print(responseCode);
    Serial.print(" IP=");
    Serial.println(publicIP.length() > 0 ? publicIP : "unavailable");
    return publicIP;
  }

  bool updateLocationFromIP() {
    lastLocationAttempt = millis();
    WiFiClient client;
    HTTPClient http;
    http.setTimeout(8000);
    String locationUrl = "http://ip-api.com/json/?fields=status,lat,lon,city,regionName";
    String publicIPv6 = getPublicIP("https://api6.ipify.org?format=text", "public IPv6");
    if (publicIPv6.length() > 0) {
      locationUrl = "http://ip-api.com/json/" + publicIPv6 + "?fields=status,lat,lon,city,regionName";
      Serial.print("[Location] using public IPv6: ");
      Serial.println(publicIPv6);
    } else {
      String publicIP = getPublicIP("https://api.ipify.org?format=text", "public IPv4");
      Serial.print("[Location] using public IPv4: ");
      Serial.println(publicIP.length() > 0 ? publicIP : "unavailable");
    }
    if (!http.begin(client, locationUrl)) return false;

    int responseCode = http.GET();
    Serial.print("[Location] HTTP=");
    Serial.println(responseCode);
    if (responseCode != HTTP_CODE_OK) {
      http.end();
      return false;
    }

    String response = http.getString();
    http.end();
    Serial.println("[Location] response begin");
    Serial.println(response);
    Serial.print("[Location] response length=");
    Serial.println(response.length());
    Serial.println("[Location] response end");
    if (response.indexOf("\"status\":\"fail\"") >= 0) return false;

    float detectedLatitude = jsonTopLevelNumber(response, "lat", -999.0);
    float detectedLongitude = jsonTopLevelNumber(response, "lon", -999.0);
    String detectedCity = jsonString(response, "city", "");
    if (detectedCity.length() == 0) {
      detectedCity = jsonString(response, "regionName", detectedCity);
    }
    if (detectedLatitude <= -90 || detectedLatitude >= 90 ||
        detectedLongitude <= -180 || detectedLongitude >= 180 ||
        (detectedLatitude == 0 && detectedLongitude == 0) ||
        detectedCity.length() == 0 || detectedCity == "LOCATING...") return false;

    latitude = detectedLatitude;
    longitude = detectedLongitude;
    locationName = detectedCity;
    locationOK = true;
    return true;
  }

  float jsonDailyNumber(const String &body, const char *key, float fallback) {
    int dailyStart = body.indexOf("\"daily\"");
    if (dailyStart < 0) return fallback;
    String token = String("\"") + key + "\":[";
    int cursor = body.indexOf(token, dailyStart);
    if (cursor < 0) return fallback;
    cursor += token.length();
    int end = cursor;
    while (end < body.length()) {
      char c = body.charAt(end);
      if (!((c >= '0' && c <= '9') || c == '-' || c == '.')) break;
      end++;
    }
    return body.substring(cursor, end).toFloat();
  }

  float jsonHourlyNumber(const String &body, const char *key, int item, float fallback) {
    int hourlyStart = body.indexOf("\"hourly\"");
    if (hourlyStart < 0) return fallback;
    String token = String("\"") + key + "\":[";
    int cursor = body.indexOf(token, hourlyStart);
    if (cursor < 0) return fallback;
    cursor += token.length();
    for (int i = 0; i < item; i++) {
      int comma = body.indexOf(',', cursor);
      if (comma < 0) return fallback;
      cursor = comma + 1;
    }
    int end = cursor;
    while (end < body.length()) {
      char c = body.charAt(end);
      if (!((c >= '0' && c <= '9') || c == '-' || c == '.')) break;
      end++;
    }
    return body.substring(cursor, end).toFloat();
  }

  String jsonHourlyTime(const String &body, int item) {
    int hourlyStart = body.indexOf("\"hourly\"");
    int cursor = body.indexOf("\"time\":[", hourlyStart);
    if (cursor < 0) return "--";
    cursor += 8;
    for (int i = 0; i <= item; i++) {
      int begin = body.indexOf('"', cursor);
      int end = body.indexOf('"', begin + 1);
      if (begin < 0 || end < 0) return "--";
      if (i == item) {
        String timestamp = body.substring(begin + 1, end);
        return timestamp.length() >= 16 ? timestamp.substring(11, 16) : "--";
      }
      cursor = end + 1;
    }
    return "--";
  }

  void setStatusLEDs(const String &kind) {
    uint32_t colour = CYAN;
    if (kind == "sun") colour = 0xFFB20F;
    if (kind == "rain" || kind == "snow") colour = 0x006EFF;
    if (kind == "storm") colour = 0x9B50FF;
    if (kind == "fog") colour = 0x7896AA;
    k10.rgb->setRangeColor(0, 2, colour);
  }

  void updateStatusLEDs() {
    if (WiFi.status() != WL_CONNECTED) {
      k10.rgb->setRangeColor(0, 2, 0xFF3232);
    } else if (!weatherOK) {
      k10.rgb->setRangeColor(0, 2, YELLOW);
    } else {
      setStatusLEDs(weatherKind(outsideCode));
    }
  }

  void printWiFiStatus(const char *label) {
    Serial.print("[WiFi] ");
    Serial.print(label);
    Serial.print(" status=");
    Serial.print((int)WiFi.status());
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print(" IP=");
      Serial.print(WiFi.localIP());
      Serial.print(" RSSI=");
      Serial.print(WiFi.RSSI());
      Serial.print("dBm");
    }
    Serial.println();
  }

  void reconnectWiFi(uint32_t now) {
    music.playTone(210, 300);
    k10.rgb->setRangeColor(0, 2, 0xFF9900);
    if (wifiSSID.length() == 0) return;
    if (WiFi.status() == WL_CONNECTED ||
        now - lastWifiReconnect < WIFI_RECONNECT_INTERVAL) return;

    lastWifiReconnect = now;
    printWiFiStatus("reconnect attempt");
    WiFi.enableIpV6();
    WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
  }

  void clearSavedData() {
    k10.rgb->setRangeColor(0, 2, 0xFF0000);
    music.playTone(550, 1000);
    wifiPreferences.begin("weather", false);
    wifiPreferences.clear();
    wifiPreferences.end();
    wifiSSID = "";
    wifiPassword = "";
    latitude = 0.0;
    longitude = 0.0;
    locationName = "LOCATING...";
    locationOK = false;
    weatherOK = false;
    WiFi.disconnect();
    Serial.println("[Data] all saved data cleared");
    showStartup("Data cleared - restart");
    music.playTone(880, 1000);
    k10.rgb->setRangeColor(0, 2, 0x000000);
    ESP.restart();
  }

  void handleSerialCommands() {
    while (Serial.available() > 0) {
      char character = Serial.read();
      if (character == '\n' || character == '\r') {
        serialCommand.trim();
        if (serialCommand.equalsIgnoreCase("clear data")) clearSavedData();
        serialCommand = "";
      } else {
        serialCommand += character;
        if (serialCommand.equalsIgnoreCase("clear data")) {
          clearSavedData();
          serialCommand = "";
        }
      }
    }
  }

  bool connectWiFi(uint32_t timeout) {
    WiFi.mode(WIFI_STA);
    WiFi.enableIpV6();
    WiFi.setAutoReconnect(true);
    Serial.print("[WiFi] connecting to SSID: ");
    Serial.println(wifiSSID);
    WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
    uint32_t started = millis();
    uint32_t lastStatusPrint = 0;
    while (WiFi.status() != WL_CONNECTED && millis() - started < timeout) {
      delay(250);
      if (millis() - lastStatusPrint >= 2000) {
        printWiFiStatus("waiting");
        lastStatusPrint = millis();
      }
    }
    printWiFiStatus(WiFi.status() == WL_CONNECTED ? "connected" : "failed");
    Serial.print("IPv4 Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("IPv6 Address: ");
    Serial.println(WiFi.localIPv6().toString());
    return WiFi.status() == WL_CONNECTED;
  }

  bool updateWeather() {
    if (WiFi.status() != WL_CONNECTED || !locationOK) return false;

    char url[400];
    snprintf(url, sizeof(url),
      "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,is_day,wind_speed_10m&hourly=temperature_2m,precipitation_probability&daily=temperature_2m_min,temperature_2m_max&forecast_hours=4&forecast_days=1&timezone=auto",
      latitude, longitude);

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(8000);
    if (!http.begin(client, url)) return false;
    int responseCode = http.GET();
    Serial.print("[Weather] HTTP=");
    Serial.println(responseCode);
    if (responseCode != HTTP_CODE_OK) {
      http.end();
      return false;
    }

    String response = http.getString();
    http.end();
    outsideTemp = jsonNumber(response, "temperature_2m", outsideTemp);
    outsideHumidity = jsonNumber(response, "relative_humidity_2m", outsideHumidity);
    outsideFeels = jsonNumber(response, "apparent_temperature", outsideFeels);
    outsideWind = jsonNumber(response, "wind_speed_10m", outsideWind);
    outsideCode = (int)jsonNumber(response, "weather_code", -1);
    outsideDay = (int)jsonNumber(response, "is_day", 1);
    weatherOK = outsideCode >= 0;
    dayMinTemp = jsonDailyNumber(response, "temperature_2m_min", dayMinTemp);
    dayMaxTemp = jsonDailyNumber(response, "temperature_2m_max", dayMaxTemp);
    dailyOK = response.indexOf("\"daily\"") >= 0;
    forecastOK = response.indexOf("\"hourly\"") >= 0;
    for (int i = 0; i < 4; i++) {
      forecastTemp[i] = jsonHourlyNumber(response, "temperature_2m", i, 0);
      forecastRain[i] = (int)jsonHourlyNumber(response, "precipitation_probability", i, 0);
      forecastTime[i] = jsonHourlyTime(response, i);
    }
    if (weatherOK) setStatusLEDs(weatherKind(outsideCode));
    if (weatherOK) checkRainAlerts();
    return weatherOK;
  }

  void drawSun(int x, int y, int radius) {
    k10.canvas->canvasCircle(x, y, radius, YELLOW, YELLOW, true);
    int offset = animationPhase % 5;
    const int direction[8][2] = {{0,-1},{1,0},{0,1},{-1,0},{1,-1},{-1,-1},{1,1},{-1,1}};
    for (int i = 0; i < 8; i++) {
      int x1 = x + direction[i][0] * (radius + 8 + offset);
      int y1 = y + direction[i][1] * (radius + 8 + offset);
      int x2 = x + direction[i][0] * (radius + 19 + offset);
      int y2 = y + direction[i][1] * (radius + 19 + offset);
      k10.canvas->canvasLine(x1, y1, x2, y2, YELLOW);
    }
  }

  void drawCloud(int x, int y, uint32_t colour = GREY) {
    k10.canvas->canvasCircle(x - 25, y + 9, 19, colour, colour, true);
    k10.canvas->canvasCircle(x, y - 5, 25, colour, colour, true);
    k10.canvas->canvasCircle(x + 28, y + 10, 18, colour, colour, true);
    k10.canvas->canvasRectangle(x - 45, y + 9, 92, 23, colour, colour, true);
  }

  void drawRain(int x, int y) {
    drawCloud(x, y - 9);
    int shift = (animationPhase % 4) * 3;
    const int drops[] = {-32, -10, 12, 34};
    for (uint8_t i = 0; i < 4; i++) {
      k10.canvas->canvasLine(x + drops[i] + shift, y + 37, x + drops[i] - 6 + shift, y + 53, RAIN);
      k10.canvas->canvasLine(x + drops[i] + shift, y + 58, x + drops[i] - 6 + shift, y + 74, RAIN);
    }
  }

  void drawSnow(int x, int y) {
    drawCloud(x, y - 9);
    const int flakes[3][2] = {{-25,43},{5,53},{31,40}};
    for (uint8_t i = 0; i < 3; i++) {
      int sx = x + flakes[i][0] + animationPhase % 3;
      int sy = y + flakes[i][1];
      k10.canvas->canvasLine(sx - 5, sy, sx + 5, sy, WHITE);
      k10.canvas->canvasLine(sx, sy - 5, sx, sy + 5, WHITE);
      k10.canvas->canvasLine(sx - 4, sy - 4, sx + 4, sy + 4, WHITE);
      k10.canvas->canvasLine(sx - 4, sy + 4, sx + 4, sy - 4, WHITE);
    }
  }

  void drawStorm(int x, int y) {
    drawCloud(x, y - 9, DARK_GREY);
    uint32_t bolt = (animationPhase % 2 == 0) ? YELLOW : WHITE;
    k10.canvas->canvasLine(x + 7, y + 32, x - 5, y + 54, bolt);
    k10.canvas->canvasLine(x - 5, y + 54, x + 8, y + 54, bolt);
    k10.canvas->canvasLine(x + 8, y + 54, x - 3, y + 78, bolt);
  }

  void drawWeatherIcon() {
    int cx = 180;
    int cy = 91;
    String kind = weatherKind(outsideCode);
    if (kind == "sun") drawSun(cx, cy, 28);
    else if (kind == "cloud") {
      if (outsideDay) drawSun(cx - 28, cy - 24, 17);
      drawCloud(cx + 5, cy + 3);
    } else if (kind == "rain") drawRain(cx, cy);
    else if (kind == "snow") drawSnow(cx, cy);
    else if (kind == "storm") drawStorm(cx, cy);
    else {
      drawCloud(cx, cy - 14, DARK_GREY);
      for (int y = cy + 25; y <= cy + 53; y += 14) k10.canvas->canvasLine(cx - 43, y, cx + 43, y, MUTED);
    }
  }

  void textAt(const String &text, int x, int y, uint32_t colour, Canvas::eFontSize_t font = Canvas::eCNAndENFont16) {
    k10.canvas->canvasText(text, x, y, colour, font, 50, false);
  }

  void drawForecast() {
    k10.canvas->canvasRectangle(7, 239, 226, 74, PANEL, PANEL_ALT, true);
    char dailyText[32];
    if (dailyOK) {
      snprintf(dailyText, sizeof(dailyText), "MIN %.0fC  MAX %.0fC", dayMinTemp, dayMaxTemp);
    } else {
      snprintf(dailyText, sizeof(dailyText), "MIN/MAX --");
    }
    textAt(dailyText, 17, 247, CYAN);
    if (!forecastOK) {
      textAt("Forecast loading...", 17, 278, MUTED);
      return;
    }
    for (int i = 0; i < 4; i++) {
      int x = 14 + i * 55;
      uint32_t bar = forecastRain[i] >= 35 ? RAIN : YELLOW;
      k10.canvas->canvasRectangle(x, 269, 48, 38, 0x184A6E, 0x184A6E, true);
      textAt(forecastTime[i], x + 4, 273, WHITE);
      char forecastText[12];
      snprintf(forecastText, sizeof(forecastText), "%.0fC", forecastTemp[i]);
      textAt(forecastText, x + 4, 291, MUTED);
      k10.canvas->canvasRectangle(x + 4, 304, 40, 3, bar, bar, true);
    }
  }

  void drawDashboard() {
    float indoorTemp = indoorSensor.getData(AHT20::eAHT20TempC);
    float indoorHumidity = indoorSensor.getData(AHT20::eAHT20HumiRH);
    uint16_t indoorLux = k10.readALS();
    updateThemeFromLight(indoorLux);

    if (screenThemeDay != themeDay) {
      k10.setScreenBackground(themeDay ? DAY : NIGHT);
      screenThemeDay = themeDay;
    }
    uint32_t background = themeDay ? DAY : NIGHT;
    k10.canvas->canvasRectangle(0, 0, 240, 320, background, background, true);
    textAt(locationDisplayName() + " WEATHER", 12, 10, WHITE);
    textAt(currentTimeText(), 178, 10, WHITE);
    uint32_t statusColour = connectionStatusColour();
    k10.canvas->canvasCircle(220, 16, 4, statusColour, statusColour, true);
    k10.canvas->canvasLine(12, 34, 228, 34, CYAN);

    if (weatherOK) {
      char value[24];
      snprintf(value, sizeof(value), "%.1fC", outsideTemp);
      textAt(value, 14, 46, WHITE, Canvas::eCNAndENFont24);
      textAt(weatherCondition(outsideCode), 16, 80, MUTED);
      snprintf(value, sizeof(value), "Feels %.1fC", outsideFeels);
      textAt(value, 16, 104, WHITE);
      snprintf(value, sizeof(value), "RAIN NEXT: %d%%", forecastRain[0]);
      textAt(value, 16, 118, CYAN);
      drawWeatherIcon();
      k10.canvas->canvasRectangle(12, 138, 98, 31, PANEL, PANEL, true);
      k10.canvas->canvasRectangle(118, 138, 110, 31, PANEL, PANEL, true);
      snprintf(value, sizeof(value), "HUM  %.0f%%", outsideHumidity);
      textAt(value, 19, 146, CYAN);
      snprintf(value, sizeof(value), "WIND %.0f km/h", outsideWind);
      textAt(value, 125, 146, CYAN);
    } else {
      textAt("OUTDOOR WEATHER", 14, 62, WHITE);
      textAt("Waiting for WiFi...", 14, 91, MUTED);
      drawCloud(180, 94, DARK_GREY);
    }

    k10.canvas->canvasRectangle(7, 174, 226, 60, PANEL, PANEL_ALT, true);
    textAt("INSIDE", 17, 181, GREEN);
    k10.canvas->canvasLine(17, 201, 223, 201, GREEN);
    char insideText[28];
    snprintf(insideText, sizeof(insideText), "%.1fC", indoorTemp);
    textAt(insideText, 17, 205, WHITE, Canvas::eCNAndENFont24);
    snprintf(insideText, sizeof(insideText), "%.0f%% RH", indoorHumidity);
    textAt(insideText, 122, 208, WHITE);
    snprintf(insideText, sizeof(insideText), "Light: %u lux", indoorLux);
    textAt(insideText, 122, 220, MUTED);
    drawForecast();
    k10.canvas->updateCanvas();
  }

  void showStartup(const char *message) {
    k10.setScreenBackground(NAVY);
    k10.canvas->canvasClear();
    textAt("K10 WEATHER", 27, 95, CYAN, Canvas::eCNAndENFont24);
    textAt(message, 25, 140, WHITE);
    k10.canvas->updateCanvas();
  }

  void drawWifiQrScannerUI(const char *message) {
    k10.canvas->canvasClear();
    textAt("WIFI SETUP", 68, 8, CYAN, Canvas::eCNAndENFont24);
    textAt(message, 42, 42, WHITE);

    const int left = 28;
    const int top = 78;
    const int right = 212;
    const int bottom = 262;
    const int corner = 22;
    k10.canvas->canvasLine(left, top, left + corner, top, GREEN);
    k10.canvas->canvasLine(left, top, left, top + corner, GREEN);
    k10.canvas->canvasLine(right - corner, top, right, top, GREEN);
    k10.canvas->canvasLine(right, top, right, top + corner, GREEN);
    k10.canvas->canvasLine(left, bottom - corner, left, bottom, GREEN);
    k10.canvas->canvasLine(left, bottom, left + corner, bottom, GREEN);
    k10.canvas->canvasLine(right - corner, bottom, right, bottom, GREEN);
    k10.canvas->canvasLine(right, bottom - corner, right, bottom, GREEN);
    textAt("Show WiFi QR to camera", 39, 282, MUTED);
    delay(500);
    k10.canvas->updateCanvas();
    delay(3000);
  }

  bool connectUsingWifiQr() {
    delay(200);
    Serial.println("[QR] starting WiFi QR scanner");
    ai.initAi();
    k10.initBgCamerImage();
    ai.switchAiMode(ai.Code);
    drawWifiQrScannerUI("Scan WiFi QR");
    delay(2000);

    while (true) {
      if (ai.isDetectContent(ai.Code)) {
        String payload = ai.getQrCodeContent();
        Serial.print("[QR] payload: ");
        Serial.println(payload);
        if (parseWifiQr(payload)) {
          drawWifiQrScannerUI("Connecting QR WiFi...");
          music.playTone(750, 1000);
          k10.rgb->setRangeColor(0, 2, 0x00FF00);
          delay(500);
          if (connectWiFi(20000)) {
            saveWifiCredentials();
            ai.switchAiMode(ai.NoMode);
            k10.setBgCamerImage(false);
            showStartup("WiFi connected");
            return true;
          }
          Serial.println("[QR] WiFi connection failed");
          k10.rgb->setRangeColor(0, 2, 0xFF0000);
          music.playTone(300, 1000);
          drawWifiQrScannerUI("Scan WiFi QR again");
          delay(500);
        } else {
          Serial.println("[QR] invalid WiFi QR");
          k10.rgb->setRangeColor(0, 2, 0xFF0000);
          music.playTone(300, 1000);
          drawWifiQrScannerUI("Invalid WiFi QR");
          delay(500);
        }
      }
      delay(100);
    }
  }

  bool connectWiFiWithQrFallback() {
    if (wifiSSID.length() == 0) {
      Serial.println("[WiFi] empty SSID, opening QR scanner");
      return connectUsingWifiQr();
    }

    for (uint8_t attempt = 1; attempt <= WIFI_FAILURES_BEFORE_QR; attempt++) {
      Serial.print("[WiFi] connection attempt ");
      Serial.println(attempt);
      if (connectWiFi(20000)) return true;
    }
    return connectUsingWifiQr();
  }

  void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n[K10 Weather] boot");
    k10.begin();
    k10.initScreen(2);
    k10.creatCanvas();
    loadSavedWifiCredentials();
    showStartup("Connecting to WiFi...");

    if (connectWiFiWithQrFallback()) {
      wifiWasConnected = true;
      showStartup("WiFi connected");
      configTime(7200, 3600, "pool.ntp.org", "time.nist.gov");
      showStartup("Loading weather...");
      if (updateLocationFromIP()) {
        if (!updateWeather()) weatherOK = false;
      } else {
        locationName = "NO LOCATION";
      }
      lastWeatherUpdate = millis();
    } else {
      showStartup("WiFi unavailable");
      k10.rgb->setRangeColor(0, 2, 0xFF3232);
    }
  }

  void loop() {
    bool pressedA = k10.buttonA->isPressed();
    uint32_t now = millis();
    handleSerialCommands();

    reconnectWiFi(now);
    bool wifiConnected = WiFi.status() == WL_CONNECTED;
    if (!wifiConnected) {
      if (wifiWasConnected) printWiFiStatus("disconnected");
      weatherOK = false;
      locationOK = false;
      locationName = "NO WIFI";
    } else if (!wifiWasConnected) {
      printWiFiStatus("reconnected");
      wifiWasConnected = true;
      weatherOK = false;
      locationOK = false;
      locationName = "LOCATING...";
      lastLocationAttempt = now - LOCATION_RETRY_INTERVAL;
    } else {
      if (!locationOK && now - lastLocationAttempt >= LOCATION_RETRY_INTERVAL && updateLocationFromIP()) {
        if (!updateWeather()) weatherOK = false;
        lastWeatherUpdate = now;
      }
      if ((pressedA && !lastButtonA) || now - lastWeatherUpdate > 600000) {
        if (updateWeather()) {
          lastWeatherUpdate = now;
        } else {
          weatherOK = false;
        }
      }
    }
    if (!wifiConnected) wifiWasConnected = false;
    lastButtonA = pressedA;
    updateStatusLEDs();

    animationPhase++;
    drawDashboard();
    delay(2000);
  }
