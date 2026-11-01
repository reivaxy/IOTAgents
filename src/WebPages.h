#pragma once

#include <Arduino.h>

static const char STYLE[] PROGMEM = R"rawliteral(
body{font-family:sans-serif;max-width:480px;margin:2em auto;padding:0 1em}
nav{margin-bottom:1em}
table{width:100%;border-collapse:collapse}
td{padding:.5em;border-bottom:1px solid #ddd}
td:last-child{text-align:right;font-weight:bold}
label{display:block;margin-top:.8em}
input{width:100%;box-sizing:border-box;padding:.4em}
input[type=checkbox]{width:auto;margin-right:.5em}
button{margin-top:1em;padding:.5em 1em}
.ok{color:green}.nok{color:red}
.row{display:flex;gap:.5em;align-items:flex-end}
.row label{flex:1}
.row button{margin-top:0}
.alert{background:#fdecea;color:#b71c1c;border:1px solid #f5c6cb;padding:.6em;margin-bottom:1em;border-radius:4px}
)rawliteral";

static const char HOME_PAGE[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Aqua Monitor</title><link rel="stylesheet" href="/style.css"></head>
<body>
<nav><a href="/config">Configuration</a></nav>
<h1 id="name">...</h1>
<div id="relay" class="alert" hidden>SmsRelay not found: alerts cannot be sent. Check the <a href="/config">configuration</a>.</div>
<table>
<tr><td>Light</td><td id="light">-</td></tr>
<tr><td>Temperature</td><td id="temp">-</td></tr>
<tr><td>Water level</td><td id="water">-</td></tr>
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
    document.getElementById('light').textContent=d.light?'ON':'OFF';
    document.getElementById('temp').textContent=d.temperature.toFixed(1)+' \u00B0C';
    const w=document.getElementById('water');
    w.textContent=d.waterLevelOk?'OK':'NOK';
    w.className=d.waterLevelOk?'ok':'nok';
    document.getElementById('relay').hidden=!d.smsRelayNotFound;
    baseEpoch=d.epoch;utcOffset=d.utcOffset||0;baseTick=performance.now();
    showTime();
    document.getElementById('err').textContent='';
  }catch(e){
    document.getElementById('err').textContent='Unable to fetch values: '+e.message;
  }
}
refresh();
setInterval(refresh,REFRESH_MS);
setInterval(showTime,1000);
</script>
</body></html>
)rawliteral";

static const char CONFIG_PAGE[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Configuration</title><link rel="stylesheet" href="/style.css"></head>
<body>
<nav><a href="/">Home</a></nav>
<h1>Configuration</h1>
<form id="f">
<label>Module name<input id="name" maxlength="10" required></label>
<h2>Home network (STA)</h2>
<label>SSID<input id="homeSsid" maxlength="32"></label>
<label>Password<input id="homePwd" type="password" maxlength="63" autocomplete="new-password" placeholder="leave blank to keep current"></label>
<h2>Local network (AP)</h2>
<label>SSID<input id="apSsid" maxlength="32"></label>
<label>Password (8-63 characters)<input id="apPwd" type="password" maxlength="63" autocomplete="new-password" placeholder="leave blank to keep current"></label>
<h2>SmsRelay</h2>
<div class="row">
<label>IP address<input id="smsRelayIp" maxlength="15" inputmode="decimal" pattern="\d{1,3}(\.\d{1,3}){3}" placeholder="not found yet"></label>
<button type="button" id="scan">Scan</button>
<button type="button" id="ping">Ping</button>
</div>
<p id="smsg"></p>
<button type="submit">Save</button>
</form>
<p id="msg"></p>
<h2>Display</h2>
<label><input id="flip" type="checkbox">Flip screen by 180°</label>
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
  }catch(e){msg('Unable to load configuration: '+e.message,false);}
}
$('f').addEventListener('submit',async e=>{
  e.preventDefault();
  const body={name:$('name').value,homeSsid:$('homeSsid').value,apSsid:$('apSsid').value,smsRelayIp:$('smsRelayIp').value};
  if($('homePwd').value)body.homePwd=$('homePwd').value;
  if($('apPwd').value)body.apPwd=$('apPwd').value;
  try{
    const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
    const d=await r.json();
    if(!r.ok)throw new Error(d.error||('HTTP '+r.status));
    $('homePwd').value='';$('apPwd').value='';
    msg(d.reboot?'Saved. The module is rebooting to apply the WiFi settings; reconnect to the network afterwards.':d.smsRelayReachable===false?'Saved, but SmsRelay did not answer at this IP.':'Saved.',d.smsRelayReachable!==false);
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
    const r=await fetch('/api/smsrelay/ping',{method:'POST'});
    const d=await r.json();
    if(!r.ok)throw new Error(d.error||('HTTP '+r.status));
    if(d.reachable)sm('SmsRelay answered.',true);else sm('SmsRelay did not answer.',false);
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
</script>
</body></html>
)rawliteral";
