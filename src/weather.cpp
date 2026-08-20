#include "weather.h"
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// Two chained public, keyless APIs — no config, no secrets:
//   1. http://ip-api.com/json/        -> approximate lat/lon from public IP
//   2. https://api.open-meteo.com/... -> current weather at that lat/lon
// Both responses are tiny, so fields are pulled with simple substring
// parsing rather than pulling in a JSON library for one or two fields.

namespace {

bool haveLocation = false;
float latitude = 0, longitude = 0;
weather::Condition condition = weather::Condition::Unknown;

uint32_t nextGeoAttemptMs = 0;
uint32_t nextWeatherFetchMs = 0;

bool extractFloat(const String& body, const char* key, float& out) {
    int idx = body.indexOf(key);
    if (idx < 0) return false;
    idx += strlen(key);
    while (idx < (int)body.length() && body[idx] == ' ') idx++;
    int start = idx;
    while (idx < (int)body.length() && (isDigit(body[idx]) || body[idx] == '-' || body[idx] == '.')) idx++;
    if (idx == start) return false;
    out = body.substring(start, idx).toFloat();
    return true;
}

bool extractInt(const String& body, const char* key, int& out) {
    float f;
    if (!extractFloat(body, key, f)) return false;
    out = (int)f;
    return true;
}

weather::Condition codeToCondition(int code) {
    if (code == 0 || code == 1) return weather::Condition::Clear;
    if (code == 2 || code == 3) return weather::Condition::Cloudy;
    if (code == 45 || code == 48) return weather::Condition::Fog;
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return weather::Condition::Rain;
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return weather::Condition::Snow;
    if (code >= 95) return weather::Condition::Storm;
    return weather::Condition::Unknown;
}

void fetchLocation() {
    HTTPClient http;
    http.setTimeout(4000);
    if (!http.begin("http://ip-api.com/json/")) return;
    int code = http.GET();
    if (code == 200) {
        String body = http.getString();
        float lat, lon;
        if (extractFloat(body, "\"lat\":", lat) && extractFloat(body, "\"lon\":", lon)) {
            latitude = lat;
            longitude = lon;
            haveLocation = true;
        }
    }
    http.end();
}

void fetchWeather() {
    WiFiClientSecure client;
    client.setInsecure(); // skip TLS cert validation — acceptable for public, non-sensitive weather data
    HTTPClient http;
    http.setTimeout(5000);

    String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(latitude, 4) +
                 "&longitude=" + String(longitude, 4) + "&current_weather=true";
    if (!http.begin(client, url)) return;

    int code = http.GET();
    if (code == 200) {
        String body = http.getString();
        int weatherCode;
        if (extractInt(body, "\"weathercode\":", weatherCode)) {
            condition = codeToCondition(weatherCode);
        }
    }
    http.end();
}

}

namespace weather {

void begin() {
    nextGeoAttemptMs = 0;
    nextWeatherFetchMs = 0;
}

void tick(uint32_t nowMs) {
    if (WiFi.status() != WL_CONNECTED) return;

    if (!haveLocation) {
        if (nowMs >= nextGeoAttemptMs) {
            fetchLocation();
            nextGeoAttemptMs = nowMs + (haveLocation ? 3600000 : 60000); // retry in 1min on failure, else re-check hourly
        }
        return;
    }

    if (nowMs >= nextWeatherFetchMs) {
        fetchWeather();
        nextWeatherFetchMs = nowMs + 1800000; // every 30 min
    }
}

Condition current() {
    return condition;
}

}
