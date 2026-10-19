#pragma once

#include <Arduino.h>

// Seiten als statisches HTML; Daten holen sie per fetch() von den /api/*-
// Endpunkten. Responsive (Handy am Access-Point bzw. im Heizraum).

static const char kPageStyle[] PROGMEM = R"CSS(
*{box-sizing:border-box}body{margin:0;font-family:system-ui,sans-serif;background:#f3f5f8;color:#1b2430}
header{background:#14213d;color:#fff;padding:10px 16px;display:flex;flex-wrap:wrap;gap:6px 14px;justify-content:space-between;align-items:center}
header h1{font-size:18px;margin:0}nav a{color:#cfd8ea;text-decoration:none;font-size:14px;margin-left:12px}nav a.on{color:#fff;font-weight:600}
main{max-width:760px;margin:0 auto;padding:12px 16px}
.card{background:#fff;border-radius:8px;padding:12px 16px;margin:12px 0;box-shadow:0 1px 3px rgba(0,0,0,.08)}
h2{font-size:15px;margin:0 0 8px}table{width:100%;border-collapse:collapse}td,th{padding:4px 6px 4px 0;vertical-align:top;text-align:left}
th{font-size:13px;color:#5b6574;font-weight:600;border-bottom:1px solid #e4e8ee}
.kv td:first-child{color:#5b6574;width:42%}.ok{color:#1a7f37;font-weight:600}.bad{color:#b42318;font-weight:600}.mute{color:#5b6574}
.num{text-align:right;white-space:nowrap}
.warn{background:#fff4e5;border:1px solid #f5c26b;border-radius:6px;padding:8px 10px;margin:12px 0}
label{display:block;margin:8px 0 2px;font-size:14px}input,select{width:100%;padding:8px;font-size:15px;border:1px solid #c5ccd6;border-radius:6px}
input[type=checkbox]{width:auto}
button{margin-top:10px;padding:9px 14px;font-size:15px;border:0;border-radius:6px;background:#2563eb;color:#fff;cursor:pointer}
button.sec{background:#e4e8ee;color:#1b2430}button.danger{background:#b42318}.hint{color:#5b6574;font-size:13px}
pre,.mono{font-family:ui-monospace,Consolas,monospace}
pre{white-space:pre-wrap;font-size:12px;background:#f3f5f8;padding:8px;border-radius:6px;max-height:300px;overflow:auto}
#con{font-size:12px;background:#0f172a;color:#cbd5e1;padding:8px;border-radius:6px;height:60vh;overflow:auto;white-space:pre-wrap;word-break:break-all}
#con .tx{color:#93c5fd}#con .rx{color:#86efac}#con .err{color:#fca5a5}
)CSS";

// gemeinsamer Kopf: Titel + Navigation (aktive Seite per data-on)
#define P4_HEAD(TITLE, ON)                                                                                    \
  "<!doctype html><html lang=\"de\"><head><meta charset=\"utf-8\">"                                            \
  "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>" TITLE "</title>"             \
  "<link rel=\"stylesheet\" href=\"/style.css\"></head><body><header><h1 id=\"name\">ESP-P4d</h1><nav>"           \
  "<a href=\"/\"" ON##_S ">Status</a><a href=\"/values\"" ON##_V ">Werte</a><a href=\"/console\"" ON##_C \
  ">Konsole</a><a href=\"/settings\"" ON##_E ">Einstellungen</a></nav></header><main>"
#define NAV_S_S " class=\"on\""
#define NAV_S_V ""
#define NAV_S_C ""
#define NAV_S_E ""
#define NAV_V_S ""
#define NAV_V_V " class=\"on\""
#define NAV_V_C ""
#define NAV_V_E ""
#define NAV_C_S ""
#define NAV_C_V ""
#define NAV_C_C " class=\"on\""
#define NAV_C_E ""
#define NAV_E_S ""
#define NAV_E_V ""
#define NAV_E_C ""
#define NAV_E_E " class=\"on\""

#define P4_JS_COMMON \
  "const $=i=>document.getElementById(i);const esc=s=>String(s==null?'':s).replace(/[&<>\"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;'}[c]));" \
  "const post=(u,d)=>fetch(u,{method:'POST',body:new URLSearchParams(d)}).then(async r=>{const t=await r.text();if(!r.ok)throw new Error(t);return t});"

static const char kStatusPage[] PROGMEM = P4_HEAD("ESP-P4d", NAV_S) R"HTML(
<div id="pw" class="warn" hidden>Das Einstellungs-Passwort ist noch <b>admin</b> &ndash; bitte unter <a href="/settings">Einstellungen</a> &auml;ndern.</div>
<div id="ap" class="warn" hidden></div>
<div class="card"><h2>Heizung</h2><table class="kv">
<tr><td>Verbindung (COM1)</td><td id="k_conn">&hellip;</td></tr>
<tr><td>Betriebsart</td><td id="k_mode">&ndash;</td></tr>
<tr><td>Zustand</td><td id="k_state">&ndash;</td></tr>
<tr><td>St&ouml;rung</td><td id="k_err">&ndash;</td></tr>
<tr><td>Kessel-Software</td><td id="k_ver">&ndash;</td></tr>
<tr><td>Letzte Antwort</td><td id="k_last">&ndash;</td></tr></table></div>
<div class="card"><h2>Ausgew&auml;hlte Werte</h2><table id="vals"></table>
<p class="hint" id="vhint"></p></div>
<div class="card"><h2>WLAN</h2><table class="kv">
<tr><td>Netz</td><td id="w_ssid">&ndash;</td></tr><tr><td>Adresse</td><td id="w_ip">&ndash;</td></tr>
<tr><td>Signal</td><td id="w_rssi">&ndash;</td></tr><tr><td>Name im Netz</td><td id="w_host">&ndash;</td></tr></table></div>
<div class="card"><h2>Ger&auml;t</h2><table class="kv">
<tr><td>Firmware</td><td id="d_fw">&ndash;</td></tr><tr><td>Uhrzeit</td><td id="d_time">&ndash;</td></tr>
<tr><td>Laufzeit</td><td id="d_up">&ndash;</td></tr><tr><td>Freier Speicher</td><td id="d_heap">&ndash;</td></tr></table></div>
</main><script>)HTML" P4_JS_COMMON R"HTML(
const dur=s=>{const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return (d?d+' d ':'')+h+' h '+m+' min'};
async function load(){try{const s=await (await fetch('/api/status',{cache:'no-store'})).json();const d=s.device,k=s.kessel;
$('name').textContent=d.systemName;document.title=d.systemName;
$('k_conn').innerHTML=k.connected?'<span class="ok">verbunden</span>':'<span class="bad">keine Antwort</span>';
$('k_mode').textContent=k.mode||'–';$('k_state').textContent=k.state||'–';
const e=(k.errors||[])[0];$('k_err').innerHTML=e&&e.state==1?'<span class="bad">'+esc(e.text)+'</span> <span class="mute">('+esc(e.time)+')</span>':'keine';
$('k_ver').textContent=k.version||'–';$('k_last').textContent=k.lastOkAgo<0?'noch nie':'vor '+k.lastOkAgo+' s';
$('w_ssid').innerHTML=d.wlan.connected?esc(d.wlan.ssid):'<span class="bad">nicht verbunden</span>';
$('w_ip').textContent=d.wlan.connected?d.wlan.ip:'–';$('w_rssi').textContent=d.wlan.connected?d.wlan.rssi+' dBm':'–';
$('w_host').textContent='http://'+d.wlan.hostname+'.local';
$('d_fw').textContent=d.firmware;$('d_up').textContent=dur(d.uptime);$('d_heap').textContent=Math.round(d.heap/1024)+' KB';
$('d_time').textContent=d.time?new Date(d.time*1000).toLocaleString('de-DE'):'noch nicht gestellt (NTP)';
$('pw').hidden=!d.defaultPassword;
$('ap').hidden=!d.ap.active;$('ap').innerHTML='Einrichtungs-WLAN <b>'+esc(d.ap.ssid)+'</b> ist aktiv (http://192.168.4.1). WLAN unter <a href="/settings">Einstellungen</a> eintragen.';
const v=await (await fetch('/api/values',{cache:'no-store'})).json();
const sel=v.values.filter(x=>x.sel);const list=sel.length?sel:v.values.slice(0,12);
$('vals').innerHTML=list.map(x=>'<tr><td>'+esc(x.n)+'</td><td class="num">'+(x.v==null?'–':x.v+' '+esc(x.u))+'</td></tr>').join('');
$('vhint').innerHTML=!v.values.length?(k.catalog=='loading'?'Werte-Katalog wird gelesen ('+k.catalogLoaded+') &hellip;':'Noch keine Werte &ndash; die Heizung muss an COM1 antworten.'):
 (sel.length?'':'Keine Auswahl &ndash; hier die ersten 12. Auswahl unter <a href="/values">Werte</a>.');
}catch(e){}}
load();setInterval(load,5000);
</script></body></html>)HTML";

static const char kValuesPage[] PROGMEM = P4_HEAD("Werte", NAV_V) R"HTML(
<div class="card"><h2>Alle Werte der Heizung</h2>
<p class="hint">Mit Haken markierte Werte gehen an home-central (<span class="mono">/api/v1/summary</span>) und stehen auf der Startseite. Ohne Haken bekommt home-central alle Werte. Speichern fragt nach dem Einstellungs-Passwort.</p>
<input id="flt" placeholder="Filter (Name oder Adresse)">
<table><thead><tr><th></th><th>Wert</th><th class="num">Aktuell</th><th class="mono">Adresse</th></tr></thead><tbody id="rows"></tbody></table>
<button id="save">Auswahl speichern</button> <button class="sec" id="none">Keine Auswahl</button><p class="hint" id="msg"></p>
<p class="hint" id="info"></p></div>
</main><script>)HTML" P4_JS_COMMON R"HTML(
let vals=[],sel=new Set(),loaded=false;
function draw(){const f=$('flt').value.toLowerCase();
$('rows').innerHTML=vals.filter(x=>!f||x.n.toLowerCase().includes(f)||x.k.toLowerCase().includes(f)).map(x=>
 '<tr><td><input type="checkbox" data-k="'+x.k+'"'+(sel.has(x.k)?' checked':'')+'></td><td>'+esc(x.n)+'</td><td class="num">'+(x.v==null?'–':x.v+' '+esc(x.u))+
 '</td><td class="mono mute">'+x.k+'</td></tr>').join('');}
async function load(){try{const v=await (await fetch('/api/values',{cache:'no-store'})).json();vals=v.values;
 if(!loaded){sel=new Set(vals.filter(x=>x.sel).map(x=>x.k));loaded=vals.length>0}
 $('info').textContent=vals.length+' Werte'+(v.connected?'':' – Heizung antwortet gerade nicht, Werte veraltet');
 if(document.activeElement.type!='checkbox')draw();}catch(e){}}
$('rows').onchange=e=>{const k=e.target.dataset.k;if(!k)return;e.target.checked?sel.add(k):sel.delete(k)};
$('flt').oninput=draw;
$('save').onclick=()=>post('/api/selection',{sel:[...sel].join(',')}).then(t=>$('msg').textContent=t).catch(e=>$('msg').textContent=e.message);
$('none').onclick=()=>{sel.clear();draw()};
load();setInterval(load,5000);
</script></body></html>)HTML";

static const char kConsolePage[] PROGMEM = P4_HEAD("Konsole", NAV_C) R"HTML(
<div class="card"><h2>COM1-Kommunikation (live)</h2>
<p class="hint"><span style="color:#2563eb">&rarr; gesendet</span> &middot; <span style="color:#1a7f37">&larr; empfangen</span> &middot; <span style="color:#b42318">&larr; ung&uuml;ltig/keine Antwort</span>. Rohbytes so wie auf der Leitung (mit Byte-Stuffing), l&auml;ngere Rahmen gek&uuml;rzt.</p>
<label><input type="checkbox" id="pause"> Anhalten</label> <label><input type="checkbox" id="onlyerr"> nur Fehler</label>
<label for="cmd">Kommando</label><select id="cmd"><option value="">alle</option><option>Check</option><option>GetState</option><option>GetValue</option><option>ValueListFirst</option><option>ValueListNext</option><option>ErrorFirst</option><option>ErrorNext</option><option>GetVersion</option></select>
<div id="con" class="mono"></div><button class="sec" id="clr">Anzeige leeren</button></div>
<div class="card"><h2>Ereignisse</h2><pre id="log"></pre></div>
</main><script>)HTML" P4_JS_COMMON R"HTML(
let next=0;const MAX=600;
function ts(ms){const s=Math.floor(ms/1000);return String(Math.floor(s/3600)).padStart(2,'0')+':'+String(Math.floor(s/60)%60).padStart(2,'0')+':'+String(s%60).padStart(2,'0')+'.'+String(ms%1000).padStart(3,'0')}
async function tick(){if($('pause').checked)return;try{const r=await (await fetch('/api/console?since='+next,{cache:'no-store'})).json();
 const c=$('con'),bottom=c.scrollTop+c.clientHeight>=c.scrollHeight-20;next=r.next;
 for(const l of r.lines){if($('onlyerr').checked&&(l.d=='tx'||l.ok))continue;if($('cmd').value&&l.n!=$('cmd').value)continue;
  const d=document.createElement('div');d.className=l.d=='tx'?'tx':(l.ok?'rx':'err');
  d.textContent=ts(l.t)+' '+(l.d=='tx'?'→':'←')+' '+l.n.padEnd(14)+' '+String(l.b).padStart(3)+' B  '+(l.h||(l.d=='rx'?'(keine Antwort)':''));c.appendChild(d)}
 while(c.childNodes.length>MAX)c.removeChild(c.firstChild);if(bottom)c.scrollTop=c.scrollHeight;}catch(e){}}
async function log(){try{$('log').textContent=await (await fetch('/log.txt',{cache:'no-store'})).text()}catch(e){}}
$('clr').onclick=()=>$('con').innerHTML='';
tick();setInterval(tick,1000);log();setInterval(log,10000);
</script></body></html>)HTML";

static const char kSettingsPage[] PROGMEM = P4_HEAD("Einstellungen", NAV_E) R"HTML(
<div class="card"><h2>WLAN</h2>
<p class="hint">Nach dem Speichern verbindet sich das Ger&auml;t mit dem neuen WLAN. Klappt das nicht, ist es nach 5 Minuten wieder &uuml;ber das Einrichtungs-WLAN <b>esp-p4d-setup</b> (Passwort esp-p4d-setup) erreichbar.</p>
<button class="sec" id="scan">Netze suchen</button>
<label for="ssid">Netzwerk (SSID)</label><input id="ssid" list="nets" autocomplete="off"><datalist id="nets"></datalist>
<label for="psk">Passwort</label><input id="psk" type="password" autocomplete="new-password">
<button id="wsave">Verbinden</button><p class="hint" id="wmsg"></p></div>
<div class="card"><h2>Ger&auml;t</h2>
<label for="sname">Ger&auml;tename (daraus wird der Name im Netz, z. B. esp-p4d.local &ndash; wirkt nach Neustart)</label><input id="sname">
<label for="ntp">Zeitserver (NTP), z. B. home-central oder pool.ntp.org &ndash; wirkt nach Neustart</label><input id="ntp">
<label for="npw">Neues Einstellungs-Passwort (Benutzer admin, leer = unver&auml;ndert, mind. 6 Zeichen)</label><input id="npw" type="password" autocomplete="new-password">
<button id="csave">Speichern</button><p class="hint" id="cmsg"></p></div>
<div class="card"><h2>API f&uuml;r home-central</h2>
<p class="hint">home-central fragt <span class="mono">http://&lt;Ger&auml;t&gt;/api/v1/summary</span> ab (au&szlig;erdem <span class="mono">/api/v1/values</span>, <span class="mono">/api/v1/status</span>) und schickt den Schl&uuml;ssel im Header <span class="mono">X-API-Key</span>.</p>
<label>API-Schl&uuml;ssel</label><input id="key" class="mono" readonly onclick="this.select()">
<button class="sec" id="newkey">Neuen Schl&uuml;ssel erzeugen</button><p class="hint" id="kmsg"></p></div>
<div class="card"><h2>Heizung</h2>
<p class="hint">Der Werte-Katalog (welche Werte die Heizung hat) wird nach jedem Start gelesen. Nach einem Software-Update der Heizung hier neu einlesen.</p>
<button class="sec" id="rescan">Werte-Katalog neu lesen</button><p class="hint" id="rmsg"></p></div>
<div class="card"><h2>Einstellungs-Backup</h2>
<p class="hint">Enth&auml;lt alle Einstellungen <b>mit</b> WLAN-Passwort, Einstellungs-Passwort und API-Schl&uuml;ssel &ndash; sicher aufbewahren. Einspielen startet das Ger&auml;t neu; stimmt das WLAN im Backup nicht, hilft das Einrichtungs-WLAN.</p>
<button class="sec" onclick="location.href='/api/backup'">Backup herunterladen</button>
<label for="bfile">Backup einspielen</label><input type="file" id="bfile" accept=".json,application/json">
<button id="restore">Einspielen</button><p class="hint" id="bmsg"></p></div>
<div class="card"><h2>Firmware-Update</h2>
<p class="hint">Nur .bin-Dateien von ESP-P4d mit gleicher oder neuerer Version werden angenommen. Die Einstellungen bleiben erhalten.</p>
<input type="file" id="bin" accept=".bin"><label><input type="checkbox" id="down"> &auml;ltere Version erlauben</label>
<button id="ota">Hochladen</button><p class="hint" id="omsg"></p></div>
<div class="card"><h2>Ereignisse</h2><pre id="log"></pre><button class="sec" onclick="location.href='/log.txt'">log.txt herunterladen</button></div>
<div class="card"><button class="danger" id="reboot">Neu starten</button></div>
</main><script>)HTML" P4_JS_COMMON R"HTML(
fetch('/api/settings',{cache:'no-store'}).then(r=>r.json()).then(s=>{$('sname').value=s.systemName;$('ntp').value=s.ntpServer;$('ssid').value=s.wlanSsid||'';$('key').value=s.apiKey;$('name').textContent=s.systemName});
fetch('/log.txt',{cache:'no-store'}).then(r=>r.text()).then(t=>{$('log').textContent=t});
$('scan').onclick=async()=>{$('scan').disabled=true;$('wmsg').textContent='suche ...';
 for(let i=0;i<15;i++){const r=await (await fetch('/api/wifi/scan',{cache:'no-store'})).json();
  if(!r.running){$('nets').innerHTML='';r.networks.forEach(n=>{const o=document.createElement('option');o.value=n.ssid;o.label=n.rssi+' dBm'+(n.open?' (offen)':'');$('nets').appendChild(o)});
   $('wmsg').textContent=r.networks.length+' Netze gefunden – im Feld Netzwerk auswählen.';break}
  await new Promise(x=>setTimeout(x,1000))}
 $('scan').disabled=false};
$('wsave').onclick=()=>{if(!$('ssid').value){$('wmsg').textContent='Bitte ein Netzwerk angeben.';return}
 post('/api/wifi/connect',{ssid:$('ssid').value,psk:$('psk').value}).then(t=>$('wmsg').textContent=t).catch(e=>$('wmsg').textContent=e.message)};
$('csave').onclick=()=>post('/api/config',{systemName:$('sname').value,ntpServer:$('ntp').value,newPassword:$('npw').value}).then(t=>$('cmsg').textContent=t).catch(e=>$('cmsg').textContent=e.message);
$('newkey').onclick=()=>{if(confirm('Neuen API-Schlüssel erzeugen? home-central muss danach den neuen bekommen.'))post('/api/apikey',{}).then(k=>{$('key').value=k;$('kmsg').textContent='Neuer Schlüssel aktiv.'})};
$('rescan').onclick=()=>post('/api/rescan',{}).then(t=>$('rmsg').textContent=t);
$('restore').onclick=async()=>{const f=$('bfile').files[0];if(!f){$('bmsg').textContent='Bitte eine Backup-Datei wählen.';return}
 if(!confirm('Einstellungen aus dem Backup übernehmen und neu starten?'))return;
 const r=await fetch('/api/restore',{method:'POST',headers:{'Content-Type':'application/json'},body:await f.text()});$('bmsg').textContent=await r.text()};
$('ota').onclick=()=>{const f=$('bin').files[0];if(!f){$('omsg').textContent='Bitte eine .bin-Datei wählen.';return}
 const fd=new FormData();if($('down').checked)fd.append('otaForceDowngrade','1');fd.append('file',f);
 $('omsg').textContent='lade hoch ...';$('ota').disabled=true;
 fetch('/update',{method:'POST',body:fd}).then(async r=>{$('omsg').textContent=await r.text();if(r.ok)setTimeout(()=>location.href='/',15000)}).catch(e=>$('omsg').textContent=e.message).finally(()=>$('ota').disabled=false)};
$('reboot').onclick=()=>{if(confirm('Gerät jetzt neu starten?'))post('/api/reboot',{}).then(t=>$('cmsg').textContent=t)};
</script></body></html>)HTML";
