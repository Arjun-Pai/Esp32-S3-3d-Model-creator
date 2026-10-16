// Tempad: ESP32-S3 camera + microSD web UI
// Live camera, SD file manager (upload / download / delete), save photos to SD
//
// Arduino IDE settings:
//   Board: ESP32S3 Dev Module
//   USB CDC On Boot: Enabled
//   PSRAM: OPI PSRAM
//   Flash Size: 16MB
//   Partition Scheme: 16M Flash (3MB APP/9.9MB FATFS)
//   Serial Monitor: 115200 baud
//
// No extra libraries needed (everything ships with the ESP32 board package).
// Use a FAT32-formatted microSD card.

#include <WiFi.h>
#include <WiFiMulti.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <SD_MMC.h>
#include <FS.h>
#include "esp_camera.h"
#include "esp_http_server.h"

// ===================== ADD YOUR WI-FI NETWORKS HERE =====================
// One line per network. The board tries them and joins the strongest one it finds.
void addWifiNetworks() {
  wifiMultiAdd("YOUR_WIFI_NAME", "YOUR_WIFI_PASSWORD");
  // wifiMultiAdd("Second network name", "second password");
  // wifiMultiAdd("My phone hotspot", "hotspot password");
}

// If no network above connects, the board makes its own hotspot:
#define AP_SSID "Tempad-ESP32"
#define AP_PASS "tempad1234"   // at least 8 characters
// ========================================================================

// ---- Camera pins (common ESP32-S3 camera board layout) ----
#define PWDN_GPIO_NUM  -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM  15
#define SIOD_GPIO_NUM  4
#define SIOC_GPIO_NUM  5
#define Y9_GPIO_NUM    16
#define Y8_GPIO_NUM    17
#define Y7_GPIO_NUM    18
#define Y6_GPIO_NUM    12
#define Y5_GPIO_NUM    10
#define Y4_GPIO_NUM    8
#define Y3_GPIO_NUM    9
#define Y2_GPIO_NUM    11
#define VSYNC_GPIO_NUM 6
#define HREF_GPIO_NUM  7
#define PCLK_GPIO_NUM  13

// ---- microSD pins (1-bit SD_MMC mode) ----
#define SD_CLK_PIN 39
#define SD_CMD_PIN 38
#define SD_D0_PIN  40

WiFiMulti wifiMulti;
WebServer server(80);
httpd_handle_t streamHttpd = NULL;
SemaphoreHandle_t camMutex;
File uploadFile;
bool sdOk = false;
bool camOk = false;
bool usingAP = false;
unsigned long lastWifiCheck = 0;

void wifiMultiAdd(const char *ssid, const char *pass) {
  wifiMulti.addAP(ssid, pass);
}

// ---------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------
String baseName(const String &p) {
  int i = p.lastIndexOf('/');
  return (i >= 0) ? p.substring(i + 1) : p;
}

bool safePath(const String &p) {
  return p.length() > 0 && p.startsWith("/") && p.indexOf("..") < 0;
}

String jsonEscape(const String &s) {
  String o;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') o += '\\';
    o += c;
  }
  return o;
}

// ---------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------
bool initCamera() {
  camera_config_t config;
  memset(&config, 0, sizeof(config));
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
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_VGA;
  config.jpeg_quality = 12;
  config.fb_count = 2;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;

  if (!psramFound()) {
    Serial.println("WARNING: PSRAM not found. Set Tools > PSRAM > OPI PSRAM. Using small frames.");
    config.frame_size = FRAMESIZE_QVGA;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("ERROR: camera init failed (0x%x).\n", err);
    Serial.println("  0x105 = camera not detected: check the ribbon cable is seated and the camera pins above match your board.");
    return false;
  }
  Serial.println("Camera initialized OK");
  return true;
}

static const char *STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=tempadframe";
static const char *STREAM_BOUNDARY = "\r\n--tempadframe\r\n";
static const char *STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static esp_err_t streamHandler(httpd_req_t *req) {
  esp_err_t res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
  if (res != ESP_OK) return res;
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  char partBuf[64];

  while (true) {
    xSemaphoreTake(camMutex, portMAX_DELAY);
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      xSemaphoreGive(camMutex);
      Serial.println("ERROR: camera frame capture failed (stream)");
      res = ESP_FAIL;
      break;
    }
    res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
    if (res == ESP_OK) {
      size_t hlen = snprintf(partBuf, sizeof(partBuf), STREAM_PART, (unsigned)fb->len);
      res = httpd_resp_send_chunk(req, partBuf, hlen);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
    }
    esp_camera_fb_return(fb);
    xSemaphoreGive(camMutex);
    if (res != ESP_OK) break;  // viewer closed the page or stopped the stream
  }
  return res;
}

void startStreamServer() {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.server_port = 81;
  httpd_uri_t streamUri = {
    .uri = "/stream",
    .method = HTTP_GET,
    .handler = streamHandler,
    .user_ctx = NULL
  };
  if (httpd_start(&streamHttpd, &cfg) == ESP_OK) {
    httpd_register_uri_handler(streamHttpd, &streamUri);
    Serial.println("Stream server started on port 81");
  } else {
    Serial.println("ERROR: stream server failed to start");
  }
}

// ---------------------------------------------------------------------
// Web page
// ---------------------------------------------------------------------
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Tempad</title>
<style>
body{font-family:system-ui,sans-serif;background:#1a1208;color:#ffb347;margin:0;padding:12px;max-width:760px;margin:auto}
h2{letter-spacing:4px;margin:6px 0}
section{border:1px solid #ff8c00;border-radius:8px;padding:10px;margin:12px 0}
img{width:100%;background:#000;border-radius:6px;min-height:120px}
button,select,input[type=file]{background:#2b1c08;color:#ffb347;border:1px solid #ff8c00;border-radius:6px;padding:6px 10px;margin:3px 0}
button:active{background:#ff8c00;color:#000}
.row{display:flex;flex-wrap:wrap;gap:6px;align-items:center}
table{width:100%;border-collapse:collapse}
td{padding:5px 4px;border-bottom:1px solid #4a3010;word-break:break-all}
a{color:#ffd27f}
progress{width:100%}
#msg{font-size:14px}
</style></head><body>
<h2>TEMPAD</h2>

<section>
<h3>Live camera</h3>
<img id="cam" alt="stream stopped">
<div class="row">
<button id="sbtn" onclick="toggleStream()">Start stream</button>
<select id="res" onchange="setRes()">
<option value="QVGA">320x240</option>
<option value="VGA" selected>640x480</option>
<option value="SVGA">800x600</option>
<option value="XGA">1024x768</option>
<option value="SXGA">1280x1024</option>
<option value="UXGA">1600x1200</option>
</select>
<button onclick="takePhoto()">Save photo to SD</button>
</div>
</section>

<section>
<h3>SD card <span id="space"></span></h3>
<div id="path"></div>
<div class="row">
<button onclick="goUp()">Up</button>
<button onclick="makeDir()">New folder</button>
<button onclick="load()">Refresh</button>
</div>
<div class="row">
<input type="file" id="file" multiple>
<button onclick="upload()">Upload</button>
</div>
<progress id="prog" value="0" max="100"></progress>
<div id="msg"></div>
<table id="files"></table>
</section>

<script>
const $=id=>document.getElementById(id);
let cwd='/',streaming=false;

function toggleStream(){
  if(!streaming){
    $('cam').src='http://'+location.hostname+':81/stream?'+Date.now();
    streaming=true;$('sbtn').textContent='Stop stream';
  }else{
    $('cam').src='';
    streaming=false;$('sbtn').textContent='Start stream';
  }
}
async function setRes(){
  const r=await fetch('/res?size='+$('res').value);
  $('msg').textContent=await r.text();
}
async function takePhoto(){
  $('msg').textContent='Saving photo...';
  const r=await fetch('/photo');
  $('msg').textContent=await r.text();
  load();
}
function fmt(b){
  if(b<1024)return b+' B';
  if(b<1048576)return (b/1024).toFixed(1)+' KB';
  return (b/1048576).toFixed(2)+' MB';
}
async function load(){
  const r=await fetch('/list?dir='+encodeURIComponent(cwd));
  const d=await r.json();
  if(d.error){$('msg').textContent=d.error;return;}
  $('path').textContent=d.dir;
  $('space').textContent=' ('+(d.used/1024).toFixed(0)+' MB used of '+(d.total/1024).toFixed(0)+' MB)';
  const t=$('files');t.innerHTML='';
  d.files.sort((a,b)=>(b.d-a.d)||a.n.localeCompare(b.n));
  for(const f of d.files){
    const full=(cwd==='/'?'':cwd)+'/'+f.n;
    const tr=t.insertRow();
    const c1=tr.insertCell();
    const a=document.createElement('a');
    if(f.d){
      a.href='#';a.textContent='[ '+f.n+' ]';
      a.onclick=e=>{e.preventDefault();cwd=full;load();};
    }else{
      a.href='/download?path='+encodeURIComponent(full);a.textContent=f.n;
    }
    c1.appendChild(a);
    tr.insertCell().textContent=f.d?'':fmt(f.s);
    const c3=tr.insertCell();
    const b=document.createElement('button');
    b.textContent='Delete';b.onclick=()=>del(full);
    c3.appendChild(b);
  }
}
function goUp(){
  cwd=cwd.substring(0,cwd.lastIndexOf('/'))||'/';
  load();
}
async function del(p){
  if(!confirm('Delete '+p+'?'))return;
  const r=await fetch('/delete?path='+encodeURIComponent(p));
  $('msg').textContent=await r.text();
  load();
}
async function makeDir(){
  const n=prompt('Folder name');
  if(!n)return;
  const full=(cwd==='/'?'':cwd)+'/'+n;
  const r=await fetch('/mkdir?path='+encodeURIComponent(full));
  $('msg').textContent=await r.text();
  load();
}
function upload(){
  const files=$('file').files;
  if(!files.length){$('msg').textContent='Choose files first';return;}
  let i=0;
  const next=()=>{
    if(i>=files.length){
      $('msg').textContent='Upload complete';
      $('file').value='';$('prog').value=0;load();return;
    }
    const fd=new FormData();
    fd.append('file',files[i]);
    const x=new XMLHttpRequest();
    x.open('POST','/upload?dir='+encodeURIComponent(cwd));
    x.upload.onprogress=e=>{
      if(e.lengthComputable){
        $('prog').value=e.loaded/e.total*100;
        $('msg').textContent='Uploading '+files[i].name;
      }
    };
    x.onload=()=>{i++;next();};
    x.onerror=()=>{$('msg').textContent='Upload failed';};
    x.send(fd);
  };
  next();
}
load();
</script>
</body></html>
)rawliteral";

// ---------------------------------------------------------------------
// Web handlers
// ---------------------------------------------------------------------
void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleList() {
  if (!sdOk) {
    server.send(500, "application/json", "{\"error\":\"SD card not available\"}");
    return;
  }
  String dir = server.hasArg("dir") ? server.arg("dir") : "/";
  if (!safePath(dir)) {
    server.send(400, "application/json", "{\"error\":\"Bad path\"}");
    return;
  }
  File root = SD_MMC.open(dir);
  if (!root || !root.isDirectory()) {
    server.send(404, "application/json", "{\"error\":\"Folder not found\"}");
    return;
  }
  String json = "{\"dir\":\"" + jsonEscape(dir) + "\"";
  json += ",\"total\":" + String((unsigned long)(SD_MMC.totalBytes() / 1024));
  json += ",\"used\":" + String((unsigned long)(SD_MMC.usedBytes() / 1024));
  json += ",\"files\":[";
  bool first = true;
  File f = root.openNextFile();
  while (f) {
    if (!first) json += ",";
    first = false;
    json += "{\"n\":\"" + jsonEscape(baseName(String(f.name()))) + "\",";
    json += "\"d\":" + String(f.isDirectory() ? 1 : 0) + ",";
    json += "\"s\":" + String((unsigned long)f.size()) + "}";
    f = root.openNextFile();
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void handleDownload() {
  String path = server.arg("path");
  if (!sdOk || !safePath(path)) {
    server.send(400, "text/plain", "Bad request");
    return;
  }
  File f = SD_MMC.open(path, FILE_READ);
  if (!f || f.isDirectory()) {
    server.send(404, "text/plain", "File not found");
    return;
  }
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + baseName(path) + "\"");
  server.streamFile(f, "application/octet-stream");
  f.close();
}

void handleUploadDone() {
  if (!sdOk) {
    server.send(500, "text/plain", "SD card not available");
    return;
  }
  server.send(200, "text/plain", "OK");
}

void handleUploadData() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    if (!sdOk) return;
    String dir = server.hasArg("dir") ? server.arg("dir") : "/";
    if (!safePath(dir)) dir = "/";
    if (!dir.endsWith("/")) dir += "/";
    String path = dir + baseName(up.filename);
    if (SD_MMC.exists(path)) SD_MMC.remove(path);
    uploadFile = SD_MMC.open(path, FILE_WRITE);
    if (!uploadFile) {
      Serial.printf("ERROR: could not create %s on the SD card\n", path.c_str());
    } else {
      Serial.printf("Upload started: %s\n", path.c_str());
    }
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
      size_t w = uploadFile.write(up.buf, up.currentSize);
      if (w != up.currentSize) Serial.println("ERROR: SD write failed (card full or removed?)");
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close();
      Serial.printf("Upload finished: %u bytes\n", (unsigned)up.totalSize);
    }
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (uploadFile) uploadFile.close();
    Serial.println("ERROR: upload aborted");
  }
}

void handleDelete() {
  String path = server.arg("path");
  if (!sdOk || !safePath(path) || path == "/") {
    server.send(400, "text/plain", "Bad request");
    return;
  }
  File f = SD_MMC.open(path);
  if (!f) {
    server.send(404, "text/plain", "Not found");
    return;
  }
  bool isDir = f.isDirectory();
  f.close();
  bool ok = isDir ? SD_MMC.rmdir(path) : SD_MMC.remove(path);
  if (ok) {
    Serial.printf("Deleted %s\n", path.c_str());
    server.send(200, "text/plain", "Deleted " + path);
  } else {
    Serial.printf("ERROR: could not delete %s\n", path.c_str());
    server.send(500, "text/plain", isDir ? "Could not delete (folder must be empty)" : "Could not delete file");
  }
}

void handleMkdir() {
  String path = server.arg("path");
  if (!sdOk || !safePath(path)) {
    server.send(400, "text/plain", "Bad request");
    return;
  }
  if (SD_MMC.mkdir(path)) {
    server.send(200, "text/plain", "Folder created: " + path);
  } else {
    server.send(500, "text/plain", "Could not create folder (already exists?)");
  }
}

void handlePhoto() {
  if (!sdOk) {
    server.send(500, "text/plain", "SD card not available");
    return;
  }
  if (!camOk) {
    server.send(500, "text/plain", "Camera not available");
    return;
  }
  if (xSemaphoreTake(camMutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
    server.send(503, "text/plain", "Camera busy, try again");
    return;
  }
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    xSemaphoreGive(camMutex);
    Serial.println("ERROR: camera frame capture failed (photo)");
    server.send(500, "text/plain", "Camera capture failed");
    return;
  }
  if (!SD_MMC.exists("/photos")) SD_MMC.mkdir("/photos");
  static int n = 0;
  String path;
  do {
    path = "/photos/IMG_" + String(n++) + ".jpg";
  } while (SD_MMC.exists(path));

  File f = SD_MMC.open(path, FILE_WRITE);
  if (!f) {
    esp_camera_fb_return(fb);
    xSemaphoreGive(camMutex);
    Serial.printf("ERROR: could not create %s\n", path.c_str());
    server.send(500, "text/plain", "Could not create file on SD");
    return;
  }
  size_t w = f.write(fb->buf, fb->len);
  size_t len = fb->len;
  f.close();
  esp_camera_fb_return(fb);
  xSemaphoreGive(camMutex);

  if (w != len) {
    Serial.println("ERROR: SD write incomplete (card full?)");
    server.send(500, "text/plain", "SD write failed");
    return;
  }
  Serial.printf("Photo saved: %s (%u bytes)\n", path.c_str(), (unsigned)len);
  server.send(200, "text/plain", "Saved " + path + " (" + String(len / 1024) + " KB)");
}

void handleRes() {
  String s = server.arg("size");
  framesize_t fs;
  if (s == "QVGA") fs = FRAMESIZE_QVGA;
  else if (s == "VGA") fs = FRAMESIZE_VGA;
  else if (s == "SVGA") fs = FRAMESIZE_SVGA;
  else if (s == "XGA") fs = FRAMESIZE_XGA;
  else if (s == "SXGA") fs = FRAMESIZE_SXGA;
  else if (s == "UXGA") fs = FRAMESIZE_UXGA;
  else {
    server.send(400, "text/plain", "Unknown size");
    return;
  }
  if (!camOk) {
    server.send(500, "text/plain", "Camera not available");
    return;
  }
  if (xSemaphoreTake(camMutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
    server.send(503, "text/plain", "Camera busy, try again");
    return;
  }
  sensor_t *sensor = esp_camera_sensor_get();
  int r = sensor ? sensor->set_framesize(sensor, fs) : -1;
  xSemaphoreGive(camMutex);
  if (r == 0) {
    Serial.printf("Resolution set to %s\n", s.c_str());
    server.send(200, "text/plain", "Resolution set to " + s);
  } else {
    Serial.println("ERROR: could not change resolution");
    server.send(500, "text/plain", "Could not change resolution");
  }
}

// ---------------------------------------------------------------------
// Wi-Fi
// ---------------------------------------------------------------------
void connectWifi() {
  addWifiNetworks();
  WiFi.mode(WIFI_STA);
  Serial.println("Connecting to Wi-Fi...");
  if (wifiMulti.run(15000) == WL_CONNECTED) {
    usingAP = false;
    Serial.printf("Connected to: %s\n", WiFi.SSID().c_str());
    Serial.print("Open in your browser: http://");
    Serial.println(WiFi.localIP());
  } else {
    usingAP = true;
    Serial.println("ERROR: could not connect to any saved network. Starting own hotspot instead.");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.printf("Join Wi-Fi '%s' (password '%s'), then open http://", AP_SSID, AP_PASS);
    Serial.println(WiFi.softAPIP());
  }
  WiFi.setSleep(false);
  if (MDNS.begin("tempad")) {
    Serial.println("Also reachable at http://tempad.local");
  }
}

// ---------------------------------------------------------------------
// Setup / loop
// ---------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  unsigned long start = millis();
  while (!Serial && millis() - start < 3000) delay(10);
  delay(500);
  Serial.println();
  Serial.println("=== Tempad camera + SD web UI ===");

  camMutex = xSemaphoreCreateMutex();

  // microSD
  SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN);
  if (!SD_MMC.begin("/sdcard", true, false)) {
    Serial.println("ERROR: SD card mount failed. Check: card inserted, FAT32 format, SD pins above match your board.");
  } else if (SD_MMC.cardType() == CARD_NONE) {
    Serial.println("ERROR: no SD card attached.");
  } else {
    sdOk = true;
    Serial.printf("SD card OK: %llu MB total, %llu MB used\n",
                  SD_MMC.totalBytes() / (1024 * 1024), SD_MMC.usedBytes() / (1024 * 1024));
  }

  // Camera
  camOk = initCamera();

  // Wi-Fi
  connectWifi();

  // Web server
  server.on("/", HTTP_GET, handleRoot);
  server.on("/list", HTTP_GET, handleList);
  server.on("/download", HTTP_GET, handleDownload);
  server.on("/upload", HTTP_POST, handleUploadDone, handleUploadData);
  server.on("/delete", HTTP_GET, handleDelete);
  server.on("/mkdir", HTTP_GET, handleMkdir);
  server.on("/photo", HTTP_GET, handlePhoto);
  server.on("/res", HTTP_GET, handleRes);
  server.begin();
  Serial.println("Web server started on port 80");

  if (camOk) startStreamServer();

  Serial.printf("Free heap: %u bytes, free PSRAM: %u bytes\n", ESP.getFreeHeap(), ESP.getFreePsram());
  Serial.println("Ready.");
}

void loop() {
  server.handleClient();

  // Try to rejoin Wi-Fi if the connection drops
  if (!usingAP && millis() - lastWifiCheck > 10000) {
    lastWifiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Wi-Fi lost. Trying to reconnect...");
      wifiMulti.run(5000);
    }
  }
  delay(2);
}
