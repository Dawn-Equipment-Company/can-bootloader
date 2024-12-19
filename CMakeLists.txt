idf_component_register(
    SRCS can_bootload.c
    INCLUDE_DIRS .
    PRIV_REQUIRES bootloader_support driver app_update
    REQUIRES 
)