# MANIFEST فریم‌ورها

فریم‌ور آمادهٔ فلش برای ESP32-S3 (فرمت `.bin` برای فلش با `esptool`/PlatformIO/Arduino IDE).

## نسخهٔ رسمی

| فایل | حجم | تاریخ | توضیح |
|------|-----|-------|--------|
| [`car-control-pro-v5.6.bin`](car-control-pro-v5.6.bin) | ۱٫۴۴ MB | ۲۰۲۶-۱۰-۲۰ | آخرین نسخهٔ پایدار؛ شامل خروجی CSV لاگ و پنل کامل |

```bash
# PlatformIO
pio run -t upload --upload-port COMx

# esptool
esptool.py --chip esp32s3 --port COMx --baud 460800 write_flash 0x0 car-control-pro-v5.6.bin
```

> سورس فعلی مخزن (`src/`) چند قابلیت جلوتر از این باینری است (پل BLE و کانال‌های LED).
> برای آخرین امکانات، firmware را از سورس بسازید: `pio run`.

## تاریخچهٔ نسخه‌ها (آرشیو)

این فایل‌ها برای مرجع و بازگشت به نسخه‌های قبلی نگه‌داری می‌شوند.
برچسب‌ها همان نام‌های کاری در طول توسعه بوده‌اند.

| فایل | حجم | برچسب اصلی | نشانه‌های قابل تشخیص در باینری |
|------|-----|-------------|--------------------------------------|
| `car-control-pro-v5.5.bin` | ۹۱۷ KB | `5.5` | ژست + خروجی CSV لاگ |
| `car-control-pro-v5.4.bin` | ۸۹۱ KB | `5.4` | ژست، بدون CSV |
| `car-control-pro-v5.3-b.bin` | ۸۹۲ KB | `clud 5.3` | خروجی CSV لاگ |
| `car-control-pro-v5.3-a.bin` | ۸۸۶ KB | `claud5.3` | بدون CSV |
| `car-control-pro-v5.2.bin` | ۷۵۵ KB | `v5.2gpt` | معماری چهار لایه (کوچک‌ترین باینری) |
| `car-control-pro-v5.1.bin` | ۸۶۲ KB | `v5.1gpt` | نسخهٔ پیش از ژست |
| `car-control-pro-v5.0-gpt.bin` | ۸۶۰ KB | `v5gpt` | نسخهٔ پیش از ژست |
| `car-control-pro-v3.0-beta.bin` | ۸۵۹ KB | `v3beta` | نسخهٔ time-based |
| `car-control-pro-v2.0-beta.bin` | ۸۵۸ KB | `v2beta` | اصلاح جهت لمسی |
| `car-control-pro-v1.0.bin` | ۸۵۳ KB | `v1claud` | اولین نسخهٔ قابل فلش |

## پارتیشن

همهٔ باینری‌ها با جدول پارتیشن `default.csv` ساخته شده‌اند (دو اسلات `app0`/`app1`) تا
**OTA** ممکن باشد. اگر firmware را دستی فلش می‌کنید، همان جدول پارتیشن را انتخاب کنید.

## نکتهٔ امنیتی

رمز پیش‌فرض AP در سورس `Car_Control_Pro` است. پیش از استفادهٔ واقعی، آن را در
`platformio.ini` یا بالای فایل `.ino` تغییر دهید و در صورت نیاز `API_PIN` را فعال کنید.
