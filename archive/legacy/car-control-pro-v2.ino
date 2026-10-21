#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_INA219.h>

// ==========================================
// تنظیمات شبکه وای‌فای (Access Point)
// ==========================================
const char* ssid = "Car_Control_Pro";
const char* password = "12345678password";

WebServer server(80);
Preferences preferences;

// ==========================================
// تنظیمات ماژول INA219 (سنسور ولتاژ/جریان)
// ==========================================
#define I2C_SDA 8
#define I2C_SCL 9

Adafruit_INA219 ina219;
bool ina219Connected = false;

// ==========================================
// تخصیص پین‌های ESP32-S3
// ==========================================
const byte DRIVER_UP_IN = 4; // ورودی لمسی بالا راننده (TTP223)
const byte DRIVER_DOWN_IN = 5; // ورودی لمسی پایین راننده
const byte PASS_UP_IN = 6; // ورودی لمسی بالا شاگرد
const byte PASS_DOWN_IN = 7; // ورودی لمسی پایین شاگرد

const byte DRIVER_UP_OUT = 15; // خروجی رله بالا راننده
const byte DRIVER_DOWN_OUT = 16; // خروجی رله پایین راننده
const byte PASS_UP_OUT = 17; // خروجی رله بالا شاگرد
const byte PASS_DOWN_OUT = 18; // خروجی رله پایین شاگرد

const byte LOCK_OUT = 19; // خروجی رله قفل شدن
const byte UNLOCK_OUT = 21; // خروجی رله باز شدن

// ==========================================
// پارامترهای زمانی و مدیریتی
// ==========================================
const unsigned long DEBOUNCE_DELAY = 50;
const unsigned long LONG_PRESS_MS = 350; // آستانه تشخیص دستی / اتوماتیک
unsigned long AUTO_TIME_MS = 4000; // زمان حرکت کامل شیشه (میلی‌ثانیه)
const unsigned long VENT_TIME_MS = 700; // زمان باز شدن حالت تهویه
const unsigned long DOOR_PULSE_MS = 400; // پالس قفل مرکزی

const byte RELAY_ACTIVE = HIGH;
const byte RELAY_INACTIVE = LOW;

unsigned long lastActivityTime = 0;
int eventLogId = 0;
String lastEventMsg = "";

// ==========================================
// ساختار داده‌ها و متغیرهای سیستم
// ==========================================
struct Window {
byte upOut, downOut;
bool autoRunning;
unsigned long autoStartTime;
unsigned long autoDuration;
bool manualRunning;
int currentPos; // 0 (بسته کامل) تا 100 (کاملاً باز)
int direction; // 1 (بالا/بستن), -1 (پایین/بازکردن), 0 (ثابت)
String name;
};

Window driverWindow = {DRIVER_UP_OUT, DRIVER_DOWN_OUT, false, 0, AUTO_TIME_MS, false, 0, 0, "شیشه راننده"};
Window passWindow = {PASS_UP_OUT, PASS_DOWN_OUT, false, 0, AUTO_TIME_MS, false, 0, 0, "شیشه شاگرد"};

// وضعیت قفل‌ها و سیستم
bool isLocked = false;
bool childLock = false;
bool isParked = true;

// ساختار تنظیمات ظاهر (Appearance)
struct Appearance {
String neon = "#00F5FF";
int glow = 70;
String wall = "carbon";
String wallColor = "#071018";
};
Appearance sysAppearance;

// Debounce Variables
bool lastDriverUpReading = false, driverUpStable = false;
bool lastDriverDownReading = false, driverDownStable = false;
bool lastPassUpReading = false, passUpStable = false;
bool lastPassDownReading = false, passDownStable = false;

unsigned long driverUpDebounceTime = 0, driverDownDebounceTime = 0;
unsigned long passUpDebounceTime = 0, passDownDebounceTime = 0;

// Touch Timing
unsigned long driverUpPressTime = 0, driverDownPressTime = 0;
unsigned long passUpPressTime = 0, passDownPressTime = 0;

bool driverUpHandled = false, driverDownHandled = false;
bool passUpHandled = false, passDownHandled = false;

// سیستم لاگ‌گیری
void webLog(String message) {
Serial.println(message);
lastEventMsg = message;
eventLogId++;
lastActivityTime = millis();
}

// ==========================================
// توابع کنترل رله‌ها و حرکت شیشه‌ها
// ==========================================
bool debouncePin(byte pin, bool &lastReading, bool &stableState, unsigned long &lastDebounceTime) {
bool reading = digitalRead(pin);
if (reading != lastReading) lastDebounceTime = millis();
if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) stableState = reading;
lastReading = reading;
return stableState;
}

void stopWindow(Window &win) {
if (win.autoRunning || win.manualRunning || win.direction != 0) {
webLog("[توقف] " + win.name);
}
digitalWrite(win.upOut, RELAY_INACTIVE);
digitalWrite(win.downOut, RELAY_INACTIVE);
win.autoRunning = false;
win.manualRunning = false;
win.direction = 0;
}

void startAutoCustom(Window &win, bool directionUp, unsigned long duration) {
stopWindow(win);
win.autoRunning = true;
win.autoStartTime = millis();
win.autoDuration = duration;
win.direction = directionUp ? 1 : -1;

webLog("[اتوماتیک] " + win.name + (directionUp ? " -> بالا" : " -> پایین"));

if (directionUp) {
digitalWrite(win.downOut, RELAY_INACTIVE);
digitalWrite(win.upOut, RELAY_ACTIVE);
} else {
digitalWrite(win.upOut, RELAY_INACTIVE);
digitalWrite(win.downOut, RELAY_ACTIVE);
}
}

void startManual(Window &win, bool directionUp) {
win.manualRunning = true;
win.autoRunning = false;
win.direction = directionUp ? 1 : -1;
webLog("[دستی] " + win.name + (directionUp ? " -> بالا" : " -> پایین"));

if (directionUp) {
digitalWrite(win.downOut, RELAY_INACTIVE);
digitalWrite(win.upOut, RELAY_ACTIVE);
} else {
digitalWrite(win.upOut, RELAY_INACTIVE);
digitalWrite(win.downOut, RELAY_ACTIVE);
}
}

void checkAutoTimer(Window &win) {
if (win.autoRunning) {
unsigned long elapsed = millis() - win.autoStartTime;

// محاسبه تخمینی میزان باز/بسته شدن شیشه بر حسب درصد
if (win.direction == 1) { // بالا (بستن)
int newPos = map(elapsed, 0, win.autoDuration, 100, 0);
win.currentPos = constrain(newPos, 0, 100);
} else if (win.direction == -1) { // پایین (باز کردن)
int newPos = map(elapsed, 0, win.autoDuration, 0, 100);
win.currentPos = constrain(newPos, 0, 100);
}

if (elapsed >= win.autoDuration) {
webLog("[اتمام زمان] " + win.name);
if (win.direction == 1) win.currentPos = 0;
else if (win.direction == -1) win.currentPos = 100;
stopWindow(win);
}
}
}

void triggerDoorPulse(byte pin, String actionName) {
webLog("[قفل مرکزی] " + actionName);
digitalWrite(pin, RELAY_ACTIVE);
delay(DOOR_PULSE_MS);
digitalWrite(pin, RELAY_INACTIVE);
}

void processTouchLogic(Window &win, bool isTouched, bool &handledFlag, unsigned long &pressTime, bool directionUp) {
if (isTouched) {
lastActivityTime = millis();
if (pressTime == 0) {
pressTime = millis();
handledFlag = false;
// اگر شیشه در حال حرکت بود، لمس مجدد آن را متوقف می‌کند
if (win.autoRunning || win.manualRunning) {
stopWindow(win);
handledFlag = true;
}
} else if (!handledFlag && (millis() - pressTime >= LONG_PRESS_MS)) {
startManual(win, directionUp);
handledFlag = true;
}
} else {
if (pressTime > 0) {
unsigned long holdDuration = millis() - pressTime;
if (!handledFlag && holdDuration < LONG_PRESS_MS) {
startAutoCustom(win, directionUp, AUTO_TIME_MS);
} else if (win.manualRunning) {
stopWindow(win);
}
pressTime = 0;
handledFlag = false;
}
}
}

// ==========================================
// کد کامل پنل حرفه‌ای (ذخیره‌‌شده در Flash)
// ==========================================
const char PANEL_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="fa" dir="rtl">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<title>پنل کنترل خودرو ESP32-S3</title>
<style>
:root{--bg:#eef0f3;--s1:#fff;--s2:#f3f5f8;--ink:#14181d;--mut:#69717c;--line:#dfe3e8;--acc:#e08a00;--accInk:#1a1200;--ok:#0f9e7e;--bad:#d93a2f;--g1:#bfe0ee;--g2:#7fb6d0;box-sizing:border-box}
@media (prefers-color-scheme:dark){:root:not([data-theme="light"]){--bg:#0d1014;--s1:#161a20;--s2:#1e242c;--ink:#edeef0;--mut:#8b939e;--line:#262d36;--acc:#ffb000;--ok:#3dd6b0;--bad:#ff5a4f;--g1:#3a6d86;--g2:#1f4459}}
:root[data-theme="dark"]{--bg:#0d1014;--s1:#161a20;--s2:#1e242c;--ink:#edeef0;--mut:#8b939e;--line:#262d36;--acc:#ffb000;--ok:#3dd6b0;--bad:#ff5a4f;--g1:#3a6d86;--g2:#1f4459}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
body{margin:0;background:var(--bg);color:var(--ink);font:400 15px/1.6 system-ui,sans-serif;max-width:560px;margin-inline:auto;padding:0 14px 96px}
header{position:sticky;top:0;z-index:5;display:flex;justify-content:space-between;align-items:center;padding:12px 0;background:color-mix(in srgb,var(--bg) 88%,transparent);backdrop-filter:blur(10px)}
h1{font-size:20px;font-weight:800;margin:0;line-height:1.2}
h1 small{display:block;font-size:12px;font-weight:400;color:var(--mut)}
h2{font-size:13px;font-weight:600;color:var(--mut);margin:0 0 10px}
.chip{display:inline-flex;align-items:center;gap:6px;font-size:12px;padding:4px 11px;border-radius:99px;background:var(--s1);border:1px solid var(--line)}
.dot{width:8px;height:8px;border-radius:50%;background:var(--ok)}
.card{background:var(--s1);border:1px solid var(--line);border-radius:22px;padding:16px;margin-bottom:12px}
.view{display:none}.view.on{display:block}
button{font:inherit;color:var(--ink);cursor:pointer;border:0;background:var(--s2);border-radius:14px;padding:10px 14px;touch-action:manipulation}
button:focus-visible,input:focus-visible{outline:2px solid var(--acc);outline-offset:2px}
.scenes{display:flex;gap:8px;overflow-x:auto;padding-bottom:12px}
.scenes button{white-space:nowrap;background:var(--s1);border:1px solid var(--line);border-radius:99px}
.scenes button:active{background:var(--acc);color:var(--accInk)}
.wins{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-bottom:12px}
.win{background:var(--s1);border:1px solid var(--line);border-radius:22px;padding:14px;display:flex;flex-direction:column;align-items:center;gap:10px}
.win header{all:unset;display:flex;width:100%;justify-content:space-between;align-items:center;font-weight:600}
.win header em{font-style:normal;font-size:11px;color:var(--mut)}
.glass{position:relative;width:100%;height:100px;padding:12px;border-radius:18px;background:var(--s2);overflow:hidden;touch-action:none;border:1px solid var(--line)}
.glass i{position:absolute;inset:auto 0 0 0;background:linear-gradient(160deg,var(--g1),var(--g2));border-top:3px solid var(--acc);transition:height .15s linear}
.glass b{position:absolute;inset:auto 0 10px;text-align:center;font-size:20px;font-weight:800;z-index:2}
.pad{display:flex;gap:8px;width:100%}
.pad button{flex:1;font-size:18px;padding:10px 0}
.pad button:active{background:var(--acc);color:var(--accInk)}
.row{display:flex;justify-content:space-between;align-items:center;padding:11px 0;border-bottom:1px solid var(--line)}
.row:last-child{border:0}
.row small{display:block;color:var(--mut);font-size:12px}
.sw{appearance:none;width:46px;height:28px;border-radius:99px;background:var(--s2);border:1px solid var(--line);position:relative;cursor:pointer;flex:none;transition:background .15s}
.sw::after{content:"";position:absolute;top:3px;inset-inline-start:3px;width:20px;height:20px;border-radius:50%;background:var(--mut);transition:transform .15s,background .15s}
.sw:checked{background:var(--acc)}
.sw:checked::after{background:#fff;transform:translateX(-18px)}
#log{list-style:none;margin:0;padding:0;font-size:13px}
#log li{padding:7px 0;border-bottom:1px solid var(--line);display:flex;gap:10px}
#log time{color:var(--mut);flex:none;direction:ltr;font-size:12px}
nav{position:fixed;inset-inline:0;bottom:0;display:flex;justify-content:center;padding:8px 14px;pointer-events:none;z-index:10}
nav div{pointer-events:auto;display:flex;width:100%;max-width:532px;background:var(--s1);border:1px solid var(--line);border-radius:22px;padding:5px;gap:4px;box-shadow:0 8px 30px rgba(0,0,0,.18)}
nav button{flex:1;background:none;font-size:13px;padding:10px 0;border-radius:17px;color:var(--mut)}
nav button[aria-current=page]{background:var(--acc);color:var(--accInk);font-weight:600}
:root{--neon:#ffad19;--wall:linear-gradient(145deg,#080b10 0%,#101721 52%,#17100a 100%)}
body{background:var(--wall);background-attachment:fixed}
.hero{background:linear-gradient(135deg,color-mix(in srgb,var(--s1) 92%,var(--acc)),var(--s1));border:1px solid var(--line);border-radius:22px;padding:15px;margin-bottom:12px}
.hero-title{display:flex;align-items:center;justify-content:space-between;gap:10px}
.hero-actions{display:grid;grid-template-columns:1fr 1fr 1fr;gap:7px;margin-top:13px}
#panic{background:color-mix(in srgb,var(--bad) 14%,var(--s1));color:var(--bad);border:1px solid color-mix(in srgb,var(--bad) 35%,var(--line))}
#quick-status{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin:0 0 12px}
.qs{background:var(--s1);border:1px solid var(--line);border-radius:16px;padding:10px;text-align:center}
.qs b{display:block;font-size:15px;direction:ltr}
.qs small{color:var(--mut);font-size:10px}
</style>
</head>
<body>
<header>
<h1>کنترل هوشمند خودرو<small>سیستم مرکزی ESP32-S3</small></h1>
<span class="chip"><i class="dot"></i><span id="hv">-- ولت</span></span>
</header>

<main>
<section class="view on" id="v0">
<div class="hero">
<div class="hero-title">
<div><strong>CAR CONTROL PRO</strong><div style="color:var(--mut);font-size:11px">سامانه کنترل شیشه و قفل</div></div>
<span class="chip"><i class="dot"></i><span id="connState">آنلاین</span></span>
</div>
<div class="hero-actions">
<button onclick="sendCmd('/cmd?lock=1')">🔒 بستن و قفل</button>
<button onclick="sendCmd('/cmd?vent=1')">🌬️ تهویه</button>
<button id="panic" onclick="sendCmd('/cmd?panic=1')">⛔ توقف همه</button>
</div>
</div>

<div id="quick-status">
<div class="qs"><b id="qsV">-- V</b><small>ولتاژ باتری</small></div>
<div class="qs"><b id="qsC0">0٪</b><small>شیشه راننده</small></div>
<div class="qs"><b id="qsC1">0٪</b><small>شیشه شاگرد</small></div>
</div>

<div class="scenes">
<button onclick="sendCmd('/cmd?w=0&goto=0');sendCmd('/cmd?w=1&goto=0')">بستن همه</button>
<button onclick="sendCmd('/cmd?w=0&goto=100');sendCmd('/cmd?w=1&goto=100')">باز کردن همه</button>
<button onclick="sendCmd('/cmd?vent=1')">حالت تهویه</button>
</div>

<div class="wins">
<div class="win">
<header>شیشه راننده <em id="s0">متوقف</em></header>
<div class="glass"><i id="f0" style="height:0%"></i><b id="p0">0%</b></div>
<div class="pad">
<button onclick="sendCmd('/cmd?w=0&d=-1')">▼</button>
<button onclick="sendCmd('/cmd?w=0&d=1')">▲</button>
</div>
</div>
<div class="win">
<header>شیشه شاگرد <em id="s1">متوقف</em></header>
<div class="glass"><i id="f1" style="height:0%"></i><b id="p1">0%</b></div>
<div class="pad">
<button onclick="sendCmd('/cmd?w=1&d=-1')">▼</button>
<button onclick="sendCmd('/cmd?w=1&d=1')">▲</button>
</div>
</div>
</div>

<div class="card">
<label class="row">
<div>قفل مرکزی<small>کنترل درب‌های خودرو</small></div>
<input class="sw" type="checkbox" id="swLock" onchange="sendCmd('/cmd?lock='+(this.checked?1:0))">
</label>
<label class="row">
<div>قفل کودک<small>غیرفعال کردن کلید شاگرد</small></div>
<input class="sw" type="checkbox" id="swChild" onchange="sendCmd('/cmd?child='+(this.checked?1:0))">
</label>
</div>
</section>

<section class="view" id="v1">
<div class="card">
<h2>لاگ رویدادهای سیستم</h2>
<ul id="log"></ul>
</div>
</section>
</main>

<nav>
<div id="nav">
<button onclick="setTab(0)" aria-current="page">داشبورد</button>
<button onclick="setTab(1)">رویدادها</button>
</div>
</nav>

<script>
function sendCmd(url) {
fetch(url).catch(e => console.error(e));
}

function setTab(idx) {
document.querySelectorAll('.view').forEach((v, i) => v.classList.toggle('on', i === idx));
document.querySelectorAll('#nav button').forEach((b, i) => {
if(i === idx) b.setAttribute('aria-current', 'page');
else b.removeAttribute('aria-current');
});
}

function addLog(msg) {
if(!msg) return;
const ul = document.getElementById('log');
const li = document.createElement('li');
li.innerHTML = '<time>' + new Date().toLocaleTimeString('fa-IR') + '</time><span>' + msg + '</span>';
ul.prepend(li);
if(ul.children.length > 30) ul.lastChild.remove();
}

let lastEid = -1;
function poll() {
fetch('/get_sensors')
.then(r => r.json())
.then(d => {
document.getElementById('qsV').textContent = d.v + ' V';
document.getElementById('hv').textContent = d.v + ' ولت';

document.getElementById('f0').style.height = (100 - d.o[0]) + '%';
document.getElementById('p0').textContent = d.o[0] + '%';
document.getElementById('qsC0').textContent = d.o[0] + '%';

document.getElementById('f1').style.height = (100 - d.o[1]) + '%';
document.getElementById('p1').textContent = d.o[1] + '%';
document.getElementById('qsC1').textContent = d.o[1] + '%';

document.getElementById('swLock').checked = d.lock;
document.getElementById('swChild').checked = d.child;

if(d.eid !== lastEid) {
if(lastEid >= 0 && d.ev) addLog(d.ev);
lastEid = d.eid;
}
})
.catch(() => {});
}

setInterval(poll, 400);
</script>
</body>
</html>
)rawliteral";

// ==========================================
// هندلینگ مسیرهای HTTP
// ==========================================
void handleRoot() {
server.send_P(200, "text/html", PANEL_HTML);
}

void handleCmd() {
if (server.hasArg("panic")) {
stopWindow(driverWindow);
stopWindow(passWindow);
webLog("[اضطراری] تمام عملیات متوقف شد");
}

if (server.hasArg("vent")) {
startAutoCustom(driverWindow, false, VENT_TIME_MS);
startAutoCustom(passWindow, false, VENT_TIME_MS);
}

if (server.hasArg("lock")) {
int val = server.arg("lock").toInt();
isLocked = (val == 1);
triggerDoorPulse(isLocked ? LOCK_OUT : UNLOCK_OUT, isLocked ? "LOCK" : "UNLOCK");
}

if (server.hasArg("child")) {
childLock = (server.arg("child").toInt() == 1);
webLog(childLock ? "قفل کودک فعال شد" : "قفل کودک غیرفعال شد");
}

if (server.hasArg("w")) {
int winIdx = server.arg("w").toInt();
Window &win = (winIdx == 0) ? driverWindow : passWindow;

if (server.hasArg("d")) {
int dir = server.arg("d").toInt();
if (dir == 1) startAutoCustom(win, true, AUTO_TIME_MS);
else if (dir == -1) startAutoCustom(win, false, AUTO_TIME_MS);
else stopWindow(win);
}

if (server.hasArg("goto")) {
int targetPos = server.arg("goto").toInt();
if (targetPos <= 15) startAutoCustom(win, false, AUTO_TIME_MS);
else if (targetPos >= 85) startAutoCustom(win, true, AUTO_TIME_MS);
else startAutoCustom(win, false, VENT_TIME_MS);
}
}

server.send(200, "text/plain", "OK");
}

void handleGetSensors() {
StaticJsonDocument<512> doc;

float busvoltage = 0;
if (ina219Connected) {
busvoltage = ina219.getBusVoltage_V();
} else {
busvoltage = 12.6; // مقدار پیش‌فرض در صورت عدم شناسایی سنسور
}

doc["v"] = String(busvoltage, 1);

JsonArray o = doc.createNestedArray("o");
o.add(driverWindow.currentPos);
o.add(passWindow.currentPos);

JsonArray d = doc.createNestedArray("d");
d.add(driverWindow.direction);
d.add(passWindow.direction);

doc["lock"] = isLocked;
doc["child"] = childLock;
doc["parked"] = isParked;
doc["eid"] = eventLogId;
doc["ev"] = lastEventMsg;

String json;
serializeJson(doc, json);
server.send(200, "application/json", json);
}

void handleGetConfig() {
StaticJsonDocument<256> doc;
JsonObject app = doc.createNestedObject("appearance");
app["neon"] = sysAppearance.neon;
app["glow"] = sysAppearance.glow;
app["wall"] = sysAppearance.wall;
app["wallColor"] = sysAppearance.wallColor;

String json;
serializeJson(doc, json);
server.send(200, "application/json", json);
}

void handleSetConfig() {
if (server.hasArg("plain")) {
StaticJsonDocument<256> doc;
deserializeJson(doc, server.arg("plain"));
if (doc.containsKey("appearance")) {
JsonObject app = doc["appearance"];
sysAppearance.neon = app["neon"].as<String>();
sysAppearance.glow = app["glow"].as<int>();
sysAppearance.wall = app["wall"].as<String>();
sysAppearance.wallColor = app["wallColor"].as<String>();

preferences.begin("config", false);
preferences.putString("neon", sysAppearance.neon);
preferences.putInt("glow", sysAppearance.glow);
preferences.putString("wall", sysAppearance.wall);
preferences.putString("wallColor", sysAppearance.wallColor);
preferences.end();
}
}
server.send(200, "text/plain", "OK");
}

// ==========================================
// Setup & Loop اصلی
// ==========================================
void setup() {
Serial.begin(115200);

// فراخوانی تنظیمات ذخیره‌شده در Flash
preferences.begin("config", true);
sysAppearance.neon = preferences.getString("neon", "#00F5FF");
sysAppearance.glow = preferences.getInt("glow", 70);
sysAppearance.wall = preferences.getString("wall", "carbon");
sysAppearance.wallColor = preferences.getString("wallColor", "#071018");
preferences.end();

// راه‌اندازی ارتباط I2C و ماژول INA219 روی پین‌های 8 و 9
Wire.begin(I2C_SDA, I2C_SCL);
if (ina219.begin()) {
ina219.setCalibration_32V_2A(); // کالیبراسیون برای سیستم برق خودرو
ina219Connected = true;
webLog("[سنسور] ماژول INA219 فعال شد.");
} else {
ina219Connected = false;
webLog("[خطا] ماژول INA219 شناسایی نشد!");
}

// ایجاد اکسس پوینت وای‌فای
WiFi.softAP(ssid, password);
webLog("[سیستم] شبکه Car_Control_Pro روشن شد.");

// مسیرهای وب‌سرور
server.on("/", handleRoot);
server.on("/cmd", handleCmd);
server.on("/led", handleCmd);
server.on("/get_sensors", handleGetSensors);
server.on("/get_config", handleGetConfig);
server.on("/set_config", HTTP_POST, handleSetConfig);

server.begin();

// کانفیگ پین‌های ورودی و خروجی
pinMode(DRIVER_UP_IN, INPUT);
pinMode(DRIVER_DOWN_IN, INPUT);
pinMode(PASS_UP_IN, INPUT);
pinMode(PASS_DOWN_IN, INPUT);

pinMode(DRIVER_UP_OUT, OUTPUT);
pinMode(DRIVER_DOWN_OUT, OUTPUT);
pinMode(PASS_UP_OUT, OUTPUT);
pinMode(PASS_DOWN_OUT, OUTPUT);

pinMode(LOCK_OUT, OUTPUT);
pinMode(UNLOCK_OUT, OUTPUT);

stopWindow(driverWindow);
stopWindow(passWindow);
digitalWrite(LOCK_OUT, RELAY_INACTIVE);
digitalWrite(UNLOCK_OUT, RELAY_INACTIVE);

lastActivityTime = millis();
}

void loop() {
server.handleClient();

// خواندن ورودی‌ها و دی‌باونس کلیدهای لمسی TTP223
bool driverUpState = debouncePin(DRIVER_UP_IN, lastDriverUpReading, driverUpStable, driverUpDebounceTime);
bool driverDownState = debouncePin(DRIVER_DOWN_IN, lastDriverDownReading, driverDownStable, driverDownDebounceTime);
bool passUpState = debouncePin(PASS_UP_IN, lastPassUpReading, passUpStable, passUpDebounceTime);
bool passDownState = debouncePin(PASS_DOWN_IN, lastPassDownReading, passDownDebounceTime);

// پردازش لمس شیشه راننده
processTouchLogic(driverWindow, driverUpState, driverUpHandled, driverUpPressTime, true);
processTouchLogic(driverWindow, driverDownState, driverDownHandled, driverDownPressTime, false);

// پردازش لمس شیشه شاگرد (در صورت فعال نبودن قفل کودک)
bool driverActive = driverUpState || driverDownState || driverWindow.manualRunning || driverWindow.autoRunning;
if (!driverActive && !childLock) {
processTouchLogic(passWindow, passUpState, passUpHandled, passUpPressTime, true);
processTouchLogic(passWindow, passDownState, passDownHandled, passDownPressTime, false);
} else if (childLock && (passWindow.manualRunning || passWindow.autoRunning)) {
stopWindow(passWindow);
}

// بررسی تایمر حرکت اتوماتیک
checkAutoTimer(driverWindow);
checkAutoTimer(passWindow);
}