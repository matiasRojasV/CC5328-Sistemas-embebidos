idf_component_register(
  SRCS "commUART.C" "main.c" "sensores/bme688.c"
  INCLUDE_DIRS "." "sensores"
  REQUIRES esp_driver_uart esp_driver_gpio esp_driver_i2c esp_timer
)