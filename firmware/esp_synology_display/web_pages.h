#pragma once
const char WIFI_HTML[] PROGMEM = R"HTML(
<!doctype html><html><meta name="viewport" content="width=device-width,initial-scale=1"><title>WiFi setup</title>
<style>body{font:16px system-ui;background:#0b1317;color:#e8f3f1;max-width:360px;margin:40px auto;padding:20px}input,button{padding:12px;margin:8px 0;width:100%;box-sizing:border-box}button{background:#4ae2bb;border:0}</style>
<h1>Connect your display</h1><form method="post" action="/connect"><label>WiFi name<input name="ssid" required maxlength="32"></label><label>Password<input name="pass" type="password" maxlength="63"></label><button>Save and connect</button></form></html>
)HTML";
const char NAS_HTML[] PROGMEM = R"HTML(
<!doctype html><html><meta name="viewport" content="width=device-width,initial-scale=1"><title>Synology NAS display</title>
<style>body{font:15px system-ui;background:#0b1317;color:#e8f3f1;max-width:650px;margin:24px auto;padding:16px}a{color:#4ae2bb}button{padding:10px 14px;margin:5px;background:#4ae2bb;border:0;border-radius:6px}pre{white-space:pre-wrap;overflow-wrap:anywhere;background:#152329;padding:16px}textarea{width:100%;box-sizing:border-box;min-height:270px;background:#152329;color:#e8f3f1;padding:12px}#status{min-height:24px}</style>
<h1>Synology NAS display</h1><p>Volume 1 · Volume 2 · RX/TX · Average drive temperature</p><p><a href="/update">Update firmware</a></p><pre id="state">Loading…</pre>
<section aria-labelledby="brightness-heading"><h2 id="brightness-heading">Screen brightness</h2><label for="brightness">Brightness <output id="brightness-value" for="brightness">100%</output></label><input id="brightness" type="range" min="0" max="100" step="1" value="100" style="display:block;width:100%;margin:14px 0;accent-color:#4ae2bb"><p id="brightness-status" role="status">Adjusts immediately and saves after you stop dragging. 0% turns the backlight off.</p></section>
<details><summary>Test the display with sample readings</summary><p>These buttons send sample values to the physical display. Your NAS sender will replace them with its next update.</p><button id="healthy">Load healthy sample</button><button id="fault">Load storage warning sample</button><label for="payload">Snapshot (JSON, converted to a form upload)</label><textarea id="payload"></textarea><button id="send">Send to display</button></details><p id="status" role="status"></p>
<script>
const sample={nasName:'SYNOLOGY',volume1UsedTB:'5.4',volume1TotalTB:'8',volume1Status:'normal',volume2UsedTB:'1.2',volume2TotalTB:'4',volume2Status:'normal',rxMBps:'12.4',txMBps:'1.8',driveTemps:'38,36,40,34',storageWarnings:''};
const payload=document.getElementById('payload'),statusEl=document.getElementById('status');
const brightness=document.getElementById('brightness'),brightnessValue=document.getElementById('brightness-value'),brightnessStatus=document.getElementById('brightness-status');
let brightnessTimer=null,brightnessPending=null,brightnessSending=false,brightnessRevision=0;
async function sendBrightness(){
  if(brightnessSending)return;
  brightnessSending=true;
  try{
    while(brightnessPending!==null){
      const value=brightnessPending;brightnessPending=null;
      const response=await fetch('/brightness',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({value:String(value)})});
      const body=await response.json();if(!response.ok)throw Error(body.error);
      brightnessStatus.textContent='Brightness set to '+body.brightness+'%. Saved automatically after a short pause.';
    }
  }catch(error){brightnessStatus.textContent='Brightness update failed: '+error.message}
  finally{brightnessSending=false;if(brightnessPending!==null&&brightnessTimer===null)brightnessTimer=setTimeout(()=>{brightnessTimer=null;sendBrightness()},150)}
}
brightness.addEventListener('input',()=>{
  ++brightnessRevision;brightnessValue.textContent=brightness.value+'%';
  brightnessPending=Number(brightness.value);
  if(brightnessTimer===null)brightnessTimer=setTimeout(()=>{brightnessTimer=null;sendBrightness()},150);
});
document.getElementById('healthy').onclick=()=>{payload.value=JSON.stringify(sample,null,2);statusEl.textContent='Healthy sample loaded; press Send to display.'};
document.getElementById('fault').onclick=()=>{payload.value=JSON.stringify({...sample,volume2Status:'degraded',storageWarnings:'Volume 2: degraded|Drive 3: failing'},null,2);statusEl.textContent='Warning sample loaded; press Send to display.'};
document.getElementById('send').onclick=async()=>{try{const snapshot=JSON.parse(payload.value);const response=await fetch('/nas',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(snapshot)});const body=await response.json();if(!response.ok)throw Error(body.error);statusEl.textContent='Snapshot sent.';await refresh()}catch(error){statusEl.textContent=error.message}};
async function refresh(){const revision=brightnessRevision;try{const response=await fetch('/state');const body=await response.json();document.getElementById('state').textContent=JSON.stringify(body,null,2);if(revision===brightnessRevision&&!brightnessSending&&brightnessPending===null&&brightnessTimer===null&&Number.isInteger(body.brightness)){brightness.value=String(body.brightness);brightnessValue.textContent=body.brightness+'%'}}catch(error){document.getElementById('state').textContent='Display unreachable: '+error.message}}refresh();setInterval(refresh,5000);
</script></html>
)HTML";
