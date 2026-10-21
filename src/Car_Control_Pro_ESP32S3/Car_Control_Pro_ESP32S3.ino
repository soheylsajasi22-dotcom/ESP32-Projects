// ======================================================================
//  کنترلر شیشه‌بالابر پراید ۱۳۸۹ — ESP32-S3 — نسخه ۵٫۶ (معماری چهار لایه + ژست دابل/تریپل‌تپ + لاگر حرکت‌ها)
// ======================================================================
//  معماری (برای بازبین‌ها):
//   لایه ۱ ایمنی (هیچ‌وقت یاد نمی‌گیرد): حداکثر زمان رله (mx)، زمان مرده ۱۵۰ms بین جهت‌ها،
//        هیچ‌وقت دو رله یک شیشه با هم، توقف همیشه مجاز، قفل ولتاژ پایین (LOWV)،
//        واچ‌داگ سخت‌افزاری، خاموشی رله‌ها هنگام OTA.
//   لایه ۲ صف و زمان‌بندی: فرمان‌های خودکار (goto/auto/صحنه‌ها) هیچ‌وقت دو موتور را هم‌زمان
//        روشن نمی‌کنند؛ اول راننده، بعد شاگرد، با مکث SETTLE_MS بین‌شان (افت ولتاژ/جریان).
//   لایه ۳ یادگیری (محدود و قابل بازگشت): موقعیت = مدل زمان + عدم‌قطعیت σ؛ T از میانه ۳ نمونه
//        کالیبراسیون، تکمیل دستی بعد از اتو و (اختیاری) افت ولتاژ؛ اصلاح سرعت با ولتاژ.
//   لایه ۴ مشاور: فقط هشدار/پیشنهاد (سلامت موتور، کیفیت کالیبراسیون)، هرگز فرمان نمی‌دهد.
//  ناوردا (invariants):
//   I1 هیچ‌وقت RUP[i] و RDN[i] با هم HIGH نیستند.   I2 هیچ حرکتی بیش از mx ثانیه نمی‌ماند.
//   I3 در هر لحظه فقط یک موتور از مسیر خودکار/صحنه روشن است.  I4 stop همیشه کار می‌کند و صف را پاک می‌کند.
//   I5 یادگیری هرگز I1..I4 را تغییر نمی‌دهد.  I6 قطع برق وسط حرکت = موقعیت نامعلوم (σ=100).
//  API: /cmd (w,d,b,auto,goto,cal=arm|cancel|close|open|done,calreset,scene,lock,child,
//        autoLock,autoRain,tap,mx,sag,sagv,gest,gup,gdn,gtu,gtd,time)  /get_sensors /diag /test /logs /log.csv /logclr /get_config /set_config
//  تنظیم‌های اختیاری (بالای فایل): API_PIN (رمز هدر X-Key)، OTA_AUTH_USER/PASS، USE_HW_WDT، USE_MDNS.
// ======================================================================
// تک‌فایل: پنل وب داخل همین کد است. فقط همین فایل را آپلود کن (USB یا OTA از /update)
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Wire.h>
#include <ElegantOTA.h>
#include <ArduinoJson.h>
#include <NimBLEDevice.h>
#ifndef USE_HW_WDT
#define USE_HW_WDT 1      // واچ‌داگ سخت‌افزاری؛ اگر روی هسته شما کامپایل نشد صفر کنید
#endif
#ifndef USE_MDNS
#define USE_MDNS 1        // آدرس pride.local
#endif
#if USE_HW_WDT
#include <esp_task_wdt.h>
#endif
#if USE_MDNS
#include <ESPmDNS.h>
#endif
// #define API_PIN "1234"            // اگر فعال شود، /cmd و /test و /set_config هدر X-Key می‌خواهند
// #define OTA_AUTH_USER "admin"     // اگر فعال شود، صفحه /update رمز می‌خواهد
// #define OTA_AUTH_PASS "change-me"
#define I(x) String((int)(x))

const char PANEL[] PROGMEM = R"HTMLPANEL(<!DOCTYPE html>
<html lang="fa" dir="rtl">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<title>پنل شیشه‌بالابر پراید</title>

<style>
:root{--bg:#eef0f3;--s1:#fff;--s2:#f3f5f8;--ink:#14181d;--mut:#69717c;--line:#dfe3e8;--acc:#e08a00;--accInk:#1a1200;--ok:#0f9e7e;--bad:#d93a2f;--g1:#bfe0ee;--g2:#7fb6d0;
box-sizing:border-box;padding-top:env(safe-area-inset-top,0px);padding-bottom:env(safe-area-inset-bottom,0px)}
@media (prefers-color-scheme:dark){:root:not([data-theme="light"]){--bg:#0d1014;--s1:#161a20;--s2:#1e242c;--ink:#edeef0;--mut:#8b939e;--line:#262d36;--acc:#ffb000;--ok:#3dd6b0;--bad:#ff5a4f;--g1:#3a6d86;--g2:#1f4459}}
:root[data-theme="dark"]{--bg:#0d1014;--s1:#161a20;--s2:#1e242c;--ink:#edeef0;--mut:#8b939e;--line:#262d36;--acc:#ffb000;--ok:#3dd6b0;--bad:#ff5a4f;--g1:#3a6d86;--g2:#1f4459}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
html{scroll-padding-top:env(safe-area-inset-top,0px)}
body{margin:0;background:var(--bg);color:var(--ink);font:400 15px/1.6 Vazirmatn,Tahoma,sans-serif;max-width:560px;margin-inline:auto;padding:0 14px 96px}
header{position:sticky;top:env(safe-area-inset-top,0px);z-index:5;display:flex;justify-content:space-between;align-items:center;padding:12px 0;background:color-mix(in srgb,var(--bg) 88%,transparent);backdrop-filter:blur(10px)}
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
.wins{display:grid;grid-template-columns:1fr;gap:12px;margin-bottom:12px}
.win{background:var(--s1);border:1px solid var(--line);border-radius:22px;padding:14px;display:flex;flex-direction:column;align-items:center;gap:10px}
.win header{all:unset;display:flex;width:100%;justify-content:space-between;align-items:center;font-weight:600}
.win header em{font-style:normal;font-size:11px;color:var(--mut)}
.glass{position:relative;width:100%;height:auto;padding:12px 12px 46px;border-radius:22px 22px 28px 28px;background:var(--s2);overflow:hidden;touch-action:none;cursor:ns-resize;border:1px solid var(--line)}.frame{position:relative;aspect-ratio:2.13;--sh:polygon(0% 100%,0% 87%,1.5% 76%,6.5% 66.5%,13.5% 56%,24.5% 40%,36.5% 23.5%,50% 7.5%,60% 1.5%,73.5% .3%,99.4% 0%,92.7% 97.5%)}.frame>*{position:absolute;inset:0}.rim{background:var(--mut);opacity:.55;clip-path:var(--sh)}.pane{background:var(--bg);clip-path:var(--sh);transform:scale(.975)}.glass::after{content:'';position:absolute;bottom:16px;right:14%;width:46px;height:12px;border-radius:6px;background:var(--line)}.win:nth-child(2) .glass::after{right:auto;left:14%}.win:nth-child(2) .frame{transform:scaleX(-1)}
.glass i{position:absolute;inset:auto 0 0 0;background:linear-gradient(160deg,var(--g1),var(--g2));border-top:3px solid var(--acc);transition:height .1s linear}
.glass b{position:absolute;inset:auto 0 10px;text-align:center;font-size:24px;font-weight:800}
.stat{font-size:12px;color:var(--mut);min-height:20px}
.pad{display:flex;gap:8px;width:100%}
.pad button{flex:1;font-size:18px;padding:12px 0}
.pad button:active{background:var(--acc);color:var(--accInk)}
.mini{font-size:12px;padding:6px 10px;width:100%}
.row{display:flex;justify-content:space-between;align-items:center;padding:11px 0;border-bottom:1px solid var(--line)}
.row:last-child{border:0}
.row small{display:block;color:var(--mut);font-size:12px}
.sw{appearance:none;width:46px;height:28px;border-radius:99px;background:var(--s2);border:1px solid var(--line);position:relative;cursor:pointer;flex:none;transition:background .15s}
.sw::after{content:"";position:absolute;top:3px;inset-inline-start:3px;width:20px;height:20px;border-radius:50%;background:var(--mut);transition:transform .15s,background .15s}
.sw:checked{background:var(--acc)}
.sw:checked::after{background:#fff;transform:translateX(-18px)}
.gauge{display:flex;align-items:center;gap:14px}
.gauge svg{width:132px;height:132px;flex:none}
.gauge .n{direction:ltr;font-size:26px;font-weight:800;line-height:1}
.grid3{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px;text-align:center;flex:1}
.grid3 div{background:var(--s2);border-radius:14px;padding:8px 2px}
.grid3 b{display:block;direction:ltr;font-size:17px}
.grid3 small{color:var(--mut);font-size:11px}
canvas{width:100%;height:110px;display:block;margin-top:10px}
.btns{display:flex;gap:8px;margin-top:10px}
.btns button{flex:1;font-size:13px}
.warn{color:var(--bad)}
.bars{display:flex;align-items:flex-end;gap:5px;height:56px}
.bars i{flex:1;background:var(--s2);border-radius:5px 5px 2px 2px;border-top:3px solid var(--ok)}
.bars i.hi{border-color:var(--bad)}
.hl{margin:0 0 4px;font-size:13px}
input[type=range]{width:100%;accent-color:var(--acc);margin:6px 0 2px}
.seg{display:flex;background:var(--s2);border-radius:14px;padding:3px;gap:3px}
.seg button{flex:1;background:none;padding:8px}
.seg button[aria-pressed=true]{background:var(--acc);color:var(--accInk);font-weight:600}
.bar{height:8px;background:var(--s2);border-radius:9px;overflow:hidden;margin-top:10px}
.bar i{display:block;height:100%;width:0;background:var(--acc)}
#log{list-style:none;margin:0;padding:0;font-size:13px}
#log li{padding:7px 0;border-bottom:1px solid var(--line);display:flex;gap:10px}
#log time{color:var(--mut);flex:none;direction:ltr;font-size:12px}
#log .w{color:var(--bad)}
nav{position:fixed;inset-inline:0;bottom:0;display:flex;justify-content:center;padding:8px 14px calc(8px + env(safe-area-inset-bottom,0px));pointer-events:none}
nav div{pointer-events:auto;display:flex;width:100%;max-width:532px;background:var(--s1);border:1px solid var(--line);border-radius:22px;padding:5px;gap:4px;box-shadow:0 8px 30px rgba(0,0,0,.18)}
nav button{flex:1;background:none;font-size:13px;padding:10px 0;border-radius:17px;color:var(--mut)}
nav button[aria-current=page]{background:var(--acc);color:var(--accInk);font-weight:600}
#toast{position:fixed;inset-inline:16px;bottom:88px;margin-inline:auto;max-width:420px;background:var(--ink);color:var(--bg);border-radius:14px;padding:10px 14px;font-size:13px;opacity:0;transform:translateY(8px);transition:.2s;pointer-events:none;z-index:9;text-align:center}
#toast.on{opacity:1;transform:none}
nav button{font-size:12.5px}
select{font:inherit;background:var(--s2);color:var(--ink);border:0;border-radius:12px;padding:8px 10px}
input[type=color]{width:46px;height:34px;border:0;border-radius:10px;background:var(--s2);padding:2px;cursor:pointer}
.sl{display:flex;justify-content:space-between;font-size:13px;margin-top:10px}
.lc .seg{margin-top:12px}.lc .seg button{padding:8px 2px;font-size:13px}
#car{display:block;width:100%;max-width:250px;height:auto;margin:0 auto 6px}.noinst .ctl{opacity:.35;pointer-events:none}
#car .z,#side .z{stroke:var(--line);stroke-width:1;transition:fill .05s}#side{display:none;width:100%;max-width:380px;height:auto;margin:0 auto 6px}
.rn{font-size:12px;color:var(--bad);min-height:18px;margin:6px 0 0}
@media (prefers-reduced-motion:reduce){*{transition:none!important}}

/* ===== PRO DASHBOARD UI ===== */
:root{
 --bg:#070a0f;--s1:#10151d;--s2:#171e28;--ink:#f4f7fb;--mut:#8793a3;
 --line:#25303d;--acc:#ffad19;--accInk:#171000;--ok:#20d6a2;--bad:#ff5d65;
 --cyan:#3fc7ff;--glass:#0d141d;
}
:root[data-theme="light"]{
 --bg:#eef2f6;--s1:#ffffff;--s2:#f4f6f9;--ink:#10151c;--mut:#697584;
 --line:#dce3ea;--acc:#e18b00;--accInk:#171000;--ok:#0e9e7d;--bad:#d93640;
 --cyan:#087fb0;--glass:#edf3f8;
}
body{max-width:720px;padding:0 12px 108px;background:
 radial-gradient(circle at 50% -15%,rgba(255,173,25,.13),transparent 34%),var(--bg)}
header{padding:10px 2px 12px;background:color-mix(in srgb,var(--bg) 78%,transparent);border-bottom:1px solid color-mix(in srgb,var(--line) 55%,transparent)}
h1{font-size:19px;letter-spacing:-.2px}
h1 small{font-size:11px;margin-top:2px}
.card,.win{border-radius:20px;box-shadow:0 10px 30px rgba(0,0,0,.08)}
.scenes{padding:2px 0 10px}
.scenes button{font-weight:600}
.win{padding:12px;background:linear-gradient(145deg,var(--s1),color-mix(in srgb,var(--s1) 75%,var(--s2)))}
.glass{background:linear-gradient(180deg,var(--glass),var(--s2));border-radius:20px}
.pad button{height:50px;font-size:21px;font-weight:800}
.stat{font-size:11px}
.gauge{background:linear-gradient(145deg,var(--s1),var(--s2));border-radius:18px;padding:10px}
nav div{backdrop-filter:blur(18px);box-shadow:0 12px 40px rgba(0,0,0,.28)}
nav button{font-weight:600}
.chip{font-weight:700}
.dot{box-shadow:0 0 0 3px color-mix(in srgb,var(--ok) 18%,transparent)}
#quick-status{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin:0 0 12px}
.qs{background:var(--s1);border:1px solid var(--line);border-radius:16px;padding:10px;text-align:center}
.qs b{display:block;font-size:15px;direction:ltr}
.qs small{color:var(--mut);font-size:10px}
.hero{
 background:linear-gradient(135deg,color-mix(in srgb,var(--s1) 92%,var(--acc)),var(--s1));
 border:1px solid var(--line);border-radius:22px;padding:15px;margin-bottom:12px;
}
.hero-title{display:flex;align-items:center;justify-content:space-between;gap:10px}
.hero-title strong{font-size:17px}
.hero-sub{color:var(--mut);font-size:11px;margin-top:3px}
.hero-actions{display:grid;grid-template-columns:1fr 1fr 1fr;gap:7px;margin-top:13px}
.hero-actions button{font-size:12px;font-weight:700}
#panic{
 background:color-mix(in srgb,var(--bad) 14%,var(--s1));color:var(--bad);
 border:1px solid color-mix(in srgb,var(--bad) 35%,var(--line))
}
@media(min-width:600px){.wins{grid-template-columns:1fr 1fr}.win:nth-child(1){grid-column:1}.win:nth-child(2){grid-column:2}}


/* ===== ESP32-S3 PRO CUSTOMIZATION ===== */
:root{
 --neon:#ffad19;
 --wall:linear-gradient(145deg,#080b10 0%,#101721 52%,#17100a 100%);
 --neon2:color-mix(in srgb,var(--neon) 38%,transparent);
}
body{
 background:var(--wall);
 background-attachment:fixed;
}
body::before{
 content:"";position:fixed;inset:0;pointer-events:none;z-index:-1;
 background:
 radial-gradient(circle at 15% 10%,var(--neon2),transparent 25%),
 radial-gradient(circle at 85% 80%,color-mix(in srgb,var(--neon) 12%,transparent),transparent 30%);
}
.card,.win,.hero,.qs{
 box-shadow:0 0 0 1px color-mix(in srgb,var(--neon) 8%,var(--line)),
            0 12px 34px rgba(0,0,0,.18);
}
button:active,.scenes button:active,.pad button:active{
 box-shadow:0 0 20px var(--neon2);
}
.sw:checked{background:var(--neon)}
input[type=range]{accent-color:var(--neon)}
.gauge svg #arc{stroke:var(--neon)}
.hero{position:relative;overflow:hidden}
.hero::after{
 content:"";position:absolute;inset:auto -25% -70% 25%;height:140px;
 background:radial-gradient(ellipse,var(--neon2),transparent 65%);
 pointer-events:none;
}
.wall-grid{display:grid;grid-template-columns:repeat(2,1fr);gap:8px;margin-top:10px}
.wall-choice{
 min-height:76px;border:1px solid var(--line);border-radius:14px;
 padding:9px;cursor:pointer;background:var(--s2);position:relative;overflow:hidden
}
.wall-choice.active{border-color:var(--neon);box-shadow:0 0 18px var(--neon2)}
.wall-preview{height:42px;border-radius:9px;margin-bottom:5px}
.wall-choice small{font-size:10px;color:var(--mut)}
.color-row{display:flex;align-items:center;gap:10px}
.color-row input[type=color]{width:58px;height:40px}
.hex{direction:ltr;font-weight:700;font-size:12px}
.custom-note{font-size:11px;color:var(--mut);line-height:1.5}

</style>

<style id="esp32-pro-appearance-css">
.appearance-pro{margin-top:14px}.appearance-pro .setting-head{display:flex;align-items:center;justify-content:space-between;gap:10px}
.appearance-grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:10px}
.appearance-item{background:rgba(255,255,255,.035);border:1px solid rgba(255,255,255,.08);border-radius:16px;padding:11px}
.appearance-item label{display:block;font-size:11px;opacity:.72;margin-bottom:7px}
.appearance-item input[type=color]{width:100%;height:42px;border:0;border-radius:12px;background:transparent;padding:0}
.appearance-item input[type=range]{width:100%}.wall-mini{height:54px;border-radius:12px;border:1px solid rgba(255,255,255,.1);margin-top:8px}
.appearance-actions{display:flex;gap:8px;flex-wrap:wrap;margin-top:10px}.appearance-actions button{flex:1;min-width:120px}
.sync-badge{font-size:10px;padding:5px 8px;border-radius:99px;background:rgba(255,255,255,.06);border:1px solid rgba(255,255,255,.08)}
.sync-badge.ok{color:#7dffbd}.sync-badge.warn{color:#ffd36b}.hex-pro{font-family:monospace;direction:ltr;text-align:center}
@media(max-width:520px){.appearance-grid{grid-template-columns:1fr}}
</style>

<style id="perf-fix">
body{background-attachment:scroll!important}
header{backdrop-filter:none!important;-webkit-backdrop-filter:none!important;background:color-mix(in srgb,var(--bg) 96%,transparent)!important}
nav div{backdrop-filter:none!important;-webkit-backdrop-filter:none!important}
.glass i{transition:none}
</style>
</head>
<body>
<header>
  <h1>پراید ۱۳۸۹<small>کنترلر ESP32-S3</small></h1>
  <span class="chip"><i class="dot"></i><span id="hv">۱۲٫۶ ولت</span></span>
</header>

<main>
<section class="view on" id="v0">

  <div class="hero">
    <div class="hero-title">
      <div><strong>CAR CONTROL PRO</strong><div class="hero-sub">ESP32-S3 • پراید ۱۳۸۹</div></div>
      <span class="chip"><i class="dot"></i><span id="connState">آنلاین</span></span>
    </div>
    <div class="hero-actions">
      <button data-sc="close">🔒 بستن همه</button>
      <button data-sc="vent">🌬️ تهویه</button>
      <button id="panic">⛔ توقف همه</button>
    </div>
  </div>
  <div id="quick-status">
    <div class="qs"><b id="qsV">۱۲٫۶ V</b><small>ولتاژ سیستم</small></div>
    <div class="qs"><b id="qsC0">۱۰۰٪</b><small>دقت موقعیت راننده</small></div>
    <div class="qs"><b id="qsC1">۱۰۰٪</b><small>دقت موقعیت شاگرد</small></div>
  </div>
  <div class="scenes">
    <button data-sc="close">بستن همه</button><button data-sc="open">باز کردن همه</button>
    <button data-sc="vent">تهویه</button><button data-sc="park">حالت پارک</button><button data-sc="safety">حالت ایمنی</button>
  </div>
  <div class="wins" id="wins"></div>
  <div class="card" id="tg"></div>
</section>

<section class="view" id="vl">
  <div class="scenes">
    <button data-ls="off">خاموش همه</button><button data-ls="dance">رقص نور همه</button><button data-ls="flash">چشمک‌زن همه</button><button data-ls="police">عقب: پلیسی</button><button data-ls="plane">عقب: هواپیما</button>
  </div>
  <div class="card">
    <div class="seg" id="vw" style="margin-bottom:10px"><button data-vw="top" aria-pressed="true">نمای بالا</button><button data-vw="side" aria-pressed="false">نمای کنار</button></div>
<svg id="car" viewBox="0 0 200 340" aria-label="نمای بالای پراید">
<g fill="#15181c" stroke="#666" stroke-width="1"><rect x="31" y="52" width="10" height="40" rx="4"/><rect x="159" y="52" width="10" height="40" rx="4"/><rect x="31" y="232" width="10" height="40" rx="4"/><rect x="159" y="232" width="10" height="40" rx="4"/></g>
<g fill="var(--s2)" stroke="var(--mut)" stroke-width="1.2"><ellipse cx="34" cy="102" rx="7" ry="4.5"/><ellipse cx="166" cy="102" rx="7" ry="4.5"/></g>
<rect x="40" y="14" width="120" height="312" rx="24" fill="var(--s1)" stroke="var(--mut)" stroke-width="2.4"/>
<g fill="none" stroke="var(--line)" stroke-width="1.2"><path d="M54 24V86M146 24V86"/><path d="M40 183H52M148 183H160M40 250H50M150 250H160"/><rect x="54" y="140" width="92" height="98" rx="8"/></g>
<g fill="#1b2635" stroke="var(--line)" stroke-width="1.2"><path d="M50 92H150L144 140H56Z"/><path d="M56 238H144L150 274H50Z"/></g>
<g fill="var(--s2)" stroke="var(--line)" stroke-width="1.2"><rect x="54" y="112" width="92" height="14" rx="5"/><rect x="94" y="140" width="12" height="52" rx="4"/><rect x="62" y="148" width="28" height="28" rx="8"/><rect x="110" y="148" width="28" height="28" rx="8"/><rect x="62" y="196" width="34" height="26" rx="8"/><rect x="104" y="196" width="34" height="26" rx="8"/><rect x="60" y="244" width="80" height="22" rx="6"/></g>
<ellipse cx="77" cy="120" rx="10" ry="3.5" fill="none" stroke="var(--mut)" stroke-width="1.5"/>
<rect x="46" y="16" width="26" height="7" rx="3" fill="#ffb000" opacity=".85"/><rect x="128" y="16" width="26" height="7" rx="3" fill="#ffb000" opacity=".85"/>
<rect x="46" y="317" width="26" height="6" rx="3" fill="var(--bad)" opacity=".6"/><rect x="128" y="317" width="26" height="6" rx="3" fill="var(--bad)" opacity=".6"/>
<text x="100" y="10" text-anchor="middle" font-size="8" fill="var(--mut)">جلو</text><text x="100" y="338" text-anchor="middle" font-size="8" fill="var(--mut)">عقب</text>
<g id="zones"></g></svg>
<svg id="side" viewBox="0 0 300 96" aria-label="نمای کنار پراید">
<path d="M2 76.6L0 66.8L19.6 58.9L74.5 48.1L123.5 8.9L149 7L227.4 6.6L266.6 43.8L296 45.2L299.5 48.1L299 75.6L270.5 81.5L241 83.5L94 83.5L19.6 82.5Z" fill="#d8dadd" stroke="#8d939b" stroke-width="1.3" stroke-linejoin="round"/>
<g fill="#1b2635" stroke="#8d939b" stroke-width="1"><path d="M80.4 50L123.5 13.8L158.8 12.9V50.5Z"/><path d="M168.6 12.5H215.6L241 46.2L168.6 50.5Z"/></g>
<path d="M163.7 12V51M80 51H241" stroke="#8d939b" stroke-width="1.6" fill="none"/>
<g fill="#8d939b"><rect x="140" y="55" width="11" height="2.6" rx="1.3"/><rect x="204" y="55" width="11" height="2.6" rx="1.3"/><rect x="76" y="44" width="8" height="4.5" rx="2"/></g>
<rect x="1" y="59" width="8" height="5" rx="2" fill="#ffb000"/><rect x="291" y="50" width="8" height="5" rx="2" fill="#d93a2f"/>
<g fill="#15181c" stroke="#666"><circle cx="55" cy="72" r="17"/><circle cx="228" cy="72" r="17"/></g><g fill="#8b9098"><circle cx="55" cy="72" r="8"/><circle cx="228" cy="72" r="8"/></g>
<g id="zs"></g></svg>
    <label class="row"><div>ماشین ایستاده<small>افکت چراغ عقب فقط در حالت توقف مجاز است (در ماشین واقعی از ترمز دستی یا دنده)</small></div><input class="sw" type="checkbox" role="switch" id="parked" checked></label>
    <p class="rn" id="rn"></p>
  </div>
  <div id="lights"></div>
</section>

<section class="view" id="v1">
  <div class="card">
    <h2>ولتاژ سیستم (INA219، فقط ولتاژ)</h2>
    <div class="gauge"><div class="grid3" style="grid-template-columns:1fr 1fr"><div><b id="v">۰</b><small>ولت</small></div><div><b id="vs">—</b><small>وضعیت باتری</small></div></div></div>
    <canvas id="ch" aria-label="نمودار ولتاژ"></canvas>
    <div class="grid3" style="margin-top:10px"><div><b id="vmin">—</b><small>کمینه</small></div><div><b id="vmax">—</b><small>بیشینه</small></div><div><b id="vavg">—</b><small>میانگین</small></div></div>
    <p class="stat" id="hint"></p>
  </div>
  <div class="card">
    <h2>تخمین موقعیت شیشه‌ها</h2>
    <div id="health"></div>
  </div>
  <div class="card">
    <h2>یادگیری رفتار راننده</h2>
    <div id="learn"></div>
  </div>
</section>

<section class="view" id="v2">
  <div class="card">
    <h2>ظاهر اپلیکیشن</h2>
    <div class="row">
      <div>رنگ نئون<small>رنگ اصلی دکمه‌ها، خطوط و افکت‌های داشبورد</small></div>
      <div class="color-row">
        <input id="neonColor" type="color" value="#ffad19">
        <span class="hex" id="neonHex">#FFAD19</span>
      </div>
    </div>
    <div class="row" style="display:block">
      <div>والپیپر داشبورد<small>سبک و داخلی؛ بدون دانلود از اینترنت</small></div>
      <div class="wall-grid" id="wallpapers">
        <button class="wall-choice" data-wall="dark">
          <div class="wall-preview" style="background:linear-gradient(145deg,#070a0f,#17202b)"></div><small>Dark Carbon</small>
        </button>
        <button class="wall-choice" data-wall="carbon">
          <div class="wall-preview" style="background:repeating-linear-gradient(45deg,#0b1016 0 4px,#111923 4px 8px)"></div><small>Carbon</small>
        </button>
        <button class="wall-choice" data-wall="blue">
          <div class="wall-preview" style="background:radial-gradient(circle at 50% 0,#12354b,#070a0f 70%)"></div><small>Blue Night</small>
        </button>
        <button class="wall-choice" data-wall="amber">
          <div class="wall-preview" style="background:radial-gradient(circle at 70% 30%,#49300b,#080a0f 68%)"></div><small>Amber Garage</small>
        </button>
        <button class="wall-choice" data-wall="light">
          <div class="wall-preview" style="background:linear-gradient(145deg,#f7f9fb,#dce4ec)"></div><small>Light</small>
        </button>
        <button class="wall-choice" data-wall="custom">
          <div class="wall-preview" id="customPreview" style="background:#111"></div><small>رنگ دلخواه</small>
        </button>
      </div>
      <div class="row" style="margin-top:8px">
        <div>رنگ والپیپر دلخواه<small>برای حالت رنگ دلخواه</small></div>
        <input id="wallColor" type="color" value="#101721">
      </div>
      <p class="custom-note">تنظیمات ظاهر در حافظه مرورگر ذخیره می‌شوند و برای اجرای آفلاین روی ESP32 مناسب‌اند.</p>
    </div>
  </div>

  <div class="card">
    <h2>حساسیت و زمان‌بندی</h2>
        <div class="row" style="display:block"><div style="display:flex;justify-content:space-between">مرز لمس سریع و طولانی<b id="l1"></b></div><input type="range" id="tap" min="60" max="600" step="10" value="100"></div>
    <div class="row" style="display:block"><div style="display:flex;justify-content:space-between">حداکثر زمان کار رله<b id="l2"></b></div><input type="range" id="mx" min="5" max="20" step="1" value="12"></div>
  </div>
  <div class="card"><h2>ژست دابل‌تپ (پنل راننده)</h2>
    <div class="row"><div>فعال<small>دو تپ سریع روی ماژول شیشه راننده، سناریو را فعال می‌کند</small></div><input class="sw" type="checkbox" id="gestSw" checked></div>
    <div class="row"><div>دابل‌تپ بالا</div><select id="gup"><option value="0">پارک (بستن همه + قفل)</option><option value="1">بستن همه</option><option value="2">حالت ایمنی</option></select></div>
    <div class="row"><div>دابل‌تپ پایین</div><select id="gdn"><option value="0">تهویه</option><option value="1">باز کردن همه</option><option value="2">حالت ایمنی</option></select></div>
    <div class="row"><div>سه‌تپ بالا</div><select id="gtu"><option value="0">غیرفعال</option><option value="1">باز کردن همه</option><option value="2">بستن همه</option><option value="3">تهویه</option><option value="4">پارک</option><option value="5">حالت ایمنی</option></select></div>
    <div class="row"><div>سه‌تپ پایین</div><select id="gtd"><option value="0">غیرفعال</option><option value="1">باز کردن همه</option><option value="2">بستن همه</option><option value="3">تهویه</option><option value="4">پارک</option><option value="5">حالت ایمنی</option></select></div>
    <p class="custom-note">هر تپ باید حدود نیم‌ثانیه بعد از قبلی باشد؛ دیرتر از آن، تپ مثل قبل شیشه را متوقف می‌کند. سه‌تپ، سناریوی دابل‌تپ را (که از تپ دوم شروع شده) عوض می‌کند.</p></div>
  <div class="card"><h2>کالیبراسیون زمان شیشه</h2>
    <p class="custom-note">ابتدا شیشه را تا انتهای جهت مخالف ببرید. دکمه را بزنید تا شیشه حرکت کند و لحظه‌ای که کاملاً رسید «رسید» را بزنید.</p>
    <div id="calbox"></div></div>
  <div class="card"><h2>تم</h2><div class="seg" id="theme"><button data-t="auto" aria-pressed="true">خودکار</button><button data-t="light" aria-pressed="false">روشن</button><button data-t="dark" aria-pressed="false">تاریک</button></div></div>
  <div class="card">
    <h2>سیستم</h2>
    <div class="row"><div>نسخه فرم‌ور<small id="fw">۱٫۲٫۰</small></div><button id="ota">به‌روزرسانی OTA</button></div>
    <div class="bar" hidden id="ob"><i id="oi"></i></div>
    <div class="row"><div>آدرس<small style="direction:ltr;text-align:right">192.168.4.1</small></div><span class="chip"><i class="dot"></i>متصل</span></div>
    <div class="row"><div>زمان روشن بودن<small id="up">۰</small></div></div>
  </div>

<div class="card appearance-pro">
  <div class="setting-head"><b>🎨 ظاهر حرفه‌ای و ESP32</b><span id="espSyncBadge" class="sync-badge warn">حافظه محلی</span></div>
  <div class="appearance-grid">
    <div class="appearance-item">
      <label>رنگ نئون اصلی</label>
      <input id="neonPickerPro" type="color" value="#00F5FF">
      <input id="neonHexPro" class="hex-pro" value="#00F5FF" maxlength="7" inputmode="text" style="width:100%;margin-top:7px">
    </div>
    <div class="appearance-item">
      <label>شدت نور نئون</label>
      <input id="glowPro" type="range" min="0" max="100" value="70">
      <div id="glowValPro" style="font-size:11px;opacity:.7;text-align:center">70%</div>
    </div>
    <div class="appearance-item">
      <label>والپیپر آماده</label>
      <select id="wallPro" style="width:100%;height:42px;border-radius:12px;padding:0 10px">
        <option value="carbon">Carbon Garage</option><option value="blue">Blue Night</option>
        <option value="amber">Amber Garage</option><option value="graphite">Graphite</option>
        <option value="light">Light</option><option value="custom">رنگ دلخواه</option>
      </select>
      <div id="wallPreviewPro" class="wall-mini"></div>
    </div>
    <div class="appearance-item">
      <label>رنگ والپیپر دلخواه</label>
      <input id="wallColorPro" type="color" value="#071018">
      <div style="font-size:10px;opacity:.58;margin-top:5px">برای حالت «رنگ دلخواه»</div>
    </div>
  </div>
  <div class="appearance-actions">
    <button class="btn" onclick="saveAppearanceESP()">💾 ذخیره روی ESP32</button>
    <button class="btn" onclick="loadAppearanceESP()">🔄 دریافت از ESP32</button>
    <button class="btn" onclick="resetAppearanceESP()">↩️ پیش‌فرض</button>
  </div>
  <div style="font-size:10px;opacity:.55;margin-top:8px">در صورت فعال بودن API تنظیمات، این ظاهر روی خود ESP32 ذخیره می‌شود و بین گوشی‌ها نیز قابل بازیابی است.</div>
</div>
</section>

<section class="view" id="v3"><div class="card"><h2>لاگ رویدادها</h2><ul id="log"></ul></div></section>
<section class="view" id="vd">
  <div class="card"><h2>ورودی‌های لمسی</h2><p class="custom-note" id="dgmsg"></p><div id="dgt"></div>
  <p class="custom-note">«نویز کوتاه» = تعداد دفعاتی که ورودی کمتر از ۶۰ میلی‌ثانیه بالا رفته و رد شده. عدد زیاد یعنی پایه شناور یا نویز.</p></div>
  <div class="card"><h2>تست رله‌ها</h2><div class="btns" id="dgr" style="flex-wrap:wrap"></div>
  <p class="custom-note">هر دکمه رله را ۰٫۳ ثانیه روشن می‌کند و شیشه کمی حرکت می‌کند. فقط وقتی شیشه‌ها ایستاده‌اند کار می‌کند.</p><div id="dgo"></div></div>
  <div class="card"><h2>سیستم</h2><div id="dgs"></div></div>
  <div class="card"><h2>لاگ دستگاه</h2><pre id="dglog" style="direction:ltr;font-size:11px;white-space:pre-wrap;margin:0"></pre></div>
  <div class="card"><h2>لاگر حرکت‌ها (برای تحلیل)</h2>
    <p class="custom-note" id="lgInfo">—</p>
    <div class="btns"><a id="lgDl" href="/log.csv" download="pride_log.csv" style="flex:1;text-decoration:none"><button type="button" style="width:100%">⬇️ دانلود CSV</button></a><button id="lgClr" type="button">🗑️ پاک کردن</button></div>
    <pre id="lgPre" style="direction:ltr;font-size:10.5px;white-space:pre-wrap;margin:10px 0 0"></pre>
    <p class="custom-note">هر حرکت یک سطر است (حداکثر ۱۲۰ حرکت آخر). بعد از هر چند روز دانلود کنید و فایل را برای تحلیل بفرستید.</p></div>
</section>
</main>

<nav><div id="nav">
  <button data-v="0" aria-current="page">کنترل</button><button data-v="1">چراغ‌ها</button><button data-v="2">سنسورها</button><button data-v="3">تنظیمات</button><button data-v="4">لاگ</button><button data-v="5">عیب‌یابی</button>
</div></nav>
<div id="toast" role="status"></div>

<script>
(()=>{const f=window.fetch.bind(window);let k='',ask=0;try{k=sessionStorage.getItem('pk')||''}catch(e){}
window.fetch=(u,o)=>{o=Object.assign({},o||{});o.headers=Object.assign({},o.headers||{},{'X-Key':k});
 return f(u,o).then(r=>{if(r.status===401&&Date.now()-ask>15000){ask=Date.now();const q=prompt('PIN');if(q){k=q;try{sessionStorage.setItem('pk',q)}catch(e){}}}return r})}})();
const $=id=>document.getElementById(id),fa=n=>String(n).replace(/\d/g,d=>'۰۱۲۳۴۵۶۷۸۹'[d]).replace('.','٫');
const S={w:[0,1].map(()=>({o:0,d:0,rev:0,sum:0,n:0,run:0})),tgt:[null,null],pinch:false,rain:false,thr:7,tap:100,mx:12,
 lock:true,safe:true,child:false,autoLock:true,autoRain:true,
 runs:[[3.2,3.3,3.2,3.4,3.3,3.4],[3.1,3.2,3.5,3.7,3.9,4.1]]};
const NAMES=['راننده','شاگرد'],N=120,hist=Array(N).fill(0);
let tt;function toast(m){const t=$('toast');t.textContent=m;t.classList.add('on');clearTimeout(tt);tt=setTimeout(()=>t.classList.remove('on'),2200)}
function log(m,bad,tst){const li=document.createElement('li');if(bad)li.className='w';li.innerHTML='<time>'+new Date().toLocaleTimeString('fa-IR',{hour:'2-digit',minute:'2-digit',second:'2-digit'})+'</time><span></span>';li.lastChild.textContent=m;$('log').prepend(li);while($('log').children.length>50)$('log').lastChild.remove();if(tst||bad)toast(m)}

// professional quick controls
$('panic').addEventListener('click',()=>{
  S.tgt=[null,null];
  S.w.forEach(w=>{w.d=0;w.rev=0});
  if(REAL){net('/cmd?w=0&d=0');net('/cmd?w=1&d=0')}
  log('توقف اضطراری — تمام فرمان‌های شیشه قطع شد',true,true);
});

// windows
$('wins').innerHTML=NAMES.map((n,i)=>`<div class="win"><header>${n}<em id="s${i}"></em></header>
<div class="glass" id="gl${i}" aria-label="کشیدن برای تعیین موقعیت شیشه ${n}"><div class="frame"><u class="rim"></u><div class="pane"><i id="f${i}"></i></div></div><b id="p${i}"></b></div>
<div class="pad"><button data-w="${i}" data-d="-1" aria-label="پایین ${n}">▼</button><button data-w="${i}" data-d="1" aria-label="بالا ${n}">▲</button></div>
${i?'<button class="mini" id="ptouch">لمس فیزیکی پنل شاگرد</button>':'<span class="stat">شیشه را بکش تا موقعیت بگیرد</span>'}</div>`).join('');
document.querySelectorAll('.pad button').forEach(b=>{
 let t0=0,hb=0;const w=+b.dataset.w,d=+b.dataset.d;
 b.addEventListener('pointerdown',e=>{e.preventDefault();b.setPointerCapture(e.pointerId);t0=performance.now();S.tgt[w]=null;Object.assign(S.w[w],{d,rev:0});if(REAL){const u='/cmd?w='+w+'&d='+d+'&b=1';net(u);clearInterval(hb);hb=setInterval(()=>net(u),250)}});
 const up=()=>{if(!t0)return;const dt=performance.now()-t0;t0=0;
  clearInterval(hb);if(dt<Math.max(S.tap,250)){if(REAL)net('/cmd?w='+w+'&auto=1');log('لمس سریع '+NAMES[w]+' — اتوماتیک')}else{S.w[w].d=0;if(REAL)net('/cmd?w='+w+'&d=0');log('لمس طولانی '+NAMES[w]+' — دستی')}};
 b.addEventListener('pointerup',up);b.addEventListener('pointercancel',up);
});
[0,1].forEach(i=>{let lg=0;const g=$('gl'+i),set=e=>{const r=g.querySelector('.pane').getBoundingClientRect();S.tgt[i]=Math.round(Math.min(100,Math.max(0,(e.clientY-r.top)/r.height*100)));S.w[i].rev=0;if(REAL&&Date.now()-lg>150){lg=Date.now();net('/cmd?w='+i+'&goto='+S.tgt[i])}};
 let on=false;g.addEventListener('pointerdown',e=>{on=true;g.setPointerCapture(e.pointerId);set(e)});
 g.addEventListener('pointermove',e=>{if(on)set(e)});g.addEventListener('pointerup',()=>{on=false;if(REAL&&S.tgt[i]!==null)net('/cmd?w='+i+'&goto='+S.tgt[i])});g.addEventListener('pointercancel',()=>on=false)});
$('ptouch').addEventListener('click',()=>{if(S.child)return log('قفل کودک فعال است — لمس پنل شاگرد نادیده گرفته شد',false,true);S.tgt[1]=0;log('لمس فیزیکی شاگرد — بستن اتوماتیک')});
// toggles
const TG=[['lock','قفل مرکزی','ورودی دربها را قفل می‌کند'],['child','قفل کودک','غیرفعال‌کردن لمس پنل شاگرد'],['autoLock','بستن با قفل‌کردن','شیشه‌ها با قفل شدن بسته شوند'],['autoRain','بستن با باران','با تشخیص باران شیشه‌ها بسته شوند'],['sag','توقف و یادگیری با افت ولتاژ','آزمایشی: تشخیص لبه مسیر از افت ناگهانی ولتاژ INA219']];
$('tg').innerHTML=TG.map(([k,t,s])=>`<label class="row"><div>${t}<small>${s}</small></div><input class="sw" type="checkbox" role="switch" data-k="${k}" ${S[k]?'checked':''}></label>`).join('');
function setT(k,v){S[k]=v;document.querySelector(`[data-k=${k}]`).checked=v}
document.querySelectorAll('.sw[data-k]').forEach(c=>c.addEventListener('change',()=>{const k=c.dataset.k;S[k]=c.checked;log(TG.find(x=>x[0]===k)[1]+': '+(c.checked?'روشن':'خاموش'));if(REAL){net('/cmd?'+k+'='+(+c.checked));if(k==='lock'&&c.checked&&S.autoLock)[0,1].forEach(i=>net('/cmd?w='+i+'&goto=0'))}if(k==='lock'&&c.checked&&S.autoLock){S.tgt=[0,0];log('بستن خودکار شیشه‌ها همزمان با قفل')}}));
// scenes
const SC={close:()=>{S.tgt=[0,0];toast('بستن همه شیشه‌ها')},open:()=>{S.tgt=[100,100];toast('باز کردن همه شیشه‌ها')},vent:()=>{S.tgt=[15,15];toast('تهویه: ۱۵٪ باز')},park:()=>{S.tgt=[0,0];setT('lock',true);toast('حالت پارک: شیشه‌ها بسته و قفل فعال')},safety:()=>{toast('حالت ایمنی: راننده ۵ ثانیه بالا، شاگرد ۵ ثانیه بالا، سپس هرکدام ۲ ثانیه پایین')}};
document.querySelectorAll('[data-sc]').forEach(b=>b.addEventListener('click',()=>{SC[b.dataset.sc]();S.w.forEach(w=>w.rev=0);if(REAL){S.tgt=[null,null];net('/cmd?scene='+b.dataset.sc)}log('صحنه: '+b.textContent)}));
// tabs
document.querySelectorAll('#nav button').forEach(b=>b.addEventListener('click',()=>{document.querySelectorAll('.view').forEach((v,i)=>v.classList.toggle('on',i==b.dataset.v));document.querySelectorAll('#nav button').forEach(x=>x.removeAttribute('aria-current'));b.setAttribute('aria-current','page');scrollTo(0,0)}));

/* ===== local ESP32-safe appearance settings ===== */
const WALLS={
 dark:'linear-gradient(145deg,#080b10 0%,#101721 52%,#17100a 100%)',
 carbon:'repeating-linear-gradient(45deg,#080d13 0 5px,#101720 5px 10px)',
 blue:'radial-gradient(circle at 50% 0,#12354b,#070a0f 72%)',
 amber:'radial-gradient(circle at 70% 30%,#49300b,#080a0f 68%)',
 light:'linear-gradient(145deg,#f7f9fb,#dce4ec)'
};
function saveLook(){
 localStorage.setItem('car_neon',$('neonColor').value);
 localStorage.setItem('car_wall',$('wallChoice')?.value||'dark');
 localStorage.setItem('car_wall_color',$('wallColor').value);
}
function applyLook(){
 const neon=localStorage.getItem('car_neon')||'#ffad19';
 const wall=localStorage.getItem('car_wall')||'dark';
 const wc=localStorage.getItem('car_wall_color')||'#101721';
 document.documentElement.style.setProperty('--neon',neon);
 document.documentElement.style.setProperty('--acc',neon);
 document.documentElement.style.setProperty('--wall',
   wall==='custom'?wc:(WALLS[wall]||WALLS.dark));
 $('neonColor').value=neon;$('neonHex').textContent=neon.toUpperCase();
 $('wallColor').value=wc;
 document.querySelectorAll('.wall-choice').forEach(b=>b.classList.toggle('active',b.dataset.wall===wall));
 $('customPreview').style.background=wc;
}
function chooseWall(w){
 localStorage.setItem('car_wall',w);
 applyLook();
}
const wallChoice={value:localStorage.getItem('car_wall')||'dark'};
Object.defineProperty(window,'wallChoice',{value:wallChoice});
$('neonColor').addEventListener('input',e=>{
 document.documentElement.style.setProperty('--neon',e.target.value);
 document.documentElement.style.setProperty('--acc',e.target.value);
 $('neonHex').textContent=e.target.value.toUpperCase();
 localStorage.setItem('car_neon',e.target.value);
});
$('wallColor').addEventListener('input',e=>{
 wallChoice.value='custom';
 localStorage.setItem('car_wall','custom');
 localStorage.setItem('car_wall_color',e.target.value);
 applyLook();
});
document.querySelectorAll('.wall-choice').forEach(b=>b.addEventListener('click',()=>chooseWall(b.dataset.wall)));
applyLook();

// settings
function lab(){$('l1').textContent=fa(S.tap)+' ms';$('l2').textContent=fa(S.mx)+' ثانیه'}
[['tap','tap'],['mx','mx']].forEach(([id,k])=>$(id).addEventListener('input',e=>{S[k]=+e.target.value;lab();if(REAL&&(k==='tap'||k==='mx'))net('/cmd?'+k+'='+S[k])}));lab();
document.querySelectorAll('#theme button').forEach(b=>b.addEventListener('click',()=>{const t=b.dataset.t;t==='auto'?document.documentElement.removeAttribute('data-theme'):document.documentElement.setAttribute('data-theme',t);document.querySelectorAll('#theme button').forEach(x=>x.setAttribute('aria-pressed',x===b))}));
$('ota').addEventListener('click',()=>{$('ob').hidden=false;let p=0;$('ota').disabled=true;const iv=setInterval(()=>{p+=4;$('oi').style.width=p+'%';if(p>=100){clearInterval(iv);$('fw').textContent='۱٫۳٫۰';$('ota').disabled=false;setTimeout(()=>$('ob').hidden=true,600);log('OTA کامل شد — نسخه ۱٫۳٫۰',false,true)}},120)});
// sim: تخمین موقعیت از روی زمان روشن بودن رله + دقت (اطمینان)
const t0=Date.now();let tk=0;
S.T=[[5,3],[5,3]];S.cal=[0,0];S.calT=[0,0];
S.w.forEach(w=>{w.c=100;w.p0=0;w.run=0;w.atend=false;w.prevd=0});
const vh=Array(N).fill(12.6);
function bs(v){return v>=13.2?'شارژ (موتور روشن)':v>=12.2?'خوب':v>=11.6?'متوسط':'ضعیف'}
function calQ(i){if(!REAL||typeof DEV==='undefined'||!DEV||!DEV.q)return '';return `<br><small style="color:var(--mut)">کیفیت کالیبراسیون: بستن <b>${fa(DEV.q[i][0])}٪</b> (${fa(DEV.n[i][0])} از ۳ نمونه) • باز شدن <b>${fa(DEV.q[i][1])}٪</b> (${fa(DEV.n[i][1])} از ۳ نمونه)</small>`+((DEV.hr&&(DEV.hr[i][0]>=1.25||DEV.hr[i][1]>=1.25))?`<br><small class="warn">هشدار سلامت: زمان حرکت نسبت به ابتدا ۲۵٪ یا بیشتر شده — ریل یا موتور را بررسی کنید</small>`:'')}
function health(){if(!$('v1').classList.contains('on'))return;$('health').innerHTML=NAMES.map((n,i)=>`<p class="hl">${n}: بستن <b>${fa(S.T[i][0].toFixed(1))}</b> ثانیه • باز شدن <b>${fa(S.T[i][1].toFixed(1))}</b> ثانیه • دقت <b>${fa(Math.round(S.w[i].c))}٪</b>${calQ(i)}</p>`).join('')+'<p class="custom-note">موقعیت تخمینی است: از زمان روشن بودن رله محاسبه می‌شود. با رسیدن به انتهای مسیر دقت ۱۰۰٪ می‌شود و با هر توقف وسط راه کمتر. زمان‌ها از کالیبراسیون و نگه‌داشتن‌های کامل شما به‌روز می‌شوند.</p>'}
$('calbox').innerHTML=NAMES.map((n,i)=>`<div class="row" style="display:block"><div style="display:flex;justify-content:space-between">${n}<small id="ct${i}"></small></div><div class="btns" style="flex-wrap:wrap"><button data-cal="${i}:close"></button><button data-cal="${i}:open"></button><button data-arm="${i}" style="flex:1 1 100%"></button></div></div>`).join('');
function calUI(){NAMES.forEach((n,i)=>{const a=S.cal[i];document.querySelector(`[data-cal="${i}:close"]`).textContent=a===1?'✓ رسید (کامل بسته شد)':'ثبت زمان بستن';document.querySelector(`[data-cal="${i}:open"]`).textContent=a===2?'✓ رسید (کامل باز شد)':'ثبت زمان باز شدن';{const bb=document.querySelector(`[data-arm="${i}"]`);if(bb)bb.textContent=a===3?'⏳ منتظر لمس… (برای لغو بزنید)':'کالیبراسیون با لمس (نگه‌داشتن دستی)'}$('ct'+i).textContent=fa(S.T[i][0].toFixed(1))+' / '+fa(S.T[i][1].toFixed(1))+' ث'})}
document.querySelectorAll('[data-cal]').forEach(b=>b.addEventListener('click',()=>{const [i,k]=b.dataset.cal.split(':'),w=+i,want=k==='close'?1:2,act=S.cal[w];
 if(act===want){if(REAL)net('/cmd?w='+w+'&cal=done');else{const obs=Math.max(1,(Date.now()-S.calT[w])/1000-.25);S.T[w][want-1]=+obs.toFixed(2);Object.assign(S.w[w],{d:0,o:want===1?0:100,c:100});S.cal[w]=0}log('کالیبراسیون '+NAMES[w]+': ثبت شد',false,true)}
 else if(!act){if(REAL)net('/cmd?w='+w+'&cal='+k);else{S.cal[w]=want;S.calT[w]=Date.now();S.tgt[w]=null;S.w[w].d=want===1?1:-1}log('کالیبراسیون '+NAMES[w]+': شیشه حرکت می‌کند؛ هنگام رسیدن «رسید» را بزنید',false,true)}}));
setInterval(()=>{
 tk++;let v=12.6;
 if(REAL&&DEV){S.w.forEach((w,i)=>{w.o=DEV.o[i];w.d=DEV.d[i];w.c=DEV.c?DEV.c[i]:100});v=DEV.v;if(DEV.cal)S.cal=DEV.cal;if(DEV.T)S.T=DEV.T}
 else if(!REAL){
  S.w.forEach((w,i)=>{
   if(S.tgt[i]!==null){if(S.tgt[i]>0&&S.tgt[i]<100&&Math.abs(w.o-S.tgt[i])<=1){w.d=0;S.tgt[i]=null}else w.d=w.o>S.tgt[i]?1:-1}
   if(w.d){
    if(!w.prevd){w.p0=w.o;w.atend=false;w.run=0}
    w.o=Math.min(100,Math.max(0,w.o-w.d*.1/S.T[i][w.d>0?0:1]*100));w.run++;
    if((w.o<=0&&w.d>0)||(w.o>=100&&w.d<0)){w.c=100;w.atend=true;S.tgt[i]=null;if(!S.cal[i]){w.d=0;log(NAMES[i]+': به انتها رسید — موقعیت همگام شد')}}
    else if(w.run>S.mx*10){w.d=0;S.tgt[i]=null;log(NAMES[i]+': تایم‌اوت رله — قطع اجباری',true)}
   }
   if(w.prevd&&!w.d&&!w.atend)w.c=Math.max(20,w.c-(8+.15*Math.abs(w.o-w.p0)));
   w.prevd=w.d});
  v=12.6-(S.w.some(w=>w.d)?.4:0)+(Math.random()-.5)*.05}
 vh.push(v);vh.shift();
 S.w.forEach((w,i)=>{$('f'+i).style.height=(100-w.o)+'%';$('p'+i).textContent='≈ '+fa(Math.round(w.o))+'٪ باز';
  $('s'+i).textContent=(w.d>0?'در حال بسته شدن':w.d<0?'در حال باز شدن':'متوقف')+' • دقت '+fa(Math.round(w.c))+'٪'+(REAL&&DEV&&DEV.u?' • ±'+fa(DEV.u[i].toFixed(0))+'٪':'');
  $('qsC'+i).textContent=fa(Math.round(w.c))+'٪'});
 $('hv').textContent=fa(v.toFixed(1))+' ولت';$('qsV').textContent=fa(v.toFixed(1))+' V';
 $('connState').textContent=REAL?(DEV?'آنلاین':'قطع ارتباط'):'دمو';
 if(tk%3===0){
  $('v').textContent=fa(v.toFixed(1));$('vs').textContent=bs(v);
  $('vmin').textContent=fa(Math.min(...vh).toFixed(1));$('vmax').textContent=fa(Math.max(...vh).toFixed(1));$('vavg').textContent=fa((vh.reduce((a,b)=>a+b)/N).toFixed(1));
  $('hint').textContent=v<11.6?'ولتاژ باتری پایین است؛ شارژ یا موتور را بررسی کنید':'';
  $('up').textContent=fa(Math.floor((Date.now()-t0)/1000))+' ثانیه';
  calUI();health();
  if($('v1').classList.contains('on'))plot();
 }
},100);
function plot(){
 const c=$('ch'),r=devicePixelRatio||1,W=c.clientWidth,H=c.clientHeight;if(c.width!==W*r){c.width=W*r;c.height=H*r}
 const x=c.getContext('2d'),cs=getComputedStyle(document.documentElement),y=a=>H-4-Math.min(Math.max((a-10)/5,0),1)*(H-8);
 x.setTransform(r,0,0,r,0,0);x.clearRect(0,0,W,H);
 x.strokeStyle=cs.getPropertyValue('--mut');x.lineWidth=1;x.setLineDash([4,4]);x.beginPath();x.moveTo(0,y(11.6));x.lineTo(W,y(11.6));x.stroke();x.setLineDash([]);
 x.strokeStyle=cs.getPropertyValue('--acc');x.lineWidth=2;x.lineJoin='round';x.beginPath();
 vh.forEach((a,i)=>{const px=i/(N-1)*W;i?x.lineTo(px,y(a)):x.moveTo(px,y(a))});x.stroke();
}
// lights
const LK=[['int','ریسه انبه‌ای زیر آینه','۵ سانتی‌متر، ۱۲ ولت، وسط ماشین',1],['sq','ال‌ای‌دی قرمز مربعی','توخالی، محیط ۸ سانتی‌متر، ۱۲ ولت، وسط سقف',1],['rear','چراغ آنتی‌هدلایت عقب','دو ال‌ای‌دی سفید، پشت چراغ‌های قرمز عقب',2],['foot','چراغ زیرپایی','هنوز نصب نشده',4]];
const L={parked:true,
 int:{inst:true,on:true,mode:'steady',br:80,sp:5,pat:'wave',col:'#ffa62b'},
 sq:{inst:true,on:false,mode:'steady',br:100,sp:5,pat:'wave',col:'#ff2a2a'},
 rear:{inst:true,on:false,mode:'steady',br:100,sp:5,pat:'chase',col:'#ffffff'},
 foot:{inst:false,on:false,mode:'steady',br:60,sp:5,pat:'wave',col:'#3aa0ff',rb:false}};
const MODES=[['steady','ثابت'],['flash','چشمک‌زن'],['dance','رقص نور']],RMODES=[MODES[0],MODES[1],['police','پلیسی'],['plane','هواپیما'],MODES[2]],MN={steady:'ثابت',flash:'چشمک‌زن',dance:'رقص نور',police:'پلیسی',plane:'چراغ هواپیما'};
const NM=k=>LK.find(x=>x[0]===k)[1];
$('lights').innerHTML=LK.map(([k,n,s,nz])=>`<div class="card lc" id="c-${k}"><label class="row" style="padding-top:0"><div>${n}<small>${s}</small></div><input class="sw" type="checkbox" role="switch" data-l="${k}" data-p="on"></label>
${k==='foot'?'<label class="row"><div>نصب شده<small>بعد از نصب روشن کنید تا کنترل فعال شود</small></div><input class="sw" type="checkbox" role="switch" data-l="foot" data-p="inst"></label>':''}
<div class="ctl"><div class="seg">${(k==='rear'?RMODES:MODES).map(([v,t])=>`<button data-l="${k}" data-p="mode" data-val="${v}">${t}</button>`).join('')}</div>
<div class="sl">روشنایی<b id="br-${k}"></b></div><input type="range" min="5" max="100" data-l="${k}" data-p="br">
<div id="x-${k}"><div class="sl">سرعت<b id="sp-${k}"></b></div><input type="range" min="1" max="10" data-l="${k}" data-p="sp"></div>
<div class="row" id="pt-${k}" style="margin-top:6px">الگوی رقص نور<select data-l="${k}" data-p="pat"><option value="wave">${nz>1?'موج':'نفس‌کشی'}</option>${nz>1?'<option value="chase">تعقیب</option>':''}<option value="rand">تصادفی</option></select></div>
${k==='foot'?'<div class="row">رنگ<input type="color" data-l="foot" data-p="col"></div><label class="row" id="rb-foot">رنگین‌کمان (در رقص نور)<input class="sw" type="checkbox" role="switch" data-l="foot" data-p="rb"></label>':''}</div></div>`).join('');
const G={int:[[88,142,24,5]],sq:[[95,184,10,10]],rear:[[52,318,14,4],[134,318,14,4]],foot:[[62,128,26,10],[112,128,26,10],[62,180,26,10],[112,180,26,10]]},
GS={int:[[126,15,16,3]],sq:[[172,14,8,8]],rear:[[292,51,6,3],[292,51,6,3]],foot:[[92,44,16,4],[92,44,16,4],[170,44,16,4],[170,44,16,4]]},ZE={};
LK.forEach(([k,,,nz])=>{ZE[k]=[];for(let z=0;z<nz;z++){const pr=[['zones',G],['zs',GS]].map(([g,M])=>{const e=document.createElementNS('http://www.w3.org/2000/svg','rect');e.setAttribute('class','z');e.setAttribute('rx',k==='sq'?1.5:2);
 ['x','y','width','height'].forEach((n,i)=>e.setAttribute(n,M[k][z][i]));$(g).appendChild(e);return e});ZE[k].push(pr)}});
function setView(v){$('car').style.display=v==='top'?'block':'none';$('side').style.display=v==='side'?'block':'none';document.querySelectorAll('#vw button').forEach(b=>b.setAttribute('aria-pressed',b.dataset.vw===v))}
document.querySelectorAll('#vw button').forEach(b=>b.addEventListener('click',()=>setView(b.dataset.vw)));setView('top');
function pat(p,t,f,z,n){
 if(p==='wave')return .5+.5*Math.sin(2*Math.PI*(t*f-z/n));
 if(p==='chase'&&n>1){const m=n-1,ph=(t*f*n)%(2*m),pos=ph<=m?ph:2*m-ph;return Math.max(0,1-Math.abs(z-pos)*.7)}
 const x=Math.sin((Math.floor(t*f*3)+z*7.13)*12.9898)*43758.5453;return (x-Math.floor(x))>.5?1:.08}
function ui(k){const l=L[k];$('br-'+k).textContent=fa(l.br)+'٪';$('sp-'+k).textContent=fa(l.sp)+' از ۱۰';
 $('x-'+k).hidden=l.mode==='steady';$('pt-'+k).hidden=l.mode!=='dance';if(k==='foot')$('rb-foot').hidden=l.mode!=='dance';
 document.querySelectorAll(`[data-l=${k}][data-p=mode]`).forEach(b=>b.setAttribute('aria-pressed',b.dataset.val===l.mode));
 $('c-'+k).classList.toggle('noinst',!l.inst);document.querySelector(`[data-l=${k}][data-p=on]`).disabled=!l.inst;
 ZE[k].forEach(pr=>pr.forEach(e=>e.style.strokeDasharray=l.inst?'':'3 2'));
 $('rn').textContent=!L.parked&&L.rear.on&&L.rear.mode!=='steady'?'ماشین در حال حرکت است — افکت چراغ عقب غیرفعال شد و فقط ثابت روشن می‌ماند':''}
function syncL(){document.querySelectorAll('#lights [data-l]').forEach(c=>{if(c.tagName==='BUTTON')return;const v=L[c.dataset.l][c.dataset.p];c.type==='checkbox'?c.checked=v:c.value=v});LK.forEach(x=>ui(x[0]))}
$('lights').addEventListener('click',e=>{const b=e.target.closest('button');if(!b)return;const k=b.dataset.l,l=L[k];l.mode=b.dataset.val;ui(k);sendL(k);
 if(k==='rear'&&!L.parked&&l.mode!=='steady')toast('افکت چراغ عقب فقط وقتی ماشین ایستاده مجاز است');log(NM(k)+': حالت '+MN[l.mode])});
$('lights').addEventListener('input',e=>{const c=e.target;if(c.tagName==='BUTTON'||!c.dataset.p)return;const k=c.dataset.l,p=c.dataset.p;
 L[k][p]=c.type==='checkbox'?c.checked:['br','sp'].includes(p)?+c.value:c.value;ui(k);sendL(k);
 if(p==='on'||p==='inst'||p==='rb')log(NM(k)+(p==='inst'?' — نصب شده: ':p==='rb'?' — رنگین‌کمان: ':': ')+(c.checked?'روشن':'خاموش'));
 else if(p==='pat')log(NM(k)+': '+c.selectedOptions[0].text)});
$('parked').addEventListener('change',e=>{L.parked=e.target.checked;ui('rear');if(REAL)net('/led?parked='+(+L.parked));log('ماشین '+(L.parked?'ایستاده':'در حال حرکت')+' (شبیه‌سازی)',false,true)});
document.querySelectorAll('[data-ls]').forEach(b=>b.addEventListener('click',()=>{const s=b.dataset.ls;
 if(s==='police'||s==='plane'){L.rear.on=true;L.rear.mode=s}else LK.forEach(([k])=>{if(!L[k].inst)return;L[k].on=s!=='off';if(s!=='off')L[k].mode=s});
 syncL();LK.forEach(x=>sendL(x[0]));log('چراغ‌ها: '+b.textContent,false,true)}));
setInterval(()=>{if(document.hidden||!$('vl').classList.contains('on'))return;const t=performance.now()/1000;
 LK.forEach(([k,,,nz])=>{const l=L[k],mode=k==='rear'&&!L.parked?'steady':l.mode,f=.5+l.sp*.7;
  for(let z=0;z<nz;z++){let v;if(!(l.inst&&l.on))v=0;else if(mode==='steady')v=1;else if(mode==='flash')v=(t*f)%1<.5?1:0;
   else if(mode==='police'){const sl=Math.floor(((t*f/3)%1)*8);v=(z?(sl===4||sl===6):(sl===0||sl===2))?1:0}
   else if(mode==='plane'){const q=(t*f/6)%1;v=(q<.06||(q>.14&&q<.2))?1:0}
   else v=pat(l.pat,t,f/2,z,nz);
   v*=l.br/100;const col=k==='foot'&&l.rb&&mode==='dance'?`hsl(${(t*f*40+z*90)%360} 90% 55%)`:l.col,on=v>.03;
   ZE[k][z].forEach(e=>{if(k==='sq'){e.style.fill='none';e.style.stroke=on?col:'var(--line)';e.style.strokeWidth=2.5}else e.style.fill=on?col:'var(--s2)';
    e.style.opacity=on?.3+.7*v:1})}})},60);

// اتصال به ESP32 (فقط وقتی صفحه از خود دستگاه با http باز شود)
const REAL=location.protocol==='http:'&&!/^(localhost|127\.)/.test(location.hostname)&&!location.search.includes('sim');
let DEV=null,fail=0,lastEid=-1,synced=false;const T={};
const net=u=>fetch(u).catch(()=>{});
function sendL(k){if(!REAL)return;clearTimeout(T[k]);T[k]=setTimeout(()=>{const l=L[k];net('/led?k='+k+'&on='+(+l.on)+'&inst='+(+l.inst)+'&mode='+l.mode+'&br='+l.br+'&sp='+l.sp+'&pat='+l.pat)},60)}
let pollBusy=false;function poll(){pollBusy=true;fetch('/get_sensors',{cache:'no-store'}).then(r=>{if(!r.ok)throw 0;return r.json()}).then(d=>{DEV=d;fail=0;if(d.ver)$('fw').textContent=fa(d.ver);
 if(!synced){synced=true;net('/cmd?time='+Math.floor(Date.now()/1000));['lock','sag'].forEach(k=>setT(k,!!d[k]));if(d.tap!=null){S.tap=+d.tap;$('tap').value=S.tap}if(d.mx!=null){S.mx=+d.mx;$('mx').value=S.mx}lab();if(d.gest!=null){$('gestSw').checked=!!d.gest;$('gup').value=d.gup;$('gdn').value=d.gdn;$('gtu').value=d.gtu;$('gtd').value=d.gtd}L.parked=!!d.parked;$('parked').checked=L.parked;
  Object.keys(d.led).forEach(k=>Object.assign(L[k],d.led[k],{on:!!d.led[k].on,inst:!!d.led[k].inst}));syncL()}
 if(d.eid!==lastEid){if(lastEid>=0&&d.ev)log(d.ev,true);lastEid=d.eid}
 document.querySelector('.dot').style.background='var(--ok)'}).catch(()=>{if(++fail>4){DEV=null;document.querySelector('.dot').style.background='var(--bad)'}}).finally(()=>{pollBusy=false})}
if(REAL){['ota'].forEach(id=>$(id).style.display='none');poll();setInterval(()=>{if(!document.hidden&&!pollBusy)poll()},400)}
syncL();
health();log(REAL?'پنل به دستگاه واقعی وصل شد':'پنل آماده است (داده‌ها شبیه‌سازی هستند)');
</script>

<script id="esp32-pro-appearance-js">
const APPEAR_KEY='car_appearance_v3', APPEAR_DEFAULT={neon:'#00F5FF',glow:70,wall:'carbon',wallColor:'#071018'};
const WALLS_PRO={
carbon:'radial-gradient(circle at 20% 20%,rgba(0,245,255,.08),transparent 30%),repeating-linear-gradient(135deg,#0a1117 0 8px,#0d151c 8px 16px)',
blue:'radial-gradient(circle at 30% 10%,rgba(0,120,255,.18),transparent 35%),linear-gradient(145deg,#030914,#071c31 55%,#02070d)',
amber:'radial-gradient(circle at 70% 10%,rgba(255,170,0,.17),transparent 35%),linear-gradient(145deg,#100c06,#21150a 55%,#070604)',
graphite:'linear-gradient(145deg,#111318,#20242a 45%,#0b0d10)',light:'linear-gradient(145deg,#f3f6f8,#dfe5ea)',custom:''};
function getAppearanceLocal(){try{return {...APPEAR_DEFAULT,...JSON.parse(localStorage.getItem(APPEAR_KEY)||'{}')}}catch(e){return {...APPEAR_DEFAULT}}}
function setAppearanceLocal(a){localStorage.setItem(APPEAR_KEY,JSON.stringify(a))}
function appearanceFromUI(){return {neon:(document.getElementById('neonPickerPro')?.value||'#00F5FF').toUpperCase(),glow:Number(document.getElementById('glowPro')?.value||70),wall:document.getElementById('wallPro')?.value||'carbon',wallColor:(document.getElementById('wallColorPro')?.value||'#071018').toUpperCase()}}
function setAppearanceUI(a){a={...APPEAR_DEFAULT,...a};document.getElementById('neonPickerPro').value=a.neon;document.getElementById('neonHexPro').value=a.neon;document.getElementById('glowPro').value=a.glow;document.getElementById('glowValPro').textContent=a.glow+'%';document.getElementById('wallPro').value=a.wall;document.getElementById('wallColorPro').value=a.wallColor;applyAppearance(a)}
function hexRGB(h){h=(h||'#00F5FF').replace('#','');return h.length===6?parseInt(h.slice(0,2),16)+','+parseInt(h.slice(2,4),16)+','+parseInt(h.slice(4,6),16):'0,245,255'}
function applyAppearance(a){
 document.documentElement.style.setProperty('--neon',a.neon);document.documentElement.style.setProperty('--neon2',a.neon);
 let bg=WALLS_PRO[a.wall]||WALLS_PRO.carbon;if(a.wall==='custom')bg=`linear-gradient(145deg,${a.wallColor},#05080c)`;
 document.body.style.backgroundImage=bg;document.getElementById('wallPreviewPro').style.backgroundImage=bg;
 let st=document.getElementById('dynamic-neon-v3');if(!st){st=document.createElement('style');st.id='dynamic-neon-v3';document.head.appendChild(st)}
 const g=Math.max(0,Math.min(100,Number(a.glow)||70))/100,rgb=hexRGB(a.neon);
 st.textContent=`.card,.hero,.quick-status,.window-card{box-shadow:0 0 ${Math.round(10+26*g)}px rgba(${rgb},${(.08+.18*g).toFixed(2)})}.btn:hover,.nav-item.active{box-shadow:0 0 ${Math.round(8+18*g)}px rgba(${rgb},${(.15+.25*g).toFixed(2)})}`;
}
function setSyncBadge(t,k='warn'){const b=document.getElementById('espSyncBadge');if(b){b.textContent=t;b.className='sync-badge '+k}}
async function saveAppearanceESP(){const a=appearanceFromUI();setAppearanceLocal(a);try{const r=await fetch('/set_config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({appearance:a})});if(!r.ok)throw 0;setSyncBadge('ذخیره روی ESP32 ✓','ok')}catch(e){setSyncBadge('ذخیره محلی ✓','warn')}applyAppearance(a)}
async function loadAppearanceESP(){try{const r=await fetch('/get_config',{cache:'no-store'});if(!r.ok)throw 0;const d=await r.json(),a=d.appearance||d;setAppearanceLocal({...APPEAR_DEFAULT,...a});setAppearanceUI(a);setSyncBadge('متصل به ESP32 ✓','ok')}catch(e){setAppearanceUI(getAppearanceLocal());setSyncBadge('ESP32 API در دسترس نیست','warn')}}
async function resetAppearanceESP(){const a={...APPEAR_DEFAULT};setAppearanceUI(a);setAppearanceLocal(a);try{const r=await fetch('/set_config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({appearance:a})});if(!r.ok)throw 0;setSyncBadge('پیش‌فرض روی ESP32 ✓','ok')}catch(e){setSyncBadge('پیش‌فرض محلی ✓','warn')}}
(function(){const a=getAppearanceLocal();setAppearanceUI(a);const p=document.getElementById('neonPickerPro'),h=document.getElementById('neonHexPro'),g=document.getElementById('glowPro'),w=document.getElementById('wallPro'),wc=document.getElementById('wallColorPro');
p.oninput=()=>{h.value=p.value.toUpperCase();const a=appearanceFromUI();setAppearanceLocal(a);applyAppearance(a)};h.onchange=()=>{let v=h.value.trim();if(!/^#[0-9a-fA-F]{6}$/.test(v))v='#00F5FF';p.value=v;h.value=v.toUpperCase();const a=appearanceFromUI();setAppearanceLocal(a);applyAppearance(a)};
g.oninput=()=>{document.getElementById('glowValPro').textContent=g.value+'%';const a=appearanceFromUI();setAppearanceLocal(a);applyAppearance(a)};w.onchange=()=>{const a=appearanceFromUI();setAppearanceLocal(a);applyAppearance(a)};wc.oninput=()=>{const a=appearanceFromUI();setAppearanceLocal(a);applyAppearance(a)}})();
</script>


<script id="usage-learning">
(()=>{const K='car_usage_v1';let U;try{U=JSON.parse(localStorage.getItem(K))}catch(e){}U=U||[[],[]];
const P=[{d:0,t:0},{d:0,t:0}],md=a=>{a=[...a].sort((x,y)=>x-y);return a.length?a[a.length>>1]:null};let prev='';
setInterval(()=>{S.w.forEach((w,i)=>{const p=P[i];
 if(!p.d&&w.d&&!S.cal[i])p.t=Date.now();
 if(p.d&&!w.d&&p.t&&!S.cal[i]){const dur=(Date.now()-p.t)/1000;
  if(dur>.4){U[i].push({o:Math.round(w.o),end:w.o<=0||w.o>=100,dur});if(U[i].length>40)U[i].shift();try{localStorage.setItem(K,JSON.stringify(U))}catch(e){}}p.t=0}
 p.d=w.d});
 if(!$('v1').classList.contains('on'))return;
 const h=NAMES.map((n,i)=>{const u=U[i],v=md(u.filter(x=>!x.end).map(x=>x.o)),a=u.filter(x=>x.end).length;
  return `<p class="hl">${n}: ${fa(u.length)} حرکت • ${fa(a)} بار تا انتها • توقف معمول وسط راه: <b>${v==null?'—':fa(v)+'٪ باز'}</b></p>${v==null?'':`<button class="mini" data-lg="${i}:${v}">رفتن به موقعیت معمول ${n}</button>`}`}).join('')+'<p class="custom-note">الگوی توقف‌های شما ثبت می‌شود تا موقعیت معمول پیشنهاد شود (فقط روی همین مرورگر ذخیره می‌شود).</p>';
 if(h!==prev){prev=h;$('learn').innerHTML=h}},500);
document.addEventListener('click',e=>{const b=e.target.closest('[data-lg]');if(!b)return;const [i,v]=b.dataset.lg.split(':');S.tgt[+i]=+v;S.w[+i].rev=0;if(REAL)net('/cmd?w='+i+'&goto='+v);log('رفتن به موقعیت معمول '+NAMES[+i])});
})();
</script>

<script id="diag">
(()=>{const g=id=>document.getElementById(id);
const TN=['راننده ▲ (۴)','راننده ▼ (۵)','شاگرد ▲ (۶)','شاگرد ▼ (۷)','پنل شاگرد ▲ (۱۰)','پنل شاگرد ▼ (۱۱)'];
const RN=['رله راننده ▲ (۱۵)','رله راننده ▼ (۱۶)','رله شاگرد ▲ (۱۷)','رله شاگرد ▼ (۱۸)'];
g('dgr').innerHTML=RN.map((n,i)=>`<button data-rt="${i}" style="flex:1 1 45%">${n}</button>`).join('');
g('dgmsg').textContent=REAL?'':'این بخش فقط روی دستگاه واقعی کار می‌کند.';
document.addEventListener('click',e=>{const b=e.target.closest('[data-rt]');if(!b||!REAL)return;
 fetch('/test?r='+b.dataset.rt).then(r=>r.text()).then(t=>toast(t==='ok'?'پالس ۰٫۳ ثانیه‌ای ارسال شد':'رد شد: شیشه در حال حرکت است'))});
document.addEventListener('click',e=>{const b=e.target.closest('[data-arm]');if(!b)return;const w=+b.dataset.arm;
 if(!REAL)return toast('کالیبراسیون با لمس فقط روی دستگاه واقعی کار می‌کند');
 if(S.cal[w]===3){net('/cmd?w='+w+'&cal=cancel');log('کالیبراسیون '+NAMES[w]+' لغو شد',false,true)}
 else{net('/cmd?w='+w+'&cal=arm');log('شیشه '+NAMES[w]+' را کاملاً به یک سمت ببر؛ سپس دکمه جهت مخالف را نگه دار و هنگام رسیدن رها کن',false,true)}});
let n=0;
setInterval(()=>{if(!REAL||document.hidden||!g('vd').classList.contains('on'))return;
 fetch('/diag',{cache:'no-store'}).then(r=>r.json()).then(d=>{
  g('dgt').innerHTML=d.t.map((v,k)=>`<div class="row"><div>${TN[k]}<small class="${d.g[k]>5?'warn':''}">خام: ${d.p[k]} • نویز کوتاه: ${fa(d.g[k])} • آخرین لمس: ${fa(d.du?d.du[k]:0)} ms</small></div><span class="chip"><i class="dot" style="background:${v?'var(--ok)':'var(--mut)'}"></i>${v?'لمس':'آزاد'}</span></div>`).join('');
  g('dgo').innerHTML=d.r.map((v,k)=>`<div class="row"><div>${RN[k]}</div><span class="chip"><i class="dot" style="background:${v?'var(--bad)':'var(--mut)'}"></i>${v?'روشن':'خاموش'}</span></div>`).join('')+`<div class="row"><div>رله قفل / باز (۱۹ / ۲۱)</div><span class="chip">${d.l[0]?'قفل':'—'} / ${d.l[1]?'باز':'—'}</span></div>`;
  g('dgs').innerHTML=`<div class="row"><div>ولتاژ</div><b>${d.v?fa(d.v.toFixed(2))+' V':'—'}</b></div><div class="row"><div>زمان روشن بودن</div><b>${fa(d.up)} ثانیه</b></div><div class="row"><div>حافظه آزاد</div><b>${fa(Math.round(d.h/1024))} KB</b></div><div class="row"><div>آخرین افت ناگهانی ولتاژ<small>راننده / شاگرد</small></div><b>${fa(d.sd[0])} / ${fa(d.sd[1])} V</b></div>`;
  if(n++%4===0)fetch('/logs').then(r=>r.text()).then(t=>g('dglog').textContent=t||'—')}).catch(()=>{})},500);
})();
</script>

<script id="gest">
(()=>{const $=id=>document.getElementById(id);
$('gestSw').addEventListener('change',e=>{if(REAL)net('/cmd?gest='+(e.target.checked?1:0));log('ژست دابل‌تپ: '+(e.target.checked?'روشن':'خاموش'))});
$('gup').addEventListener('change',e=>{if(REAL)net('/cmd?gup='+e.target.value)});
$('gdn').addEventListener('change',e=>{if(REAL)net('/cmd?gdn='+e.target.value)});
$('gtu').addEventListener('change',e=>{if(REAL)net('/cmd?gtu='+e.target.value)});
$('gtd').addEventListener('change',e=>{if(REAL)net('/cmd?gtd='+e.target.value)});
})();
</script>

<script id="logger">
(()=>{const g=id=>document.getElementById(id);let n=0;
g('lgClr').addEventListener('click',()=>{if(confirm('همه لاگ‌های ثبت‌شده حذف شود؟')){net('/logclr');toast('لاگر پاک شد')}});
setInterval(()=>{if(!REAL||document.hidden||!g('vd').classList.contains('on'))return;
 g('lgInfo').textContent=DEV?('ثبت‌شده: '+fa(DEV.lgn)+' حرکت • مجموع از ابتدا: '+fa(DEV.lgs)+' • نوبت روشن‌شدن: '+fa(DEV.boot)):'—';
 if(n++%2===0)fetch('/log.csv',{cache:'no-store'}).then(r=>r.text()).then(t=>{const L=t.trim().split('\n');g('lgPre').textContent=L.length>1?L.slice(-6).join('\n'):'هنوز حرکتی ثبت نشده'}).catch(()=>{})},2500);
})();
</script>

<!-- ===== LOGIN TEST LAYER (client-side demo) v3 ===== -->
<style>
#lg{position:fixed;inset:0;z-index:9999;display:flex;align-items:center;justify-content:center;padding:20px;background:var(--bg);color:var(--ink)}
#lg[hidden]{display:none}
#lg .box{width:100%;max-width:340px;background:var(--s1);border:1px solid var(--line);border-radius:22px;padding:24px 20px;text-align:center;box-shadow:0 12px 40px rgba(0,0,0,.3)}
#lg h2{margin:0 0 4px;font-size:19px}#lg p{margin:0 0 16px;color:var(--mut);font-size:12.5px}
#lg input{width:100%;box-sizing:border-box;margin:5px 0;padding:13px;border-radius:12px;border:1px solid var(--line);background:var(--s2);color:var(--ink);font-size:16px;text-align:center}
#lg button{width:100%;margin-top:10px;padding:14px;border:0;border-radius:12px;background:var(--acc);color:var(--accInk);font-size:15px;font-weight:700;cursor:pointer}
#lgErr{min-height:18px;margin-top:8px;color:var(--bad);font-size:12.5px}
#lgOut{margin-inline-start:8px;padding:5px 10px;border-radius:10px;border:1px solid var(--line);background:var(--s2);color:var(--ink);font-size:12px}
#lg .hint{margin-top:10px;font-size:11px;color:var(--mut);direction:ltr}
body[data-role=guest] .hero,body[data-role=guest] .scenes,body[data-role=guest] #tg,body[data-role=guest] #quick-status,body[data-role=guest] #wins .win:first-child{display:none!important}
</style>
<div id="lg"><div class="box">
<h2>🔐 ورود به کنترلر</h2><p>پراید ۱۳۸۹ • ESP32-S3</p>
<input id="lgU" placeholder="نام کاربری" autocapitalize="off" autocomplete="off">
<input id="lgP" type="password" placeholder="رمز عبور" autocomplete="off">
<button type="button" id="lgB">ورود</button><div id="lgErr"></div>
<div class="hint">test: admin/1234 • driver/1111 • guest/0000</div>
<div class="hint" id="lgS">⏳ اسکریپت اجرا نشده</div>
</div></div>
<script>
(()=>{
const USERS={admin:{p:'1234',tabs:[0,1,2,3,4,5]},driver:{p:'1111',tabs:[0,1,2,4]},guest:{p:'0000',tabs:[0,1]}};
const $=id=>document.getElementById(id);let fails=0,lockUntil=0;
const ST={get:k=>{try{return sessionStorage.getItem(k)}catch(e){return null}},set:(k,v)=>{try{sessionStorage.setItem(k,v)}catch(e){}},del:k=>{try{sessionStorage.removeItem(k)}catch(e){}}};
window.addEventListener('error',e=>{$('lgErr').textContent='خطای صفحه: '+(e.message||'?')});
function apply(u){
  const t=USERS[u].tabs;document.body.dataset.role=u;
  document.querySelectorAll('#nav button').forEach(b=>{b.style.display=t.includes(+b.dataset.v)?'':'none'});
  try{document.querySelector('#nav button[data-v="'+t[0]+'"]').click()}catch(e){}
  $('lg').hidden=true;ST.set('role',u);
  if(!$('lgOut')){const b=document.createElement('button');b.id='lgOut';b.textContent='خروج ('+u+')';b.onclick=()=>{ST.del('role');location.reload()};const h=document.querySelector('header');if(h)h.appendChild(b)}
}
const nd=x=>x.replace(/[۰-۹]/g,d=>'۰۱۲۳۴۵۶۷۸۹'.indexOf(d)).replace(/[٠-٩]/g,d=>'٠١٢٣٤٥٦٧٨٩'.indexOf(d));
function go(){
  const now=Date.now(),err=$('lgErr');
  if(now<lockUntil){err.textContent='قفل موقت؛ '+Math.ceil((lockUntil-now)/1000)+' ثانیه صبر کنید';return}
  const u=nd($('lgU').value).trim().toLowerCase(),p=nd($('lgP').value).trim();
  if(USERS[u]&&USERS[u].p===p){fails=0;err.textContent='';$('lgP').value='';apply(u)}
  else{fails++;err.textContent='نام کاربری یا رمز اشتباه است';if(fails>=5){lockUntil=now+30000;fails=0}}
}
$('lgB').addEventListener('click',go);
['lgU','lgP'].forEach(id=>$(id).addEventListener('keydown',e=>{if(e.key==='Enter'){e.preventDefault();go()}}));
$('lgS').textContent='✓ اسکریپت فعال';
const s=ST.get('role');if(s&&USERS[s])apply(s);
})();
</script>
</body>

<!-- ESP32-S3 persistent appearance API:
GET /get_config -> {"appearance":{"neon":"#00F5FF","glow":70,"wall":"carbon","wallColor":"#071018"}}
POST /set_config with JSON {"appearance":{...}}
Existing /cmd, /led, /get_sensors endpoints remain unchanged.
-->

</html>
)HTMLPANEL";

const char* ssid="Car_Control_Pro";
const char* password="12345678password";

// ---- پایه‌ها (همان سیم‌کشی خودت) ----
const uint8_t RUP[2]={15,17};   // رله بستن (بالا): راننده، شاگرد
const uint8_t RDN[2]={16,18};   // رله باز شدن (پایین)
const uint8_t LOCK_OUT=19, UNLOCK_OUT=21;
#ifndef HAS_LOCK_RELAY
#define HAS_LOCK_RELAY 0   // رله قفل مرکزی فعلاً وصل نیست؛ پایه ۱۹/۲۰ روی ESP32-S3 همان USB است و نباید خروجی شود
#endif
#if HAS_LOCK_RELAY
#define LRD(p) digitalRead(p)
#else
#define LRD(p) 0
#endif
// هشدار: روی ESP32-S3 پایه ۱۹ و ۲۰ همان USB هستند. اگر آپلود/سریال با USB خراب شد، رله قفل را به پایه آزاد (مثلاً ۳۸) ببر.
#define HAS_PAS_PANEL 0   // پنل شاگرد (پایه‌های ۱۰ و ۱۱) فقط وقتی واقعاً سیم‌کشی شده ۱ شود؛ پایه آزاد = لمس فرضی
struct Touch{uint8_t pin,w;int8_t d;bool pas,on;uint32_t t0;bool raw;uint32_t tr;uint16_t gl;bool act,sup,qOk,qOk3,dbl;uint32_t qEnd,qEnd3;uint16_t dur;};
Touch TC[6]={{4,0,1,0,0,0},{5,0,-1,0,0,0},   // پنل راننده: شیشه راننده
             {6,1,1,0,0,0},{7,1,-1,0,0,0},   // پنل راننده: شیشه شاگرد
             {10,1,1,1,0,0},{11,1,-1,1,0,0}};  // پنل شاگرد
const int NT=HAS_PAS_PANEL?6:4;
const float LAG=0.20f;            // تاخیر استارت موتور (ثانیه)
const uint32_t PULSE_MS=500;      // پالس قفل مرکزی
const uint32_t DEB=40;            // دیبانس لمس (ms)
const float LOWV=9.5f;            // زیر این ولتاژ (در حالت ایستاده) حرکت جدید شروع نمی‌شود
const uint32_t SETTLE_MS=300;     // مکث بین پایان یک شیشه و شروع شیشه دیگر
const uint32_t CAL_ARM_MS=45000;  // انقضای حالت «آماده کالیبراسیون»
const uint32_t WDT_S=8;           // واچ‌داگ (ثانیه)
const uint32_t GEST_TAP_MS=250;   // هر تپِ ژست کمتر از این زمان
const uint32_t GEST_GAP_MS=450;   // فاصله رها کردن تپ اول تا لمس تپ دوم
const uint16_t CFG_VER=0x0520;    // نسخه ساختار تنظیمات (تغییر = بازنشانی امن)

struct Cfg{uint16_t ver=CFG_VER;float pos[2]={0,0},sv[2]={10000,10000},T[2][2]={{5,3},{5,3}},V[2][2]={{0,0},{0,0}},S[2][2][3]={};
 uint8_t sn[2][2]={{0,0},{0,0}};uint16_t tap=250;uint8_t mx=12,dirty=0;bool lock=1,child=0,autoLock=1,autoRain=1,sag=0;float sagV=0.25f;float Tb[2][2]={{0,0},{0,0}};} C;
// pos = درصد باز بودن، sv = واریانس خطای موقعیت (σ² به درصد²؛ ۱۰۰۰۰ = کاملاً نامعلوم، ۰ = همگام)
// T[i][0] = زمان بستن (بالا)، T[i][1] = زمان باز شدن (پایین)؛ S = سه نمونه آخر کالیبراسیون، sn = تعداد نمونه، V = ولتاژ هنگام کالیبراسیون
struct Win{int8_t d=0,last=1,laDir=0,pd=0,offDir=0;uint8_t mode=0,cal=0,sagN=0,pm=0,src=0,res=0,psrc=0;bool sy=false,sagSeen=false,topoff=false,laOk=false;uint16_t vn=0;
 float tgt=0,extra=0,dist=0,vs=0,p0=0,s0=0,vb=0,sagD=0,laMov=0,laF=0,vStart=0,vMin=99;uint32_t t0=0,tl=0,beat=0,endT=0,calT=0,sagT=0,laStop=0,pt=0,offT=0,calArm=0;} W[2];
struct Job{bool on;uint8_t kind;int8_t dir;float val;} J[2]={{false,0,0,0},{false,0,0,0}};   // فرمان خودکار منتظر (آخرین فرمان هر شیشه)
struct Step{int8_t w;uint8_t kind;int8_t dir;float val;};   // kind: 0 goto، 1 تا انتها، 2 زمان‌دار (ثانیه)، 3 قفل مرکزی
Step SCR[8];int scN=0,scI=0;bool scRun=false;uint32_t scT=0;
uint32_t lastStopT=0,pumpNext=0,vrNext=0,lowWarnT=0,otaT=0,evSaveT=0;int8_t lastStopW=-1;
float Vrest=0;bool otaBusy=false,evDirty=false;uint8_t gDirty=0;
uint8_t gEn=1,gUp=0,gDn=0,gTu=0,gTd=1;   // سه‌تپ: 0 غیرفعال،1 باز کردن همه،2 بستن همه،3 تهویه،4 پارک،5 ایمنی (پیش‌فرض: پایین×۳ = باز کردن همه)
// ---- لاگر حرکت‌ها (فقط ثبت؛ هیچ اثری روی منطق کنترل ندارد) ----
struct LogE{uint32_t ts,up;uint16_t boot,dur;uint8_t wd,mode,src,res,p0,p1,s0,s1,vs,vm,sd,pad;};   // ۲۴ بایت
const int LGN=120;
struct LogBlob{uint32_t seq;uint16_t head,count;LogE e[LGN];} LG;
uint16_t bootNo=0;uint32_t tBase=0;bool lgDirty=false;uint8_t curSrc=1,scSrc=2;   // src: 0 لمس، 1 وب، 2 صحنه، 3 ژست، 4 صف، 5 سایر   // ژست دابل‌تپ: فعال؟، سناریوی بالا (0 پارک،1 بستن همه،2 ایمنی)، سناریوی پایین (0 تهویه،1 باز کردن،2 ایمنی)
float lsd[2]={0,0};   // آخرین افت ناگهانی ولتاژ دیده‌شده (برای عیب‌یابی)
// mode: 0 ایستاده، 1 دستی، 2 رفتن به درصد، 3 رفتن تا انتها، 4 کالیبراسیون، 5 زمان‌دار
WebServer srv(80);Preferences pf;String EV="";uint32_t EID=0;
uint8_t pulsePin=0;uint32_t pulseEnd=0;uint8_t testPin=0;int8_t testWin=0;uint32_t testEnd=0;

const int LOGN=30;String LB[LOGN];int li=0;
void webLog(const String&m){Serial.println(m);LB[li]=m;li=(li+1)%LOGN;evDirty=true;}
void warn(const String&m){webLog(m);EV=m;EID++;}   // فقط هشدارها به پنل می‌روند
void save(){pf.putBytes("c",&C,sizeof(C));}
void saveEv(){String ln[20];int n=0;size_t tot=0;
 for(int k=LOGN-1;k>=0&&n<20;k--){String&m=LB[(li+k)%LOGN];if(!m.length())continue;if(tot+m.length()+1>3500)break;ln[n++]=m;tot+=m.length()+1;}
 String o;for(int k=n-1;k>=0;k--){o+=ln[k];o+="\n";}pf.putString("ev",o);}
void loadEv(){String o=pf.getString("ev","");String cur="";for(size_t n=0;n<o.length();n++){char c=o[n];if(c=='\n'){if(cur.length()){LB[li]=String("~ ")+cur;li=(li+1)%LOGN;}cur="";}else cur+=c;}}
void setDirty(int i,bool on){uint8_t m=gDirty;if(on)m|=(1<<i);else m&=~(1<<i);if(m!=gDirty){gDirty=m;pf.putUChar("dirty",m);}}   // نوشتن کوچک به‌جای کل ساختار
void feedWdt(){
#if USE_HW_WDT
 esp_task_wdt_reset();
#endif
}

float readVolt(){ // INA219 فقط ولتاژ
 Wire.beginTransmission(0x40);Wire.write(0x02);if(Wire.endTransmission()!=0)return 0;
 if(Wire.requestFrom(0x40,2)!=2)return 0;
 uint16_t r=(Wire.read()<<8)|Wire.read();return (r>>3)*0.004f;}

float sqr(float x){return x*x;}
float sgm(int i){return sqrtf(C.sv[i]);}
float spread(int i,int k){int n=C.sn[i][k];if(n<2)return 0;
 float lo=C.S[i][k][0],hi=lo;for(int x=1;x<n;x++){lo=min(lo,C.S[i][k][x]);hi=max(hi,C.S[i][k][x]);}
 return (hi-lo)/C.T[i][k];}
float med3(const float*a,int n){float b[3]={0,0,0};for(int x=0;x<n;x++)b[x]=a[x];
 for(int x=0;x<n;x++)for(int y=x+1;y<n;y++)if(b[y]<b[x]){float t=b[x];b[x]=b[y];b[y]=t;}
 return n<=1?b[0]:n==2?(b[0]+b[1])/2:b[1];}
float hrat(int i,int k){return C.Tb[i][k]>0?C.T[i][k]/C.Tb[i][k]:0;}   // نسبت زمان فعلی به زمان اولیه (سلامت)
float relErr(int i,int k){int n=C.sn[i][k];float r;   // خطای نسبی تخمین زمان (از کیفیت کالیبراسیون)
 if(n==0)r=0.15f;else if(n==1)r=0.10f;else if(n==2)r=max(0.05f,spread(i,k));else r=max(0.03f,0.7f*spread(i,k));
 if(C.V[i][k]<6)r+=0.03f;return r;}
int calQ(int i,int k){int n=C.sn[i][k];if(n==0)return 0;if(n==1)return 40;float sp=spread(i,k);
 if(n==2)return sp<0.1f?70:50;return sp<0.05f?100:sp<0.1f?85:sp<0.2f?65:40;}
float liveSg(int i){Win&w=W[i];float v=C.sv[i];     // σ لحظه‌ای شامل حرکت جاری
 if(w.d&&!w.sy){int k=w.d>0?0:1;float jt=0.08f/C.T[i][k]*100.0f;v=min(10000.0f,v+sqr(relErr(i,k)*w.dist)+2*sqr(jt));}
 return sqrtf(v);}
float Vlive=0;uint32_t vTs=0,vNext=0;
float vfac(int i,int k){if(Vlive<6||C.V[i][k]<6)return 1;return constrain(Vlive/C.V[i][k],0.8f,1.2f);}   // سرعت ∝ ولتاژ
void sampleV(uint32_t now){float v=readVolt();if(v<6||v>18){Vlive=0;return;}
 if(Vlive<=0||now-vTs>500)Vlive=v;else Vlive=0.92f*Vlive+0.08f*v;vTs=now;
 int mv=0,wi=0;for(int i=0;i<2;i++)if(W[i].d){mv++;wi=i;}
 for(int i=0;i<2;i++){Win&w=W[i];if(w.d){if(w.vStart<=0)w.vStart=Vlive;w.vMin=min(w.vMin,Vlive);}if(w.d&&!w.sagSeen&&(int32_t)(now-w.t0)>700){w.vs+=Vlive;w.vn++;}}
 if(mv!=1)return;Win&w=W[wi];                          // افت ولتاژ فقط وقتی یک شیشه حرکت می‌کند معنی دارد
 if(w.sagSeen||((int32_t)(now-w.t0))/1000.0f-LAG<1.0f)return;     // ثانیه اول (جریان راه‌اندازی) نادیده گرفته می‌شود
 if(w.vb<=0)w.vb=v;
 if(v<w.vb-C.sagV){if(w.sagN==0)w.sagT=now;if(++w.sagN>=3){lsd[wi]=w.vb-v;
   bool okp=w.cal==3||w.s0>20||(w.d>0&&C.pos[wi]<=30)||(w.d<0&&C.pos[wi]>=70);   // افت فقط نزدیک انتهای مسیر معتبر است
   if(okp){w.sagSeen=true;w.sagD=lsd[wi];webLog("[SAG] W"+I(wi)+" drop="+String(w.sagD,2)+"V");}
   else{w.vb=v;w.sagN=0;webLog("[SAG-IGN] W"+I(wi));}}}
 else{w.sagN=0;w.vb=0.98f*w.vb+0.02f*v;}}

bool addSample(int i,int k,float obs,float va){int n=C.sn[i][k];bool good=obs>=1&&obs<=20;
 if(good&&n>=2){float m=med3(C.S[i][k],n);if(fabsf(obs-m)>0.4f*m)good=false;}   // نمونه پرت رد می‌شود
 if(!good)return false;
 for(int x=2;x>0;x--)C.S[i][k][x]=C.S[i][k][x-1];C.S[i][k][0]=obs;if(n<3)C.sn[i][k]=n+1;
 C.T[i][k]=med3(C.S[i][k],C.sn[i][k]);                                          // میانه ۳ نمونه آخر
 if(C.sn[i][k]>=3&&C.Tb[i][k]<=0)C.Tb[i][k]=C.T[i][k];
 if(hrat(i,k)>=1.25f)warn("هشدار سلامت: زمان حرکت شیشه "+String(i?"شاگرد":"راننده")+" حدود "+I((hrat(i,k)-1)*100)+"٪ بیشتر از ابتدا شده — ریل یا موتور را بررسی کنید");
 if(va>=6&&va<=18)C.V[i][k]=C.V[i][k]>=6?(C.V[i][k]+va)/2:va;
 webLog("[CAL] W"+I(i)+" k"+I(k)+" obs="+String(obs,2)+" T="+String(C.T[i][k],2)+" n="+I(C.sn[i][k]));return true;}

// بعد از هر توقف: کالیبراسیون با لمس، ثبت حرکت خودکار، و یادگیری از تکمیل دستیِ بعدی
void afterStop(int i,int8_t dir,uint8_t mode,uint32_t t0,uint32_t now){Win&w=W[i];int k=dir>0?0:1;
 bool us=C.sag&&w.sagSeen;
 float mv=((int32_t)((us?w.sagT:now)-t0))/1000.0f-LAG,va=w.vn>=3?w.vs/w.vn:0;
 if(mode==1&&w.cal==3){                                  // کالیبراسیون با لمس: مدت نگه‌داشتن = زمان کل مسیر
  w.topoff=false;if(mv<1.0f)return;                      // خیلی کوتاه: همچنان منتظر لمس می‌ماند
  w.cal=0;
  if(addSample(i,k,mv,va))warn("کالیبراسیون ثبت شد: "+String(C.T[i][k],2)+" ثانیه — نمونه "+I(C.sn[i][k])+" از ۳");
  else warn("نمونه کالیبراسیون ناهمخوان — رد شد");
  C.pos[i]=dir>0?0:100;C.sv[i]=us?0:4;w.laOk=false;return;}
 if(mode==3){float f=dir>0?w.p0/100.0f:(100-w.p0)/100.0f;  // حرکت خودکار کامل‌شده را برای یادگیری بعدی ثبت کن
  w.laDir=dir;w.laStop=now;w.laMov=((int32_t)(now-t0))/1000.0f-LAG;w.laF=f;
  w.laOk=w.sy&&!w.sagSeen&&w.s0<=6&&f>=0.5f&&w.laMov>=0.5f;w.topoff=false;return;}
 if(mode==1&&w.topoff){w.topoff=false;                   // راننده بعد از اتو در همان جهت دستی ادامه داد
  if(w.laOk&&dir==w.laDir&&mv>=0.4f&&mv<=0.35f*C.T[i][k]){float ts=(w.laMov+mv)/w.laF;
   if(ts>=1&&ts<=20&&(C.sn[i][k]==0||fabsf(ts-C.T[i][k])<=0.4f*C.T[i][k])&&addSample(i,k,ts,0)){
    warn("یادگیری از تکمیل دستی: زمان "+String(k?"باز شدن ":"بستن ")+String(C.T[i][k],2)+" ثانیه");
    C.pos[i]=dir>0?0:100;C.sv[i]=us?0:4;}}
  w.laOk=false;return;}
 if(mode==1||mode==2||mode==5)w.laOk=false;}

void lgAdd(int i,int8_t dir,uint8_t mode,uint32_t t0,uint32_t now){
 uint32_t d=now-t0;if(d<200)return;                          // پالس‌های خیلی کوتاه ثبت نمی‌شوند
 Win&w=W[i];LogE&e=LG.e[LG.head];
 e.ts=tBase?tBase+now/1000:0;e.up=now/1000;e.boot=bootNo;e.dur=(uint16_t)min(d/10,(uint32_t)65535);
 e.wd=(uint8_t)((i&1)|(dir>0?2:0));e.mode=mode;e.src=w.src;e.res=w.res;
 e.p0=(uint8_t)constrain(w.p0+0.5f,0.0f,100.0f);e.p1=(uint8_t)constrain(C.pos[i]+0.5f,0.0f,100.0f);
 e.s0=(uint8_t)min(100.0f,w.s0);e.s1=(uint8_t)min(100.0f,sgm(i));
 e.vs=(uint8_t)(w.vStart>0?constrain(w.vStart*10.0f,0.0f,255.0f):0);e.vm=(uint8_t)(w.vMin<90?constrain(w.vMin*10.0f,0.0f,255.0f):0);
 e.sd=(uint8_t)constrain(w.sagD*100.0f,0.0f,255.0f);e.pad=0;
 LG.head=(LG.head+1)%LGN;if(LG.count<LGN)LG.count++;LG.seq++;lgDirty=true;}
void lgSave(){pf.putBytes("lg",&LG,sizeof(LG));}
void loadLg(){memset(&LG,0,sizeof(LG));if(pf.getBytesLength("lg")==sizeof(LG))pf.getBytes("lg",&LG,sizeof(LG));
 if(LG.count>LGN||LG.head>=LGN){LG.count=0;LG.head=0;}
 bootNo=pf.getUShort("bn",0)+1;pf.putUShort("bn",bootNo);lgDirty=false;}

void relaysOff(int i){digitalWrite(RUP[i],LOW);digitalWrite(RDN[i],LOW);}
void stopM(int i){Win&w=W[i];w.pd=0;if(!w.d)return;relaysOff(i);
 if(!w.sy){int k=w.d>0?0:1;float jt=0.08f/C.T[i][k]*100.0f;   // هر توقف وسط راه: خطای زمان‌بندی + خطای نسبی مسافت
  C.sv[i]=min(10000.0f,C.sv[i]+sqr(relErr(i,k)*w.dist)+2*sqr(jt));}
 webLog("[STOP] W"+I(i)+" pos="+String(C.pos[i],0)+" sigma="+String(sgm(i),0));
 int8_t dir=w.d;uint8_t mode=w.mode;uint32_t t0=w.t0;
 lgAdd(i,dir,mode,t0,millis());w.res=0;
 w.d=0;w.mode=0;w.offT=millis();w.offDir=dir;lastStopT=w.offT;lastStopW=i;setDirty(i,false);
 afterStop(i,dir,mode,t0,millis());
 save();}
void startM(int i,int8_t d,uint8_t mode){Win&w=W[i];
 if(w.pd){w.pd=d;w.pm=mode;w.psrc=curSrc;return;}                   // شروع در انتظار زمان مرده: فقط پارامترها به‌روز شود
 if(w.d==d){w.mode=mode;return;}
 if((w.d&&w.d!=d)||(!w.d&&w.offDir&&w.offDir!=d&&millis()-w.offT<150)){   // تغییر جهت: زمان مرده ۱۵۰ms بدون بلاک‌کردن حلقه
  if(w.d){w.res=5;stopM(i);}w.pd=d;w.pm=mode;w.psrc=curSrc;w.pt=millis()+150;return;}
 //                       // زمان مرده هنگام تغییر جهت (رفع باگ برگشت آنی رله)
 if(testPin)return;                                              // تست رله در جریان است
 if(otaBusy&&(uint32_t)(millis()-otaT)<120000UL)return;       // در حال OTA: حرکت جدید ممنوع
 if(Vrest>0&&Vrest<LOWV){if((uint32_t)(millis()-lowWarnT)>5000){lowWarnT=millis();warn("ولتاژ باتری پایین ("+String(Vrest,1)+" V) — حرکت شیشه انجام نشد");}return;}   // قفل ولتاژ پایین
 w.d=d;w.mode=mode;w.last=d;w.t0=w.tl=w.beat=millis();w.endT=0;w.sy=false;w.dist=0;w.vs=0;w.vn=0;w.p0=C.pos[i];w.s0=sgm(i);w.vb=0;w.sagN=0;w.sagSeen=false;w.src=curSrc;w.res=0;w.vStart=0;w.vMin=99;w.sagD=0;
 w.topoff=(mode==1&&w.laOk&&w.laDir==d&&millis()-w.laStop<=3000);   // شروع دستی بلافاصله بعد از اتو در همان جهت
 w.extra=0.4f+min(100.0f,2*sgm(i))/100.0f*C.T[i][d>0?0:1];   // عدم‌قطعیت زیاد = اضافه‌ران بیشتر تا همگام شدن
 digitalWrite(d>0?RDN[i]:RUP[i],LOW);digitalWrite(d>0?RUP[i]:RDN[i],HIGH);
 setDirty(i,true);webLog("[START] W"+I(i)+(d>0?" UP":" DOWN")+" m"+I(mode));}
void goTo(int i,float t){Win&w=W[i];t=constrain(t,0.0f,100.0f);
 if(t<=0||t>=100){int8_t d=t<=0?1:-1;
  if(C.sv[i]<=0.25f&&((d>0&&C.pos[i]<=0)||(d<0&&C.pos[i]>=100)))return;
  startM(i,d,3);return;}
 if(fabsf(C.pos[i]-t)<1)return;
 w.tgt=t;startM(i,C.pos[i]>t?1:-1,2);}
void pulse(uint8_t pin){                              // پالس قفل بدون بلاک‌شدن
#if HAS_LOCK_RELAY
 digitalWrite(LOCK_OUT,LOW);digitalWrite(UNLOCK_OUT,LOW);
 digitalWrite(pin,HIGH);pulsePin=pin;pulseEnd=millis()+PULSE_MS;webLog(pin==LOCK_OUT?"[DOOR] LOCK":"[DOOR] UNLOCK");
#else
 webLog(pin==LOCK_OUT?"[DOOR] LOCK ignored: relay not connected":"[DOOR] UNLOCK ignored: relay not connected");
#endif
}

// ---------------- لایه ۲: صف خودکار و صحنه‌ها ----------------
void launch(int i,uint8_t kind,int8_t dir,float val){
 if(kind==0)goTo(i,val);
 else if(kind==1)startM(i,dir,3);
 else if(kind==2){W[i].tgt=constrain(val,0.5f,(float)C.mx-0.5f);startM(i,dir,5);}
 else if(kind==3)pulse(val>0?LOCK_OUT:UNLOCK_OUT);}
bool settled(int i,uint32_t now){return lastStopW==i||(uint32_t)(now-lastStopT)>=SETTLE_MS;}
void cancelAll(){scRun=false;J[0].on=J[1].on=false;}                // I4: توقف/دخالت دستی همه صف‌ها را پاک می‌کند
void autoCmd(int i,uint8_t kind,int8_t dir,float val){               // I3: فقط یک موتور خودکار در هر لحظه
 Win&w=W[i];uint32_t now=millis();
 if(w.d||w.pd){launch(i,kind,dir,val);return;}                         // همین شیشه در حرکت است: هدف جدید
 bool other=W[1-i].d||W[1-i].pd;
 if(!other&&settled(i,now)&&!(i==1&&J[0].on)){launch(i,kind,dir,val);return;}
 J[i]={true,kind,dir,val};}                                            // منتظر بماند (راننده اولویت دارد)
void scAdd(int8_t w,uint8_t kind,int8_t dir,float val){if(scN<8){SCR[scN].w=w;SCR[scN].kind=kind;SCR[scN].dir=dir;SCR[scN].val=val;scN++;}}
bool startScene(const String&n){
 scN=0;scI=0;scSrc=(curSrc==3)?3:2;
 if(n=="close"){scAdd(0,0,0,0);scAdd(1,0,0,0);}
 else if(n=="open"){scAdd(0,0,0,100);scAdd(1,0,0,100);}
 else if(n=="vent"){scAdd(0,0,0,15);scAdd(1,0,0,15);}
 else if(n=="park"){scAdd(0,0,0,0);scAdd(1,0,0,0);scAdd(0,3,0,1);}            // اول راننده، بعد شاگرد، آخر قفل
 else if(n=="safety"){scAdd(0,2,1,5);scAdd(1,2,1,5);scAdd(0,2,-1,2);scAdd(1,2,-1,2);}
 else return false;
 J[0].on=J[1].on=false;scRun=true;scT=millis();webLog("[SCENE] "+n);return true;}
void pump(uint32_t now){
 if((int32_t)(now-pumpNext)<0)return;pumpNext=now+40;
 if(testPin||W[0].d||W[1].d||W[0].pd||W[1].pd)return;                  // صبر تا هر دو شیشه کاملاً بایستند
 if((uint32_t)(now-lastStopT)<SETTLE_MS)return;                         // مکث برای برگشت ولتاژ
 bool want=J[0].on||J[1].on||scRun;if(!want)return;
 if(Vrest>0&&Vrest<LOWV){if(scRun&&(uint32_t)(now-scT)>5000){scRun=false;warn("صحنه لغو شد: ولتاژ پایین");}return;}
 if(J[0].on||J[1].on){int i=J[0].on?0:1;Job j=J[i];J[i].on=false;curSrc=4;launch(i,j.kind,j.dir,j.val);curSrc=1;return;}
 if(scI>=scN){scRun=false;return;}
 Step st=SCR[scI++];scT=now;curSrc=scSrc;launch(st.w,st.kind,st.dir,st.val);curSrc=1;}

// ژست دابل‌تپ: گام اول سناریو (شیشه راننده) فوراً اجرا می‌شود، حتی اگر شیشه از تپ اول در حرکت باشد؛ بقیه از صف می‌آیند
void fireGesture(int8_t d){const char*n;uint8_t sv=curSrc;curSrc=3;
 if(d>0)n=gUp==1?"close":gUp==2?"safety":"park";else n=gDn==1?"open":gDn==2?"safety":"vent";
 webLog(String("[GESTURE] ")+String(d>0?"up x2 -> ":"down x2 -> ")+String(n));
 if(startScene(n)&&scN>0&&SCR[0].w==0){Step st=SCR[scI++];scT=millis();launch(st.w,st.kind,st.dir,st.val);}
 curSrc=sv;}

const char* sceneOf3(int8_t d){static const char*N[6]={nullptr,"open","close","vent","park","safety"};uint8_t c=d>0?gTu:gTd;return c<6?N[c]:nullptr;}
void fireTriple(int8_t d){const char*n=sceneOf3(d);if(!n)return;uint8_t sv=curSrc;curSrc=3;
 webLog(String("[GESTURE] ")+String(d>0?"up x3 -> ":"down x3 -> ")+String(n));
 if(startScene(n)&&scN>0&&SCR[0].w==0){Step st=SCR[scI++];scT=millis();launch(st.w,st.kind,st.dir,st.val);}
 curSrc=sv;}

void tick(int i,uint32_t now){Win&w=W[i];
 now=millis();
 if(w.pd&&!w.d&&(int32_t)(now-w.pt)>=0){int8_t pd=w.pd;uint8_t pm=w.pm;w.pd=0;{uint8_t sv=curSrc;curSrc=w.psrc;startM(i,pd,pm);curSrc=sv;}now=millis();}   // پایان زمان مرده: شروع جهت جدید
 if(!w.d){w.tl=now;return;}
 int k=w.d>0?0:1;
 float dt=((int32_t)(now-w.tl))/1000.0f;if(dt<0)dt=0;w.tl=now;float run=((int32_t)(now-w.t0))/1000.0f-LAG;   // تفاضل علامت‌دار: ساعت هرگز عقب‌تر از t0 فرض نمی‌شود
 if(run>0){dt=min(dt,run);float&p=C.pos[i];float b=p;
  float np=p-w.d*dt/C.T[i][k]*100.0f*vfac(i,k);p=constrain(np,0.0f,100.0f);w.dist+=fabsf(p-b);
  if(((w.d>0&&p<=0)||(w.d<0&&p>=100))&&!w.endT)w.endT=now;}
 if(w.endT&&!w.sy&&(int32_t)(now-w.endT)>=(int32_t)(w.extra*1000)){C.sv[i]=0;w.sy=true;}   // انتها بعد از اضافه‌ران کامل تأیید شد: عدم‌قطعیت صفر
 bool st=false;
 if(C.sag&&w.sagSeen&&!w.sy&&w.mode!=4){   // افت ناگهانی ولتاژ = لبه مسیر
  float tm=((int32_t)(w.sagT-w.t0))/1000.0f-LAG,f=w.d>0?w.p0/100.0f:(100-w.p0)/100.0f;
  if(w.cal!=3&&w.s0<=6&&f>=0.5f&&tm>=0.5f){float ts=tm/f;if(ts>=1&&ts<=20&&(C.sn[i][k]==0||fabsf(ts-C.T[i][k])<=0.35f*C.T[i][k]))addSample(i,k,ts,0);}
  C.pos[i]=w.d>0?0:100;C.sv[i]=0;w.sy=true;w.endT=w.sagT;st=true;w.res=4;webLog("[SAG-END] W"+I(i));}
 if(w.mode==1&&(int32_t)(now-w.beat)>700)st=true;
 if(w.mode==2&&((w.d>0&&C.pos[i]<=w.tgt)||(w.d<0&&C.pos[i]>=w.tgt))){st=true;if(!w.res)w.res=2;}
 if(w.mode==3&&w.sy){st=true;if(!w.res)w.res=1;}
 if(w.mode==5&&(int32_t)(now-w.t0)>=(int32_t)(w.tgt*1000.0f)){st=true;if(!w.res)w.res=2;}   // حرکت زمان‌دار (حالت ایمنی)
 if((int32_t)(now-w.t0)>(int32_t)(C.mx*1000UL)){st=true;w.res=3;warn("تایم‌اوت رله — قطع اجباری");}
 if(st)stopM(i);}

void calDone(int i){Win&w=W[i];int k=w.cal-1;if(k<0||k>1)return;
 float obs=(millis()-w.calT)/1000.0f-LAG,va=w.vn>=3?w.vs/w.vn:0;
 stopM(i);w.cal=0;
 if(!addSample(i,k,obs,va))warn("نمونه کالیبراسیون ناهمخوان — رد شد (موقعیت همگام شد)");
 C.pos[i]=k?100:0;C.sv[i]=0;save();}
void ok(){srv.send(200,"text/plain","ok");}
#ifdef API_PIN
#define GUARD if(!srv.header("X-Key").equals(API_PIN)){srv.send(401,"text/plain","auth");return;}
#else
#define GUARD
#endif
void hCmd(){
 GUARD
 curSrc=1;
 if(srv.hasArg("w")){int i=srv.arg("w").toInt();if(i<0||i>1){srv.send(400,"text/plain","bad");return;}
  Win&w=W[i];String a=srv.arg("cal");
  if(srv.hasArg("d")){int d=srv.arg("d").toInt();
   if(!d){cancelAll();stopM(i);}else if(w.mode==1&&w.d==d)w.beat=millis();else startM(i,d,1);}
  else if(srv.hasArg("auto")){if(w.cal==3)stopM(i);else if(w.pd)w.pm=3;else if(w.d)w.mode=3;else autoCmd(i,1,w.last,0);}
  else if(srv.hasArg("goto")){String g=srv.arg("goto");if(g.length())autoCmd(i,0,0,g.toFloat());}   // مقدار خالی نادیده گرفته می‌شود
  else if(a=="close"||a=="open"){w.cal=a=="close"?1:2;w.calT=millis();startM(i,a=="close"?1:-1,4);}
  else if(a=="done"&&w.cal)calDone(i);
  else if(a=="arm"){stopM(i);w.cal=3;w.calArm=millis();webLog("[CAL] armed W"+I(i));}
  else if(a=="cancel"){if(w.cal==1||w.cal==2)stopM(i);w.cal=0;}
  else if(srv.hasArg("calreset")){C.T[i][0]=5;C.T[i][1]=3;C.sn[i][0]=C.sn[i][1]=0;C.V[i][0]=C.V[i][1]=0;C.Tb[i][0]=C.Tb[i][1]=0;C.sv[i]=10000;save();webLog("[CAL] reset W"+I(i));}}
 if(srv.hasArg("scene")){if(!startScene(srv.arg("scene"))){srv.send(400,"text/plain","bad_scene");return;}}
 if(srv.hasArg("lock")){C.lock=srv.arg("lock").toInt();pulse(C.lock?LOCK_OUT:UNLOCK_OUT);save();}
 if(srv.hasArg("child"))C.child=srv.arg("child").toInt();
 if(srv.hasArg("autoLock"))C.autoLock=srv.arg("autoLock").toInt();
 if(srv.hasArg("autoRain"))C.autoRain=srv.arg("autoRain").toInt();
 if(srv.hasArg("tap"))C.tap=constrain(srv.arg("tap").toInt(),60,800);
 if(srv.hasArg("sag")){C.sag=srv.arg("sag").toInt();save();}
 if(srv.hasArg("time")){uint32_t u=(uint32_t)srv.arg("time").toInt();if(u>1600000000UL)tBase=u-millis()/1000;}   // هم‌زمان‌سازی ساعت از گوشی (برای ستون unix)
 if(srv.hasArg("gest")){gEn=srv.arg("gest").toInt()?1:0;pf.putUChar("gs",gEn);}
 if(srv.hasArg("gup")){gUp=constrain(srv.arg("gup").toInt(),0,2);pf.putUChar("gu",gUp);}
 if(srv.hasArg("gtu")){gTu=constrain(srv.arg("gtu").toInt(),0,5);pf.putUChar("gtu",gTu);}
 if(srv.hasArg("gtd")){gTd=constrain(srv.arg("gtd").toInt(),0,5);pf.putUChar("gtd",gTd);}
 if(srv.hasArg("gdn")){gDn=constrain(srv.arg("gdn").toInt(),0,2);pf.putUChar("gd",gDn);}
 if(srv.hasArg("sagv"))C.sagV=constrain(srv.arg("sagv").toFloat(),0.05f,2.0f);
 if(srv.hasArg("mx"))C.mx=constrain(srv.arg("mx").toInt(),3,30);
 ok();}
String jsonEsc(const String&s){String o;for(size_t n=0;n<s.length();n++){char c=s[n];if(c=='\\'||c=='"'){o+='\\';o+=c;}else if(c=='\n'||c=='\r')o+=' ';else o+=c;}return o;}
// ---------------- LED (real outputs) + shared state JSON ----------------
struct LedCfg{uint8_t pin=255;bool on=false,inst=true;uint8_t mode=0,br=80,sp=5,pat=0;bool rb=false;uint32_t col=0xFFFFFF;};
LedCfg LEDS[4];
bool ledParked=true;
bool wifiOn=true;
const char* LED_KEYS[4]={"int","sq","rear","foot"};
const char* LED_MODE_NAMES[5]={"steady","flash","dance","police","plane"};
const char* LED_PAT_NAMES[3]={"wave","chase","rand"};
void ledDefaults(){
 LEDS[0].pin=255;LEDS[0].on=true; LEDS[0].inst=true; LEDS[0].mode=0;LEDS[0].br=80; LEDS[0].sp=5;LEDS[0].pat=0;LEDS[0].rb=false;LEDS[0].col=0xFFA62B;
 LEDS[1].pin=255;LEDS[1].on=false;LEDS[1].inst=true; LEDS[1].mode=0;LEDS[1].br=100;LEDS[1].sp=5;LEDS[1].pat=0;LEDS[1].rb=false;LEDS[1].col=0xFF2A2A;
 LEDS[2].pin=255;LEDS[2].on=false;LEDS[2].inst=true; LEDS[2].mode=0;LEDS[2].br=100;LEDS[2].sp=5;LEDS[2].pat=1;LEDS[2].rb=false;LEDS[2].col=0xFFFFFF;
 LEDS[3].pin=255;LEDS[3].on=false;LEDS[3].inst=false;LEDS[3].mode=0;LEDS[3].br=60; LEDS[3].sp=5;LEDS[3].pat=0;LEDS[3].rb=false;LEDS[3].col=0x3AA0FF;
}
String ledHex(uint32_t c){char b[9];snprintf(b,sizeof(b),"#%06X",(unsigned)(c&0xFFFFFF));return String(b);}
String ledStateJson(){
 String s="{";
 for(int i=0;i<4;i++){LedCfg&L=LEDS[i];if(i)s+=",";
  s+="\""+String(LED_KEYS[i])+"\":{\"on\":"+I(L.on)+",\"inst\":"+I(L.inst)+",\"mode\":\""+String(LED_MODE_NAMES[L.mode<5?L.mode:0])
   +"\",\"br\":"+I(L.br)+",\"sp\":"+I(L.sp)+",\"pat\":\""+String(LED_PAT_NAMES[L.pat<3?L.pat:0])+"\",\"col\":\""+ledHex(L.col)
   +"\",\"rb\":"+I(L.rb)+",\"pin\":"+I(L.pin==255?-1:(int)L.pin)+"}";}
 return s+"}";}
String stateJson(){
 String j;j.reserve(950);
 j="{\"t\":\"state\",\"name\":\"PrideCar\",\"ver\":\"6.0\",\"ts\":"+String(millis()/1000);
 j+=",\"o\":["+String(C.pos[0],1)+","+String(C.pos[1],1)+"]";
 j+=",\"d\":["+I(W[0].d)+","+I(W[1].d)+"]";
 j+=",\"c\":["+String(100-min(100.0f,4*liveSg(0)),0)+","+String(100-min(100.0f,4*liveSg(1)),0)+"]";
 j+=",\"u\":["+String(liveSg(0),1)+","+String(liveSg(1),1)+"]";
 j+=",\"q\":[["+I(calQ(0,0))+","+I(calQ(0,1))+"],["+I(calQ(1,0))+","+I(calQ(1,1))+"]]";
 j+=",\"n\":[["+I(C.sn[0][0])+","+I(C.sn[0][1])+"],["+I(C.sn[1][0])+","+I(C.sn[1][1])+"]]";
 j+=",\"T\":[["+String(C.T[0][0],2)+","+String(C.T[0][1],2)+"],["+String(C.T[1][0],2)+","+String(C.T[1][1],2)+"]]";
 j+=",\"cal\":["+I(W[0].cal)+","+I(W[1].cal)+"],\"v\":"+String(readVolt(),2);
 j+=",\"lock\":"+I(C.lock)+",\"sag\":"+I(C.sag)+",\"child\":"+I(C.child)+",\"autoLock\":"+I(C.autoLock)+",\"autoRain\":"+I(C.autoRain);
 j+=",\"sc\":"+I(scRun)+",\"jb\":["+I(J[0].on)+","+I(J[1].on)+"]";
 j+=",\"hr\":[["+String(hrat(0,0),2)+","+String(hrat(0,1),2)+"],["+String(hrat(1,0),2)+","+String(hrat(1,1),2)+"]]";
 j+=",\"vr\":"+String(Vrest,2)+",\"tap\":"+I(C.tap)+",\"mx\":"+I(C.mx)+",\"sagv\":"+String(C.sagV,2);
 j+=",\"gest\":"+I(gEn)+",\"gup\":"+I(gUp)+",\"gdn\":"+I(gDn)+",\"gtu\":"+I(gTu)+",\"gtd\":"+I(gTd);
 j+=",\"lgn\":"+I(LG.count)+",\"lgs\":"+String(LG.seq)+",\"boot\":"+I(bootNo);
 j+=",\"parked\":"+I(ledParked)+",\"led\":"+ledStateJson();
 j+=",\"wifi\":"+I(wifiOn)+",\"ip\":\""+(wifiOn?WiFi.softAPIP().toString():String(""))+"\"";
 j+=",\"up\":"+String(millis()/1000)+",\"eid\":"+String(EID)+",\"ev\":\""+jsonEsc(EV)+"\"}";
 return j;}
void hGet(){srv.send(200,"application/json",stateJson());}
void hLogs(){String s;for(int k=0;k<LOGN;k++){String&m=LB[(li+k)%LOGN];if(m.length())s+=m+"\n";}srv.send(200,"text/plain; charset=utf-8",s);}
void hTest(){GUARD int r=srv.arg("r").toInt();
 if(r<0||r>3||W[0].d||W[1].d||W[0].pd||W[1].pd||testPin){srv.send(409,"text/plain","busy");return;}
 int i=r/2;testWin=i;testPin=(r%2)?RDN[i]:RUP[i];digitalWrite(testPin,HIGH);testEnd=millis()+300;
 C.sv[i]=min(10000.0f,C.sv[i]+25.0f);webLog("[TEST] relay "+I(r));ok();}
String diagJson(){
 String t="[",p="[",g="[",r="[",u="[";
 for(int k=0;k<NT;k++){String c=k?",":"";t+=c+I(TC[k].on);p+=c+I(TC[k].raw);g+=c+I(TC[k].gl);u+=c+I(TC[k].dur);}
 const uint8_t rp[4]={RUP[0],RDN[0],RUP[1],RDN[1]};
 for(int k=0;k<4;k++)r+=String(k?",":"")+I(digitalRead(rp[k]));
 return "{\"t\":"+t+"],\"p\":"+p+"],\"g\":"+g+"],\"du\":"+u+"],\"r\":"+r+"],\"l\":["+I(LRD(LOCK_OUT))+","+I(LRD(UNLOCK_OUT))
  +"],\"v\":"+String(readVolt(),2)+",\"up\":"+String(millis()/1000)+",\"h\":"+String(ESP.getFreeHeap())+",\"sd\":["+String(lsd[0],2)+","+String(lsd[1],2)+"]}";}
void hDiag(){srv.send(200,"application/json",diagJson());}
const char*RN_MODE[]={"stop","manual","goto","auto_to_end","calibration","timed"};
const char*RN_SRC[]={"touch","web","scene","gesture","queue","other"};
const char*RN_RES[]={"manual_stop","reached_end","target_reached","timeout","voltage_end","reversal"};
void cs(String&o,const String&v,bool last=false){o+=v;o+=last?'\n':',';}
String csvData(){String o;o.reserve((size_t)LG.count*80+260);
 o+="id,boot,uptime_s,unix,window,direction,mode,source,result,duration_s,pos_start,pos_end,sigma_start,sigma_end,v_start,v_min,sag_drop\n";
 for(uint16_t k=0;k<LG.count;k++){LogE&e=LG.e[(LG.head+LGN-LG.count+k)%LGN];
  cs(o,String(LG.seq-LG.count+k+1));cs(o,I(e.boot));cs(o,String(e.up));cs(o,e.ts?String(e.ts):String(""));
  cs(o,String((e.wd&1)?"passenger":"driver"));cs(o,String((e.wd&2)?"up_close":"down_open"));
  cs(o,String(RN_MODE[e.mode<6?e.mode:0]));cs(o,String(RN_SRC[e.src<6?e.src:5]));cs(o,String(RN_RES[e.res<6?e.res:0]));
  cs(o,String(e.dur/100.0f,2));cs(o,I(e.p0));cs(o,I(e.p1));cs(o,I(e.s0));cs(o,I(e.s1));
  cs(o,String(e.vs/10.0f,1));cs(o,String(e.vm/10.0f,1));cs(o,String(e.sd/100.0f,2),true);}
 return o;}
void hCsv(){
 srv.sendHeader("Content-Disposition","attachment; filename=pride_log.csv");
 srv.send(200,"text/csv; charset=utf-8",csvData());}
void hLogClr(){GUARD LG.count=0;LG.head=0;lgSave();lgDirty=false;ok();}
void hRoot(){srv.send_P(200,"text/html; charset=utf-8",PANEL);}

// ======================================================================
//  BLE bridge (v6.0) — همان موتور کنترل، بدون نیاز به Wi-Fi
//  Service UUID: 6d1e0001-...  RX (write): ...0002  TX (notify): ...0003
//  پروتکل: هر فرمان یک JSON در هر خط (\n). پاسخ‌ها هم JSON خط‌به‌خط.
// ======================================================================
#define BLE_SVC_UUID "6d1e0001-3c4a-4d8f-9b1a-7f2c5e8a9b01"
#define BLE_RX_UUID  "6d1e0002-3c4a-4d8f-9b1a-7f2c5e8a9b01"
#define BLE_TX_UUID  "6d1e0003-3c4a-4d8f-9b1a-7f2c5e8a9b01"

static NimBLEServer* bleServer=nullptr;
static NimBLECharacteristic* bleTx=nullptr;
static QueueHandle_t bleQ=nullptr;
static bool bleConnected=false,bleAuthed=false;
static uint16_t bleConnHandle=0xFFFF;
static String bleToken,bleBuf;
static uint32_t bleNextState=0,bleLastEid=0;
static char devPin[9]="1234";

static String newToken(){char b[9];for(int i=0;i<8;i++)b[i]="0123456789abcdef"[(esp_random()&15)];b[8]=0;return String(b);}
static bool tkOk(const char*t){return bleAuthed&&t&&bleToken.length()&&bleToken==String(t);}
// state ممکن است از یک بستهٔ BLE بزرگ‌تر باشد؛ آن را به قطعات امن می‌شکنیم
static void blePushLine(const String&s){if(!bleConnected||!bleTx)return;
 size_t CH=200;
 if(bleServer&&bleConnHandle!=0xFFFF){size_t pm=bleServer->getPeerMTU(bleConnHandle);if(pm>=23)CH=min(CH,pm-3);}
 for(size_t off=0;off<s.length();off+=CH){size_t n=min(CH,s.length()-off);
  bleTx->setValue((uint8_t*)s.c_str()+off,n);
  bleTx->notify();
  delay(8);}}
static void bleAck(long id,bool ok,const char*err=nullptr,const String&extra=""){
 String s="{\"id\":"+String(id)+",\"ok\":"+(ok?"1":"0");
 if(err)s+=",\"err\":\""+String(err)+"\"";
 if(extra.length())s+=","+extra;
 blePushLine(s+"}\n");}

// base64 (بدون وابستگی اضافه) برای انتقال لاگ/CSV در بسته‌های کوچک BLE
static String b64enc(const uint8_t*d,size_t n){
 static const char*T="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
 String o;o.reserve((n+2)/3*4);
 for(size_t i=0;i<n;i+=3){uint32_t v=(uint32_t)d[i]<<16;if(i+1<n)v|=(uint32_t)d[i+1]<<8;if(i+2<n)v|=d[i+2];
  o+=T[(v>>18)&63];o+=T[(v>>12)&63];o+=i+1<n?T[(v>>6)&63]:'=';o+=i+2<n?T[v&63]:'=';}
 return o;}
static void bleSendChunks(const String&payload,long id,const char*kind){
 if(!bleConnected)return;
 const size_t CH=110;int total=(int)((payload.length()+CH-1)/CH);if(total<1)total=1;
 for(int i=0;i<total;i++){if(!bleConnected)return;size_t off=(size_t)i*CH,ln=payload.length()>off?min(CH,payload.length()-off):0;
  String j="{\"t\":\"chunk\",\"id\":"+String(id)+",\"k\":\""+String(kind)+"\",\"i\":"+String(i)+",\"n\":"+String(total)+",\"d\":\"";
  j+=b64enc((const uint8_t*)payload.c_str()+off,ln);j+="\"}\n";blePushLine(j);feedWdt();delay(8);}}

// ---------------- LED hardware ----------------
static int ledIndex(const String&k){for(int i=0;i<4;i++)if(k==LED_KEYS[i])return i;return -1;}
static int ledModeIdx(const String&s){for(int i=0;i<5;i++)if(s==LED_MODE_NAMES[i])return i;return 0;}
static int ledPatIdx(const String&s){for(int i=0;i<3;i++)if(s==LED_PAT_NAMES[i])return i;return 0;}
static bool pinBusy(int p,int self){if(p<0)return false;
 // فقط پایه‌های امن ESP32-S3-WROOM-1 (Uno form) برای LED؛
 // 4-7/10/11 لمس، 8/9 I2C، 15-21 رله/قفل/USB، 43/44 UART، 45/46 استرپینگ، 48 RGB آنبرد
 static const uint8_t safe[]={1,2,12,13,14,38,39,40,41,42,47};
 bool ok=false;for(uint8_t q:safe)if(q==p){ok=true;break;}
 if(!ok)return true;
 for(int i=0;i<4;i++)if(i!=self&&LEDS[i].pin==p)return true;
 return false;}
static void ledAttach(int i){LedCfg&L=LEDS[i];if(L.pin==255)return;
#if defined(ESP_ARDUINO_VERSION_MAJOR)&&ESP_ARDUINO_VERSION_MAJOR>=3
 ledcAttach(L.pin,5000,8);
#else
 ledcSetup(i,5000,8);ledcAttachPin(L.pin,i);
#endif
}
static void ledWrite(int i,uint8_t v){LedCfg&L=LEDS[i];if(L.pin==255)return;
#if defined(ESP_ARDUINO_VERSION_MAJOR)&&ESP_ARDUINO_VERSION_MAJOR>=3
 ledcWrite(L.pin,v);
#else
 ledcWrite(i,v);
#endif
}
static bool ledSetPin(int i,int p){if(i<0||i>3)return false;LedCfg&L=LEDS[i];
 if(p<0||p>48){if(L.pin!=255){ledWrite(i,0);
#if defined(ESP_ARDUINO_VERSION_MAJOR)&&ESP_ARDUINO_VERSION_MAJOR>=3
   ledcDetach(L.pin);
#else
   ledcDetachPin(L.pin);
#endif
  }L.pin=255;return true;}
 if(pinBusy(p,i))return false;
 if(L.pin!=255){ledWrite(i,0);
#if defined(ESP_ARDUINO_VERSION_MAJOR)&&ESP_ARDUINO_VERSION_MAJOR>=3
  ledcDetach(L.pin);
#else
  ledcDetachPin(L.pin);
#endif
 }
 L.pin=(uint8_t)p;ledAttach(i);return true;}
struct LedSave{uint8_t pin,on,inst,mode,br,sp,pat,rb;uint32_t col;};
struct LedBlob{uint8_t ver;LedSave l[4];};
static void saveLeds(){LedBlob b;b.ver=1;for(int i=0;i<4;i++){LedCfg&L=LEDS[i];b.l[i]={L.pin,(uint8_t)L.on,(uint8_t)L.inst,L.mode,L.br,L.sp,L.pat,(uint8_t)L.rb,L.col};}pf.putBytes("led",&b,sizeof(b));}
static void loadLeds(){ledDefaults();if(pf.getBytesLength("led")==sizeof(LedBlob)){LedBlob b;pf.getBytes("led",&b,sizeof(b));if(b.ver==1)for(int i=0;i<4;i++){LedCfg&L=LEDS[i];L.on=b.l[i].on;L.inst=b.l[i].inst;L.mode=b.l[i].mode<5?b.l[i].mode:0;L.br=constrain((int)b.l[i].br,5,100);L.sp=constrain((int)b.l[i].sp,1,10);L.pat=b.l[i].pat<3?b.l[i].pat:0;L.rb=b.l[i].rb;L.col=b.l[i].col&0xFFFFFF;if(!ledSetPin(i,b.l[i].pin==255?-1:(int)b.l[i].pin))LEDS[i].pin=255;}}}
static uint32_t ledNext=0;static void ledTick(uint32_t now){
 if((int32_t)(now-ledNext)<0)return;ledNext=now+33;float t=now/1000.0f;
 for(int i=0;i<4;i++){LedCfg&L=LEDS[i];if(L.pin==255)continue;float v=0;
  uint8_t mode=(i==2&&!ledParked&&L.mode!=0)?0:L.mode;float f=0.5f+L.sp*0.7f;
  if(L.inst&&L.on){
   if(mode==0)v=1;
   else if(mode==1)v=(fmodf(t*f,1.0f)<0.5f)?1:0;
   else if(mode==3){int sl=(int)(fmodf(t*f/3.0f,1.0f)*8);v=((i&1)?(sl==4||sl==6):(sl==0||sl==2))?1:0;}
   else if(mode==4){float q=fmodf(t*f/6.0f,1.0f);v=(q<.06f||(q>.14f&&q<.2f))?1:0;}
   else{
    if(L.pat==0)v=.5f+.5f*sinf(2.0f*PI*(t*f-i/4.0f));
    else if(L.pat==1){const int m=3;float ph=fmodf(t*f*4.0f,2.0f*m),pos=ph<=m?ph:2*m-ph;v=1.0f-fabsf(i-pos)*.7f;if(v<0)v=0;}
    else{float x=sinf((floorf(t*f*3.0f)+i*7.13f)*12.9898f)*43758.5453f;v=((x-floorf(x))>.5f)?1:.08f;}
   }
   v*=L.br/100.0f;
  }
  ledWrite(i,(uint8_t)constrain(v*255.0f,0.0f,255.0f));
 }
}
static uint32_t colParse(const String&h){String s=h;if(s.startsWith("#"))s=s.substring(1);return strtoul(s.c_str(),nullptr,16)&0xFFFFFF;}

// ---------------- Wi-Fi AP on/off (BLE keeps working) ----------------
static void applyWifi(bool on,bool persist=true){
 if(on==wifiOn)return;
 if(on){
  WiFi.mode(WIFI_AP);WiFi.softAP(ssid,password);
#if USE_MDNS
  MDNS.begin("pride");MDNS.addService("http","tcp",80);
#endif
  srv.begin();wifiOn=true;
 }else{
  srv.stop();
#if USE_MDNS
  MDNS.end();
#endif
  WiFi.softAPdisconnect(true);WiFi.mode(WIFI_OFF);wifiOn=false;
 }
 if(persist)pf.putUChar("wf",wifiOn?1:0);
 webLog(String("[WIFI] AP ")+(wifiOn?"ON":"OFF"));
}

// ---------------- BLE command handling ----------------
static void bleHandleLine(const String&line){
 JsonDocument d;DeserializationError er=deserializeJson(d,line);
 if(er){bleAck(0,false,"json");return;}
 long id=(long)(d["id"]|0L);
 const char*c=d["cmd"]|"";
 if(!strcmp(c,"hello")){
  if(strlen(devPin)==0){bleAuthed=true;bleToken=newToken();}
  String x="{\"id\":"+String(id)+",\"ok\":1,\"name\":\"PrideCar\",\"ver\":\"6.0\",\"need\":"+String((int)(strlen(devPin)>0));
  if(bleAuthed)x+=",\"tk\":\""+bleToken+"\"";
  x+="}";blePushLine(x+"\n");return;}
 if(!strcmp(c,"auth")){
  String pin=d["pin"]|"";
  if(strlen(devPin)==0||pin==String(devPin)){bleAuthed=true;bleToken=newToken();bleAck(id,true,nullptr,"\"tk\":\""+bleToken+"\"");}
  else bleAck(id,false,"pin");
  return;}
 if(!tkOk(d["tk"]|"")){bleAck(id,false,"auth");return;}
 if(!strcmp(c,"state")){blePushLine(stateJson()+"\n");return;}
 if(!strcmp(c,"set")){
  int i=d["w"]|0;if(i<0||i>1){bleAck(id,false,"w");return;}
  curSrc=1;Win&w=W[i];
  if(d.containsKey("cal")){
   String a=d["cal"]|"";
   if(a=="close"||a=="open"){w.cal=a=="close"?1:2;w.calT=millis();startM(i,a=="close"?1:-1,4);}
   else if(a=="done"&&w.cal)calDone(i);
   else if(a=="arm"){stopM(i);w.cal=3;w.calArm=millis();webLog("[CAL] armed W"+I(i));}
   else if(a=="cancel"){if(w.cal==1||w.cal==2)stopM(i);w.cal=0;}
   else if(a=="reset"){C.T[i][0]=5;C.T[i][1]=3;C.sn[i][0]=C.sn[i][1]=0;C.V[i][0]=C.V[i][1]=0;C.Tb[i][0]=C.Tb[i][1]=0;C.sv[i]=10000;save();webLog("[CAL] reset W"+I(i));}
  }else if(d.containsKey("d")){
   int dd=d["d"]|0;
   if(!dd){cancelAll();stopM(i);}else if(w.mode==1&&w.d==dd)w.beat=millis();else startM(i,dd,1);
  }else if(d.containsKey("auto")){
   if(w.cal==3)stopM(i);else if(w.pd)w.pm=3;else if(w.d)w.mode=3;else autoCmd(i,1,w.last,0);
  }else if(d.containsKey("goto")){autoCmd(i,0,0,d["goto"].as<float>());}
  else{w.cal=0;}
  curSrc=1;bleAck(id,true);return;}
 if(!strcmp(c,"scene")){String n=d["name"]|"";bool r=startScene(n);bleAck(id,r,r?"":"scene");return;}
 if(!strcmp(c,"lock")){C.lock=d["v"].as<bool>();pulse(C.lock?LOCK_OUT:UNLOCK_OUT);save();bleAck(id,true);return;}
 if(!strcmp(c,"child")){C.child=d["v"].as<bool>();bleAck(id,true);return;}
 if(!strcmp(c,"autoLock")){C.autoLock=d["v"].as<bool>();save();bleAck(id,true);return;}
 if(!strcmp(c,"autoRain")){C.autoRain=d["v"].as<bool>();save();bleAck(id,true);return;}
 if(!strcmp(c,"tap")){C.tap=constrain((int)d["v"].as<int>(),60,800);save();bleAck(id,true);return;}
 if(!strcmp(c,"mx")){C.mx=constrain((int)d["v"].as<int>(),3,30);save();bleAck(id,true);return;}
 if(!strcmp(c,"sag")){C.sag=d["v"].as<bool>();save();bleAck(id,true);return;}
 if(!strcmp(c,"sagv")){C.sagV=constrain(d["v"].as<float>(),0.05f,2.0f);save();bleAck(id,true);return;}
 if(!strcmp(c,"gest")){gEn=d["v"].as<bool>()?1:0;pf.putUChar("gs",gEn);bleAck(id,true);return;}
 if(!strcmp(c,"gup")){gUp=constrain((int)d["v"].as<int>(),0,2);pf.putUChar("gu",gUp);bleAck(id,true);return;}
 if(!strcmp(c,"gdn")){gDn=constrain((int)d["v"].as<int>(),0,2);pf.putUChar("gd",gDn);bleAck(id,true);return;}
 if(!strcmp(c,"gtu")){gTu=constrain((int)d["v"].as<int>(),0,5);pf.putUChar("gtu",gTu);bleAck(id,true);return;}
 if(!strcmp(c,"gtd")){gTd=constrain((int)d["v"].as<int>(),0,5);pf.putUChar("gtd",gTd);bleAck(id,true);return;}
 if(!strcmp(c,"time")){uint32_t u=(uint32_t)d["v"].as<uint32_t>();if(u>1600000000UL)tBase=u-millis()/1000;bleAck(id,true);return;}
 if(!strcmp(c,"wifi")){bool on=d["on"].as<bool>();applyWifi(on);bleAck(id,true,nullptr,"\"wifi\":"+String(wifiOn?1:0));return;}
 if(!strcmp(c,"led")){
  if(d.containsKey("parked")){ledParked=d["parked"].as<bool>();}
  if(d.containsKey("k")){
   int i=ledIndex(String(d["k"]|""));if(i<0){bleAck(id,false,"led");return;}LedCfg&L=LEDS[i];
   if(d.containsKey("on"))L.on=d["on"].as<bool>();
   if(d.containsKey("inst"))L.inst=d["inst"].as<bool>();
   if(d.containsKey("mode")){JsonVariant m=d["mode"];L.mode=m.is<const char*>()?ledModeIdx(String(m.as<const char*>())):constrain(m.as<int>(),0,4);}
   if(d.containsKey("br"))L.br=constrain((int)d["br"].as<int>(),5,100);
   if(d.containsKey("sp"))L.sp=constrain((int)d["sp"].as<int>(),1,10);
   if(d.containsKey("pat")){JsonVariant p=d["pat"];L.pat=p.is<const char*>()?ledPatIdx(String(p.as<const char*>())):constrain(p.as<int>(),0,2);}
   if(d.containsKey("rb"))L.rb=d["rb"].as<bool>();
   if(d.containsKey("col"))L.col=colParse(String(d["col"]|"#FFFFFF"));
   if(d.containsKey("pin")){if(!ledSetPin(i,(int)d["pin"].as<int>())){bleAck(id,false,"pin_busy");return;}}
  }
  saveLeds();bleAck(id,true,nullptr,"\"parked\":"+String(ledParked?1:0));return;}
 if(!strcmp(c,"ledpin")){int i=ledIndex(String(d["k"]|""));int p=(int)d["pin"].as<int>();if(i<0){bleAck(id,false,"led");return;}bool r=ledSetPin(i,p);if(r)saveLeds();bleAck(id,r,r?"":"pin_busy");return;}
 if(!strcmp(c,"test")){
  int r=d["r"]|0;
  if(r<0||r>3||W[0].d||W[1].d||W[0].pd||W[1].pd||testPin){bleAck(id,false,"busy");return;}
  int i=r/2;testWin=i;testPin=(r%2)?RDN[i]:RUP[i];digitalWrite(testPin,HIGH);testEnd=millis()+300;
  C.sv[i]=min(10000.0f,C.sv[i]+25.0f);webLog("[TEST] relay "+I(r));bleAck(id,true);return;}
 if(!strcmp(c,"diag")){bleSendChunks(diagJson(),id,"diag");return;}
 if(!strcmp(c,"logs")){String s;for(int k=0;k<LOGN;k++){String&m=LB[(li+k)%LOGN];if(m.length())s+=m+"\n";}bleSendChunks(s,id,"logs");return;}
 if(!strcmp(c,"csv")){bleSendChunks(csvData(),id,"csv");return;}
 if(!strcmp(c,"cfgget")){bleSendChunks(pf.getString("ap","{}"),id,"cfg");return;}
 if(!strcmp(c,"cfgset")){String ap;serializeJson(d["appearance"],ap);String full="{\"appearance\":"+ap+"}";if(full.length()>512){bleAck(id,false,"big");return;}pf.putString("ap",full);bleAck(id,true);return;}
 if(!strcmp(c,"pin")){String o=d["old"]|"",n=d["new"]|"";if(o!=String(devPin)){bleAck(id,false,"pin");return;}if(n.length()>8){bleAck(id,false,"len");return;}n.toCharArray(devPin,sizeof(devPin));pf.putString("pin",n);bleAck(id,true);return;}
 if(!strcmp(c,"logclr")){LG.count=0;LG.head=0;lgSave();lgDirty=false;bleAck(id,true);return;}
 if(!strcmp(c,"reboot")){bleAck(id,true);delay(150);ESP.restart();return;}
 bleAck(id,false,"cmd");
}

// ---------------- BLE glue / loop ----------------
class BleServerCB:public NimBLEServerCallbacks{
 void onConnect(NimBLEServer*,ble_gap_conn_desc*desc)override{bleConnected=true;bleAuthed=false;bleToken="";bleBuf="";bleConnHandle=desc?desc->conn_handle:0xFFFF;}
 void onDisconnect(NimBLEServer*)override{bleConnected=false;bleAuthed=false;bleToken="";bleBuf="";bleConnHandle=0xFFFF;NimBLEDevice::startAdvertising();}
};
class BleRxCB:public NimBLECharacteristicCallbacks{
 void onWrite(NimBLECharacteristic*c)override{
  std::string v=c->getValue();
  for(size_t i=0;i<v.size();i++){char ch=v[i];
   if(ch=='\n'){if(bleBuf.length()){char*s=strdup(bleBuf.c_str());if(s){if(xQueueSend(bleQ,&s,0)!=pdTRUE)free(s);}bleBuf="";}}
   else if(ch!='\r'&&bleBuf.length()<2048)bleBuf+=ch;
  }}
};
static void initBle(){
 NimBLEDevice::init("PrideCar");
 NimBLEDevice::setMTU(517);
 bleServer=NimBLEDevice::createServer();
 bleServer->setCallbacks(new BleServerCB());
 NimBLEService*svc=bleServer->createService(BLE_SVC_UUID);
 NimBLECharacteristic*rx=svc->createCharacteristic(BLE_RX_UUID,NIMBLE_PROPERTY::WRITE|NIMBLE_PROPERTY::WRITE_NR);
 rx->setCallbacks(new BleRxCB());
 bleTx=svc->createCharacteristic(BLE_TX_UUID,NIMBLE_PROPERTY::NOTIFY);
 svc->start();
 NimBLEAdvertising*adv=NimBLEDevice::getAdvertising();
 adv->addServiceUUID(BLE_SVC_UUID);
 adv->setName("PrideCar");
 adv->setScanResponse(true);
 adv->start();
 bleQ=xQueueCreate(10,sizeof(char*));
 webLog("[BLE] ready as PrideCar");
}
static void bleLoop(){
 if(bleQ){char*line=nullptr;int g=0;while(xQueueReceive(bleQ,&line,0)==pdTRUE&&g++<5){if(line){bleHandleLine(String(line));free(line);}}}
 if(!bleConnected||!bleAuthed)return;
 uint32_t now=millis();
 if(!otaBusy){if((int32_t)(now-bleNextState)>=0){bleNextState=now+500;blePushLine(stateJson()+"\n");}}   // در حین OTA پوش state را متوقف کن
 if(EID!=bleLastEid){bleLastEid=EID;blePushLine("{\"t\":\"event\",\"eid\":"+String(EID)+",\"msg\":\""+jsonEsc(EV)+"\"}\n");}
}

// ---------------- HTTP /led (پنل وب حالا واقعاً چراغ‌ها را کنترل می‌کند) ----------------
void hLed(){
 GUARD
 if(srv.hasArg("parked")){ledParked=srv.arg("parked").toInt()!=0;saveLeds();ok();return;}
 int i=ledIndex(srv.arg("k"));if(i<0){srv.send(400,"text/plain","bad_led");return;}LedCfg&L=LEDS[i];
 if(srv.hasArg("on"))L.on=srv.arg("on").toInt()!=0;
 if(srv.hasArg("inst"))L.inst=srv.arg("inst").toInt()!=0;
 if(srv.hasArg("mode"))L.mode=ledModeIdx(srv.arg("mode"));
 if(srv.hasArg("br"))L.br=constrain(srv.arg("br").toInt(),5,100);
 if(srv.hasArg("sp"))L.sp=constrain(srv.arg("sp").toInt(),1,10);
 if(srv.hasArg("pat"))L.pat=ledPatIdx(srv.arg("pat"));
 if(srv.hasArg("rb"))L.rb=srv.arg("rb").toInt()!=0;
 if(srv.hasArg("col"))L.col=colParse(srv.arg("col"));
 saveLeds();ok();}

void setup(){
 Serial.begin(115200);
 for(int i=0;i<2;i++){pinMode(RUP[i],OUTPUT);pinMode(RDN[i],OUTPUT);relaysOff(i);}
 #if HAS_LOCK_RELAY
 pinMode(LOCK_OUT,OUTPUT);pinMode(UNLOCK_OUT,OUTPUT);digitalWrite(LOCK_OUT,LOW);digitalWrite(UNLOCK_OUT,LOW);
 #endif
 for(int k=0;k<NT;k++)pinMode(TC[k].pin,INPUT_PULLDOWN);       // پایه شناور = لمس فرضی نمی‌شود
 Wire.begin(8,9);pf.begin("win",false);
 {Cfg tmp;bool okc=false;if(pf.getBytesLength("c")==sizeof(C)){pf.getBytes("c",&tmp,sizeof(C));okc=(tmp.ver==CFG_VER);}C=okc?tmp:Cfg();}   // ساختار/نسخه ناهمخوان = پیش‌فرض امن
 uint8_t dm=pf.getUChar("dirty",0);
 for(int i=0;i<2;i++){if(dm&(1<<i)||(C.dirty&(1<<i)))C.sv[i]=10000;else C.sv[i]=min(10000.0f,C.sv[i]+36.0f);}   // I6 + هر بار روشن‌شدن ±۶٪ بیشتر
 C.dirty=0;gDirty=dm;if(dm){pf.putUChar("dirty",0);gDirty=0;}
 C.tap=constrain((int)C.tap,60,800);C.mx=constrain((int)C.mx,3,30);C.sagV=constrain(C.sagV,0.05f,2.0f);
 for(int i=0;i<2;i++)for(int k=0;k<2;k++)if(!(C.T[i][k]>0.5f&&C.T[i][k]<60.0f))C.T[i][k]=k?3.0f:5.0f;
 gEn=pf.getUChar("gs",1);gUp=pf.getUChar("gu",0);gDn=pf.getUChar("gd",0);if(gUp>2)gUp=0;if(gDn>2)gDn=0;gTu=pf.getUChar("gtu",0);gTd=pf.getUChar("gtd",1);if(gTu>5)gTu=0;if(gTd>5)gTd=1;
 if(pf.getUChar("v54",0)==0){if(C.tap<250)C.tap=250;pf.putUChar("v54",1);save();}   // یک‌بار: مرز ۱۰۰ms برای انگشت واقعی خیلی کم بود
 loadEv();
 loadLg();
 {float v=readVolt();Vrest=(v>6&&v<18)?v:0;}
 String dp=pf.getString("pin","1234");dp.toCharArray(devPin,sizeof(devPin));
 loadLeds();
 wifiOn=pf.getUChar("wf",1)!=0;
 if(wifiOn){WiFi.softAP(ssid,password);webLog("[SYS] AP "+String(ssid));}else{WiFi.mode(WIFI_OFF);webLog("[SYS] AP off (guest Wi-Fi disabled)");}
#if USE_MDNS
 if(wifiOn&&MDNS.begin("pride"))MDNS.addService("http","tcp",80);
#endif
#ifdef API_PIN
 {const char*hk[]={"X-Key"};srv.collectHeaders(hk,1);}
#endif
 srv.on("/",hRoot);srv.on("/cmd",hCmd);srv.on("/get_sensors",hGet);srv.on("/logs",hLogs);srv.on("/diag",hDiag);srv.on("/test",hTest);srv.on("/log.csv",hCsv);srv.on("/logclr",hLogClr);
 srv.on("/led",hLed);
 srv.on("/get_config",[](){srv.send(200,"application/json",pf.getString("ap","{}"));});
 srv.on("/set_config",HTTP_POST,[](){GUARD String b=srv.arg("plain");if(b.length()>512||b.indexOf("appearance")<0){srv.send(400,"text/plain","bad_config");return;}pf.putString("ap",b);ok();});
 // سازگاری با مسیرهای قدیمی (همه از همان صف عبور می‌کنند)
 srv.on("/d_up",[](){GUARD autoCmd(0,0,0,0);ok();});srv.on("/d_down",[](){GUARD autoCmd(0,0,0,100);ok();});srv.on("/d_stop",[](){GUARD cancelAll();stopM(0);ok();});
 srv.on("/p_up",[](){GUARD autoCmd(1,0,0,0);ok();});srv.on("/p_down",[](){GUARD autoCmd(1,0,0,100);ok();});srv.on("/p_stop",[](){GUARD cancelAll();stopM(1);ok();});
 srv.on("/vent",[](){GUARD startScene("vent");ok();});
 srv.on("/lock",[](){GUARD pulse(LOCK_OUT);ok();});srv.on("/unlock",[](){GUARD pulse(UNLOCK_OUT);ok();});
 srv.on("/set_time",ok);   // دیگر لازم نیست؛ زمان‌ها با کالیبراسیون یاد گرفته می‌شوند
#ifdef OTA_AUTH_USER
 ElegantOTA.setAuth(OTA_AUTH_USER,OTA_AUTH_PASS);
#endif
 ElegantOTA.onStart([](){otaBusy=true;otaT=millis();cancelAll();stopM(0);stopM(1);relaysOff(0);relaysOff(1);if(lgDirty){lgDirty=false;lgSave();}feedWdt();});   // I1: رله‌ها هنگام OTA خاموش
 ElegantOTA.onProgress([](size_t c,size_t f){(void)c;(void)f;feedWdt();});
 ElegantOTA.onEnd([](bool ok){(void)ok;pf.putUChar("wf",1);});   // پس از پایان OTA، وای‌فای حتماً روشن بماند
 ElegantOTA.begin(&srv);if(wifiOn)srv.begin();
 initBle();
#if USE_HW_WDT
#if defined(ESP_ARDUINO_VERSION_MAJOR)&&ESP_ARDUINO_VERSION_MAJOR>=3
 {esp_task_wdt_config_t wc;wc.timeout_ms=WDT_S*1000;wc.idle_core_mask=0;wc.trigger_panic=true;esp_task_wdt_reconfigure(&wc);}
#else
 esp_task_wdt_init(WDT_S,true);
#endif
 esp_task_wdt_add(NULL);
#endif
 webLog("[SYS] Ready v6.0");}

void loop(){
 feedWdt();
 srv.handleClient();ElegantOTA.loop();uint32_t now=millis();
 bleLoop();ledTick(now);
 bool moving=W[0].d||W[1].d;
 if(moving&&(int32_t)(now-vNext)>=0){vNext=now+25;sampleV(now);}   // ولتاژ حین حرکت (برای اصلاح سرعت و افت ناگهانی)
 else if(!moving&&(int32_t)(now-vrNext)>=0){vrNext=now+1000;float v=readVolt();Vrest=(v>6&&v<18)?v:0;}   // ولتاژ در حالت ایستاده (قفل ولتاژ پایین)
 if(testPin&&(int32_t)(now-testEnd)>=0){if(!W[testWin].d)digitalWrite(testPin,LOW);testPin=0;}
 if(pulsePin&&(int32_t)(now-pulseEnd)>=0){digitalWrite(pulsePin,LOW);pulsePin=0;}
 for(int i=0;i<2;i++)if(W[i].cal==3&&!W[i].d&&(uint32_t)(now-W[i].calArm)>CAL_ARM_MS){W[i].cal=0;webLog("[CAL] arm expired W"+I(i));}
 for(int k=0;k<NT;k++){Touch&t=TC[k];bool r=digitalRead(t.pin);
  if(r!=t.raw){if(!r&&!t.on&&now-t.tr<DEB)t.gl++;t.raw=r;t.tr=now;}   // شمارش نویز کوتاه
  if(now<3000){t.tr=now;continue;}                      // ۳ ثانیه اول بعد از روشن‌شدن نادیده گرفته می‌شود
  bool p=(now-t.tr>=DEB)?t.raw:t.on;                    // دیبانس: لمس باید ۴۰ms پایدار باشد (نویز = لمس نیست)
  bool allow=!(t.pas&&C.child)&&!(t.w==1&&W[0].d)&&!testPin;Win&w=W[t.w];   // شیشه راننده اولویت دارد
  bool gpad=(t.w==0&&!t.pas);                           // فقط پدهای راننده برای شیشه راننده ژست دارند
  if(p&&!t.on){t.on=1;t.t0=now;cancelAll();t.act=false;t.sup=false;curSrc=0;bool q3=t.qOk3&&(uint32_t)(now-t.qEnd3)<=GEST_GAP_MS;t.qOk3=false;t.dbl=false;   // دخالت دستی: صف و صحنه لغو می‌شود
   if(gEn&&gpad&&q3&&w.cal==0&&!testPin&&sceneOf3(t.d)){t.qOk=false;t.sup=true;fireTriple(t.d);}   // تپ سوم داخل فاصله = سه‌تپ (جایگزین سناریوی دابل‌تپ)
   else if(gEn&&gpad&&t.qOk&&(uint32_t)(now-t.qEnd)<=GEST_GAP_MS&&w.cal==0&&!testPin){t.qOk=false;t.sup=true;t.dbl=true;fireGesture(t.d);}   // تپ دوم داخل فاصله = ژست
   else if(w.mode==4){}                                    // کالیبراسیون: لمس نادیده
   else if(w.d&&w.d==t.d)stopM(t.w);                       // همان جهتِ در حال حرکت = توقف
   else if(allow){startM(t.w,t.d,1);t.act=true;}}          // ایستاده یا جهت مخالف = شروع / تغییر جهت فوری
  else if(p&&w.mode==1&&w.d==t.d&&!t.sup)w.beat=now;
  else if(!p&&t.on){t.on=0;uint32_t dur=now-t.t0;t.dur=(uint16_t)min(dur,(uint32_t)65535);
   if(t.sup){t.sup=false;if(t.dbl){t.dbl=false;t.qOk3=dur<GEST_TAP_MS;t.qEnd3=now;}}   // رها کردن تپ دوم/سومِ ژست: کاری ندارد؛ اگر تپ دوم سریع بود، تپ سوم ممکن است
   else{
    t.qOk=gpad&&t.act&&dur<GEST_TAP_MS;t.qEnd=now;         // فقط تپی که خودش حرکت را شروع کرده می‌تواند تپ اولِ ژست باشد
    if(w.pd==t.d&&w.pm==1){if(dur<C.tap&&w.cal!=3)w.pm=3;else w.pd=0;}      // رها شدن قبل از پایان زمان مرده
    else if(w.d==t.d&&w.mode==1){if(dur<C.tap&&w.cal!=3)w.mode=3;else stopM(t.w);}}}}
 curSrc=1;tick(0,now);tick(1,now);pump(now);
 bool quiet=!W[0].d&&!W[1].d&&(uint32_t)(now-lastStopT)>3000;   // ۳ ثانیه بعد از آخرین توقف (قبل از قطع برق با خاموش شدن ماشین)
 if(quiet&&lgDirty){lgDirty=false;lgSave();}
 if(quiet&&evDirty&&(uint32_t)(now-evSaveT)>10000UL){evDirty=false;evSaveT=now;saveEv();}}