/**
 * File: gconfig.h
 * Author: Diego Parrilla Santamaría
 * Date: November 2024
 * Copyright: 2024 - GOODDATA LABS SL
 * Description: Header file for the global configuration manager
 */

#ifndef GCONFIG_H
#define GCONFIG_H

#define PARAM_APPS_FOLDER "APPS_FOLDER"
#define PARAM_BOOT_FEATURE "BOOT_FEATURE"
#define PARAM_SD_BAUD_RATE_KB "SD_BAUD_RATE_KB"

#define GCONFIG_SUCCESS 0
#define GCONFIG_INIT_ERROR -1
#define GCONFIG_MISMATCHED_APP -2

int gconfig_init(const char *currentAppName);
const char *gconfig_get_apps_folder(void);
// The SD card's SPI speed set in Booster, in kbit/s; 0 when unset.
unsigned int gconfig_get_sd_baud_rate_kb(void);

#endif  // GCONFIG_H
