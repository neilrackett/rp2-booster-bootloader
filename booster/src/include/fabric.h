/**
 * File: fabric.h
 * Author: Diego Parrilla Santamaría
 * Date: December 2024
 * Copyright: 2024 - GOODDATA LABS SL
 * Description: Header file for the generic fabric config mode.
 */

#ifndef FABRIC_H
#define FABRIC_H

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "blink.h"
#include "constants.h"
#include "debug.h"
#include "display.h"
#include "display_fabric.h"
#include "fabric_httpd.h"
#include "gconfig.h"
#include "lwip/apps/httpd.h"
#include "network.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "reset.h"
#include "sdcard.h"
#include "wificonf.h"

// Credentials collected by the factory web form. The .wificonf path does
// not go through this struct any more: wificonf.c applies the file straight
// into the settings.
typedef struct {
  char ssid[MAX_SSID_LENGTH];
  char pass[MAX_PASSWORD_LENGTH];
  uint16_t auth;
  bool is_set;
} fabric_config_t;

void fabric_set_config(char ssid[MAX_SSID_LENGTH],
                       char pass[MAX_PASSWORD_LENGTH], uint16_t auth,
                       bool is_set);
int fabric_init(void);
void fabric_loop();

#endif  // HTTPD_H
