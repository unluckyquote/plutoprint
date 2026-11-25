# Third-party software

Pluto Print is free software, copyright (C) 2026 UnluckyQuote, licensed under the GNU General Public License version 3 or any later version (see `LICENSE`).

**Credit (additional term under section 7(b) of the GPL).** Any version you distribute must keep the credit "Pluto Print by UnluckyQuote", with a link to <https://github.com/unluckyquote/plutoprint>, in its README or about text, together with the licence notices.

It builds on the following, which stay under their own licences. All of them are compatible with the GPL:

| Component | Licence | Where |
|---|---|---|
| [ESP-IDF](https://github.com/espressif/esp-idf) 5.5 (FreeRTOS, lwIP, mbedTLS, esp_http_server and others) | Apache-2.0 | Firmware |
| cJSON (bundled with ESP-IDF) | MIT | Firmware |
| [espressif/mdns](https://components.espressif.com/components/espressif/mdns) | Apache-2.0 | Firmware, downloaded at build time |
| [espressif/led_strip](https://components.espressif.com/components/espressif/led_strip) | Apache-2.0 | Firmware, downloaded at build time |
| [ESP Web Tools](https://github.com/esphome/esp-web-tools) and its Material Web components | Apache-2.0 | Installer page, downloaded at build time and served from the same site |
| [three.js](https://threejs.org/) | MIT | Installer page (case viewer), downloaded at build time and served from the same site |

The installer's copies of ESP Web Tools and three.js keep their own licence files in `vendor/` on the published site.

The Wi-Fi setup over the serial port follows the [Improv Wi-Fi](https://www.improv-wifi.com/) serial protocol.

"AirPrint" and "Apple" are trademarks of Apple Inc. "Samsung" is a trademark of Samsung Electronics. This project is not affiliated with or endorsed by either company.
