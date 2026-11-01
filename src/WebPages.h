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
button{margin-top:1em;padding:.5em 1em}
.ok{color:green}.nok{color:red}
)rawliteral";

static const char HOME_PAGE[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Aqua Monitor</title><link rel="stylesheet" href="/style.css"></head>
<body>
<nav><a href="/config">Configuration</a></nav>
<h1 id="name">...</h1>
<table>
<tr><td>Light</td><td id="light">-</td></tr>
<tr><td>Temperature</td><td id="temp">-</td></tr>
<tr><td>Water level</td><td id="water">-</td></tr>
<tr><td>Module time (UTC)</td><td id="time">-</td></tr>
</table>
<p id="err" class="nok"></p>
<script>
const REFRESH_MS=10000;
let baseEpoch=null,baseTick=0;
function showTime(){
  const t=document.getElementById('time');
  if(baseEpoch===null){t.textContent='Not synchronised';return;}
  // Module epoch advanced by elapsed time since the last fetch; the browser clock is not used.
  const s=baseEpoch+Math.floor((performance.now()-baseTick)/1000);
  t.textContent=new Date(s*1000).toISOString().slice(0,19).replace('T',' ');
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
    baseEpoch=d.epoch;baseTick=performance.now();
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
<label>Module name<input id="name" maxlength="32" required></label>
<h2>Home network (STA)</h2>
<label>SSID<input id="homeSsid" maxlength="32"></label>
<label>Password<input id="homePwd" type="password" maxlength="63" autocomplete="new-password" placeholder="leave blank to keep current"></label>
<h2>Local network (AP)</h2>
<label>SSID<input id="apSsid" maxlength="32"></label>
<label>Password (8-63 characters)<input id="apPwd" type="password" maxlength="63" autocomplete="new-password" placeholder="leave blank to keep current"></label>
<button type="submit">Save</button>
</form>
<p id="msg"></p>
<h2>Firmware</h2>
<table>
<tr><td>Git commit</td><td id="gitHash">-</td></tr>
<tr><td>Build time</td><td id="buildTime">-</td></tr>
</table>
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
    $('gitHash').textContent=d.gitHash;
    $('buildTime').textContent=d.buildTime;
  }catch(e){msg('Unable to load configuration: '+e.message,false);}
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
load();
</script>
</body></html>
)rawliteral";
