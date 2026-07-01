
// Deklarasi variabel dari file utama yang dipakai di sini
extern String   ai_klasifikasi;
extern String   ai_prediksi;
extern uint32_t ai_freq_min;
extern uint32_t ai_freq_max;
extern bool     ai_pernah_terima;

void webserver_setup() {
  apIP = WiFi.softAPIP();
  dnsServer.start(53, "*", apIP);

  server.on("/", webserver_page_root);
  server.on("/api/setting",   HTTP_GET,  webserver_get_setting);
  server.on("/api/setting",   HTTP_POST, webserver_set_setting);
  server.on("/api/ai_status", HTTP_GET,  webserver_get_ai_status);
  server.onNotFound(webserver_page_not_found);
  server.begin();
}

void webserver_loop() {
  dnsServer.processNextRequest();
  server.handleClient();
}

// ===================== HALAMAN UTAMA =====================
/*void webserver_page_root() {
  const char page_root[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="id">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Pulse Generator</title>
<style>
*,::after,::before{box-sizing:border-box}
body{margin:0;padding:16px;font-family:system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;font-size:14px;line-height:1.5;color:#5c4040;background:#fcf8f8}
h2{margin-top:0;margin-bottom:12px;font-size:18px}
label{display:inline-block;margin-bottom:4px;font-weight:500}
input,select{display:block;width:100%;padding:6px 8px;font:inherit;color:#212529;background:#fff;border:1px solid #ced4da;border-radius:6px;outline:0;transition:border-color .15s,box-shadow .15s}
input:focus,select:focus{border-color:#0d6efd;box-shadow:0 0 0 2px rgba(13,110,253,.22)}
button,input[type=submit]{display:inline-block;padding:6px 14px;font:inherit;font-weight:500;border-radius:6px;border:1px solid transparent;cursor:pointer;color:#fff;background:#67b2d8;transition:background .15s,transform 50ms}
button:hover,input[type=submit]:hover{background:#0b5ed7}
button:active,input[type=submit]:active{transform:scale(.98)}
.form-group{margin-bottom:12px}
#ai-panel{display:none;margin-top:16px;padding:12px 14px;border-radius:8px;background:#f0f7ff;border:1px solid #b6d6f5}
#ai-panel h3{margin:0 0 8px;font-size:14px;color:#1a4a7a}
.ai-row{display:flex;justify-content:space-between;margin-bottom:4px;font-size:13px}
.ai-label{color:#555}
.ai-val{font-weight:500;color:#1a4a7a}
.badge{display:inline-block;padding:2px 8px;border-radius:10px;font-size:12px;font-weight:600}
.badge-rendah{background:#d1fae5;color:#065f46}
.badge-sedang{background:#fef3c7;color:#92400e}
.badge-tinggi{background:#fee2e2;color:#991b1b}
.badge-none{background:#e5e7eb;color:#6b7280}
.ai-wait{color:#888;font-size:12px;margin-top:4px}
hr{border:none;border-top:1px solid #e5e7eb;margin:16px 0}
</style>
</head>
<body>
<h2>Pulse Generator</h2>
<form id="formSettings">
  <div class="form-group">
    <label for="fmin">Frekuensi Min (Hz)</label>
    <input type="number" name="fmin" id="fmin" min="0" required>
  </div>
  <div class="form-group">
    <label for="fmax">Frekuensi Maks (Hz)</label>
    <input type="number" name="fmax" id="fmax" min="0" required>
  </div>
  <div class="form-group">
    <label for="conf">Mode</label>
    <select name="conf" id="conf" required onchange="onModeChange(this.value)">
      <option value="5">Mode 1 – On/off seharian</option>
      <option value="6">Mode 2 – Sore (16:00–23:59)</option>
      <option value="7">Mode 3 – Malam (00:00–08:00)</option>
      <option value="8">Mode 4 – Cerdas (Dikontrol AI)</option>
    </select>
  </div>
  <div class="form-group">
    <label for="state">Kontrol Manual</label>
    <select name="state" id="state" required>
      <option value="0">Ikuti Mode</option>
      <option value="1">On (paksa nyala)</option>
      <option value="2">Off (paksa mati)</option>
    </select>
  </div>
  <button type="submit">Simpan</button>
</form>

<!-- Panel status AI (tampil hanya saat Mode 4 aktif) -->
<div id="ai-panel">
  <h3>Status Alat Tambahan (AI)</h3>
  <div class="ai-row">
    <span class="ai-label">Klasifikasi aktivitas tikus</span>
    <span class="ai-val"><span id="ai-klas" class="badge badge-none">-</span></span>
  </div>
  <div class="ai-row">
    <span class="ai-label">Rentang frekuensi aktif</span>
    <span class="ai-val" id="ai-range">-</span>
  </div>
  <div class="ai-row">
    <span class="ai-label">Prediksi 1 jam ke depan</span>
    <span class="ai-val"><span id="ai-pred" class="badge badge-none">-</span></span>
  </div>
  <p class="ai-wait" id="ai-wait"></p>
</div>

<script>
function dgi(id){ return document.getElementById(id); }

function get_settings(){
  return fetch("/api/setting").then(r=>r.json());
}
function set_settings(fd){
  return fetch("/api/setting",{method:"POST",body:fd});
}
function get_ai_status(){
  return fetch("/api/ai_status").then(r=>r.json());
}

function onModeChange(val){
  dgi("ai-panel").style.display = (val==="8") ? "block" : "none";
  if(val==="8") refreshAiStatus();
}

function badgeClass(k){
  if(k==="rendah") return "badge-rendah";
  if(k==="sedang") return "badge-sedang";
  if(k==="tinggi") return "badge-tinggi";
  return "badge-none";
}

function refreshAiStatus(){
  get_ai_status().then(d=>{
    let kEl = dgi("ai-klas");
    kEl.textContent = d.ai_klas || "-";
    kEl.className   = "badge " + badgeClass(d.ai_klas);
    dgi("ai-range").textContent = d.ai_pernah
      ? (d.ai_fmin + " – " + d.ai_fmax + " Hz") : "-";
    let pEl = dgi("ai-pred");
    pEl.textContent = d.ai_pred || "-";
    pEl.className   = "badge " + badgeClass(d.ai_pred);
    dgi("ai-wait").textContent = d.ai_pernah
      ? "Data terakhir diterima dari alat tambahan."
      : "Menunggu data dari alat tambahan...";
  });
}

document.addEventListener("DOMContentLoaded", ()=>{
  get_settings().then(d=>{
    dgi("fmin").value  = d.fmin;
    dgi("fmax").value  = d.fmax;
    dgi("state").value = d.state;
    dgi("conf").value  = d.conf;
    onModeChange(String(d.conf));
  });

  setInterval(()=>{
    if(dgi("ai-panel").style.display === "block") refreshAiStatus();
  }, 10000);

  let form = dgi("formSettings");
  form.addEventListener("submit", e=>{
    e.preventDefault();
    let fmin = parseFloat(dgi("fmin").value);
    let fmax  = parseFloat(dgi("fmax").value);
    if(fmax < fmin){
      alert("Frekuensi Maks harus >= Frekuensi Min.");
      dgi("fmax").focus();
      return;
    }
    let fd  = new FormData(form);
    let now = new Date();
    fd.append("h", now.getHours());
    fd.append("m", now.getMinutes());
    fd.append("s", now.getSeconds());
    set_settings(fd).then(()=>{
      get_settings().then(d=>{
        dgi("fmin").value  = d.fmin;
        dgi("fmax").value  = d.fmax;
        dgi("state").value = d.state;
        dgi("conf").value  = d.conf;
        onModeChange(String(d.conf));
      });
    });
  });
});
</script>
</body>
</html>
)rawliteral";
  server.send(200, "text/html", page_root);
}*/
void webserver_page_root() {
  const char page_root[] PROGMEM = R"rawliteral(<!DOCTYPE html><html lang="id"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Pulse Generator</title><style>*,::after,::before{box-sizing:border-box}body{margin:0;padding:16px;font-family:system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;font-size:14px;line-height:1.5;color:#5c4040;background-color:#fcf8f8}h1,h2,h3,h4,h5,h6{margin-top:0;margin-bottom:.5rem}label{display:inline-block;margin-bottom:.25rem;font-weight:500}input,select,textarea{display:block;width:100%;padding:.375rem .5rem;font:inherit;color:#212529;background-color:#fff;border:1px solid #ced4da;border-radius:4px;outline:0;transition:border-color .15s ease-in-out,box-shadow .15s ease-in-out,background-color .15s ease-in-out}input:focus,select:focus,textarea:focus{border-color:#0d6efd;box-shadow:0 0 0 2px rgba(13,110,253,.25);background-color:#fff}button,input[type=button],input[type=reset],input[type=submit]{display:inline-block;padding:.375rem .75rem;font:inherit;font-weight:500;line-height:1.5;border-radius:4px;border:1px solid transparent;cursor:pointer;text-align:center;text-decoration:none;color:#fff;background-color:#67b2d8;transition:background-color .15s ease-in-out,border-color .15s ease-in-out,box-shadow .15s ease-in-out,transform 50ms ease-in-out}button:hover,input[type=button]:hover,input[type=reset]:hover,input[type=submit]:hover{background-color:#0b5ed7}button:active,input[type=button]:active,input[type=reset]:active,input[type=submit]:active{transform:scale(.98)}button:disabled,input[type=button]:disabled,input[type=reset]:disabled,input[type=submit]:disabled{opacity:.65;cursor:default}table{width:100%;border-collapse:collapse;margin-bottom:1rem;background-color:#fff;border-radius:6px;overflow:hidden;box-shadow:0 .25rem .75rem rgba(0,0,0,.05)}thead{background-color:#f1f3f5}td,th{padding:.5rem .75rem;text-align:left;border-bottom:1px solid #dee2e6}tr:last-child td{border-bottom:none}tbody tr:nth-child(odd){background-color:#fafbfc}tbody tr:hover{background-color:#eef4ff}form>div{margin-bottom:.75rem}.row{display:flex;flex-wrap:wrap;margin-right:-12px;margin-left:-12px}.row>[class*=col]{padding-right:12px;padding-left:12px}.col{flex:1 0 0%}.col-auto{flex:0 0 auto;width:auto}.pointer{cursor:pointer}button[type=submit]{margin-right:1rem}.ai-info{display:none;margin-top:12px;padding:10px 12px;background:#eef7ff;border:1px solid #b6d9f7;border-radius:6px;font-size:13px;color:#1a5276}.ai-info.show{display:block}.ai-info span{font-weight:600}.badge{display:inline-block;padding:2px 8px;border-radius:12px;font-size:12px;font-weight:600;margin-left:6px}.badge-rendah{background:#d4edda;color:#155724}.badge-sedang{background:#fff3cd;color:#856404}.badge-tinggi{background:#f8d7da;color:#721c24}</style></head><body><h2>Pulse Generator</h2><form id="formSettings"><div><label for="fmin">Frequency Min (Hz)</label><input type="number" name="fmin" id="fmin" min="0" required></div><div><label for="fmax">Frequency Max (Hz)</label><input type="number" name="fmax" id="fmax" min="0" required></div><div><label for="conf">Mode</label><select name="conf" id="conf" required><option value="5">Mode 1 — Manual (5 on / 5 off)</option><option value="6">Mode 2 — Otomatis Sore (16:00–23:59)</option><option value="7">Mode 3 — Otomatis Pagi (00:00–08:00)</option><option value="8">Mode 4 — Otomatis Pintar (AI)</option></select></div><div id="wrapState"><label for="state">Conf</label><select name="state" id="state" required><option value="0">Mode</option><option value="1">On</option><option value="2">Off</option></select></div><div id="aiInfo" class="ai-info"><b>Status AI (Mode 4)</b><br>Aktivitas tikus: <span id="aiActivity">-</span><br>Prediksi 1 jam ke depan: <span id="aiPrediction">-</span><br>Rentang frekuensi AI: <span id="aiFreq">-</span></div><button type="submit">Simpan</button></form><script>function get_settings(){return fetch("/api/setting").then(e=>e.json())}function set_settings(e){return fetch("/api/setting",{method:"POST",body:e})}function dgi(e){return document.getElementById(e)}function badge(t){let c=t==="tinggi"?"badge-tinggi":t==="sedang"?"badge-sedang":"badge-rendah";return`<span class="badge ${c}">${t}</span>`}function updateAiVisibility(conf){let aiDiv=dgi("aiInfo"),stateDiv=dgi("wrapState");if(conf=="8"){aiDiv.classList.add("show");stateDiv.style.display="none"}else{aiDiv.classList.remove("show");stateDiv.style.display=""}}document.addEventListener("DOMContentLoaded",()=>{function e(){get_settings().then(e=>{dgi("fmin").value=e.fmin;dgi("fmax").value=e.fmax;dgi("state").value=e.state;dgi("conf").value=e.conf;updateAiVisibility(e.conf);if(e.ai_activity){dgi("aiActivity").innerHTML=e.ai_activity+badge(e.ai_activity)}if(e.ai_prediction){dgi("aiPrediction").innerHTML=e.ai_prediction+badge(e.ai_prediction)}if(e.ai_fmin&&e.ai_fmax){dgi("aiFreq").textContent=e.ai_fmin+" – "+e.ai_fmax+" Hz"}})}e();dgi("conf").addEventListener("change",function(){updateAiVisibility(this.value)});let t=dgi("formSettings");t.addEventListener("submit",n=>{n.preventDefault();let conf=dgi("conf").value;if(conf!="8"){let i=parseFloat(dgi("fmin").value),a=parseFloat(dgi("fmax").value);if(a<i){alert("Frequency Max harus lebih besar atau sama dengan Frequency Min.");dgi("fmax").focus();return}}let s=new FormData(t),g=new Date;s.append("h",g.getHours());s.append("m",g.getMinutes());s.append("s",g.getSeconds());if(conf=="8"){s.set("state","0")}set_settings(s).then(()=>e())})});</script></body></html>)rawliteral";
  server.send(200, "text/html", page_root);
}

void webserver_page_not_found() {
  server.sendHeader("Location", String("http://") + apIP.toString(), true);
  server.send(302, "text/plain", "");
}

// ===================== API: GET /api/setting =====================
/*void webserver_get_setting() {
  String js;
  js.reserve(80);
  js += "{";
  js += "\"fmin\":"  + String(freq_min) + ",";
  js += "\"fmax\":"  + String(freq_max) + ",";
  js += "\"state\":" + String(state)    + ",";
  js += "\"conf\":"  + String(config);
  js += "}";
  server.send(200, "application/json", js);
}*/
void webserver_get_setting() {
  String js;
  js.reserve(128);
  js += "{";
  js += "\"fmin\":"  + String(freq_min)  + ",";
  js += "\"fmax\":"  + String(freq_max)  + ",";
  js += "\"state\":" + String(state)     + ",";
  js += "\"conf\":"  + String(config)    + ",";
  // Tambahan data AI untuk ditampilkan di UI Mode 4
  js += "\"ai_klasifikasi\":\"" + ai_klasifikasi   + "\",";
  js += "\"ai_prediksi\":\"" + ai_prediksi + "\",";
  js += "\"ai_freq_min\":"  + String(ai_freq_min) + ",";
  js += "\"ai_freq_max\":"  + String(ai_freq_max);
  js += "}";

  server.send(200, "application/json", js);
}

// ===================== API: GET /api/ai_status =====================
void webserver_get_ai_status() {
  String js;
  js.reserve(128);
  js += "{";
  js += "\"ai_klas\":\"" + ai_klasifikasi + "\",";
  js += "\"ai_pred\":\"" + ai_prediksi    + "\",";
  js += "\"ai_fmin\":"   + String(ai_freq_min) + ",";
  js += "\"ai_fmax\":"   + String(ai_freq_max) + ",";
  js += "\"ai_pernah\":" + String(ai_pernah_terima ? "true" : "false");
  js += "}";
  server.send(200, "application/json", js);
}

// ===================== API: POST /api/setting =====================
/*void webserver_set_setting() {
  if (server.hasArg("fmin"))  freq_min = (uint32_t)atoi(server.arg("fmin").c_str());
  if (server.hasArg("fmax"))  freq_max = (uint32_t)atoi(server.arg("fmax").c_str());
  if (server.hasArg("state")) state    = atoi(server.arg("state").c_str());
  if (server.hasArg("conf")) {
    uint8_t new_config = atoi(server.arg("conf").c_str());
    // Saat beralih ke Mode 4: terapkan range AI terakhir jika sudah ada
    if (new_config == 8 && ai_pernah_terima) {
      freq_min = ai_freq_min;
      freq_max = ai_freq_max;
    }
    config = new_config;
  }
  if (server.hasArg("h")) hour   = atoi(server.arg("h").c_str());
  if (server.hasArg("m")) minute = atoi(server.arg("m").c_str());
  if (server.hasArg("s")) second = atoi(server.arg("s").c_str());

  server.send(200, "application/json", "{\"ok\":true}");
}*/
void webserver_set_setting() {
  if (server.hasArg("fmin")) {
    freq_min = (uint32_t)atoi(server.arg("fmin").c_str());
  }
  if (server.hasArg("fmax")) {
    freq_max = (uint32_t)atoi(server.arg("fmax").c_str());
  }
  if (server.hasArg("state")) {
    state = atoi(server.arg("state").c_str());
  }
  if (server.hasArg("conf")) {
    config = atoi(server.arg("conf").c_str());
    // Jika Mode 4 dipilih, paksa state = 0 (ikuti mode)
    if (config == 8) { state = 0; }
  }
  if (server.hasArg("h")) {
    hour   = atoi(server.arg("h").c_str());
  }
  if (server.hasArg("m")) {
    minute = atoi(server.arg("m").c_str());
  }
  if (server.hasArg("s")) {
    second = atoi(server.arg("s").c_str());
  }

  server.send(200, "application/json", "{\"ok\":true}");
}


