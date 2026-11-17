idf_component_register(
    SRCS
        "app.cpp"
        "main.cpp"
        "navigation_runtime.cpp"
        "st7565_display.cpp"
        "subsystem_status.cpp"
        "system_diagnostics.cpp"
        "uart_gps_source.cpp"
        "ble_peripheral.cpp"
        "../../core/src/framebuffer.cpp"
        "../../core/src/geo.cpp"
        "../../core/src/gps.cpp"
        "../../core/src/gps_simulator.cpp"
        "../../core/src/map_data.cpp"
        "../../core/src/map_renderer.cpp"
        "../../core/src/navigation.cpp"
        "../../core/src/nmea.cpp"
        "../../core/src/route_data.cpp"
        "../../protocol/src/protocol.cpp"
    INCLUDE_DIRS "." "../../core/include" "../../protocol/include"
    REQUIRES bt nvs_flash esp_app_format esp_driver_gpio esp_driver_spi esp_driver_uart esp_psram esp_system esp_timer heap spi_flash
)

target_add_binary_data(${COMPONENT_LIB}
    "${CMAKE_CURRENT_LIST_DIR}/../../data/amsterdam_demo.bmap"
    BINARY RENAME_TO "amsterdam_demo_bmap")
target_add_binary_data(${COMPONENT_LIB}
    "${CMAKE_CURRENT_LIST_DIR}/../../data/amsterdam_demo.brte"
    BINARY RENAME_TO "amsterdam_demo_brte")

