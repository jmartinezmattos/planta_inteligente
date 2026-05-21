#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <time.h>

// ================= PINES =================
#define LM35_PIN 34
#define SOIL_PIN 35
#define LDR_PIN  32

// ================= CALIBRACIONES =================
const int DRY_VALUE   = 3000;
const int WET_VALUE   = 1225;
const int DARK_VALUE  = 200;
const int LIGHT_VALUE = 4095;

// ================= WIFI AP =================
const char* AP_SSID = "MacetaInteligente_Auto";
const char* AP_PASS = "";

// ================= SERVER =================
AsyncWebServer server(80);

// ================= CSV EN RAM =================
String csvData   = "";
int recordCount  = 0;

// ================= VARIABLES GLOBALES =================
float filteredHumidity = 0.0;
bool  firstRead        = true;
float currentTemp      = 0;
float currentHumPct    = 0;
float currentLightPct  = 0;
int   currentHumRaw    = 0;
int   currentLightRaw  = 0;

// Variables para el etiquetado automático
String globalLabel     = "Sin estado";
bool   autoLogEnabled  = false;
unsigned long lastLogMillis = 0;
const unsigned long LOG_INTERVAL = 60000; // 1 minuto (60000 ms)

// ================= FUNCIONES SENSORES =================

float lm35_read_celsius() {
    const int samples = 10;
    long mVSum = 0;
    analogRead(LM35_PIN);
    delay(5);
    for (int i = 0; i < samples; i++) {
        mVSum += analogReadMilliVolts(LM35_PIN);
        delay(10);
    }
    return (mVSum / (float)samples) / 10.0f;
}

float readSoilPercent(int &rawOut) {
    const int samples = 20;
    long rawSum = 0;
    analogRead(SOIL_PIN);
    delay(10);
    for (int i = 0; i < samples; i++) {
        rawSum += analogRead(SOIL_PIN);
        delay(20);
    }
    rawOut = rawSum / samples;
    float percent = 100.0f * (DRY_VALUE - rawOut) / (DRY_VALUE - WET_VALUE);
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;
    return percent;
}

float readLightPercent(int &rawOut) {
    const int samples = 10;
    long rawSum = 0;
    analogRead(LDR_PIN);
    delay(5);
    for (int i = 0; i < samples; i++) {
        rawSum += analogRead(LDR_PIN);
        delay(10);
    }
    rawOut = rawSum / samples;
    float percent = 100.0f * (rawOut - DARK_VALUE) / (float)(LIGHT_VALUE - DARK_VALUE);
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;
    return percent;
}

// Función centralizada para leer sensores
void updateSensors() {
    int hRaw, lRaw;
    float t = lm35_read_celsius();
    float hP = readSoilPercent(hRaw);
    float lP = readLightPercent(lRaw);

    if (firstRead) {
        filteredHumidity = hP;
        firstRead = false;
    } else {
        filteredHumidity = 0.8 * filteredHumidity + 0.2 * hP;
    }

    currentTemp     = t;
    currentHumPct   = filteredHumidity;
    currentLightPct = lP;
    currentHumRaw   = hRaw;
    currentLightRaw = lRaw;
}

// Función para guardar registro en el CSV
void saveRecord(String label) {
    updateSensors();
    
    time_t now;
    struct tm timeinfo;
    char fStr[12], hStr[10];
    
    if (getLocalTime(&timeinfo)) {
        strftime(fStr, sizeof(fStr), "%d/%m/%Y", &timeinfo);
        strftime(hStr, sizeof(hStr), "%H:%M:%S", &timeinfo);
    } else {
        strcpy(fStr, "00/00/0000");
        strcpy(hStr, "00:00:00");
    }

    recordCount++;
    csvData += String(fStr) + "," +
               String(hStr) + "," +
               String(currentTemp, 1) + "," +
               String(currentHumRaw)  + "," +
               String(currentLightRaw) + "," +
               label + "\n";

    Serial.printf("[REGISTRO #%d] %s %s | Estado: %s\n", recordCount, fStr, hStr, label.c_str());
}

// ================= HTML DASHBOARD =================
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Maceta Pro - Auto Label</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body { font-family: sans-serif; background: #0f172a; color: #e2e8f0; min-height: 100vh; padding: 20px; }
  .container { max-width: 900px; margin: 0 auto; }
  h1 { text-align: center; color: #38bdf8; margin-bottom: 20px; }
  .cards { display: flex; gap: 12px; flex-wrap: wrap; margin-bottom: 20px; }
  .card { background: #1e293b; border-radius: 12px; padding: 20px; flex: 1; min-width: 150px; text-align: center; border: 1px solid #334155; }
  .card .label { font-size: 0.7rem; color: #94a3b8; text-transform: uppercase; margin-bottom: 5px; }
  .card .value { font-size: 1.8rem; font-weight: 700; }
  .temp .value { color: #f97316; } .hum .value { color: #34d399; } .light .value { color: #fbbf24; }
  
  .section { background: #1e293b; border-radius: 12px; padding: 20px; margin-bottom: 20px; border: 1px solid #334155; }
  .section h2 { font-size: 1rem; color: #94a3b8; margin-bottom: 15px; }
  
  .auto-box { display: flex; flex-direction: column; gap: 15px; }
  .input-group { display: flex; gap: 10px; }
  input { flex: 1; padding: 12px; border-radius: 8px; border: 1px solid #334155; background: #0f172a; color: white; }
  
  .btn { padding: 12px 20px; border-radius: 8px; border: none; font-weight: 600; cursor: pointer; transition: 0.2s; }
  .btn:disabled { opacity: 0.5; cursor: not-allowed; }
  .btn-primary { background: #38bdf8; color: #0f172a; }
  .btn-danger { background: #f87171; color: #0f172a; }
  .btn-success { background: #34d399; color: #0f172a; }
  
  .status-badge { display: inline-block; padding: 4px 12px; border-radius: 20px; font-size: 0.8rem; margin-top: 10px; }
  .active { background: #065f46; color: #34d399; }
  .inactive { background: #450a0a; color: #f87171; }
  
  table { width: 100%; border-collapse: collapse; font-size: 0.8rem; margin-top: 15px; }
  th { text-align: left; padding: 10px; border-bottom: 1px solid #334155; color: #64748b; }
  td { padding: 10px; border-bottom: 1px solid #1e293b; }
</style>
</head>
<body>
<div class="container">
  <h1>🪴 Maceta Automática</h1>

  <div class="cards">
    <div class="card temp"><div class="label">Temp</div><div class="value" id="t">--</div><span>°C</span></div>
    <div class="card hum"><div class="label">Humedad</div><div class="value" id="h">--</div><span>%</span></div>
    <div class="card light"><div class="label">Luz</div><div class="value" id="l">--</div><span>%</span></div>
  </div>

  <div class="section">
    <h2>Etiquetado de Datos Automático (cada 1 min)</h2>
    <div class="auto-box">
      <div class="input-group">
        <input type="text" id="labelInput" placeholder="Ej: Marchita, Saludable, Seca..." />
        <button id="btnStart" class="btn btn-primary" onclick="toggleAuto(true)">Iniciar</button>
        <button id="btnStop" class="btn btn-danger" onclick="toggleAuto(false)">Detener</button>
      </div>
      <div>
        <span>Estado actual: <b id="currentLabel">Ninguno</b></span>
        <div id="badge" class="status-badge inactive">Inactivo</div>
      </div>
    </div>
  </div>

  <div class="section">
    <h2>Historial y Herramientas</h2>
    <div style="display:flex; gap:10px;">
      <button class="btn btn-success" onclick="downloadCSV()">Descargar CSV</button>
      <button class="btn btn-danger" onclick="clearCSV()">Borrar Datos</button>
    </div>
    <div id="count" style="margin-top:10px; font-size:0.8rem; color:#64748b;">Registros: 0</div>
  </div>
</div>

<script>
let isLogging = false;

// Al cargar, sincronizamos la hora de la placa con la del navegador
function syncTime() {
  const now = Math.floor(Date.now() / 1000);
  fetch('/sync?t=' + now);
}

function update() {
  fetch('/status')
    .then(r => r.json())
    .then(d => {
      document.getElementById('t').textContent = d.temp.toFixed(1);
      document.getElementById('h').textContent = d.hum.toFixed(1);
      document.getElementById('l').textContent = d.light.toFixed(1);
      document.getElementById('count').textContent = "Registros en memoria: " + d.count;
      document.getElementById('currentLabel').textContent = d.label;
      
      const badge = document.getElementById('badge');
      if(d.auto) {
        badge.textContent = "Grabando automáticamente...";
        badge.className = "status-badge active";
        document.getElementById('btnStart').disabled = true;
        document.getElementById('labelInput').disabled = true;
      } else {
        badge.textContent = "Pausado";
        badge.className = "status-badge inactive";
        document.getElementById('btnStart').disabled = false;
        document.getElementById('labelInput').disabled = false;
      }
    });
}

function toggleAuto(enable) {
  const label = document.getElementById('labelInput').value.trim();
  if(enable && !label) { alert("Escribe un estado primero"); return; }
  
  fetch(`/config_auto?enable=${enable ? 1:0}&label=${encodeURIComponent(label)}`)
    .then(() => update());
}

function downloadCSV() { window.location.href = '/csv'; }

function clearCSV() {
  if(confirm("¿Borrar todos los registros?")) {
    fetch('/clear').then(() => update());
  }
}

syncTime();
setInterval(update, 3000);
update();
</script>
</body>
</html>
)rawliteral";

// ================= SETUP =================
void setup() {
    Serial.begin(115200);
    
    analogReadResolution(12);
    analogSetPinAttenuation(LM35_PIN, ADC_11db);
    analogSetPinAttenuation(SOIL_PIN, ADC_11db);
    analogSetPinAttenuation(LDR_PIN,  ADC_11db);

    csvData = "fecha,hora,temperatura,humedad_raw,luz_raw,condicion\n";

    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.println("AP: " + String(AP_SSID));
    Serial.println("IP: " + WiFi.softAPIP().toString());

    // --- RUTAS DEL SERVIDOR ---

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send_P(200, "text/html", INDEX_HTML);
    });

    // Sincronizar hora desde el navegador
    server.on("/sync", HTTP_GET, [](AsyncWebServerRequest *req) {
        if (req->hasParam("t")) {
            time_t t = req->getParam("t")->value().toInt();
            struct timeval tv = { .tv_sec = t };
            settimeofday(&tv, NULL);
            Serial.println("Reloj sincronizado con el navegador.");
        }
        req->send(200, "text/plain", "ok");
    });

    // Estado general (JSON)
    server.on("/status", HTTP_GET, [](AsyncWebServerRequest *req) {
        updateSensors();
        String json = "{";
        json += "\"temp\":"   + String(currentTemp, 1) + ",";
        json += "\"hum\":"    + String(currentHumPct, 1) + ",";
        json += "\"light\":"  + String(currentLightPct, 1) + ",";
        json += "\"count\":"  + String(recordCount) + ",";
        json += "\"label\":\"" + globalLabel + "\",";
        json += "\"auto\":"   + String(autoLogEnabled ? "true" : "false");
        json += "}";
        req->send(200, "application/json", json);
    });

    // Configurar el etiquetado automático
    server.on("/config_auto", HTTP_GET, [](AsyncWebServerRequest *req) {
        if (req->hasParam("enable")) {
            autoLogEnabled = req->getParam("enable")->value() == "1";
            if (autoLogEnabled) lastLogMillis = millis() - LOG_INTERVAL; // Forzar primer guardado
        }
        if (req->hasParam("label")) {
            globalLabel = req->getParam("label")->value();
        }
        req->send(200, "text/plain", "ok");
    });

    server.on("/csv", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "text/csv", csvData);
    });

    server.on("/clear", HTTP_GET, [](AsyncWebServerRequest *req) {
        csvData = "fecha,hora,temperatura,humedad_raw,luz_raw,condicion\n";
        recordCount = 0;
        req->send(200, "text/plain", "ok");
    });

    server.begin();
}

// ================= LOOP =================
void loop() {
    // Lógica de guardado automático
    if (autoLogEnabled) {
        unsigned long currentMillis = millis();
        if (currentMillis - lastLogMillis >= LOG_INTERVAL) {
            lastLogMillis = currentMillis;
            saveRecord(globalLabel);
        }
    }
    
    delay(1000); 
}
