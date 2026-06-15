#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#define SD_MOUNT_POINT "/sdcard"

/**
 * @brief Mount microSD card via SDSPI on shared SPI2 bus.
 * Safe to call even if SPI2 was already initialized by e-paper driver.
 */
bool sdcard_mount(void);

/**
 * @brief Unmount and release SD card resources.
 */
void sdcard_unmount(void);

/**
 * @brief Check if SD card is currently mounted.
 */
bool sdcard_is_mounted(void);

/**
 * @brief Get free space on SD card in bytes.
 * @return Free bytes, or 0 if not mounted.
 */
uint64_t sdcard_get_free_bytes(void);

/**
 * @brief Get total capacity of SD card in bytes.
 * @return Total bytes, or 0 if not mounted.
 */
uint64_t sdcard_get_total_bytes(void);
