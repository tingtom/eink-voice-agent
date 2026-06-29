#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include "ff.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "driver/spi_common.h"
#include "driver/gpio.h"

#include "sdcard.h"

static const char *TAG = "SDCARD";

// ── Pin mapping (from Waveshare ESP32-C6-ePaper-1.54 schematic) ──
// SD card shares SPI2 with e-paper display (same SCK/MOSI/MISO, different CS)
#define PIN_MISO    GPIO_NUM_4
#define PIN_MOSI    GPIO_NUM_5
#define PIN_CLK     GPIO_NUM_6
#define PIN_CS      GPIO_NUM_3

#define SPI_HOST    SPI2_HOST

static sdmmc_card_t *sdcard_card = NULL;
static bool sdcard_mounted = false;

// ── Public API ────────────────────────────────────────────────

bool sdcard_mount(void)
{
    if (sdcard_mounted) return true;

    // Enable pull-up on CS pin for stability on shared SPI bus
    gpio_set_pull_mode(PIN_CS, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(PIN_MOSI, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(PIN_CLK, GPIO_PULLUP_ONLY);

    // Try to initialize SPI bus. If e-paper already did this, it will
    // return ESP_ERR_INVALID_STATE — that's fine, bus is ready.
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = PIN_MISO,
        .sclk_io_num = PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    esp_err_t ret = spi_bus_initialize(SPI_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return false;
    }
    // ESP_ERR_INVALID_STATE means bus already active (e-paper inited it) — OK

    // Lower clock speed for shared bus stability
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI_HOST;
    host.max_freq_khz = 5000;

    // Configure SDSPI slot
    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.gpio_cs = PIN_CS;
    slot_cfg.host_id = SPI_HOST;

    // Mount FAT filesystem — NEVER auto-format (destroys user recordings)
    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 512,
    };

    ESP_LOGI(TAG, "Mounting SD card at %s (SPI%d, CS=GPIO%d, freq=%dkHz)",
             SD_MOUNT_POINT, SPI_HOST + 1, PIN_CS, host.max_freq_khz);

    ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_cfg, &mount_cfg, &sdcard_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD card mount failed: %s", esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(100));
        ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_cfg, &mount_cfg, &sdcard_card);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "SD card mount still failed: %s", esp_err_to_name(ret));
            return false;
        }
    }

    sdcard_mounted = true;
    sdmmc_card_print_info(stdout, sdcard_card);
    ESP_LOGI(TAG, "SD card mounted at %s", SD_MOUNT_POINT);

    return true;
}

void sdcard_unmount(void)
{
    if (!sdcard_mounted) return;
    esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, sdcard_card);
    sdcard_card = NULL;
    sdcard_mounted = false;
    ESP_LOGI(TAG, "SD card unmounted");
}

bool sdcard_is_mounted(void)
{
    return sdcard_mounted;
}

uint64_t sdcard_get_free_bytes(void)
{
    if (!sdcard_card) return 0;
    FATFS *fs = NULL;
    DWORD free_clusters = 0;
    if (f_getfree("0:", &free_clusters, &fs) != FR_OK) return 0;
    if (!fs) return 0;
    uint64_t bytes = (uint64_t)free_clusters * fs->csize * 512ULL;
    return bytes;
}

uint64_t sdcard_get_total_bytes(void)
{
    if (!sdcard_mounted) return 0;
    // sdmmc_card_print_info already shows size; compute from CSD
    if (!sdcard_card) return 0;
    return (uint64_t)sdcard_card->csd.capacity * sdcard_card->csd.sector_size;
}
