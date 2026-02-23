#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <Preferences.h>
#include "RTClib.h"
#include <math.h>

/* ================== PINS ================== */
#define LED_R   2
#define LED_G   25
#define LED_B   26
#define SD_CS_PIN 5
#define BOOT_BTN 4

/* ================== WIFI AP ================== */
const char* AP_SSID = "ESP32-LOGGER";
const char* AP_PASS = "12345678";
#define WEB_SESSION_MS (3 * 60 * 1000UL)

/* ================== OBJECTS ================== */
RTC_DS3231 rtc;
Preferences prefs;
WebServer server(80);

uint32_t packetCounter = 0;
unsigned long lastLogTime = 0;
unsigned long webStartTime = 0;
bool webMode = false;

/* ================== LED ================== */
void rgbOff() {
  digitalWrite(LED_R, LOW);
  digitalWrite(LED_G, LOW);
  digitalWrite(LED_B, LOW);
}

void rgbGreen() { rgbOff(); digitalWrite(LED_G, HIGH); }
void rgbBlue()  { rgbOff(); digitalWrite(LED_B, HIGH); }

void blinkLED(uint8_t n = 1) {
  while (n--) {
    digitalWrite(LED_G, HIGH);
    delay(50);
    digitalWrite(LED_G, LOW);
    delay(50);
  }
}

/* ================== RTC ================== */
String rtcDate() {
  DateTime n = rtc.now();
  char b[16];
  sprintf(b, "%04d-%02d-%02d", n.year(), n.month(), n.day());
  return String(b);
}

String rtcTime() {
  DateTime n = rtc.now();
  char b[16];
  sprintf(b, "%02d:%02d:%02d", n.hour(), n.minute(), n.second());
  return String(b);
}

/* ================== RANDOM DATA LOG ================== */
void logRandomData() {

  int16_t ax = random(-2000, 2000);
  int16_t ay = random(-2000, 2000);
  int16_t az = random(-2000, 2000);

  int16_t gx = random(-500, 500);
  int16_t gy = random(-500, 500);
  int16_t gz = random(-500, 500);

  int8_t temp = random(20, 40);
  uint8_t batt = random(50, 100);

  packetCounter++;
  prefs.putUInt("cnt", packetCounter);

  float pitch = atan2(ay, az) * 57.2958;
  float roll  = atan2(-ax, sqrt(ay * ay + az * az)) * 57.2958;

  String file = "/log_" + rtcDate() + ".csv";

  if (!SD.exists(file)) {
    File f = SD.open(file, FILE_WRITE);
    f.println("Date,Time,Count,AX,AY,AZ,GX,GY,GZ,Pitch,Roll,Temp,Battery");
    f.close();
  }

  File f = SD.open(file, FILE_APPEND);
  if (!f) return;

  f.printf(
    "%s,%s,%lu,%d,%d,%d,%d,%d,%d,%.2f,%.2f,%d,%d\n",
    rtcDate().c_str(), rtcTime().c_str(), packetCounter,
    ax, ay, az, gx, gy, gz, pitch, roll, temp, batt
  );

  f.close();
  Serial.println("Data Logged");
  blinkLED(1);
}

/* ================== WEB UI (YOUR ORIGINAL UI) ================== */
void handleRoot() {
server.send(200, "text/html", R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<title>ESP32 SD Log Viewer</title>
<style>
body { font-family: Arial; background:#f5f6fa; margin:0; padding:20px; }
h2 { color:#2c3e50; }
.container { display:flex; gap:20px; }
.file-list { width:30%; background:#fff; border:1px solid #ccc; height:80vh; overflow:auto; }
.file { padding:10px; border-bottom:1px solid #eee; cursor:pointer; }
.file:hover { background:#ecf0f1; }
.viewer { width:70%; background:#fff; border:1px solid #ccc; padding:10px; overflow:auto; }
button { padding:8px 12px; margin-top:10px; background:#3498db; color:white; border:none; cursor:pointer; border-radius:4px; }
button:hover { background:#2980b9; }
table { border-collapse:collapse; width:100%; }
th, td { border:1px solid #ccc; padding:6px; text-align:center; }
th { background:#006e5f; color:white; }
pre { white-space:pre-wrap; }
@media print { button { display:none; } }
</style>
</head>

<body>
<h2>ESP32 SD Card Log Viewer</h2>

<div class="container">
<div class="file-list" id="files"></div>
<div class="viewer" id="viewer">Select a file</div>
</div>

<script>
let currentFile="";

function loadFiles(){
 fetch('/api/list')
 .then(r=>r.json())
 .then(files=>{
  const list=document.getElementById('files');
  list.innerHTML='';
  files.forEach(f=>{
   const div=document.createElement('div');
   div.className='file';
   div.textContent=f.name+' ('+f.size+' bytes)';
   div.onclick=()=>openFile(f.name);
   list.appendChild(div);
  });
 });
}

function openFile(name){
 currentFile=name;
 fetch('/api/file?name='+name)
 .then(r=>r.text())
 .then(text=>{
  const viewer=document.getElementById('viewer');
  let content='';

  if(name.endsWith('.csv')){
    content=csvToTable(text);
  }else{
    content='<pre>'+text+'</pre>';
  }

  content+=`
  <br>
  <button onclick="printLog()">Print</button>
  <button onclick="downloadFile()">Download</button>
  `;

  viewer.innerHTML=content;
 });
}

function downloadFile(){
 window.location.href='/api/download?name='+currentFile;
}

function printLog(){ window.print(); }

function csvToTable(text){
 let html='<table>';
 const rows=text.trim().split(/\r?\n/);
 rows.forEach((row,i)=>{
  html+='<tr>';
  row.split(',').forEach(col=>{
   html+= i===0 ? '<th>'+col+'</th>' : '<td>'+col+'</td>';
  });
  html+='</tr>';
 });
 html+='</table>';
 return html;
}

loadFiles();
</script>
</body>
</html>
)rawliteral");
}

/* ================== FILE API ================== */
void handleList() {
  File root = SD.open("/");
  String json = "[";

  while (true) {
    File f = root.openNextFile();
    if (!f) break;

    if (json.length() > 1) json += ",";
    json += "{\"name\":\"" + String(f.name()) +
            "\",\"size\":" + String(f.size()) + "}";
    f.close();
  }

  json += "]";
  server.send(200, "application/json", json);
}

void handleFile() {
  if (!server.hasArg("name")) return;
  File f = SD.open("/" + server.arg("name"));
  if (!f) return;
  server.streamFile(f, "text/plain");
  f.close();
}



void handleDownload() {
  if (!server.hasArg("name")) return;

  String filename = server.arg("name");

  File f = SD.open("/" + filename);
  if (!f) return;

  // force CSV download
  server.sendHeader("Content-Type", "text/csv");
  server.sendHeader(
    "Content-Disposition",
    "attachment; filename=\"" + filename + "\""
  );

  server.streamFile(f, "text/csv");
  f.close();
}

/* ================== START AP ================== */
void startWebServer() {
  Serial.println("Starting AP Mode...");
  rgbBlue();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  server.on("/", handleRoot);
  server.on("/api/list", handleList);
  server.on("/api/file", handleFile);
  server.on("/api/download", handleDownload);

  server.begin();
  webStartTime = millis();
  webMode = true;

  Serial.print("Open browser: ");
  Serial.println(WiFi.softAPIP());
}

/* ================== SETUP ================== */
void setup() {
  Serial.begin(115200);

  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  pinMode(BOOT_BTN, INPUT_PULLUP);

  rgbGreen();

  if (!rtc.begin()) {
    Serial.println("RTC FAIL");
    while (1);
  }

  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("SD FAIL");
    while (1);
  }

  prefs.begin("data-log", false);
  packetCounter = prefs.getUInt("cnt", 0);

  randomSeed(esp_random());
}

/* ================== LOOP ================== */
void loop() {

  /* ----- BUTTON → AP MODE ----- */
  if (!webMode && digitalRead(BOOT_BTN) == LOW) {
    delay(50);
    if (digitalRead(BOOT_BTN) == LOW) startWebServer();
  }

  /* ----- WEB MODE ----- */
  if (webMode) {
    server.handleClient();

    if (millis() - webStartTime > WEB_SESSION_MS)
      ESP.restart();

    return;
  }

  /* ----- NORMAL LOGGING ----- */
  if (millis() - lastLogTime > 2000) {
    lastLogTime = millis();
    logRandomData();
  }
}