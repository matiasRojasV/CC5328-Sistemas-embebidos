idf_component_register(
  SRCS "main.c" "sensores/acelerometro.c" "sensores/ambiente.c"
  INCLUDE_DIRS "." "sensores"
  REQUIRES esp_driver_uart esp_driver_gpio esp_timer
)