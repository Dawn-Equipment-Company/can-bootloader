
#include <string.h>
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_flash_partitions.h"
#include "esp_partition.h"
#include "driver/twai.h"
#include "can_bootload.h"

#define BUFFSIZE 1024

#define TAG "CAN_BOOTLOAD"
uint32_t OTA_BEGIN_HEADER = 0x18E30099;
uint32_t OTA_END_SEGMENT = 0x18E40099;
uint32_t OTA_COMPLETE_HEADER = 0x18E50099;
uint32_t OTA_TP_DATA_HEADER = 0x18EB0099;
uint32_t OTA_TP_CTRL_HEADER = 0x18EC0099;

uint32_t OTA_TP_CTRL_TX_HEADER = 0x18EC9900;
uint32_t OTA_STATUS_TX_HEADER = 0x18E32100;

/*an ota data write buffer ready to write to the flash*/
static char ota_write_data[BUFFSIZE + 1] = {0};
static esp_ota_handle_t update_handle = 0;
const esp_partition_t *update_partition = NULL;


static UpdateState state = UPDATE_Idle;

UpdateState Bootload_current_state(void) {
    return state;
}

void Bootload_task(void *pvParameters)
{
    twai_message_t msg;
    msg.identifier = OTA_STATUS_TX_HEADER;
    msg.extd = 1;
    msg.rtr = 0;
    msg.data_length_code = 1;

    while(true)
    {
        msg.data[0] = (uint8_t)state;
        if (twai_transmit(&msg, pdMS_TO_TICKS(10)) != ESP_OK)
        {
            ESP_LOGW(TAG, "Failed to queue status message for transmission\n");
        }

        vTaskDelay(3000 / portTICK_PERIOD_MS);
    }
}

void Bootload_init(uint32_t addr)
{
    ESP_LOGI(TAG, "Starting CAN bootloader");
    OTA_TP_CTRL_TX_HEADER = OTA_TP_CTRL_TX_HEADER | addr;
    OTA_STATUS_TX_HEADER = OTA_STATUS_TX_HEADER | addr;
    OTA_BEGIN_HEADER = OTA_BEGIN_HEADER | (addr << 8);
    OTA_TP_CTRL_HEADER = OTA_TP_CTRL_HEADER | (addr << 8);
    OTA_TP_DATA_HEADER = OTA_TP_DATA_HEADER | (addr << 8);
    OTA_END_SEGMENT = OTA_END_SEGMENT | (addr << 8);
    OTA_COMPLETE_HEADER = OTA_COMPLETE_HEADER | (addr << 8);

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
}

void Bootload_rx(twai_message_t rx_msg)
{
    esp_err_t err;
    static uint16_t data_count = 0;
    static uint16_t offset = 0;
    static uint16_t segment_byte_count = 0;
    if (rx_msg.identifier == OTA_BEGIN_HEADER)
    {
        err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
            esp_ota_abort(update_handle);
        }
        ESP_LOGI(TAG, "esp_ota_begin succeeded");
        state = UPDATE_InProgress;
    }
    else if (rx_msg.identifier == OTA_TP_CTRL_HEADER)
    {
        // RTS message incoming
        if (rx_msg.data[0] == 16)
        {
            //ESP_LOGI(TAG, "prev data count: %i", data_count);
            data_count = 0;
            uint16_t byte_count = (rx_msg.data[1]) | ((uint16_t)(rx_msg.data[2] << 8));
            uint8_t packet_count = rx_msg.data[3];
            //ESP_LOGI(TAG, "TP segment: %i bytes in %i packets", byte_count, packet_count);

            // send CTS
            static twai_message_t message;
            message.identifier = OTA_TP_CTRL_TX_HEADER;
            message.extd = 1;
            message.rtr = 0;
            message.data_length_code = 1;
            message.data[0] = 19;
            twai_transmit(&message, pdMS_TO_TICKS(10));
        }
    }
    else if (rx_msg.identifier == OTA_TP_DATA_HEADER)
    {
        segment_byte_count += rx_msg.data_length_code - 1;
        // ESP_LOGI(TAG, "TP data: %u", rx_msg.data[0]);
        data_count++;
        offset = rx_msg.data[0] * 7;
        memcpy(ota_write_data + offset, rx_msg.data + 1, rx_msg.data_length_code - 1);
    }
    else if (rx_msg.identifier == OTA_END_SEGMENT)
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
    else if (rx_msg.identifier == OTA_COMPLETE_HEADER)
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
        state = UPDATE_Complete;

        err = esp_ota_set_boot_partition(update_partition);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(err));
        }
        ESP_LOGI(TAG, "Prepare to restart system!");
        esp_restart();
    }
}