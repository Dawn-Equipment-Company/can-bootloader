idf_component_register(
    SRCS src/can_bootload.c
    INCLUDE_DIRS src
    PRIV_REQUIRES bootloader_support driver app_update
    REQUIRES 
)