# Changelog

All notable changes are listed here. The project follows [Semantic Versioning](https://semver.org/).

## 1.0.0 - 2026-11-25

First release.

### Printing
- AirPrint over WiFi at 600 dpi for iPhone, iPad and Mac, announced over Bonjour
- Apple raster (URF) and PWG raster, grey and sRGB, with PWG converted to URF on the ESP32
- Documents are held in memory until they have fully arrived, so a dropped connection or a cancel never makes the printer print an error page
- Copies, by sending the stored document once per copy
- Paper sizes, trays and colour support read from the printer's device ID and status file, with switches for colour and two-sided printing
- Toner level in the web page and in macOS "Supply Levels" (black only)

### Setup and management
- Web installer for GitHub Pages: a guided, step-by-step setup that flashes the ESP32 from the browser
- WiFi setup over the serial port (Improv WiFi) and through a setup hotspot
- Web page with status, the last five jobs, settings, firmware update and an optional password for the changing actions
- Firmware updates over WiFi with automatic rollback
- Log readable over WiFi at `/api/log`

### Hardware
- Snap-close 3D-printable case (OpenSCAD source and STL files)

### Not yet tested
- Colour and two-sided printing, printers other than the Samsung Xpress M2020W, and PWG printing from Windows, Android and Linux clients
