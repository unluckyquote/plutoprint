idf_component_register(SRCS "improv.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver wifi_mgr settings esp_netif app_update)
