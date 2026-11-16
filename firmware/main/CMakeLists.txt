idf_component_register(
    SRCS
        "app.cpp"
        "main.cpp"
        "subsystem_status.cpp"
        "system_diagnostics.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_app_format esp_psram esp_system esp_timer heap spi_flash
)

