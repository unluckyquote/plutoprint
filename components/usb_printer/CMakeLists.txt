idf_component_register(SRCS "usb_printer.c"
                       INCLUDE_DIRS "."
                       REQUIRES usb esp_event freertos)
