#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/twai.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "can_bootload.h"

#define TAG "version_a"

#define TX_GPIO_NUM GPIO_NUM_1
#define RX_GPIO_NUM GPIO_NUM_0
static const twai_timing_config_t t_config = TWAI_TIMING_CONFIG_250KBITS();
static const twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
static const twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(TX_GPIO_NUM, RX_GPIO_NUM, TWAI_MODE_NORMAL);

void app_setup(void)
{
    // Initialize NVS.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        // OTA app partition table has a smaller NVS partition size than the non-OTA
        // partition table. This size mismatch may cause NVS initialization to fail.
        // If this happens, we erase NVS partition and initialize NVS again.
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(twai_driver_install(&g_config, &t_config, &f_config));
    ESP_LOGI(TAG, "CAN Driver installed");
    if (twai_start() != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start CAN driver");
    }
}

void app_main(void)
{
    static twai_message_t rx_msg;
	app_setup();

	printf("THIS IS VERSION A\r\n");

	Bootload_init();
	while(true)
	{
        if (twai_receive(&rx_msg, pdMS_TO_TICKS(10)) == ESP_OK)
        {
			Bootload_rx(rx_msg);
		}
	}
}
