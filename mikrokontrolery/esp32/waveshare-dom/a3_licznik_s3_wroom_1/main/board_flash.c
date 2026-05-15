#include "board_flash.h"

#include <inttypes.h>

#include "esp_flash.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_spiffs.h"

static const char *TAG = "board_flash";

#define EXPECTED_FLASH_BYTES  (8U * 1024U * 1024U)
#define SPIFFS_MOUNT_PATH     "/storage"
#define SPIFFS_PARTITION      "storage"

esp_err_t board_flash_init(void)
{
    uint32_t flash_size = 0;
    esp_err_t err = esp_flash_get_physical_size(NULL, &flash_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_flash_get_physical_size: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Flash: %" PRIu32 " MB (%" PRIu32 " bytes)",
             flash_size / (1024U * 1024U), flash_size);

    if (flash_size < EXPECTED_FLASH_BYTES) {
        ESP_LOGW(TAG, "Expected >= 8 MB — check sdkconfig (ESPTOOLPY_FLASHSIZE_8MB)");
    }

    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY,
                                                   ESP_PARTITION_SUBTYPE_ANY, NULL);
    for (; it != NULL; it = esp_partition_next(it)) {
        const esp_partition_t *part = esp_partition_get(it);
        ESP_LOGI(TAG, "  %-8s %-8s @ 0x%06" PRIx32 "  size %" PRIu32 " KB",
                 part->label,
                 (part->type == ESP_PARTITION_TYPE_APP) ? "app" : "data",
                 part->address,
                 part->size / 1024U);
    }
    esp_partition_iterator_release(it);

    esp_vfs_spiffs_conf_t spiffs_cfg = {
        .base_path = SPIFFS_MOUNT_PATH,
        .partition_label = SPIFFS_PARTITION,
        .max_files = 5,
        .format_if_mount_failed = true,
    };
    err = esp_vfs_spiffs_register(&spiffs_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(err));
        return err;
    }

    size_t total = 0;
    size_t used = 0;
    err = esp_spiffs_info(SPIFFS_PARTITION, &total, &used);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS %s: %u / %u bytes used",
                 SPIFFS_MOUNT_PATH, (unsigned)used, (unsigned)total);
    }

    return ESP_OK;
}
