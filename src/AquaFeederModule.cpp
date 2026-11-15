#include "AquaFeederModule.h"

bool AquaFeederModule::parseTime(const char* text, int& minute) {
  if (!text || strlen(text) != 5 || text[2] != ':') return false;
  for (int i : {0, 1, 3, 4}) {
    if (text[i] < '0' || text[i] > '9') return false;
  }
  const int hour = (text[0] - '0') * 10 + text[1] - '0';
  const int min = (text[3] - '0') * 10 + text[4] - '0';
  if (hour > 23 || min > 59) return false;
  minute = hour * 60 + min;
  return true;
}

String AquaFeederModule::formatTime(int minute) {
  if (minute < 0 || minute >= 1440) return String();
  char text[6];
  const unsigned hour = static_cast<unsigned>(minute / 60);
  const unsigned min = static_cast<unsigned>(minute % 60);
  snprintf(text, sizeof(text), "%02u:%02u", hour, min);
  return String(text);
}

bool AquaFeederModule::parseFeeds(JsonObjectConst cfg, Feed (&feeds)[MAX_FEEDS], String& error) {
  if (!cfg["feeds"].is<JsonArrayConst>() || cfg["feeds"].size() > MAX_FEEDS) {
    error = "feeds must be an array of up to 4 entries";
    return false;
  }
  size_t index = 0;
  for (JsonVariantConst item : cfg["feeds"].as<JsonArrayConst>()) {
    if (!item.is<JsonObjectConst>() || !item["time"].is<const char*>() ||
        !parseTime(item["time"].as<const char*>(), feeds[index].minute) ||
        !item["quantity"].is<int>() || item["quantity"].as<int>() < 1 ||
        item["quantity"].as<int>() > MAX_QUANTITY) {
      error = "Each feed needs a time (HH:MM) and integer quantity (1-10000)";
      return false;
    }
    feeds[index].quantity = item["quantity"].as<int>();
    for (size_t j = 0; j < index; ++j) {
      if (feeds[j].minute == feeds[index].minute) {
        error = "Dispensing times must be different";
        return false;
      }
    }
    ++index;
  }
  return true;
}

JsonDocument AquaFeederModule::scheduleDocument(const Feed (&feeds)[MAX_FEEDS]) {
  JsonDocument doc;
  JsonArray entries = doc["feeds"].to<JsonArray>();
  for (const Feed& feed : feeds) {
    if (feed.minute < 0) continue;
    JsonObject entry = entries.add<JsonObject>();
    entry["time"] = formatTime(feed.minute);
    entry["quantity"] = feed.quantity;
  }
  return doc;
}

bool AquaFeederModule::writeJson(const char* key, const JsonDocument& doc) {
  String json;
  serializeJson(doc, json);
  Preferences prefs;
  if (!prefs.begin(NAMESPACE, false)) return false;
  const bool ok = prefs.putString(key, json) == json.length();
  prefs.end();
  return ok;
}

void AquaFeederModule::load() {
  Preferences prefs;
  if (!prefs.begin(NAMESPACE, false)) {
    storageError_ = "Cannot open feeder preferences";
    return;
  }
  lastScheduledMinute_ = prefs.getLong64("lastMinute", -1);
  if (prefs.isKey("schedule")) {
    JsonDocument doc;
    Feed loaded[MAX_FEEDS];
    String error;
    if (!deserializeJson(doc, prefs.getString("schedule")) &&
        parseFeeds(doc.as<JsonObjectConst>(), loaded, error)) {
      for (size_t i = 0; i < MAX_FEEDS; ++i) feeds_[i] = loaded[i];
    } else {
      storageError_ = "Stored schedule is invalid; automatic feeding disabled";
    }
  }
  if (prefs.isKey("previous")) {
    JsonDocument doc;
    if (!deserializeJson(doc, prefs.getString("previous"))) {
      previousEpoch_ = doc["epoch"] | int64_t(0);
      previousQuantity_ = doc["quantity"] | 0;
      previousSuccess_ = doc["success"] | false;
      if (previousQuantity_ > 0 && !previousSuccess_) {
        alertMessage_ = "Previous dispensing failed: no food detected";
      }
    } else {
      storageError_ = "Cannot read previous feed history";
    }
  }
  prefs.end();
}

void AquaFeederModule::begin() {
  load();
  auto& server = httpServer_.webServer();
  server.on("/api/feeder/config", HTTP_GET, [this]() {
    httpServer_.sendJson(200, scheduleDocument(feeds_));
  });
  server.on("/api/feeder/run", HTTP_POST, [this]() { handleRun(); });
  // Development-only sensor mock; not persisted and always starts in success mode.
  server.on("/api/feeder/mock-sensor", HTTP_POST, [this]() {
    JsonDocument doc;
    if (deserializeJson(doc, httpServer_.webServer().arg("plain")) || !doc["ok"].is<bool>()) {
      httpServer_.sendError(400, "Expected a boolean ok field");
      return;
    }
    mockSensorOk_ = doc["ok"].as<bool>();
    JsonDocument result;
    result["ok"] = true;
    httpServer_.sendJson(200, result);
  });
}

bool AquaFeederModule::saveConfig(JsonObjectConst cfg, String& error) {
  Feed next[MAX_FEEDS];
  if (!parseFeeds(cfg, next, error)) return false;
  if (!writeJson("schedule", scheduleDocument(next))) {
    error = "Cannot save feeder schedule";
    return false;
  }
  for (size_t i = 0; i < MAX_FEEDS; ++i) feeds_[i] = next[i];
  storageError_ = "";
  return true;
}

void AquaFeederModule::update() {
  if (!clockReady()) return;
  const int64_t localMinute = (static_cast<int64_t>(timeSync_.now()) + timeSync_.utcOffset()) / 60;
  // Never replay old slots after reboot or a backward clock adjustment. Missed minutes are
  // intentionally skipped rather than dispensing a potentially dangerous catch-up meal.
  if (localMinute <= lastScheduledMinute_) return;
  for (const Feed& feed : feeds_) {
    if (feed.minute < 0 || feed.minute != localMinute % 1440) continue;
    Preferences prefs;
    if (!prefs.begin(NAMESPACE, false)) {
      storageError_ = "Scheduled feed blocked: cannot save execution marker";
      return;
    }
    const bool saved = prefs.putLong64("lastMinute", localMinute) == sizeof(int64_t);
    prefs.end();
    if (!saved) {
      storageError_ = "Scheduled feed blocked: cannot save execution marker";
      return;
    }
    // Claim before operating: power loss must not cause a second meal in the same minute.
    lastScheduledMinute_ = localMinute;
    dispense(feed.quantity);
    return;
  }
}

bool AquaFeederModule::dispense(int quantity) {
  const bool motorOk = runDispenser(quantity);
  if (quantity < 0) return motorOk;  // reversing is not a meal and must not clear feed failures
  previousEpoch_ = timeSync_.isSynced() ? static_cast<int64_t>(timeSync_.now()) : 0;
  previousQuantity_ = quantity;
  previousSuccess_ = motorOk && dispensingDetected();
  alertMessage_ = previousSuccess_ ? "" : "Previous dispensing failed: no food detected";
  JsonDocument history;
  history["epoch"] = previousEpoch_;
  history["quantity"] = previousQuantity_;
  history["success"] = previousSuccess_;
  if (!writeJson("previous", history)) storageError_ = "Cannot save previous feed history";
  return previousSuccess_;
}

void AquaFeederModule::handleRun() {
  JsonDocument doc;
  if (deserializeJson(doc, httpServer_.webServer().arg("plain")) ||
      !doc["quantity"].is<int>()) {
    httpServer_.sendError(400, "Expected an integer quantity");
    return;
  }
  const int quantity = doc["quantity"].as<int>();
  if (quantity != REVERSE_QUANTITY && (quantity < 1 || quantity > MAX_QUANTITY)) {
    httpServer_.sendError(400, "Quantity must be 1-10000, or -100 for reverse");
    return;
  }
  const bool ok = dispense(quantity);
  JsonDocument result;
  result["ok"] = ok;
  result["quantity"] = quantity;
  if (!ok) result["error"] = quantity < 0 ? "Reverse run failed" : alertMessage_.c_str();
  httpServer_.sendJson(ok ? 200 : 500, result);
}

JsonDocument AquaFeederModule::getStatus() {
  JsonDocument doc;
  doc["mockHardware"] = true;
  doc["mockSensorOk"] = mockSensorOk_;
  doc["clockReady"] = clockReady();
  doc["utcOffset"] = timeSync_.utcOffset();
  doc["alert"] = alertMessage_;
  doc["storageError"] = storageError_;
  doc["previous"] = nullptr;
  if (previousQuantity_ > 0) {
    JsonObject previous = doc["previous"].to<JsonObject>();
    if (previousEpoch_ > 0) previous["epoch"] = previousEpoch_;
    else previous["epoch"] = nullptr;
    previous["quantity"] = previousQuantity_;
    previous["success"] = previousSuccess_;
  }
  doc["next"] = nullptr;
  if (clockReady()) {
    const int64_t now = static_cast<int64_t>(timeSync_.now());
    const int64_t localNow = now + timeSync_.utcOffset();
    int64_t earliest = 0;
    int quantity = 0;
    for (const Feed& feed : feeds_) {
      if (feed.minute < 0) continue;
      int64_t candidate = localNow / 86400 * 86400 + feed.minute * 60;
      // The scheduler may still execute the current minute on its next update().
      if (candidate + 60 <= localNow || candidate / 60 <= lastScheduledMinute_) candidate += 86400;
      if (!earliest || candidate < earliest) {
        earliest = candidate;
        quantity = feed.quantity;
      }
    }
    if (earliest) {
      JsonObject next = doc["next"].to<JsonObject>();
      next["epoch"] = earliest - timeSync_.utcOffset();
      next["quantity"] = quantity;
    }
  }
  return doc;
}

String AquaFeederModule::getHomeHtml() {
  // The generic insertion point is inside a table: forms must be inside a table cell.
  return String(F(R"html(
<tr><td>Previous feed</td><td id="feedPrevious">No feed yet</td></tr>
<tr><td>Next feed</td><td id="feedNext">-</td></tr>
<tr><td colspan="2" style="text-align:left;font-family:inherit;font-weight:normal">
<div id="feedAlert" class="alert" role="alert" hidden></div>
<p>Hardware simulation: no motor is connected. Quantities are dispenser units.</p>
<form id="feedForm">
<label>Manual quantity<input id="feedQuantity" type="number" min="1" max="10000" step="1" value="1" required></label>
<div class="row"><button id="feedNow" type="submit">Feed now</button>
<button id="feedReverse" type="button">Reverse (-100)</button></div>
</form><p id="feedResult" role="status"></p>
</td></tr>
<script>
function moduleStatus(m){
  const date=e=>e===null?'Time unavailable':new Date((e+m.utcOffset)*1000).toISOString().slice(0,19).replace('T',' ');
  document.getElementById('feedPrevious').textContent=m.previous?
    date(m.previous.epoch)+' / '+m.previous.quantity+' units / '+(m.previous.success?'OK':'FAILED'):'No feed yet';
  document.getElementById('feedNext').textContent=!m.clockReady?'Waiting for clock and UTC offset':
    m.next?date(m.next.epoch)+' / '+m.next.quantity+' units':'No scheduled feeds';
  const warning=document.getElementById('feedAlert');
  warning.textContent=[m.alert,m.storageError].filter(Boolean).join(' — ');warning.hidden=!warning.textContent;
}
async function feederRun(quantity){
  const buttons=[document.getElementById('feedNow'),document.getElementById('feedReverse')];
  const result=document.getElementById('feedResult');buttons.forEach(b=>b.disabled=true);
  try{
    const r=await fetch('/api/feeder/run',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({quantity})});
    const d=await r.json();if(!r.ok||!d.ok)throw new Error(d.error||('HTTP '+r.status));
    result.textContent=quantity<0?'Reverse run completed.':'Feed completed.';result.className='ok';
  }catch(e){result.textContent=e.message;result.className='nok';}
  finally{buttons.forEach(b=>b.disabled=false);if(typeof refresh==='function')refresh();}
}
document.getElementById('feedForm').addEventListener('submit',e=>{
  e.preventDefault();feederRun(Number(document.getElementById('feedQuantity').value));
});
document.getElementById('feedReverse').addEventListener('click',()=>feederRun(-100));
</script>
)html"));
}

String AquaFeederModule::getConfigHtml() {
  return String(F(R"html(
<h2>Daily dispensing schedule</h2>
<p>Set up to four local-time feeds. Use Remove to clear a feed, then Save schedule to permanently cancel it. Quantity: 1-10000 dispenser units.</p>
<form id="feederConfig"><div id="feederSlots"></div><button id="feederSave" type="submit" disabled>Save schedule</button></form>
<p id="feederConfigResult" role="status"></p>
<script>
const feederSlots=document.getElementById('feederSlots');
for(let i=0;i<4;i++){
  const row=document.createElement('div');row.className='row';
  row.innerHTML='<label>Time '+(i+1)+'<input type="time" step="60" id="feedTime'+i+'"></label>'+
    '<label>Quantity<input type="number" min="1" max="10000" step="1" id="feedAmount'+i+'"></label>'+
    '<button type="button" class="danger" aria-label="Remove feed '+(i+1)+'">Remove</button>';
  row.querySelector('button').addEventListener('click',()=>{
    document.getElementById('feedTime'+i).value='';
    document.getElementById('feedAmount'+i).value='';
    feederMessage('Feed '+(i+1)+' removed. Save schedule to apply the cancellation.',true);
  });
  feederSlots.appendChild(row);
}
const feederMessage=(text,ok)=>{const p=document.getElementById('feederConfigResult');p.textContent=text;p.className=ok?'ok':'nok';};
async function loadFeeder(){
  try{
    const r=await fetch('/api/feeder/config');if(!r.ok)throw new Error('HTTP '+r.status);
    const d=await r.json();d.feeds.forEach((f,i)=>{
      document.getElementById('feedTime'+i).value=f.time;document.getElementById('feedAmount'+i).value=f.quantity;
    });document.getElementById('feederSave').disabled=false;
  }catch(e){feederMessage('Cannot load schedule: '+e.message,false);}
}
document.getElementById('feederConfig').addEventListener('submit',async e=>{
  e.preventDefault();const feeds=[];
  for(let i=0;i<4;i++){
    const time=document.getElementById('feedTime'+i).value,amount=document.getElementById('feedAmount'+i).value;
    if(!time&&!amount)continue;
    if(!time||!amount){feederMessage('Fill both fields or leave both blank for slot '+(i+1),false);return;}
    feeds.push({time,quantity:Number(amount)});
  }
  const button=document.getElementById('feederSave');button.disabled=true;
  try{
    const r=await fetch('/api/module/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({feeds})});
    const d=await r.json();if(!r.ok)throw new Error(d.error||('HTTP '+r.status));feederMessage('Schedule saved.',true);
  }catch(err){feederMessage(err.message,false);}finally{button.disabled=false;}
});
loadFeeder();
</script>
)html"));
}