#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ================= CONFIGURATION (FILL THESE IN!) =================
const char* HOME_SSID = "wifi name";      // <--- CHANGE THIS
const char* HOME_PASS = "password";  // <--- CHANGE THIS
// Paste ONLY the ID part of your Google URL:
String GOOGLE_SCRIPT_ID = "google sheet id"; 
// ==================================================================

const char *softAP_ssid = "laPasion WiFi";
const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 1, 1);
DNSServer dnsServer;
WebServer server(80);

// --- Helper: Connect to Internet & Upload ---
void syncToCloud() {
  Serial.println("\n[SYNC] Stopping Portal...");
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  
  // 🔥 THE FIX: Configure DNS to Google (8.8.8.8) BEFORE connecting.
  // This uses WiFi.config which is universally supported and fixes the "member not found" error.
  WiFi.config(IPAddress(0,0,0,0), IPAddress(0,0,0,0), IPAddress(0,0,0,0), IPAddress(8,8,8,8), IPAddress(8,8,4,4));
  
  WiFi.begin(HOME_SSID, HOME_PASS);

  Serial.print("[SYNC] Connecting to Home WiFi");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[SYNC] Connected! Reading data...");

    File file = LittleFS.open("/responses.txt", "r");
    
    if (!file || file.size() == 0) {
      Serial.println("[SYNC] No data to upload.");
    } else {
      while (file.available()) {
        String line = file.readStringUntil('\n');
        if (line.length() < 3) continue;

        int splitIndex = line.indexOf(':');
        String uName = line.substring(0, splitIndex);
        String uMsg = line.substring(splitIndex + 2);

        // Prepare Upload
        HTTPClient http;
        String url = "https://script.google.com/macros/s/" + GOOGLE_SCRIPT_ID + "/exec"; 
        
        http.begin(url.c_str());
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        http.addHeader("Content-Type", "application/json");
        
        // Use JsonDocument for cleaner syntax (removes warning)
        JsonDocument doc;
        doc["name"] = uName;
        doc["message"] = uMsg;
        
        String jsonPayload;
        serializeJson(doc, jsonPayload);
        
        int httpResponseCode = http.POST(jsonPayload);
        
        if (httpResponseCode > 0) {
           Serial.print("[SYNC] Uploaded! HTTP Code: ");
           Serial.println(httpResponseCode);
        } else {
           Serial.print("[SYNC] Failed. Error: "); Serial.println(httpResponseCode);
        }
        http.end();
      }
      file.close();
      LittleFS.remove("/responses.txt"); 
      Serial.println("[SYNC] Data wiped from chip.");
    }
  } else {
    Serial.println("\n[SYNC] WiFi Connection Failed. Check Password?");
  }

  Serial.println("[SYNC] Restarting Portal...");
  delay(2000);
  ESP.restart();
}

void handleRoot() {
  File file = LittleFS.open("/index.html", "r");
  if (!file) server.send(404, "text/plain", "Error: index.html missing.");
  else server.streamFile(file, "text/html");
  file.close();
}

void handleForm() {
  if (server.hasArg("name")) {
    // 1. Save data to file as usual
    File f = LittleFS.open("/responses.txt", "a"); 
    f.println(server.arg("name") + ": " + server.arg("msg"));
    f.close();

    // 2. Load and serve the thanks.html file
    File file = LittleFS.open("/thanks.html", "r");
    if (file) {
      server.streamFile(file, "text/html");
      file.close();
    } else {
      // Fallback just in case the file is missing
      server.send(200, "text/html", "<h1>Saved!</h1><p>(thanks.html not found)</p>");
    }
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleNotFound() { handleRoot(); }

void setup() {
  Serial.begin(115200);
  LittleFS.begin(true);
  pinMode(0, INPUT_PULLUP); // BOOT Button

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(softAP_ssid);
  dnsServer.start(DNS_PORT, "*", apIP);
  
  server.on("/", handleRoot);
  server.on("/submit", HTTP_POST, handleForm);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("System Ready. Press BOOT button to Sync.");
}

void loop() {
  if (digitalRead(0) == LOW) {
    delay(100);
    if (digitalRead(0) == LOW) syncToCloud();
  }
  dnsServer.processNextRequest();
  server.handleClient();
}