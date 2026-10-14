idf_component_register(SRCS "main.cpp" INCLUDE_DIRS "../../common"
    REQUIRES esp_wifi esp_event esp_netif esp_http_server nvs_flash esp_driver_uart json esp_timer
    EMBED_TXTFILES "../../../web/index.html" "../../../web/style.css" "../../../web/app.js" "../../../web/api.js" "../../../web/schema.json")
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
