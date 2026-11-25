# Contributing

Thanks for wanting to help. The most useful contributions at the moment are **reports from other printers**, since the project has only been tested on a Samsung Xpress M2020W.

## Reporting a printer

Open an issue with the "Printer report" form. It asks for the model, what worked and the printer's device ID, which you can read from the ESP32's log at `http://printer.local/api/log` right after connecting the printer (look for the line starting `device id:`).

## Reporting a bug or suggesting something

Use the bug report or feature request form. For a bug, include the ESP32's log (`/api/log`) and what you did, and remove your WiFi name and any other private details first.

## Changing the code

You'll need [ESP-IDF](https://docs.espressif.com/projects/esp-idf/) 5.5 for the firmware.

```bash
idf.py set-target esp32s3
idf.py build
```

The parsing and conversion code has tests that run on a computer. Please run them before you open a pull request:

```bash
make -C test/host test
```

A few conventions:

- Keep changes small and focused, and explain why in the pull request.
- Add a test for new parsing or conversion code, in `test/host/`.
- Match the style of the file you're editing, and keep comments to what isn't obvious from the code.
- New source files start with the two-line licence header you'll see in the existing ones.
- User-facing text uses plain wording, "WiFi" spelled that way, and the web page stays free of anything that loads from other sites.

For the installer in `web/`, run `sh tools/vendor-web-libs.sh` once to download its libraries, then serve the folder locally, for example with `python3 -m http.server`. The page can flash a real ESP32 from `localhost` in Chrome or Edge.

## Licence of contributions

Pluto Print is licensed under the GNU General Public License version 3 or any later version. By contributing, you agree that your contribution is licensed the same way, and that the credit "Pluto Print by UnluckyQuote" described in `NOTICE.md` stays in place.
