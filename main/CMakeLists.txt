idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES logbuf improv driver wifi_mgr usb_printer print_server settings web_ui app_update)
