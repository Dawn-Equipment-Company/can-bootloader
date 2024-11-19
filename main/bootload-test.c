#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "driver/twai.h"
#include "esp_flash_partitions.h"
#include "esp_partition.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "errno.h"
#include "can_bootload.h"

#define TX_GPIO_NUM GPIO_NUM_1
#define RX_GPIO_NUM GPIO_NUM_0
static const twai_timing_config_t t_config = TWAI_TIMING_CONFIG_250KBITS();
static const twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
static const twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(TX_GPIO_NUM, RX_GPIO_NUM, TWAI_MODE_NORMAL);
#define BUFFSIZE 1024
#define HASH_LEN 32 /* SHA-256 digest length */

static const char *TAG = "native_ota_example";
/*an ota data write buffer ready to write to the flash*/
static char ota_write_data[BUFFSIZE + 1] = {0};

void app_main(void)
{
    printf("hgello\r\n");
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

    /* update handle : set by esp_ota_begin(), must be freed via esp_ota_end() */
    esp_ota_handle_t update_handle = 0;
    const esp_partition_t *update_partition = NULL;

    ESP_LOGI(TAG, "Starting OTA example task");

    const esp_partition_t *configured = esp_ota_get_boot_partition();
    const esp_partition_t *running = esp_ota_get_running_partition();

    if (configured != running)
    {
        ESP_LOGW(TAG, "Configured OTA boot partition at offset 0x%08" PRIx32 ", but running from offset 0x%08" PRIx32,
                 configured->address, running->address);
        ESP_LOGW(TAG, "(This can happen if either the OTA boot data or preferred boot image become corrupted somehow.)");
    }
    ESP_LOGI(TAG, "Running partition type %d subtype %d (offset 0x%08" PRIx32 ")",
             running->type, running->subtype, running->address);

    update_partition = esp_ota_get_next_update_partition(NULL);
    assert(update_partition != NULL);
    ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%" PRIx32,
             update_partition->subtype, update_partition->address);

    static twai_message_t rx_msg;

    static uint16_t data_count = 0;
    uint16_t offset = 0;
    uint16_t segment_byte_count = 0;
    while (true)
    {
        if (twai_receive(&rx_msg, pdMS_TO_TICKS(10)) == ESP_OK)
        {
            // isotp control messages
            if(rx_msg.identifier == 0x18EC7799)
            {
                // RTS message incoming
                if(rx_msg.data[0] == 16)
                {
                    ESP_LOGI(TAG, "prev data count: %i", data_count);
                    data_count = 0;
                    uint16_t byte_count = (rx_msg.data[1]) | ((uint16_t)(rx_msg.data[2] << 8));
                    uint8_t packet_count = rx_msg.data[3];
                    ESP_LOGI(TAG, "TP segment: %i bytes in %i packets", byte_count, packet_count);
                    //segment_byte_count = byte_count;

                    // send CTS
                    static twai_message_t message;
                    message.identifier = 0x18EC9977;
                    message.extd = 1;
                    message.rtr = 0;
                    message.data_length_code = 1;
                    message.data[0] = 19;
                    twai_transmit(&message, pdMS_TO_TICKS(10));
                }
            }
            if(rx_msg.identifier == 0x18EB7799)
            {
                segment_byte_count += rx_msg.data_length_code - 1;
                //ESP_LOGI(TAG, "TP data: %u", rx_msg.data[0]);
                data_count++;
                offset = rx_msg.data[0] * 7;
                //printf("of: %i", offset);
                memcpy(ota_write_data + offset, rx_msg.data + 1, rx_msg.data_length_code - 1);
            }
            // 
            else if(rx_msg.identifier == 0x18E32399)
            {
                ESP_LOGI(TAG, "End of segment, writing %i bytes to ota memory", segment_byte_count);
                err = esp_ota_write(update_handle, (const void *)ota_write_data, segment_byte_count);
                if (err != ESP_OK)
                {
                    ESP_LOGE(TAG, "error writing ota data");
                }
                memset(ota_write_data, 0, BUFFSIZE);
                segment_byte_count = 0;
            }
            // data incoming for ota
            if (rx_msg.identifier == 0x18E32099)
            {
                err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
                if (err != ESP_OK)
                {
                    ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
                    esp_ota_abort(update_handle);
                }
                ESP_LOGI(TAG, "esp_ota_begin succeeded");
            }
            else if (rx_msg.identifier == 0x18E32199)
            {
                /*size_t data_read = rx_msg.data_length_code;
                ESP_LOGI(TAG, "data rx %u", data_read);
                err = esp_ota_write(update_handle, (const void *)rx_msg.data, data_read);
                if (err != ESP_OK)
                {
                    ESP_LOGE(TAG, "error writing ota data");
                }*/
            }
            else if (rx_msg.identifier == 0x18E32299)
            {
                // update complete
                err = esp_ota_end(update_handle);
                if (err != ESP_OK)
                {
                    if (err == ESP_ERR_OTA_VALIDATE_FAILED)
                    {
                        ESP_LOGE(TAG, "Image validation failed, image is corrupted");
                    }
                    else
                    {
                        ESP_LOGE(TAG, "esp_ota_end failed (%s)!", esp_err_to_name(err));
                    }
                }

                err = esp_ota_set_boot_partition(update_partition);
                if (err != ESP_OK)
                {
                    ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(err));
                }
                ESP_LOGI(TAG, "Prepare to restart system!");
                esp_restart();
            }
        }
    }
}