// ============================================================
// Maceta Inteligente — Tarea 2 IA
// Web server + clasificación automática con Árbol de Decisión
//
// Modelo: DecisionTreeClassifier entrenado en el notebook
//   max_depth=3, criterion=gini, min_samples_leaf=2
//   Dataset: 191 muestras reales (abril 2026)
//   Clases: 0=Debil | 1=Marchita | 2=Saludable
// ============================================================

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

// ─── Pines (mismos que el firmware original) ──────────────
#define LM35_PIN 34
#define SOIL_PIN 35
#define LDR_PIN  32
#define BUZZER_PIN 25

// ─── Tiempos y Control ────────────────────────────────────
unsigned long lastMeasureTime = 0;
const int measureInterval = 3000; // Medir cada 3 segundos
unsigned long lastBuzzerMillis = 0;
bool buzzerState = false;

// ─── Calibración (deben coincidir con el notebook) ────────
const int DRY_VALUE   = 3000;
const int WET_VALUE   = 1225;
const int DARK_VALUE  = 200;
const int LIGHT_VALUE = 4095;

// ─── WiFi AP ──────────────────────────────────────────────
const char* AP_SSID = "MacetaInteligente";
const char* AP_PASS = "";

// ─── Servidor ─────────────────────────────────────────────
AsyncWebServer server(80);

// ─── CSV en RAM ───────────────────────────────────────────
String csvData  = "";
int recordCount = 0;

// ─── Estado global de sensores ────────────────────────────
float filteredHumidity  = 0.0;
bool  firstRead         = true;
float currentTemp       = 0;
float currentHumPct     = 0;
float currentLightPct   = 0;
int   currentHumRaw     = 0;
int   currentLightRaw   = 0;
String currentCondicion = "Desconocido";

// ════════════════════════════════════════════════════════════
// ÁRBOL DE DECISIÓN — exportado desde el notebook de la Tarea 2
//
// Entradas:
//   temperatura → °C directos del LM35 (rango dataset: 18–55 °C)
//   humedad     → % suelo convertida desde ADC (0–100)
//   luz         → % luz convertida desde ADC (0–100)
//
// Retorna: 0=Debil | 1=Marchita | 2=Saludable
// ════════════════════════════════════════════════════════════
int clasificarPlanta(float temperatura, float humedad, float luz) {
    if (humedad <= 28.8500f) {
        if (luz <= 81.6000f) {
            return 1;  // Marchita
        } else {
            return 0;  // Debil
        }
    } else {
        if (luz <= 87.8500f) {
            if (temperatura <= 23.6000f) {
                return 0;  // Debil
            } else {
                return 2;  // Saludable
            }
        } else {
            if (humedad <= 71.9000f) {
                return 0;  // Debil
            } else {
                return 2;  // Saludable
            }
        }
    }
    return -1;
}

String claseANombre(int clase) {
    switch (clase) {
        case 0: return "Debil";
        case 1: return "Marchita";
        case 2: return "Saludable";
        default: return "Desconocido";
    }
}

// ─── LM35 — promedio de mV dividido por 10 → °C ──────────
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

// ─── Humedad del suelo — ADC promediado → % ──────────────
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
    float percent = 100.0f * (DRY_VALUE - rawOut) / (float)(DRY_VALUE - WET_VALUE);
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;
    return percent;
}

// ─── LDR — ADC promediado → % ────────────────────────────
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

// ─── Medición y Clasificación ─────────────────────────────
void performMeasurement() {
    int humRaw   = 0;
    int lightRaw = 0;

    float temp     = lm35_read_celsius();
    float humPct   = readSoilPercent(humRaw);
    float lightPct = readLightPercent(lightRaw);

    if (firstRead) {
        filteredHumidity = humPct;
        firstRead = false;
    } else {
        filteredHumidity = 0.8f * filteredHumidity + 0.2f * humPct;
    }

    currentTemp      = temp;
    currentHumPct    = filteredHumidity;
    currentLightPct  = lightPct;
    currentHumRaw    = humRaw;
    currentLightRaw  = lightRaw;

    // Clasificación con el árbol entrenado en el notebook
    int claseIdx = clasificarPlanta(currentTemp, currentHumPct, currentLightPct);
    currentCondicion = claseANombre(claseIdx);

    Serial.printf("[AI] Temp=%.1f°C  Hum=%.1f%%  Luz=%.1f%%  → %s\n",
                  currentTemp, currentHumPct, currentLightPct, currentCondicion.c_str());
}

// ─── HTML del dashboard ───────────────────────────────────
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Maceta Inteligente — IA</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body { font-family: sans-serif; background: #0f172a; color: #e2e8f0; min-height: 100vh; padding: 24px; }
  h1   { text-align: center; font-size: 1.6rem; margin-bottom: 8px; color: #38bdf8; }
  .subtitle { text-align: center; font-size: 0.85rem; color: #64748b; margin-bottom: 28px; }

  /* ── Tarjetas de sensores ── */
  .cards { display: flex; gap: 16px; flex-wrap: wrap; justify-content: center; margin-bottom: 28px; }
  .card { background: #1e293b; border-radius: 16px; padding: 24px; min-width: 160px; flex: 1; max-width: 220px; text-align: center; border: 1px solid #334155; }
  .card .icon  { font-size: 2rem; margin-bottom: 8px; }
  .card .label { font-size: 0.75rem; color: #64748b; text-transform: uppercase; letter-spacing: 0.05em; margin-bottom: 4px; }
  .card .value { font-size: 2rem; font-weight: 700; }
  .card .unit  { font-size: 0.85rem; color: #94a3b8; }
  .temp  .value { color: #f97316; }
  .hum   .value { color: #34d399; }
  .light .value { color: #fbbf24; }

  /* ── Tarjeta de estado IA ── */
  .estado-card { background: #1e293b; border-radius: 16px; padding: 24px 20px; min-width: 180px; flex: 1; max-width: 240px; text-align: center; border: 2px solid #334155; transition: border-color 0.5s, background 0.5s; }
  .estado-card .emoji     { font-size: 3.2rem; margin-bottom: 10px; line-height: 1; transition: opacity 0.3s; }
  .estado-card .label     { font-size: 0.75rem; color: #64748b; text-transform: uppercase; letter-spacing: 0.05em; margin-bottom: 6px; }
  .estado-card .condicion { font-size: 1.3rem; font-weight: 700; transition: color 0.5s; }
  .estado-card.saludable  { border-color: #34d399; background: #0d2b1f; }
  .estado-card.saludable .condicion { color: #34d399; }
  .estado-card.debil      { border-color: #fbbf24; background: #2b1f05; }
  .estado-card.debil      .condicion { color: #fbbf24; }
  .estado-card.marchita   { border-color: #f87171; background: #2b0a0a; }
  .estado-card.marchita   .condicion { color: #f87171; }

  /* ── Secciones ── */
  .section { background: #1e293b; border-radius: 16px; padding: 24px; margin-bottom: 20px; border: 1px solid #334155; max-width: 820px; margin-left: auto; margin-right: auto; }
  .section h2 { font-size: 1rem; color: #94a3b8; margin-bottom: 16px; }
  .btn-row { display: flex; gap: 12px; flex-wrap: wrap; }
  button { padding: 12px 24px; border-radius: 10px; border: none; font-size: 0.95rem; cursor: pointer; font-weight: 600; transition: opacity 0.2s; }
  button:hover { opacity: 0.85; }
  .btn-save     { background: #38bdf8; color: #0f172a; }
  .btn-download { background: #34d399; color: #0f172a; }
  .btn-clear    { background: #f87171; color: #0f172a; }
  .status { margin-top: 12px; font-size: 0.85rem; color: #34d399; min-height: 20px; }

  /* ── Tabla ── */
  table { width: 100%; border-collapse: collapse; font-size: 0.85rem; }
  th { text-align: left; padding: 10px 12px; color: #64748b; border-bottom: 1px solid #334155; font-weight: 500; }
  td { padding: 10px 12px; border-bottom: 1px solid #1e293b; }
  tr:last-child td { border-bottom: none; }
  tr:hover td { background: #0f172a; }
  .no-records { text-align: center; color: #475569; padding: 24px; }
  .count { font-size: 0.8rem; color: #475569; margin-top: 8px; }

  /* ── Badges de condición ── */
  .badge { display: inline-block; padding: 3px 10px; border-radius: 99px; font-size: 0.78rem; font-weight: 600; }
  .badge-saludable { background: #14532d; color: #34d399; }
  .badge-debil     { background: #451a03; color: #fbbf24; }
  .badge-marchita  { background: #450a0a; color: #f87171; }
</style>
</head>
<body>

<h1>🪴 Maceta Inteligente — IA</h1>
<p class="subtitle">Clasificación automática con Árbol de Decisión entrenado en el notebook</p>

<div class="cards">
  <div class="card temp">
    <div class="icon">🌡️</div>
    <div class="label">Temperatura</div>
    <div class="value" id="temp">--</div>
    <div class="unit">°C</div>
  </div>
  <div class="card hum">
    <div class="icon">🌱</div>
    <div class="label">Humedad suelo</div>
    <div class="value" id="hum">--</div>
    <div class="unit">%</div>
  </div>
  <div class="card light">
    <div class="icon">☀️</div>
    <div class="label">Luz</div>
    <div class="value" id="light">--</div>
    <div class="unit">%</div>
  </div>
  <div class="estado-card" id="estado-card">
    <div class="emoji" id="emoji">🌿</div>
    <div class="label">Estado IA</div>
    <div class="condicion" id="condicion">--</div>
  </div>
</div>

<div class="section">
  <h2>Registros de la sesión</h2>
  <div class="btn-row">
    <button class="btn-save"     onclick="saveRecord()">💾 Guardar lectura</button>
    <button class="btn-download" onclick="downloadCSV()">⬇️ Descargar CSV</button>
    <button class="btn-clear"    onclick="clearRecords()">🗑️ Limpiar</button>
  </div>
  <div class="status" id="status"></div>
  <div id="table-container" style="margin-top:16px">
    <p class="no-records">Aún no hay registros guardados.</p>
  </div>
  <p class="count" id="count"></p>
</div>

<script>
let records = [];

const EMOJIS = { 'Saludable': '😊', 'Debil': '😐', 'Marchita': '💀' };
const CSS    = { 'Saludable': 'saludable', 'Debil': 'debil', 'Marchita': 'marchita' };

function updateSensors() {
  fetch('/sensors')
    .then(r => r.json())
    .then(d => {
      document.getElementById('temp').textContent  = d.temp.toFixed(1);
      document.getElementById('hum').textContent   = d.hum_pct.toFixed(1);
      document.getElementById('light').textContent = d.light_pct.toFixed(1);

      document.getElementById('emoji').textContent     = EMOJIS[d.condicion] || '❓';
      document.getElementById('condicion').textContent = d.condicion;
      document.getElementById('estado-card').className = 'estado-card ' + (CSS[d.condicion] || '');
    })
    .catch(() => {});
}

function saveRecord() {
  const now   = new Date();
  const fecha = now.toLocaleDateString('es-UY');
  const hora  = now.toLocaleTimeString('es-UY');
  fetch('/save?fecha=' + encodeURIComponent(fecha) + '&hora=' + encodeURIComponent(hora))
    .then(r => r.json())
    .then(d => {
      records.push(d);
      document.getElementById('status').style.color = '#34d399';
      document.getElementById('status').textContent = '✅ Lectura guardada — Estado: ' + d.condicion;
      renderTable();
      setTimeout(() => document.getElementById('status').textContent = '', 3500);
    })
    .catch(() => {
      document.getElementById('status').style.color = '#f87171';
      document.getElementById('status').textContent = 'Error al guardar.';
    });
}

function badgeHTML(cond) {
  const cls = CSS[cond] ? 'badge-' + CSS[cond] : '';
  return '<span class="badge ' + cls + '">' + (EMOJIS[cond] || '') + ' ' + cond + '</span>';
}

function renderTable() {
  if (records.length === 0) {
    document.getElementById('table-container').innerHTML = '<p class="no-records">Aún no hay registros guardados.</p>';
    document.getElementById('count').textContent = '';
    return;
  }
  let html = '<table><thead><tr>' +
    '<th>#</th><th>Fecha</th><th>Hora</th>' +
    '<th>Temp (°C)</th><th>Humedad (%)</th><th>Luz (%)</th><th>Estado IA</th>' +
    '</tr></thead><tbody>';
  records.forEach((r, i) => {
    html += '<tr>' +
      '<td>' + (i + 1)             + '</td>' +
      '<td>' + r.fecha             + '</td>' +
      '<td>' + r.hora              + '</td>' +
      '<td>' + r.temp              + '</td>' +
      '<td>' + r.hum_pct           + '</td>' +
      '<td>' + r.light_pct         + '</td>' +
      '<td>' + badgeHTML(r.condicion) + '</td>' +
      '</tr>';
  });
  html += '</tbody></table>';
  document.getElementById('table-container').innerHTML = html;
  document.getElementById('count').textContent = records.length + ' registro(s) en esta sesión.';
}

function downloadCSV() {
  fetch('/csv')
    .then(r => r.text())
    .then(text => {
      if (!text || text === 'vacio') { alert('No hay registros para descargar.'); return; }
      const blob = new Blob([text], { type: 'text/csv' });
      const url  = URL.createObjectURL(blob);
      const a    = document.createElement('a');
      a.href     = url;
      a.download = 'maceta_IA_' + new Date().toISOString().slice(0, 10) + '.csv';
      a.click();
      URL.revokeObjectURL(url);
    });
}

function clearRecords() {
  if (!confirm('¿Seguro que querés limpiar todos los registros?')) return;
  fetch('/clear').then(() => {
    records = [];
    renderTable();
    document.getElementById('status').style.color = '#f87171';
    document.getElementById('status').textContent = 'Registros eliminados.';
    setTimeout(() => document.getElementById('status').textContent = '', 3000);
  });
}

updateSensors();
setInterval(updateSensors, 3000);
</script>
</body>
</html>
)rawliteral";

// ─── Setup ────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(1000);

    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

    analogReadResolution(12);
    analogSetPinAttenuation(LM35_PIN, ADC_11db);
    analogSetPinAttenuation(SOIL_PIN, ADC_11db);
    analogSetPinAttenuation(LDR_PIN,  ADC_11db);

    csvData = "fecha,hora,temperatura,humedad,luz,condicion\n";

    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.print("AP iniciado. IP: ");
    Serial.println(WiFi.softAPIP());

    // ── Página principal ──────────────────────────────────
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send_P(200, "text/html", INDEX_HTML);
    });

    // ── Sensores + predicción → JSON ──────────────────────
    server.on("/sensors", HTTP_GET, [](AsyncWebServerRequest *req) {
        String json = "{\"temp\":"        + String(currentTemp, 1)      +
                      ",\"hum_pct\":"     + String(currentHumPct, 1)    +
                      ",\"light_pct\":"   + String(currentLightPct, 1)  +
                      ",\"condicion\":\"" + currentCondicion             + "\"}";
        req->send(200, "application/json", json);
    });

    // ── Guardar lectura con predicción automática ─────────
    server.on("/save", HTTP_GET, [](AsyncWebServerRequest *req) {
        String fecha = "sin fecha";
        String hora  = "sin hora";
        if (req->hasParam("fecha")) fecha = req->getParam("fecha")->value();
        if (req->hasParam("hora"))  hora  = req->getParam("hora")->value();

        recordCount++;

        // CSV guarda valores raw (mismo formato que el dataset de entrenamiento)
        csvData += fecha + "," +
                   hora  + "," +
                   String(currentTemp, 1)  + "," +
                   String(currentHumRaw)   + "," +
                   String(currentLightRaw) + "," +
                   currentCondicion        + "\n";

        // JSON devuelve porcentajes para la tabla del dashboard
        String json = "{\"fecha\":\""    + fecha           + "\"," +
                      "\"hora\":\""      + hora            + "\"," +
                      "\"temp\":"        + String(currentTemp, 1)     + "," +
                      "\"hum_pct\":"     + String(currentHumPct, 1)   + "," +
                      "\"light_pct\":"   + String(currentLightPct, 1) + "," +
                      "\"condicion\":\"" + currentCondicion            + "\"}";
        req->send(200, "application/json", json);

        Serial.printf("[SAVE] %s %s | hum_raw:%d luz_raw:%d | %s\n",
                      fecha.c_str(), hora.c_str(),
                      currentHumRaw, currentLightRaw, currentCondicion.c_str());
    });

    // ── Descargar CSV ─────────────────────────────────────
    server.on("/csv", HTTP_GET, [](AsyncWebServerRequest *req) {
        if (recordCount == 0) { req->send(200, "text/plain", "vacio"); return; }
        req->send(200, "text/csv", csvData);
    });

    // ── Limpiar registros ─────────────────────────────────
    server.on("/clear", HTTP_GET, [](AsyncWebServerRequest *req) {
        csvData = "fecha,hora,temperatura,humedad,luz,condicion\n";
        recordCount = 0;
        req->send(200, "text/plain", "ok");
        Serial.println("[CLEAR] Registros eliminados.");
    });

    server.begin();
    Serial.println("Servidor iniciado.");
    Serial.println("Conectate a WiFi: MacetaInteligente");
    Serial.println("Luego abrí: http://192.168.4.1");
}

// ─── Loop ─────────────────────────────────────────────────
void loop() {
    unsigned long currentMillis = millis();

    // Medición periódica
    if (currentMillis - lastMeasureTime >= measureInterval) {
        lastMeasureTime = currentMillis;
        performMeasurement();
    }

    // Lógica del Buzzer (No bloqueante)
    if (currentCondicion == "Marchita") {
        // Beep rápido (100ms ON, 100ms OFF)
        if (currentMillis - lastBuzzerMillis >= 100) {
            lastBuzzerMillis = currentMillis;
            buzzerState = !buzzerState;
            digitalWrite(BUZZER_PIN, buzzerState ? HIGH : LOW);
        }
    } else if (currentCondicion == "Debil") {
        // Beep lento (500ms ON, 500ms OFF)
        if (currentMillis - lastBuzzerMillis >= 500) {
            lastBuzzerMillis = currentMillis;
            buzzerState = !buzzerState;
            digitalWrite(BUZZER_PIN, buzzerState ? HIGH : LOW);
        }
    } else {
        // Saludable o Desconocido: Apagar
        digitalWrite(BUZZER_PIN, LOW);
        buzzerState = false;
    }

    delay(10); // Pequeña pausa para estabilidad
}
