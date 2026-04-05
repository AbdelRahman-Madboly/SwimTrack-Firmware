/**
 * @file    wifi_server.cpp
 * @brief   WiFi SoftAP init, WebServer setup, route handlers, and
 *          embedded single-page web dashboard (Prompt 7).
 *
 *          The dashboard is a C++ raw-string literal (DASHBOARD_HTML)
 *          served at GET /.  It is ~7.8 KB of minified HTML/CSS/JS that
 *          runs entirely on the client and communicates with the device
 *          via the existing REST API endpoints.
 *
 *          Dashboard features:
 *            - Status bar: Mode badge (IDLE/RECORDING), Battery %, WiFi count
 *            - Live panel: Strokes, Laps, SPM, SWOLF estimate, Lap time,
 *              Resting indicator — polls /api/live every 500 ms when active
 *            - IMU panel: ax ay az / gx gy gz
 *            - Control: Pool length input, START (green) / STOP (red) buttons
 *            - History: table of sessions; tap row for lap-by-lap breakdown
 *
 *          Polling schedule:
 *            - /api/status  : every 2 s (always)
 *            - /api/live    : every 500 ms ONLY while session_active == true
 *
 *          PROGMEM note:
 *            DASHBOARD_HTML is placed in flash via PROGMEM and served with
 *            send_P() to avoid copying 8 KB into SRAM on each request.
 *
 * @author  SwimTrack Firmware Team
 * @date    2025-01-01
 */

#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "wifi_server.h"

// ============================================================
//  EMBEDDED WEB DASHBOARD  (7.78 KB minified)
// ============================================================

static const char DASHBOARD_HTML[] PROGMEM = R"rawhtml(<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>SwimTrack</title><style>
*{box-sizing:border-box;margin:0;padding:0}
body{background:#1a1a2e;color:#fff;font-family:sans-serif;max-width:480px;margin:0 auto;padding:8px}
h2{color:#0066cc;font-size:.85rem;margin:8px 0 4px;text-transform:uppercase;letter-spacing:1px}
.card{background:#16213e;border-radius:8px;padding:10px;margin-bottom:8px}
.sb{display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:4px}
.badge{padding:3px 8px;border-radius:12px;font-size:.75rem;font-weight:bold}
.bi{background:#333;color:#aaa}.br{background:#cc0000;color:#fff;animation:bk 1s infinite}
@keyframes bk{0%,100%{opacity:1}50%{opacity:.5}}
.g2{display:grid;grid-template-columns:1fr 1fr;gap:6px}
.g3{display:grid;grid-template-columns:1fr 1fr 1fr;gap:6px}
.m{background:#0d1117;border-radius:6px;padding:8px;text-align:center}
.m .v{font-size:1.6rem;font-weight:bold;color:#4fc3f7}
.m .l{font-size:.65rem;color:#888;margin-top:2px}
.ig{display:grid;grid-template-columns:repeat(3,1fr);gap:4px}
.iv{background:#0d1117;border-radius:4px;padding:4px;text-align:center;font-size:.8rem}
.iv span{display:block;font-size:.6rem;color:#666}
input[type=number]{background:#0d1117;border:1px solid #0066cc;color:#fff;padding:6px 8px;border-radius:4px;width:70px;font-size:.9rem}
.btn{display:block;width:100%;padding:12px;border:none;border-radius:6px;font-size:1rem;font-weight:bold;cursor:pointer;min-height:48px;margin-top:6px}
.bs{background:#00aa44;color:#fff}.bs:disabled{background:#1a4a2e;color:#555;cursor:not-allowed}
.bx{background:#cc0000;color:#fff}.bx:disabled{background:#4a1a1a;color:#555;cursor:not-allowed}
.bh{background:#0066cc;color:#fff}
.ry{color:#ff6b6b;font-weight:bold}.rn{color:#69db7c}
table{width:100%;border-collapse:collapse;font-size:.78rem}
th{background:#0066cc;padding:5px 4px;text-align:left}
td{padding:4px;border-bottom:1px solid #1e2a3a;cursor:pointer}
tr:hover td{background:#1e2a3a}
.hidden{display:none}.msg{color:#aaa;font-size:.8rem;padding:4px 0}
</style></head><body>
<div class="card"><div class="sb">
  <strong style="color:#0066cc;font-size:1.1rem">SwimTrack</strong>
  <span id="md" class="badge bi">IDLE</span>
  <span id="bt" style="font-size:.8rem">Batt: ---%</span>
  <span id="wf" style="font-size:.8rem;color:#888">WiFi: 0</span>
</div></div>
<div class="card"><h2>Live</h2>
  <div class="g3">
    <div class="m"><div class="v" id="st">0</div><div class="l">Strokes</div></div>
    <div class="m"><div class="v" id="lp">0</div><div class="l">Laps</div></div>
    <div class="m"><div class="v" id="sp">0.0</div><div class="l">SPM</div></div>
  </div>
  <div class="g2" style="margin-top:6px">
    <div class="m"><div class="v" id="sw">--</div><div class="l">SWOLF est.</div></div>
    <div class="m"><div class="v" id="el">0s</div><div class="l">Lap Time</div></div>
  </div>
  <div style="margin-top:6px;text-align:center;font-size:.85rem">Resting: <span id="rs" class="rn">NO</span></div>
</div>
<div class="card"><h2>IMU</h2>
  <div class="ig">
    <div class="iv"><span>ax</span><span id="ax">0.000</span></div>
    <div class="iv"><span>ay</span><span id="ay">0.000</span></div>
    <div class="iv"><span>az</span><span id="az">0.000</span></div>
    <div class="iv"><span>gx</span><span id="gx">0.00</span></div>
    <div class="iv"><span>gy</span><span id="gy">0.00</span></div>
    <div class="iv"><span>gz</span><span id="gz">0.00</span></div>
  </div>
</div>
<div class="card"><h2>Control</h2>
  <div style="display:flex;align-items:center;gap:8px;margin-bottom:4px">
    <label style="font-size:.85rem">Pool:</label>
    <input type="number" id="pl" value="25" min="10" max="100">
    <span style="font-size:.8rem;color:#888">m</span>
  </div>
  <button id="btnS" class="btn bs" onclick="startSess()">&#9654; START SESSION</button>
  <button id="btnX" class="btn bx hidden" onclick="stopSess()">&#9632; STOP &amp; SAVE</button>
  <div id="cm" class="msg"></div>
</div>
<div class="card"><h2>History</h2>
  <button class="btn bh" onclick="loadHist()" style="margin-bottom:6px">Refresh Sessions</button>
  <div id="ht"></div><div id="ld" class="hidden"></div>
</div>
<script>
var active=false,liveT=null;
function G(i){return document.getElementById(i)}
function T(i,v){var e=G(i);if(e)e.textContent=v}
function fetchSt(){
  fetch('/api/status').then(function(r){return r.json();}).then(function(d){
    active=d.session_active;
    var b=G('md');b.textContent=d.mode;b.className='badge '+(active?'br':'bi');
    T('bt','Batt: '+d.battery_pct+'%');T('wf','WiFi: '+d.wifi_clients);
    G('btnS').disabled=active;
    G('btnX').classList.toggle('hidden',!active);
    G('btnS').classList.toggle('hidden',active);
    if(active&&!liveT)liveT=setInterval(fetchLv,500);
    if(!active&&liveT){clearInterval(liveT);liveT=null;}
  }).catch(function(){});
}
function fetchLv(){
  fetch('/api/live').then(function(r){return r.json();}).then(function(d){
    T('st',d.strokes||0);T('lp',d.laps||0);
    T('sp',(d.rate_spm||0).toFixed(1));
    T('sw',d.swolf_est?parseFloat(d.swolf_est).toFixed(1):'--');
    T('el',(d.lap_elapsed_s||0).toFixed(0)+'s');
    var r=G('rs');r.textContent=d.resting?'YES':'NO';r.className=d.resting?'ry':'rn';
    T('ax',parseFloat(d.ax||0).toFixed(3));T('ay',parseFloat(d.ay||0).toFixed(3));
    T('az',parseFloat(d.az||0).toFixed(3));T('gx',parseFloat(d.gx||0).toFixed(2));
    T('gy',parseFloat(d.gy||0).toFixed(2));T('gz',parseFloat(d.gz||0).toFixed(2));
  }).catch(function(){});
}
function startSess(){
  var p=parseInt(G('pl').value)||25;T('cm','Starting...');
  fetch('/api/session/start',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({pool_length_m:p})})
  .then(function(r){return r.json();}).then(function(d){
    T('cm',d.ok?'Started! ID:'+d.id:'Err:'+JSON.stringify(d));
    fetchSt();if(!liveT)liveT=setInterval(fetchLv,500);
  }).catch(function(e){T('cm','Err:'+e);});
}
function stopSess(){
  T('cm','Saving...');
  fetch('/api/session/stop',{method:'POST'}).then(function(r){return r.json();}).then(function(d){
    T('cm',d.ok?'Saved! ID:'+d.saved_id:'Err:'+JSON.stringify(d));
    fetchSt();if(liveT){clearInterval(liveT);liveT=null;}
  }).catch(function(e){T('cm','Err:'+e);});
}
function loadHist(){
  G('ht').innerHTML='<div class="msg">Loading...</div>';G('ld').classList.add('hidden');
  fetch('/api/sessions').then(function(r){return r.json();}).then(function(ss){
    if(!ss.length){G('ht').innerHTML='<div class="msg">No sessions.</div>';return;}
    var h='<table><tr><th>ID</th><th>Laps</th><th>Dist</th><th>SWOLF</th><th>Time</th></tr>';
    ss.forEach(function(s){
      h+='<tr onclick="showLaps('+s.id+')"><td>'+s.id+'</td><td>'+s.laps+'</td>'
        +'<td>'+(s.total_dist_m||0)+'m</td><td>'+(parseFloat(s.avg_swolf)||0).toFixed(1)+'</td>'
        +'<td>'+(parseFloat(s.duration_s)||0).toFixed(0)+'s</td></tr>';
    });
    G('ht').innerHTML=h+'</table><div class="msg" style="color:#555">Tap row for laps</div>';
  }).catch(function(){G('ht').innerHTML='<div class="msg">Failed.</div>';});
}
function showLaps(id){
  var ld=G('ld');ld.innerHTML='<div class="msg">Loading...</div>';ld.classList.remove('hidden');
  fetch('/api/sessions/'+id).then(function(r){return r.json();}).then(function(s){
    var h='<h2 style="margin-top:8px">Session '+id+'</h2>'
      +'<table><tr><th>#</th><th>Time</th><th>Str</th><th>SWOLF</th><th>SPM</th></tr>';
    (s.lap_data||[]).forEach(function(l){
      h+='<tr><td>'+l.n+'</td><td>'+parseFloat(l.t_s).toFixed(1)+'s</td>'
        +'<td>'+l.strokes+'</td><td>'+parseFloat(l.swolf).toFixed(1)+'</td>'
        +'<td>'+parseFloat(l.spm).toFixed(1)+'</td></tr>';
    });
    ld.innerHTML=h+'</table>';
  }).catch(function(){ld.innerHTML='<div class="msg">Failed.</div>';});
}
fetchSt();setInterval(fetchSt,2000);fetchLv();
</script></body></html>)rawhtml";

// ============================================================
//  MODULE-PRIVATE STATE
// ============================================================

static WebServer       s_server(80);
static SessionManager* s_sm     = nullptr;
static StrokeDetector* s_sd     = nullptr;
static LapCounter*     s_lc     = nullptr;
static IMUSample*      s_sample = nullptr;
static uint32_t        s_bootMs = 0;

// ============================================================
//  INTERNAL HELPER
// ============================================================

static void sendJson(int code, const String& json)
{
    s_server.sendHeader("Access-Control-Allow-Origin",  "*");
    s_server.sendHeader("Access-Control-Allow-Methods", "GET,POST,DELETE,OPTIONS");
    s_server.send(code, "application/json", json);
}

// ============================================================
//  ROUTE HANDLER — GET /   (web dashboard)
// ============================================================

/**
 * @brief Serve the embedded dashboard HTML from PROGMEM.
 *
 *        send_P() streams directly from flash without copying the
 *        8 KB string into SRAM, which is important on a device with
 *        only 320 KB total SRAM.
 */
static void handleRoot()
{
    s_server.sendHeader("Access-Control-Allow-Origin", "*");
    s_server.send_P(200, "text/html", DASHBOARD_HTML);
}

// ============================================================
//  ROUTE HANDLER — GET /api/status
// ============================================================

static void handleStatus()
{
    JsonDocument doc;
    doc["mode"]           = s_sm->isActive() ? "RECORDING" : "IDLE";
    doc["session_active"] = s_sm->isActive();
    doc["wifi_clients"]   = (int)WiFi.softAPgetStationNum();
    doc["uptime_s"]       = (millis() - s_bootMs) / 1000UL;
    doc["battery_pct"]    = 100;     // Stub — replaced in Prompt 8
    doc["battery_v"]      = 4.20f;  // Stub — replaced in Prompt 8
    doc["pool_m"]         = poolLengthM;
    doc["free_heap"]      = (uint32_t)ESP.getFreeHeap();
    String out;
    serializeJson(doc, out);
    sendJson(200, out);
}

// ============================================================
//  ROUTE HANDLER — OPTIONS (CORS preflight)
// ============================================================

static void handleOptions()
{
    s_server.sendHeader("Access-Control-Allow-Origin",  "*");
    s_server.sendHeader("Access-Control-Allow-Methods", "GET,POST,DELETE,OPTIONS");
    s_server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    s_server.send(204);
}

// ============================================================
//  ROUTE HANDLER — 404 fallback
// ============================================================

static void handleNotFound()
{
    String msg = "{\"error\":\"not found\",\"uri\":\"";
    msg += s_server.uri();
    msg += "\"}";
    sendJson(404, msg);
}

// ============================================================
//  FORWARD DECLARATIONS — handlers in other translation units
// ============================================================

void handleLive();              // wifi_live.cpp
void handleGetSessions();       // wifi_api.cpp
void handleGetSession();        // wifi_api.cpp
void handlePostSessionStart();  // wifi_api.cpp
void handlePostSessionStop();   // wifi_api.cpp
void handlePostConfig();        // wifi_api.cpp
void handleDeleteSession();     // wifi_api.cpp

// ============================================================
//  PUBLIC API — wifiBegin()
// ============================================================

void wifiBegin(SessionManager* sm,
               StrokeDetector* sd,
               LapCounter*     lc,
               IMUSample*      s)
{
    s_sm     = sm;
    s_sd     = sd;
    s_lc     = lc;
    s_sample = s;
    s_bootMs = millis();

    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
    delay(100);

    DBG("WIFI", "SoftAP started. SSID=%s  IP=%s",
        WIFI_AP_SSID, WiFi.softAPIP().toString().c_str());
    Serial.printf("[WIFI] AP started. Connect to \"%s\" (pass: %s)\n",
                  WIFI_AP_SSID, WIFI_AP_PASS);
    Serial.printf("[WIFI] Dashboard at http://%s/\n",
                  WiFi.softAPIP().toString().c_str());

    s_server.on("/",                  HTTP_GET,  handleRoot);
    s_server.on("/api/status",        HTTP_GET,  handleStatus);
    s_server.on("/api/live",          HTTP_GET,  handleLive);
    s_server.on("/api/sessions",      HTTP_GET,  handleGetSessions);
    s_server.on("/api/session/start", HTTP_POST, handlePostSessionStart);
    s_server.on("/api/session/stop",  HTTP_POST, handlePostSessionStop);
    s_server.on("/api/config",        HTTP_POST, handlePostConfig);

    s_server.onNotFound([]() {
        const String& uri = s_server.uri();
        if (uri.startsWith("/api/sessions/")) {
            if (s_server.method() == HTTP_GET)    { handleGetSession();    return; }
            if (s_server.method() == HTTP_DELETE) { handleDeleteSession(); return; }
        }
        if (s_server.method() == HTTP_OPTIONS) { handleOptions();  return; }
        handleNotFound();
    });

    s_server.begin();
    DBG("WIFI", "HTTP server started on port 80");
}

// ============================================================
//  PUBLIC API — wifiLoop()
// ============================================================

void wifiLoop()
{
    s_server.handleClient();
}

// ============================================================
//  PUBLIC API — wifiIsClientConnected()
// ============================================================

bool wifiIsClientConnected()
{
    return WiFi.softAPgetStationNum() > 0;
}

// ============================================================
//  ACCESSORS — used by wifi_live.cpp and wifi_api.cpp
// ============================================================

WebServer&      wifiServerRef()   { return s_server; }
SessionManager* wifiSessMgr()     { return s_sm;     }
StrokeDetector* wifiStrokeDet()   { return s_sd;     }
LapCounter*     wifiLapCtr()      { return s_lc;     }
IMUSample*      wifiSamplePtr()   { return s_sample; }