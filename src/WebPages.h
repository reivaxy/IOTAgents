#pragma once

#include <Arduino.h>

// Stylesheet shared by all the pages (served as /style.css)
static const char STYLE[] PROGMEM = R"rawliteral(
body{font-family:Arial,sans-serif;max-width:600px;margin:5px auto;padding:5px;background:#f5f5f5;color:#333}
.container{background:#fff;padding:10px 12px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,.1)}
a{color:#007bff;text-decoration:none}
a:hover{text-decoration:underline}
h1{color:#333;text-align:center;margin:0 0 8px 0;font-size:22px}
h2{margin:12px 0 4px 0;padding:6px 8px;border-left:4px solid #007bff;background:#f8f9fa;color:#007bff;font-size:14px}
nav{display:flex;justify-content:center;gap:16px;margin-bottom:10px;padding-bottom:8px;border-bottom:1px solid #eee;font-size:13px}
nav span{color:#666}
table{width:100%;border-collapse:collapse;font-size:13px;background:#fff}
td{padding:6px 4px;border-bottom:1px solid #eee}
td:last-child{text-align:right;font-weight:bold;font-family:monospace}
label{display:block;margin-top:6px;margin-bottom:2px;font-weight:bold;font-size:12px;color:#333}
input{width:100%;box-sizing:border-box;padding:6px;border:1px solid #ddd;border-radius:4px;font-size:14px}
input:focus{outline:none;border-color:#007bff}
.check-row{display:flex;align-items:center;font-weight:normal;font-size:14px}
.check-row input,input[type=checkbox]{width:auto;margin:0 8px 0 0}
button{display:block;width:auto;margin:10px auto 0;padding:8px 16px;background:#007bff;color:#fff;border:none;border-radius:4px;font-size:14px;cursor:pointer}
button:hover{background:#0056b3}
button:disabled{background:#9bbfe8;cursor:not-allowed}
button.danger{background:#dc3545}
button.danger:hover{background:#b02a37}
progress{width:100%;margin-top:8px}
p{margin:6px 0;font-size:12px;min-height:1em}
.ok{color:#155724}.nok{color:#721c24}
p.ok,p.nok{padding:6px;border-radius:4px}
p.ok:not(:empty){background:#d4edda;border:1px solid #c3e6cb}
p.nok:not(:empty){background:#f8d7da;border:1px solid #f5c6cb}
.row{display:flex;gap:6px;align-items:flex-end}
.row label{flex:1}
.row button{margin:0;padding:7px 14px}
.alert{background:#fff5f5;color:#dc3545;border:1px solid #f5c6cb;border-left:4px solid #dc3545;padding:8px;margin-bottom:10px;border-radius:4px;font-size:13px}
.alert[hidden]{display:none}
)rawliteral";

// Navigation shared by all the pages: same items in the same order everywhere. Each page uses the
// plain-text variant (*_HERE) for its own entry and the link variant (*_LINK) for the others.
#define NAV_HOME_LINK "<a href=\"/\">Home</a>"
#define NAV_MODULE_LINK "<a href=\"/module/config\">Module settings</a>"
#define NAV_CONFIG_LINK "<a href=\"/config\">Generic Settings</a>"
#define NAV_HOME_HERE "<span>Home</span>"
#define NAV_MODULE_HERE "<span>Module settings</span>"
#define NAV_CONFIG_HERE "<span>Generic Settings</span>"

static const char HOME_PAGE[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Aqua Monitor</title><link rel="stylesheet" href="/style.css"></head>
<body><div class="container">
)rawliteral"
    "<nav>" NAV_HOME_HERE NAV_MODULE_LINK NAV_CONFIG_LINK "</nav>\n"
    R"rawliteral(<h1 id="name">...</h1>
<div id="relay" class="alert" hidden>SmsRelay not found: alerts cannot be sent. Check the <a href="/config">Generic Settings</a>.</div>
<table>
<!--MODULE_STATUS-->
<tr><td>Module time</td><td id="time">-</td></tr>
</table>
<p id="err" class="nok"></p>
<script>
const REFRESH_MS=10000;
let baseEpoch=null,baseTick=0,utcOffset=0;
function showTime(){
  const t=document.getElementById('time');
  if(baseEpoch===null){t.textContent='Not synchronised';return;}
  // Module epoch advanced by elapsed time since the last fetch; the browser clock is not used.
  const s=baseEpoch+utcOffset+Math.floor((performance.now()-baseTick)/1000);
  const a=Math.abs(utcOffset);
  const z='UTC'+(utcOffset<0?'-':'+')+String(Math.floor(a/3600)).padStart(2,'0')+':'+String(Math.floor(a%3600/60)).padStart(2,'0');
  t.textContent=new Date(s*1000).toISOString().slice(0,19).replace('T',' ')+' ('+z+')';
}
async function refresh(){
  try{
    const r=await fetch('/api/status');
    if(!r.ok)throw new Error('HTTP '+r.status);
    const d=await r.json();
    document.getElementById('name').textContent=d.name;
    document.title=d.name;
    document.getElementById('relay').hidden=!d.smsRelayNotFound;
    baseEpoch=d.epoch;utcOffset=d.utcOffset||0;baseTick=performance.now();
    showTime();
    if(typeof moduleStatus==='function')moduleStatus(d.module);
    document.getElementById('err').textContent='';
  }catch(e){
    document.getElementById('err').textContent='Unable to fetch values: '+e.message;
  }
}
refresh();
setInterval(refresh,REFRESH_MS);
setInterval(showTime,1000);
</script>
</div></body></html>
)rawliteral";

static const char MODULE_CONFIG_PAGE[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Module settings</title><link rel="stylesheet" href="/style.css"></head>
<body><div class="container">
)rawliteral"
    "<nav>" NAV_HOME_LINK NAV_MODULE_HERE NAV_CONFIG_LINK "</nav>\n"
    R"rawliteral(<h1>Module settings</h1>
<!--MODULE_CONFIG-->
</div></body></html>
)rawliteral";

static const char CONFIG_PAGE[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Generic Settings</title><link rel="stylesheet" href="/style.css"></head>
<body><div class="container">
)rawliteral"
    "<nav>" NAV_HOME_LINK NAV_MODULE_LINK NAV_CONFIG_HERE "</nav>\n"
    R"rawliteral(<h1>Generic Settings</h1>
<form id="f">
<h2>Module</h2>
<label>Module name<input id="name" maxlength="10" required></label>
<h2>Home network (STA)</h2>
<label>SSID<input id="homeSsid" maxlength="32"></label>
<label>Password<input id="homePwd" type="password" maxlength="63" autocomplete="new-password" placeholder="leave blank to keep current"></label>
<h2>Local network (AP)</h2>
<label>SSID<input id="apSsid" maxlength="32"></label>
<label>Password (8-63 characters)<input id="apPwd" type="password" maxlength="63" autocomplete="new-password" placeholder="leave blank to keep current"></label>
<button type="submit">Save</button>
<p id="msg"></p>
</form>
<h2>SmsRelay</h2>
<div class="row">
<label>IP address<input id="smsRelayIp" maxlength="15" inputmode="decimal" pattern="\d{1,3}(\.\d{1,3}){3}" placeholder="not found yet"></label>
<button type="button" id="ping">Ping</button>
<button type="button" id="scan">Scan</button>
</div>
<p id="smsg"></p>
<h2>Display</h2>
<label class="check-row"><input id="flip" type="checkbox">Flip screen by 180°</label>
<p id="dmsg"></p>
<h2>Firmware</h2>
<table>
<tr><td>Git commit</td><td id="gitHash">-</td></tr>
<tr><td>Build time</td><td id="buildTime">-</td></tr>
</table>
<form id="u">
<label>Firmware file (.bin)<input id="fw" type="file" accept=".bin" required></label>
<button id="ub" type="submit">Upload and update</button>
<progress id="up" value="0" max="100" hidden></progress>
</form>
<p id="umsg"></p>
<h2>Device</h2>
<button id="restart" type="button" class="danger">Restart device</button>
<p id="rmsg"></p>
<script>
const $=id=>document.getElementById(id);
const msg=(t,ok)=>{$('msg').textContent=t;$('msg').className=ok?'ok':'nok';};
async function load(){
  try{
    const r=await fetch('/api/config');
    if(!r.ok)throw new Error('HTTP '+r.status);
    const d=await r.json();
    $('name').value=d.name;
    $('homeSsid').value=d.homeSsid;
    $('apSsid').value=d.apSsid||d.apSsidDefault;
    $('flip').checked=d.flipScreen;
    $('smsRelayIp').value=d.smsRelayIp;
    if(d.smsRelayScanning)waitScan();
    $('gitHash').textContent=d.gitHash;
    $('buildTime').textContent=d.buildTime;
  }catch(e){msg('Unable to load settings: '+e.message,false);}
}
$('f').addEventListener('submit',async e=>{
  e.preventDefault();
  const body={name:$('name').value,homeSsid:$('homeSsid').value,apSsid:$('apSsid').value};
  if($('homePwd').value)body.homePwd=$('homePwd').value;
  if($('apPwd').value)body.apPwd=$('apPwd').value;
  try{
    const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
    const d=await r.json();
    if(!r.ok)throw new Error(d.error||('HTTP '+r.status));
    $('homePwd').value='';$('apPwd').value='';
    msg(d.reboot?'Saved. The module is rebooting to apply the WiFi settings; reconnect to the network afterwards.':'Saved.',true);
  }catch(err){msg('Save failed: '+err.message,false);}
});
const sm=(t,ok)=>{$('smsg').textContent=t;$('smsg').className=ok?'ok':'nok';};
// Polls the module until its background SmsRelay scan is over, then shows the result.
async function waitScan(){
  $('scan').disabled=true;sm('Scanning the network for SmsRelay...',true);
  try{
    const r=await fetch('/api/config');
    if(!r.ok)throw new Error('HTTP '+r.status);
    const d=await r.json();
    if(d.smsRelayScanning){setTimeout(waitScan,2000);return;}
    $('smsRelayIp').value=d.smsRelayIp;
    if(d.smsRelayIp)sm('SmsRelay IP: '+d.smsRelayIp,true);else sm('SmsRelay not found',false);
  }catch(e){sm('Unable to read scan status: '+e.message,false);}
  $('scan').disabled=false;
}
$('scan').addEventListener('click',async()=>{
  $('scan').disabled=true;
  try{
    const r=await fetch('/api/smsrelay/scan',{method:'POST'});
    const d=await r.json();
    if(!r.ok)throw new Error(d.error||('HTTP '+r.status));
    waitScan();
  }catch(err){$('scan').disabled=false;sm('Scan failed: '+err.message,false);}
});
$('ping').addEventListener('click',async()=>{
  $('ping').disabled=true;sm('Pinging SmsRelay...',true);
  try{
    const r=await fetch('/api/smsrelay/ping',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({smsRelayIp:$('smsRelayIp').value})});
    const d=await r.json();
    if(!r.ok)throw new Error(d.error||('HTTP '+r.status));
    if(d.reachable){sm('SmsRelay answered. IP saved.',true);}else sm('SmsRelay did not answer.',false);
  }catch(err){sm('Ping failed: '+err.message,false);}
  $('ping').disabled=false;
});
load();
$('flip').addEventListener('change',async()=>{
  const dm=(t,ok)=>{$('dmsg').textContent=t;$('dmsg').className=ok?'ok':'nok';};
  try{
    const r=await fetch('/api/display',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({flipScreen:$('flip').checked})});
    const d=await r.json();
    if(!r.ok)throw new Error(d.error||('HTTP '+r.status));
    dm('Saved.',true);
  }catch(err){$('flip').checked=!$('flip').checked;dm('Save failed: '+err.message,false);}
});
$('u').addEventListener('submit',e=>{
  e.preventDefault();
  const file=$('fw').files[0];
  if(!file)return;
  const um=(t,ok)=>{$('umsg').textContent=t;$('umsg').className=ok?'ok':'nok';};
  const fd=new FormData();
  fd.append('firmware',file,file.name);
  const xhr=new XMLHttpRequest();
  xhr.open('POST','/api/update');
  xhr.upload.onprogress=ev=>{if(ev.lengthComputable){$('up').value=ev.loaded*100/ev.total;}};
  xhr.onload=()=>{
    $('ub').disabled=false;
    let d={};try{d=JSON.parse(xhr.responseText);}catch(_){}
    if(xhr.status===200){um('Update successful. The module is rebooting; reload this page in a few seconds.',true);}
    else{um('Update failed: '+(d.error||('HTTP '+xhr.status)),false);}
  };
  xhr.onerror=()=>{$('ub').disabled=false;um('Update failed: connection error',false);};
  $('ub').disabled=true;$('up').hidden=false;$('up').value=0;um('Uploading...',true);
  xhr.send(fd);
});
$('restart').addEventListener('click',async()=>{
  if(!confirm('Restart this device?'))return;
  $('restart').disabled=true;
  try{
    const r=await fetch('/api/restart',{method:'POST'});
    const d=await r.json();
    if(!r.ok)throw new Error(d.error||('HTTP '+r.status));
    $('rmsg').textContent='The device is restarting.';
  }catch(err){
    $('restart').disabled=false;
    $('rmsg').textContent='Restart failed: '+err.message;
  }
});
</script>
</div></body></html>
)rawliteral";
