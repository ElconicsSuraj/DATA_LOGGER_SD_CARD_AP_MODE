#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <SD.h>

// ---------- Access Point Config ----------
const char* ap_ssid = "ESP32-SD-Explorer";
const char* ap_password = "12345678";   // min 8 chars

#define SD_CS 5

WebServer server(80);

// ---------- List files ----------
void handleList() {
  File root = SD.open("/");
  String json = "[";

  while (true) {
    File file = root.openNextFile();
    if (!file) break;

    if (json.length() > 1) json += ",";
    json += "{";
    json += "\"name\":\"" + String(file.name()) + "\",";
    json += "\"size\":" + String(file.size());
    json += "}";

    file.close();
  }
  json += "]";
  server.send(200, "application/json", json);
}

// ---------- View file (NO download) ----------
void handleFile() {
  if (!server.hasArg("name")) {
    server.send(400, "text/plain", "Missing filename");
    return;
  }

  String name = "/" + server.arg("name");
  File file = SD.open(name);
  if (!file) {
    server.send(404, "text/plain", "File not found");
    return;
  }

  String type = "text/plain";
  if (name.endsWith(".csv")) type = "text/csv";
  else if (name.endsWith(".html")) type = "text/html";
  else if (name.endsWith(".json")) type = "application/json";

  server.streamFile(file, type);
  file.close();
}

// ---------- Download file ----------
void handleDownload() {
  if (!server.hasArg("name")) {
    server.send(400, "text/plain", "Missing filename");
    return;
  }

  String name = "/" + server.arg("name");
  File file = SD.open(name);
  if (!file) {
    server.send(404, "text/plain", "File not found");
    return;
  }

  server.sendHeader(
    "Content-Disposition",
    "attachment; filename=\"" + server.arg("name") + "\""
  );
  server.streamFile(file, "application/octet-stream");
  file.close();
}

// ---------- UI ----------
void handleRoot() {
  server.send(200, "text/html", R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<title>ESP32 SD File Manager</title>
<style>
body { font-family: Arial; background:#f5f6fa; padding:20px; }
h2 { color:#2c3e50; }
.container { display:flex; gap:20px; }
.file-list {
  width:30%;
  background:#fff;
  border:1px solid #ccc;
  height:80vh;
  overflow:auto;
}
.file {
  padding:10px;
  border-bottom:1px solid #eee;
  cursor:pointer;
}
.file:hover { background:#ecf0f1; }
.viewer {
  width:70%;
  background:#fff;
  border:1px solid #ccc;
  padding:10px;
  overflow:auto;
}
button {
  padding:8px 12px;
  margin-top:10px;
  background:#3498db;
  color:white;
  border:none;
  cursor:pointer;
  border-radius:4px;
}
table { border-collapse:collapse; width:100%; }
th, td { border:1px solid #ccc; padding:6px; text-align:center; }
th { background:#34495e; color:white; }
pre { white-space:pre-wrap; }
</style>
</head>

<body>
<h2>ESP32 SD Card File Explorer (AP Mode)</h2>

<div class="container">
  <div class="file-list" id="files"></div>
  <div class="viewer" id="viewer">Select a file</div>
</div>

<script>
let currentFile = "";

function loadFiles() {
  fetch('/api/list')
    .then(r => r.json())
    .then(files => {
      const list = document.getElementById('files');
      list.innerHTML = '';
      files.forEach(f => {
        const div = document.createElement('div');
        div.className = 'file';
        div.textContent = f.name + ' (' + f.size + ' bytes)';
        div.onclick = () => openFile(f.name);
        list.appendChild(div);
      });
    });
}

function openFile(name) {
  currentFile = name;
  fetch('/api/file?name=' + name)
    .then(r => r.text())
    .then(text => {
      const viewer = document.getElementById('viewer');
      let content = '';

      if (name.endsWith('.csv')) {
        content = csvToTable(text);
      } else {
        content = '<pre>' + text + '</pre>';
      }

      content += '<br><button onclick="downloadFile()">Download File</button>';
      viewer.innerHTML = content;
    });
}

function downloadFile() {
  window.location.href = '/api/download?name=' + currentFile;
}

function csvToTable(text) {
  let html = '<table>';
  const rows = text.trim().split('\n');
  rows.forEach((row,i)=>{
    html += '<tr>';
    row.split(',').forEach(col=>{
      html += i==0 ? '<th>'+col+'</th>' : '<td>'+col+'</td>';
    });
    html += '</tr>';
  });
  html += '</table>';
  return html;
}

loadFiles();
</script>
</body>
</html>
)rawliteral");
}

void setup() {
  Serial.begin(115200);

  // ---------- AP Mode ----------
  WiFi.softAP(ap_ssid, ap_password);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  // ---------- SD ----------
  if (!SD.begin(SD_CS)) {
    Serial.println("SD Card Mount Failed");
    return;
  }

  // ---------- Routes ----------
  server.on("/", handleRoot);
  server.on("/api/list", handleList);
  server.on("/api/file", handleFile);
  server.on("/api/download", handleDownload);

  server.begin();
  Serial.println("Server started");
}

void loop() {
  server.handleClient();
}
