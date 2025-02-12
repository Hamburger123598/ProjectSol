#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPSupabaseRealtime.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>

// WiFi Configuration
const char* SSID = "Get Your Own Wifi Kid";
const char* WIFI_PASSWORD = "385Sunset";

// Supabase Configuration
const String SUPABASE_URL = "https://ftzrnnktducikjugztsg.supabase.co";
const String SUPABASE_ANON_KEY = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImZ0enJubmt0ZHVjaWtqdWd6dHNnIiwicm9sZSI6ImFub24iLCJpYXQiOjE3MzYwMTI3MjksImV4cCI6MjA1MTU4ODcyOX0.mVRwXFJCbfFbsLBFeirZ57KfDB_YrgJRkGMO2str5oU";
const String SUPABASE_EMAIL = "test@gmail.com";
const String SUPABASE_PASSWORD = "Test123";

// Auth endpoint
const String AUTH_ENDPOINT = "/auth/v1/token?grant_type=password";

// Sensor Configuration (from sol_sensors table)
const String SENSOR_UUID = "d103c4ab-e2f0-4889-a4f5-f8f5e33c1c34";
const int MOISTURE_SENSOR_PIN = A0;

// HTTP Client Configuration
const int HTTP_TIMEOUT = 20000;  // 20 second timeout
const int MAX_RETRIES = 3;
const int RETRY_DELAY = 5000;    // 5 seconds between retries

// Global Variables
SupabaseRealtime realtime;
HTTPClient http;
unsigned long lastUpdateTime = 0;
unsigned long updateFrequency = 300000;
String accessToken = "";
unsigned long tokenExpireTime = 0;
Preferences preferences;

// Custom HTTP Client with proper headers
class CustomHTTPClient : public HTTPClient {
public:
    void addCloudflareHeaders() {
        addHeader("User-Agent", "Mozilla/5.0 (compatible; ESP32 Sensor/1.0)");
        addHeader("Accept", "application/json");
        addHeader("Cache-Control", "no-cache");
        addHeader("Connection", "keep-alive");
    }
};

// Token management
void saveToken(const String& token, unsigned long expireTime) {
    preferences.begin("supabase", false);
    preferences.putString("token", token);
    preferences.putULong("expire", expireTime);
    preferences.end();
}

bool loadToken() {
    preferences.begin("supabase", true);
    accessToken = preferences.getString("token", "");
    tokenExpireTime = preferences.getULong("expire", 0);
    preferences.end();
    
    if (accessToken.length() > 0 && tokenExpireTime > millis()) {
        Serial.println("Loaded valid token from flash");
        return true;
    }
    return false;
}

bool isTokenExpired() {
    return millis() >= tokenExpireTime;
}

// HTTP request with retry logic
int makeHTTPRequest(CustomHTTPClient& client, const String& method, const String& payload = "") {
    int attempts = 0;
    int responseCode;
    
    while (attempts < MAX_RETRIES) {
        if (attempts > 0) {
            Serial.printf("Retry attempt %d after %d ms\n", attempts + 1, RETRY_DELAY);
            delay(RETRY_DELAY);
        }
        
        if (method == "GET") {
            responseCode = client.GET();
        } else if (method == "POST") {
            responseCode = client.POST(payload);
        }
        
        if (responseCode > 0 && responseCode != 429) {  // 429 is Too Many Requests
            return responseCode;
        }
        
        if (responseCode == 429) {
            Serial.println("Rate limited, waiting longer before retry...");
            delay(RETRY_DELAY * 2);  // Wait longer for rate limit
        }
        
        attempts++;
    }
    return responseCode;
}

bool authenticateSupabase() {
    if (loadToken()) {
        return true;
    }
    
    Serial.println("\n--- Authenticating with Supabase ---");
    
    String authUrl = SUPABASE_URL + AUTH_ENDPOINT;
    CustomHTTPClient authHttp;
    
    authHttp.begin(authUrl);
    authHttp.setTimeout(HTTP_TIMEOUT);
    authHttp.addCloudflareHeaders();
    authHttp.addHeader("apikey", SUPABASE_ANON_KEY);
    authHttp.addHeader("Content-Type", "application/json");
    
    DynamicJsonDocument authDoc(256);
    authDoc["email"] = SUPABASE_EMAIL;
    authDoc["password"] = SUPABASE_PASSWORD;
    
    String authPayload;
    serializeJson(authDoc, authPayload);
    
    int httpResponseCode = makeHTTPRequest(authHttp, "POST", authPayload);
    
    if (httpResponseCode == 200) {
        String response = authHttp.getString();
        DynamicJsonDocument doc(1024);
        DeserializationError error = deserializeJson(doc, response);
        
        if (!error) {
            accessToken = doc["access_token"].as<String>();
            // Set token expiration to 23 hours from now (conservative)
            tokenExpireTime = millis() + (23UL * 60 * 60 * 1000);
            saveToken(accessToken, tokenExpireTime);
            Serial.println("Authentication successful!");
            authHttp.end();
            return true;
        }
    }
    
    authHttp.end();
    return false;
}

void fetchSensorSettings() {
    if (isTokenExpired() && !authenticateSupabase()) {
        Serial.println("Failed to refresh authentication");
        return;
    }
    
    String query = String("/rest/v1/sensor_settings?sensor_id=eq.") + SENSOR_UUID;
    String fullUrl = SUPABASE_URL + query;
    
    CustomHTTPClient settingsHttp;
    settingsHttp.begin(fullUrl);
    settingsHttp.setTimeout(HTTP_TIMEOUT);
    settingsHttp.addCloudflareHeaders();
    settingsHttp.addHeader("apikey", SUPABASE_ANON_KEY);
    settingsHttp.addHeader("Authorization", "Bearer " + accessToken);
    settingsHttp.addHeader("Content-Type", "application/json");
    settingsHttp.addHeader("Prefer", "return=representation");
    
    int httpResponseCode = makeHTTPRequest(settingsHttp, "GET");
    
    if (httpResponseCode > 0) {
        String payload = settingsHttp.getString();
        DynamicJsonDocument doc(1024);
        DeserializationError error = deserializeJson(doc, payload);
        
        if (!error && !payload.isEmpty() && payload != "[]") {
            JsonObject settings = doc[0];
            updateFrequency = settings["update_frequency"].as<unsigned long>() * 60000;
            Serial.printf("Settings updated: Frequency = %lu minutes\n", updateFrequency / 60000);
        }
    }
    
    settingsHttp.end();
}

void uploadSensorReading() {
    if (isTokenExpired() && !authenticateSupabase()) {
        return;
    }
    
    int rawMoistureValue = analogRead(MOISTURE_SENSOR_PIN);
    float moistureLevel = map(rawMoistureValue, 0, 4095, 0, 10);
    
    DynamicJsonDocument doc(256);
    doc["sensor_id"] = SENSOR_UUID;
    doc["moisture_level"] = moistureLevel;
    doc["signal_strength"] = WiFi.RSSI();
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    CustomHTTPClient uploadHttp;
    uploadHttp.begin(SUPABASE_URL + "/rest/v1/sensor_readings");
    uploadHttp.setTimeout(HTTP_TIMEOUT);
    uploadHttp.addCloudflareHeaders();
    uploadHttp.addHeader("apikey", SUPABASE_ANON_KEY);
    uploadHttp.addHeader("Authorization", "Bearer " + accessToken);
    uploadHttp.addHeader("Content-Type", "application/json");
    uploadHttp.addHeader("Prefer", "return=representation");
    
    int httpResponseCode = uploadHttp.PUT(jsonString);

    //int httpResponseCode = makeHTTPRequest(uploadHttp, "POST", jsonString);
    
    if (httpResponseCode > 0) {
        Serial.printf("Upload successful. Moisture: %.1f, RSSI: %d\n", moistureLevel, WiFi.RSSI());
    }
    
    uploadHttp.end();
}

void setup() {
    Serial.begin(115200);
    
    WiFi.begin(SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi Connected!");
    
    if (authenticateSupabase()) {
        fetchSensorSettings();
    }
}

void loop() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi disconnected. Reconnecting...");
        WiFi.begin(SSID, WIFI_PASSWORD);
        delay(5000);
        return;
    }
    
    if (millis() - lastUpdateTime >= updateFrequency) {
        fetchSensorSettings();
        uploadSensorReading();
        lastUpdateTime = millis();
    }
    
    delay(1000);  // Prevent tight looping
}