# 🌤️ UNIHIKER K10 Smart Weather Station & QR Wi-Fi Provisioning

An advanced, feature-rich IoT Weather Station built for the **UNIHIKER K10** (ESP32-S3) in Mind+ / Arduino C++ mode. It integrates automatic IP-based geolocation, real-time outdoor weather forecasting via Open-Meteo, indoor climate monitoring, dynamic Day/Night UI themes, custom animated weather icons, audio alerts, and camera-based Wi-Fi QR code setup.

---
[img](https://github.com/Lazpal/UNIHIKER-K10-Weather-Station/blob/main/Gemini_Generated_Image_w3ud2lw3ud2lw3ud.jpg?raw=true)
---

## 🌟 Key Features

* 📍 **Automatic IP Geolocation**: Automatically determines latitude, longitude, and city name using public IPv4/IPv6 APIs (`ipify` & `ip-api.com`) without manual configuration.
* 🌦️ **Real-Time Outdoor Weather & 4-Hour Forecast**: Fetches current temperature, apparent "feels like" temp, humidity, wind speed, daily min/max, and a 4-hour hourly forecast (temperature & rain probability) from **Open-Meteo API** (No API key required).
* 📷 **Camera QR Code Wi-Fi Provisioning**: If no Wi-Fi credentials are stored or connection fails, the device automatically launches the AI camera to scan standard Wi-Fi QR codes (`WIFI:S:...;P:...;;`).
* 💾 **Persistent NVS Storage**: Stores Wi-Fi credentials in non-volatile flash memory via `Preferences.h`.
* 🏠 **Indoor Environment Sensing**: Displays real-time indoor room temperature and humidity using the onboard **AHT20** sensor.
* 🌓 **Adaptive Day/Night Theme**: Uses the onboard **Ambient Light Sensor (ALS)** to automatically switch between Light and Dark UI background themes based on room lux levels.
* 🎨 **Animated Custom Canvas Visuals**: Hand-crafted vector graphics for weather conditions (Sun with rotating rays, Clouds, Animated Rain drops, Animated Snowflakes, Lightning Storms).
* 🔔 **Smart Rain Alerts & Sound Effects**: 
  * Audio tone sequences via onboard buzzer when rain starts or when rain probability exceeds 35%.
  * Color-coded status alerts via onboard RGB LEDs matching weather conditions or connection status.
* 🕒 **NTP Time Synchronization**: Automatically syncs local time via NTP server (`pool.ntp.org`).
* 🔘 **Manual Refresh & Serial Management**:
  * Press **Button A** to manually refresh weather data.
  * Send `clear data` over Serial (115200 baud) to wipe stored Wi-Fi credentials and reset the device.

---

## 🛠️ Hardware Requirements

* **DFRobot UNIHIKER K10** (ESP32-S3 IoT Development Board)
* Onboard **AHT20** Temperature & Humidity Sensor
* Onboard **AI Camera**
* Onboard **Ambient Light Sensor (ALS)**
* Onboard **240x320 Color LCD Display**
* Onboard **Buzzer** & **RGB LEDs**

---

## 📦 Required Libraries

Ensure the following libraries are installed in your Arduino IDE or Mind+ environment:

* `unihiker_k10.h` (UNIHIKER K10 Hardware Support)
* `AIRecognition.h` (UNIHIKER AI / Camera Recognition Module)
* `WiFi.h` & `WiFiClientSecure.h` (ESP32 Network Stack)
* `HTTPClient.h` (HTTP requests)
* `Preferences.h` (ESP32 NVS Flash Storage)
* `time.h` (ESP32 Time API)

---

## 🚀 Getting Started

### 1. Installation
1. Clone this repository:
   ```bash
   git clone [https://github.com/your-username/unihiker-k10-weather-station.git](https://github.com/your-username/unihiker-k10-weather-station.git)

---

## 📄 License
This project is open-source and available under the MIT License.
