#pragma once

const char SETUP_PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="en"><head><meta name="viewport" content="width=device-width, initial-scale=1">
<meta charset="utf-8"><title>Rootlet Wi-Fi setup</title>
<style>
body{font:16px system-ui;max-width:36rem;margin:2rem auto;padding:0 1rem;color:#25382b}
button,select,input{font:inherit;padding:.6rem;box-sizing:border-box}select,input{width:100%;margin:.4rem 0 1rem}
button{cursor:pointer}label{display:block}#message{white-space:pre-wrap}section{margin:1.5rem 0}
</style></head><body>
<h1>Rootlet Wi-Fi setup</h1>
<p>Choose a visible 2.4 GHz network. The setup network is open; use it in a trusted location.</p>
<section id="setup">
<button id="scan" type="button">Scan networks</button>
<p id="scanStatus"></p>
<form id="form">
<label for="network">Wi-Fi network</label><select id="network" required></select>
<label for="password">Password (leave blank for an open network)</label>
<input id="password" type="password" autocomplete="current-password" maxlength="64">
<button id="connect" type="submit">Connect</button>
</form></section>
<p id="message" role="status"></p>
<section id="handoff" hidden>
<p>Rootlet is connected. Switch this device to your home Wi-Fi, then open:</p>
<p><a id="ip"></a></p><p id="mdns"></p>
<p>Finish setup to turn off the open rootlet network. Your browser will disconnect from it.</p>
<button id="finish" type="button">Finish setup</button>
</section>
<script>
const $ = id => document.getElementById(id);
let scanning = false, connecting = false, requestError = '';
async function request(path, options) {
  const response = await fetch(path, options);
  const data = await response.json();
  if (!response.ok) throw new Error(data.error || 'Request failed');
  return data;
}
async function scan() {
  if (scanning) return;
  scanning = true; $('scan').disabled = true; $('connect').disabled = true; $('scanStatus').textContent = 'Scanning...';
  try {
    await request('/api/scan', {method:'POST'});
    let data;
    do {
      await new Promise(resolve => setTimeout(resolve, 1000));
      data = await request('/api/scan');
    } while (data.scanning);
    $('network').replaceChildren();
    for (const network of data.networks) {
      const option = document.createElement('option');
      option.value = network.ssid;
      option.textContent = network.ssid + ' (' + network.signal + ' dBm, ' + network.security + ')';
      option.disabled = !network.supported;
      $('network').appendChild(option);
    }
    $('scanStatus').textContent = data.networks.length ? 'Select a supported network.' : 'No visible networks found. Try scanning again.';
  } catch (error) { $('scanStatus').textContent = 'Scan failed: ' + error.message; }
  scanning = false; $('scan').disabled = false; $('connect').disabled = connecting;
}
$('scan').onclick = scan;
$('form').onsubmit = async event => {
  event.preventDefault();
  requestError = '';
  connecting = true; $('connect').disabled = true; $('message').textContent = 'Connecting...';
  try {
    await request('/api/connect', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'},
      body:new URLSearchParams({ssid:$('network').value, password:$('password').value})});
    $('password').value = '';
  } catch (error) {
    connecting = false; $('connect').disabled = false; requestError = error.message; $('message').textContent = requestError;
  }
};
$('finish').onclick = async () => {
  requestError = ''; $('finish').disabled = true;
  try {
    await request('/api/finish', {method:'POST'});
    $('message').textContent = 'Finishing setup. Switch to your home Wi-Fi and open the address above.';
  } catch (error) { requestError = error.message; $('message').textContent = requestError; }
  $('finish').disabled = false;
};
async function status() {
  try {
    const data = await request('/api/status');
    $('message').textContent = requestError || data.message;
    $('handoff').hidden = !data.ready;
    $('setup').hidden = data.ready;
    if (data.ready) {
      $('ip').href = 'http://' + data.ip + '/'; $('ip').textContent = data.ip;
      $('mdns').textContent = data.mdns ? 'Or try http://rootlet.local/ on your home network.' :
        'The rootlet.local name is unavailable; use the IP address.';
    }
    connecting = data.connecting;
    $('connect').disabled = connecting || scanning;
  } catch (error) {
    $('message').textContent = 'Waiting for Rootlet. If the connection dropped, reconnect to the rootlet network.';
  }
}
scan(); status(); setInterval(status, 2000);
</script></body></html>
)HTML";

const char MOISTURE_PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="en"><head><meta name="viewport" content="width=device-width, initial-scale=1">
<meta charset="utf-8"><title>Rootlet moisture</title>
<style>
body{font:16px system-ui;max-width:36rem;margin:2rem auto;padding:0 1rem;color:#25382b}
strong{font-size:3rem}button{font:inherit;padding:.6rem;cursor:pointer}
</style></head><body>
<h1>Rootlet</h1><p>Soil moisture</p><p><strong id="moisture">--</strong></p>
<p id="raw">Waiting for a reading...</p>
<p>100% = wet (1500 or below); 0% = dry (3000 or above).</p>
<p id="message" role="status"></p>
<button id="reset" type="button">Reset Wi-Fi</button>
<script>
let resetting = false;
async function update() {
  if (resetting) return;
  try {
    const response = await fetch('/api/moisture', {cache:'no-store'});
    if (!response.ok) throw new Error('Reading unavailable');
    const data = await response.json();
    if (resetting) return;
    document.getElementById('moisture').textContent = data.percent + '%';
    document.getElementById('raw').textContent = 'Raw ADC reading: ' + data.raw;
    document.getElementById('message').textContent = '';
  } catch (error) {
    if (!resetting) document.getElementById('message').textContent = 'Cannot reach Rootlet. Check your connection.';
  }
}
document.getElementById('reset').onclick = async () => {
  if (!confirm('Forget the saved Wi-Fi and return to setup?')) return;
  try {
    const response = await fetch('/api/reset', {method:'POST'});
    const data = await response.json();
    if (!response.ok) throw new Error(data.error || 'Reset failed');
    resetting = true;
    document.getElementById('reset').disabled = true;
    document.getElementById('message').textContent = 'Wi-Fi reset. Connect to rootlet and open 192.168.4.1.';
  } catch (error) { document.getElementById('message').textContent = error.message; }
};
update(); setInterval(update, 2000);
</script></body></html>
)HTML";
