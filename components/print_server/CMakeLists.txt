idf_component_register(SRCS "raw9100.c" "printer_attrs.c" "ipp_server.c" "print_mdns.c" "print_state.c" "job_log_sync.c" "printer_detect.c" "printer_features.c"
                       INCLUDE_DIRS "."
                       REQUIRES printer_caps raster_convert pjl_frame http_min ipp usb_printer printer_state job_log mdns esp_wifi esp_timer settings)
