#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
// ================= CONFIGURATIONS =================
const char* ssid = "IotServer";
const char* password = "00000000";

#define BOT_TOKEN "8351058008:AAFMmCoWFeA8pXPITouE9gTymo4Q8ohlFO4"
#define CHAT_ID "6013040269"

// --- CHANGE PIN TO 34 (Input Only - Safe from Wi-Fi conflicts) ---
#define SENSOR_PIN 34 

const float FREQ_AER = 18200.0; 
const float FREQ_APA = 14500.0; 
int thresholdValue = 20;        
unsigned long alertCooldown = 60000;
unsigned long lastAlertTime = 0;

String eventLogs[5] = {"--", "--", "--", "--", "--"}; 
int dailyLowCount = 0; 
const float RESISTOR_DIVISOR = 21000.0; 

WiFiClientSecure client;
UniversalTelegramBot bot(BOT_TOKEN, client);
AsyncWebServer server(80);

float currentFrequency = 0;
float currentCapacitance = 0;
int currentMoisture = 0;

// (HTML is unchanged, keeping it short for this snippet)
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Umiditatea Plantei</title>
  <script src="https://code.highcharts.com/highcharts.js"></script>
  <style>
    body { font-family: 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; background-color: #f4f7f6; color: #333; margin: 0; padding: 20px; }
    h2 { text-align: center; color: #555; }
    .container { display: flex; flex-wrap: wrap; justify-content: center; gap: 20px; max-width: 1200px; margin: 0 auto; margin-bottom: 30px; }
    .card-wide { width: 100%; flex: none; padding: 30px; color: white; transition: background 0.5s ease; }
    .state-value { font-size: 4rem; font-weight: 800; letter-spacing: 2px; text-shadow: 2px 2px 4px rgba(0,0,0,0.2); }
    .card { background: #fff; border-radius: 12px; box-shadow: 0 4px 15px rgba(0,0,0,0.05); padding: 20px; text-align: center; flex: 1; min-width: 250px; }
    .card-header { font-size: 1.1rem; color: #888; margin-bottom: 10px; font-weight: 600; text-transform: uppercase; letter-spacing: 1px; }
    .value { font-size: 3.5rem; font-weight: bold; color: #2c3e50; }
    .unit { font-size: 1.2rem; color: #95a5a6; }
    .chart-container { width: 100%; height: 250px; }
    .log-box { width: 100%; max-width: 600px; background: #fff; padding: 20px; border-radius: 12px; box-shadow: 0 4px 15px rgba(0,0,0,0.05); margin-top: 20px; }
    .log-item { border-bottom: 1px solid #eee; padding: 10px 0; display: flex; justify-content: space-between; }
  </style>
</head>
<body>
  <h2>Umiditatea Plantei</h2>
  <div class="container">
    <div id="stateCard" class="card card-wide" style="background: #95a5a6;">
      <div class="card-header" style="color: rgba(255,255,255,0.8);">STATUS GENERAL</div>
      <div id="stateVal" class="state-value">INITIALIZARE...</div>
    </div>
  </div>
  <div class="container">
    <div class="card"><div class="card-header">Nivelul Umiditatii</div><div id="moistVal" class="value">--</div><span class="unit">%</span></div>
    <div class="card"><div class="card-header">Frecventa Senzorului</div><div id="freqVal" class="value" style="font-size: 2.5rem;">--</div><span class="unit">Hz</span></div>
    <div class="card"><div class="card-header">Capacitate</div><div id="capVal" class="value" style="font-size: 2.5rem;">--</div><span class="unit">nF</span></div>
  </div>
  <div class="container">
    <div class="card" style="min-width: 100%;"><div id="chart-freq" class="chart-container"></div></div>
    <div class="card" style="min-width: 100%;"><div id="chart-cap" class="chart-container"></div></div>
  </div>
  <div class="container">
    <div class="log-box"><div class="card-header" style="text-align: left;">Instante umiditate scazuta (Ultimele 5)</div><div id="log-list">Loading logs...</div><p style="text-align: right; color: #888; font-size: 0.8rem;">Numarul total de evenimente astazi: <span id="total-count">0</span></p></div>
  </div>
<script>
  var chartFreq = new Highcharts.Chart({ chart: { renderTo: 'chart-freq', type: 'spline', backgroundColor: 'transparent' }, title: { text: 'Freventa timp real' }, series: [{ name: 'Frequency', data: [], color: '#3498db' }], yAxis: { title: { text: 'Hz' } }, xAxis: { type: 'datetime', tickPixelInterval: 150 }, credits: { enabled: false }, legend: { enabled: false } });
  var chartCap = new Highcharts.Chart({ chart: { renderTo: 'chart-cap', type: 'spline', backgroundColor: 'transparent' }, title: { text: 'Capacitate timp real' }, series: [{ name: 'Capacitance', data: [], color: '#9b59b6' }], yAxis: { title: { text: 'nF' } }, xAxis: { type: 'datetime', tickPixelInterval: 150 }, credits: { enabled: false }, legend: { enabled: false } });
  
  setInterval(function ( ) {
    var xhttp = new XMLHttpRequest();
    xhttp.onreadystatechange = function() {
      var data = JSON.parse(this.responseText);
        var moist = data.moisture;

        // --- NEW: LOGIC FOR THE TOP STATE CARD ---
        var stateCard = document.getElementById("stateCard");
        var stateVal = document.getElementById("stateVal");
        
        if (moist < 20) {
            stateCard.style.background = "#e74c3c"; // Red (Dry)
            stateVal.innerHTML = "USCAT!";
        } else if (moist >= 20 && moist < 40) {
            stateCard.style.background = "#f39c12"; // Orange (Moist)
            stateVal.innerHTML = "UMED";
        } else if (moist >= 40 && moist < 75) {
            stateCard.style.background = "#27ae60"; // Green (Normal)
            stateVal.innerHTML = "OPTIM (NORMAL)";
        } else {
            stateCard.style.background = "#2980b9"; // Blue (Too Wet)
            stateVal.innerHTML = "PREA UD!";
        }

      if (this.readyState == 4 && this.status == 200) {
        var data = JSON.parse(this.responseText);
        document.getElementById("moistVal").innerHTML = data.moisture;
        document.getElementById("freqVal").innerHTML = data.frequency;
        document.getElementById("capVal").innerHTML = data.capacitance;
        document.getElementById("total-count").innerHTML = data.dailyCount;
        var mVal = document.getElementById("moistVal");
        if(data.moisture < 20) { mVal.style.color = "#c0392b"; } else { mVal.style.color = "#27ae60"; }
        var x = (new Date()).getTime();
        if(chartFreq.series[0].data.length > 40) { chartFreq.series[0].addPoint([x, parseFloat(data.frequency)], true, true); } else { chartFreq.series[0].addPoint([x, parseFloat(data.frequency)], true, false); }
        if(chartCap.series[0].data.length > 40) { chartCap.series[0].addPoint([x, parseFloat(data.capacitance)], true, true); } else { chartCap.series[0].addPoint([x, parseFloat(data.capacitance)], true, false); }
        var logHtml = "";
        for (var i = 0; i < data.logs.length; i++) { logHtml += '<div class="log-item"><span>Eveniment Critic</span><span class="log-time">' + data.logs[i] + '</span></div>'; }
        document.getElementById("log-list").innerHTML = logHtml;
      }
    };
    xhttp.open("GET", "/data", true);
    xhttp.send();
  }, 2000);
</script>
</body></html>
)rawliteral";

String getUptime() {
  unsigned long millisec = millis();
  unsigned long seconds = millisec / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  return String(hours) + "h " + String(minutes % 60) + "m " + String(seconds % 60) + "s";
}

void setup() {
  Serial.begin(115200);
  pinMode(2, OUTPUT); 
  pinMode(SENSOR_PIN, INPUT); // Standard Input

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
   if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi pierdut! Se reconecteaza...");
    WiFi.disconnect();
    WiFi.reconnect();

    
    // Wait until connected again to avoid freezing
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print("WiFi neconectat.");
    }
    Serial.println("\nReconectat!");
  }

  
  digitalWrite(2, HIGH); 
  Serial.println("\nConectat! Adresa IP: ");
  Serial.println(WiFi.localIP());
  if(MDNS.begin("planta")) {
      Serial.println("MDNS raspunde");
      Serial.println("Acces la: http://planta.local");
    }

  client.setInsecure(); 

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){ request->send_P(200, "text/html", index_html); });
  server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request){
    String json = "{";
    json += "\"moisture\":" + String(currentMoisture) + ",";
    json += "\"frequency\":" + String(currentFrequency) + ",";
    json += "\"capacitance\":" + String(currentCapacitance, 3) + ",";
    json += "\"dailyCount\":" + String(dailyLowCount) + ",";
    json += "\"logs\":[";
    for(int i=0; i<5; i++){ json += "\"" + eventLogs[i] + "\""; if(i < 4) json += ","; }
    json += "]}";
    request->send(200, "application/json", json);
  });

  server.begin();
}

void loop() {
  unsigned long durationHigh = 0;
  unsigned long durationLow = 0;

  // Let Wi-Fi handle its business before we block for reading
  yield(); 

  // Read Pulse
  // Note: We removed the 'critical' block because it can crash some boards if used wrong.
  // Pin 34 is safer and doesn't usually need it.
  durationHigh = pulseIn(SENSOR_PIN, HIGH, 60000); // 60ms timeout
  durationLow = pulseIn(SENSOR_PIN, LOW, 60000);
  
  unsigned long totalPeriod = durationHigh + durationLow;

  if (totalPeriod > 0) {
    currentFrequency = 1000000.0 / totalPeriod;

    // Sanity Check: If frequency is wildly wrong (>30k or <1k), ignore it
    if(currentFrequency > 30000 || currentFrequency < 1000) {
       // Do nothing, keep old value
    } else {
       // Valid Reading
       int rawProcent = map((long)currentFrequency, (long)FREQ_AER, (long)FREQ_APA, 0, 100);
       currentMoisture = constrain(rawProcent, 0, 100);
       
       if(currentFrequency > 0) {
         currentCapacitance = (1.44 / (RESISTOR_DIVISOR * currentFrequency)) * 1000000000; 
       }
    }
  } else {
    // Timeout
    Serial.println("Error: Sensor Timeout");
  }

  // Alert Logic
  if (currentMoisture < thresholdValue && currentFrequency > 1000) { 
    if (millis() - lastAlertTime > alertCooldown) {
      String message = "PLANT ALERT! \nMoisture: " + String(currentMoisture) + "% (CRITICAL)\nFreq: " + String(currentFrequency) + "Hz";
      if(bot.sendMessage(CHAT_ID, message, "")) {
        Serial.println("Telegram Alert Sent!");
      }
      dailyLowCount++;
      for(int i=4; i>0; i--) { eventLogs[i] = eventLogs[i-1]; }
      eventLogs[0] = "Uptime: " + getUptime() + " (Val: " + String(currentMoisture) + "%)";
      lastAlertTime = millis();
    }
  }

  Serial.print("Freq: "); Serial.print(currentFrequency);
  Serial.print(" | Moist: "); Serial.println(currentMoisture);

  delay(1000);
}