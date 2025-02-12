#include <Arduino.h>
#include <ESPSupabase.h>

#include <WiFi.h>

Supabase db;

// Put your supabase URL and Anon key here...
String supabase_url = "https://ftzrnnktducikjugztsg.supabase.co";
String anon_key = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImZ0enJubmt0ZHVjaWtqdWd6dHNnIiwicm9sZSI6ImFub24iLCJpYXQiOjE3MzYwMTI3MjksImV4cCI6MjA1MTU4ODcyOX0.mVRwXFJCbfFbsLBFeirZ57KfDB_YrgJRkGMO2str5oU";

// Put your target table here
String table = "sensor_readings";

// Put your JSON that you want to insert rows
// You can also use library like ArduinoJson generate this
String JSON = "40";

bool upsert = false;

void setup() {
  Serial.begin(9600);

  Serial.print("Connecting to WiFi");
  WiFi.begin("FourJoes", "404notfound");
  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
    Serial.print(".");
  }
  Serial.println("Connected!");

  // Beginning Supabase Connection
  db.begin(supabase_url, anon_key);

  int code = db.update(table).eq("moisture_level", "50").doUpdate(JSON);
  Serial.println(code);
  db.urlQuery_reset();
}

void loop() {
  delay(10);
}