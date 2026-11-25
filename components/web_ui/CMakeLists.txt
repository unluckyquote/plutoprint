idf_component_register(SRCS "web_ui.c"
                       INCLUDE_DIRS "."
                       EMBED_TXTFILES "www/index.html" "www/app.css" "www/app.js" "www/favicon.svg"
                       REQUIRES esp_http_server esp_wifi app_update esp_system esp_timer json job_log
                                urf_testpage logbuf printer_caps wifi_mgr mbedtls settings printer_state print_server usb_printer)
