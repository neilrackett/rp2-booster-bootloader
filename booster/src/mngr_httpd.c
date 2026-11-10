/**
 * File: mngr_httpd.c
 * Author: Diego Parrilla Santamaría
 * Date: December 2024
 * Copyright: 2024-2025 - GOODDATA LABS SL
 * Description: HTTPD server functions for manager httpd
 */

#include "mngr_httpd.h"

#include "cgiparam.h"
#include "devapi.h"
#include "textescape.h"
#include "uuid.h"

#define WIFI_PASS_BUFSIZE 64
static char *ssid = NULL;
static char *pass = NULL;
static int auth = -1;
static void *current_connection;
static void *valid_connection;
static char apps_installed_json[MAXIMUM_APP_INFO_SIZE];
static bool apps_installed_found = false;
static wifi_scan_data_t *networks = NULL;
static mngr_httpd_response_status_t response_status = MNGR_HTTPD_RESPONSE_OK;
static char httpd_response_message[128] = {0};

static int mngr_httpd_base64_value(unsigned char c) {
  if (c >= 'A' && c <= 'Z') {
    return c - 'A';
  }
  if (c >= 'a' && c <= 'z') {
    return 26 + (c - 'a');
  }
  if (c >= '0' && c <= '9') {
    return 52 + (c - '0');
  }
  if (c == '+') {
    return 62;
  }
  if (c == '/') {
    return 63;
  }
  if (c == '=') {
    return -2;
  }
  return -1;
}

static int mngr_httpd_base64_decode(unsigned char *dest, size_t dest_size,
                                    size_t *decoded_len,
                                    const unsigned char *src,
                                    size_t src_len) {
  size_t out = 0;
  int quad[4];
  size_t quad_len = 0;

  for (size_t i = 0; i < src_len; ++i) {
    unsigned char c = src[i];
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      continue;
    }

    int value = mngr_httpd_base64_value(c);
    if (value < -1) {
      quad[quad_len++] = value;
    } else if (value >= 0) {
      quad[quad_len++] = value;
    } else {
      return -1;
    }

    if (quad_len != 4) {
      continue;
    }

    if (quad[0] < 0 || quad[1] < 0) {
      return -1;
    }
    if (out >= dest_size) {
      return -2;
    }
    dest[out++] = (unsigned char)((quad[0] << 2) | (quad[1] >> 4));

    if (quad[2] == -2) {
      if (quad[3] != -2) {
        return -1;
      }
      quad_len = 0;
      break;
    }
    if (quad[2] < 0) {
      return -1;
    }
    if (out >= dest_size) {
      return -2;
    }
    dest[out++] = (unsigned char)(((quad[1] & 0x0F) << 4) | (quad[2] >> 2));

    if (quad[3] == -2) {
      quad_len = 0;
      break;
    }
    if (quad[3] < 0) {
      return -1;
    }
    if (out >= dest_size) {
      return -2;
    }
    dest[out++] = (unsigned char)(((quad[2] & 0x03) << 6) | quad[3]);
    quad_len = 0;
  }

  if (quad_len != 0) {
    return -1;
  }

  *decoded_len = out;
  return 0;
}

/**
 * @brief Check if the string starts with specified characters
 * (case-insensitive).
 *
 * @param str The string to check.
 * @param chars The characters to match (e.g., "YyTt").
 * @return 1 if the string starts with any of the specified characters;
 * otherwise, 0.
 */
static int starts_with_case_insensitive(const char *str, const char *chars) {
  if (str == NULL || chars == NULL || str[0] == '\0') {
    return 0;
  }
  char first_char = tolower((unsigned char)str[0]);
  while (*chars) {
    if (first_char == tolower((unsigned char)*chars)) {
      return 1;
    }
    chars++;
  }
  return 0;
}

static char *get_status_message(mngr_httpd_response_status_t status,
                                const char *detail) {
  // Concatenate the detail
  char *message = (char *)malloc(128);
  if (message == NULL) {
    return NULL;
  }
  switch (status) {
    case MNGR_HTTPD_RESPONSE_OK:
      snprintf(message, 128, "OK: %s", detail);
      break;
    case MNGR_HTTPD_RESPONSE_BAD_REQUEST:
      snprintf(message, 128, "Bad Request: %s", detail);
      break;
    case MNGR_HTTPD_RESPONSE_NOT_FOUND:
      snprintf(message, 128, "Not Found: %s", detail);
      break;
    case MNGR_HTTPD_RESPONSE_INTERNAL_SERVER_ERROR:
      snprintf(message, 128, "Internal Server Error: %s", detail);
      break;
    default:
      snprintf(message, 128, "Unknown status: %s", detail);
      break;
  }
  return message;
}

/**
 * @brief Array of SSI tags for the HTTP server.
 *
 * This array contains the SSI tags used by the HTTP server to dynamically
 * insert content into web pages. Each tag corresponds to a specific piece of
 * information that can be updated or retrieved from the server.
 */
static const char *ssi_tags[] = {
    // Max size of SSI tag is 8 chars
    "HOMEPAGE",  // 0 - Redirect to the homepage
    "SDCARDST",  // 1 - SD card status
    "APPSFLDR",  // 2 - Apps folder
    "SSID",      // 3 - SSID
    "IPADDR",    // 4 - IP address
    "SDCARDB",   // 5 - SD card status true or false
    "APPSFLDB",  // 6 - Apps folder status true or false
    "JSONPLD",   // 7 - JSON payload
    "DWNLDSTS",  // 8 - Download status
    "TITLEHDR",  // 9 - Title header
    "WIFILST",   // 10 - WiFi list
    "WLSTSTP",   // 11 - WiFi list stop
    "RSPSTS",    // 12 - Response status
    "RSPMSG",    // 13 - Response message
    "BTFTR",     // 14 - Boot feature
    "SFCNFGRB",  // 15 - Safe Config Reboot
    "SDBDRTKB",  // 16 - SD Card Baud Rate
    "APPSURL",   // 17 - Apps Catalog URL
    "NVERSION",  // 18 - New Version
    "NVERSTR",   // 19 - New Version String
    "DEVMODE",   // 20 - Developer deploy API enabled (EPIC-05)
    "DLSTAT",    // 21 - Download progress JSON (was PLHLDR12)
    "DLNAME",    // 22 - Name of the app being installed (was PLHLDR13)
    "TITLEH1",   // 23 - Page heading, version in <small> (was PLHLDR14)
    "PLHLDR15",  // 24 - Placeholder 15
    "PLHLDR16",  // 25 - Placeholder 16
    "PLHLDR17",  // 26 - Placeholder 17
    "PLHLDR18",  // 27 - Placeholder 18
    "PLHLDR19",  // 28 - Placeholder 19
    "PLHLDR20",  // 29 - Placeholder 20
    "PLHLDR21",  // 30 - Placeholder 21
    "PLHLDR22",  // 31 - Placeholder 22
    "PLHLDR23",  // 32 - Placeholder 23
    "PLHLDR24",  // 33 - Placeholder 24
    "PLHLDR25",  // 34 - Placeholder 25
    "PLHLDR26",  // 35 - Placeholder 26
    "PLHLDR27",  // 36 - Placeholder 27
    "PLHLDR28",  // 37 - Placeholder 28
    "PLHLDR29",  // 38 - Placeholder 29
    "PLHLDR30",  // 39 - Placeholder 30
    "WDHCP",     // 40 - WiFi DHCP
    "WIP",       // 41 - WiFi IP
    "WNTMSK",    // 42 - WiFi Netmask
    "WGTWY",     // 43 - WiFi Gateway
    "WDNS",      // 44 - WiFi DNS
    "WCNTRY",    // 45 - WiFi Country
    "WHSTNM",    // 46 - WiFi Hostname
    "WPWR",      // 47 - WiFi Power
    "WRSS",      // 48 - WiFi RSSI
    "MACADDR",   // 49 - Device MAC address
    "WSIGNAL",   // 50 - Live WiFi signal strength
    "WIFICONF",  // 51 - /.wificonf on the SD card locks the Network page
};

void mngr_httpd_setResponse(mngr_httpd_response_status_t status,
                            const char *message) {
  response_status = status;
  snprintf(httpd_response_message, sizeof(httpd_response_message), "%s",
           message != NULL ? message : "");
}

// The answer to a request the Manager refuses: the reason and its code, shown
// by 400_refused.shtml, with a 400 status so a script using curl --fail sees
// the refusal. lwIP opens the page a handler returns exactly as
// given, query included, so "/error.shtml?error=..." found no file and the
// browser got no answer at all.
static const char *mngr_httpd_refuse(const char *reason, int code) {
  response_status = MNGR_HTTPD_RESPONSE_BAD_REQUEST;
  snprintf(httpd_response_message, sizeof(httpd_response_message), "%s (%d)",
           reason, code);
  return "/400_refused.shtml";
}

/**
 * @brief
 *
 * @param iIndex The index of the CGI handler.
 * @param iNumParams The number of parameters passed to the CGI handler.
 * @param pcParam An array of parameter names.
 * @param pcValue An array of parameter values.
 * @return The URL of the page to redirect to after the floppy disk image is
 * selected.
 */
static const char *cgi_test(int iIndex, int iNumParams, char *pcParam[],
                            char *pcValue[]) {
  DPRINTF("TEST CGI handler called with index %d\n", iIndex);
  return "/test.shtml";
}

/**
 * @brief Download the selected app fron the data passed as json in the GET
 * request
 *
 *
 * @param iIndex The index of the CGI handler.
 * @param iNumParams The number of parameters passed to the CGI handler.
 * @param pcParam An array of parameter names.
 * @param pcValue An array of parameter values.
 * @return The URL of the page to redirect to wait for the download process
 */
const char *cgi_download(int iIndex, int iNumParams, char *pcParam[],
                         char *pcValue[]) {
  DPRINTF("cgi_download called with index %d\n", iIndex);
  // One download at a time: a second one would rebuild the HTTP request and
  // reopen the file while lwIP still writes the first (two tabs, a double
  // click).
  if (appmngr_download_in_flight()) {
    return mngr_httpd_refuse("Another download is in progress",
                             DOWNLOAD_BUSY_ERROR);
  }
  const char *json = cgiparam_get("json", iNumParams, pcParam, pcValue);
  if (json == NULL) {
    DPRINTF("No app to download: the json parameter is missing\n");
    return mngr_httpd_refuse("No app to install", DOWNLOAD_PARSEJSON_ERROR);
  }
  const char *update_value =
      cgiparam_get("update", iNumParams, pcParam, pcValue);
  bool update = update_value != NULL &&
                starts_with_case_insensitive(update_value, "YyTt");
  DPRINTF("Update flag set to: %d\n", update);
  appmngr_set_download_update(update);

  DPRINTF("JSON encoded value: %s\n", json);
  size_t len = 0;
  char output_buffer[4096];
  int ret = mngr_httpd_base64_decode((unsigned char *)output_buffer,
                                     sizeof(output_buffer) - 1, &len,
                                     (const unsigned char *)json, strlen(json));
  // Place a null terminator at the end of the string
  output_buffer[len] = '\0';
  if (ret != 0) {
    DPRINTF("Error decoding base64: %d\n", ret);
    return mngr_httpd_refuse("Decoding error", ret);
  }
  DPRINTF("Decoded value: %s\n", output_buffer);
  download_err_t err = appmngr_save_app_info(output_buffer);
  if (err != DOWNLOAD_OK) {
    DPRINTF("Error saving app info: %d\n", err);
    return mngr_httpd_refuse("Downloading error", err);
  }
  DPRINTF("App info saved\n");
  appmngr_download_status(DOWNLOAD_STATUS_REQUESTED);
  return "/downloading.shtml";
}

// True while /.wificonf is on the card. The file re-applies the WiFi network
// and the TCP/IP settings at every boot (EPIC-10), so those are read-only on
// the Network page and their saves are refused in cgi_saveparams: a change
// made here would only last until the next reboot.
static bool mngr_httpd_wificonfLocks(void) {
  if (!appmngr_get_sdcard_info()->ready) {
    return false;
  }
  return wificonf_isPresent();
}

// The settings keys /.wificonf owns.
static bool mngr_httpd_isWificonfKey(const char *name) {
  static const char *const keys[] = {
      PARAM_WIFI_SSID, PARAM_WIFI_PASSWORD, PARAM_WIFI_AUTH,    PARAM_WIFI_DHCP,
      PARAM_WIFI_IP,   PARAM_WIFI_NETMASK,  PARAM_WIFI_GATEWAY, PARAM_WIFI_DNS};
  for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
    if (strcmp(name, keys[i]) == 0) {
      return true;
    }
  }
  return false;
}

// The value of the named setting in a /saveparams.cgi payload, or NULL.
static const char *mngr_httpd_jsonParam(const cJSON *root, const char *key) {
  const cJSON *item = NULL;
  cJSON_ArrayForEach(item, root) {
    const cJSON *name = cJSON_GetObjectItem(item, "name");
    const cJSON *value = cJSON_GetObjectItem(item, "value");
    if (cJSON_IsString(name) && cJSON_IsString(value) &&
        strcmp(name->valuestring, key) == 0) {
      return value->valuestring;
    }
  }
  return NULL;
}

// A setting from the payload, or its stored value when the payload has none.
static const char *mngr_httpd_paramOrStored(const char *value,
                                            const char *key) {
  if (value != NULL) {
    return value;
  }
  SettingsConfigEntry *entry = settings_find_entry(gconfig_getContext(), key);
  return entry != NULL ? entry->value : NULL;
}

// Checks the static TCP/IP settings in a /saveparams.cgi payload before any of
// them is stored: a typo stored here is applied at the next boot and takes the
// device off the network. Only when the payload touches them and DHCP ends up
// off, so a value left in flash cannot block saving something else. Fields the
// payload does not carry count with their stored value. Returns the name of
// the first invalid field, or NULL; *isStatic says *out holds what to store.
static const char *mngr_httpd_checkStaticTcpip(const cJSON *root,
                                               network_static_config_t *out,
                                               bool *isStatic) {
  *isStatic = false;
  const char *dhcp = mngr_httpd_jsonParam(root, PARAM_WIFI_DHCP);
  const char *address = mngr_httpd_jsonParam(root, PARAM_WIFI_IP);
  const char *netmask = mngr_httpd_jsonParam(root, PARAM_WIFI_NETMASK);
  const char *gateway = mngr_httpd_jsonParam(root, PARAM_WIFI_GATEWAY);
  const char *dns = mngr_httpd_jsonParam(root, PARAM_WIFI_DNS);
  if (dhcp == NULL && address == NULL && netmask == NULL && gateway == NULL &&
      dns == NULL) {
    return NULL;
  }
  const char *finalDhcp = mngr_httpd_paramOrStored(dhcp, PARAM_WIFI_DHCP);
  if (finalDhcp != NULL && starts_with_case_insensitive(finalDhcp, "YyTt")) {
    return NULL;
  }
  const char *bad = network_checkStaticConfig(
      mngr_httpd_paramOrStored(address, PARAM_WIFI_IP),
      mngr_httpd_paramOrStored(netmask, PARAM_WIFI_NETMASK),
      mngr_httpd_paramOrStored(gateway, PARAM_WIFI_GATEWAY),
      mngr_httpd_paramOrStored(dns, PARAM_WIFI_DNS), out);
  *isStatic = (bad == NULL);
  return bad;
}

// The canonical text to store for a static TCP/IP setting, or NULL for any
// other setting.
static const char *mngr_httpd_canonicalTcpip(
    const char *name, const network_static_config_t *cfg) {
  if (strcmp(name, PARAM_WIFI_IP) == 0) return cfg->address;
  if (strcmp(name, PARAM_WIFI_NETMASK) == 0) return cfg->netmask;
  if (strcmp(name, PARAM_WIFI_GATEWAY) == 0) return cfg->gateway;
  if (strcmp(name, PARAM_WIFI_DNS) == 0) return cfg->dns;
  return NULL;
}

/**
 * @brief Update the given params with the new values
 *
 *
 * @param iIndex The index of the CGI handler.
 * @param iNumParams The number of parameters passed to the CGI handler.
 * @param pcParam An array of parameter names.
 * @param pcValue An array of parameter values.
 * @return The URL of the page to redirect to wait for the download process
 */
const char *cgi_saveparams(int iIndex, int iNumParams, char *pcParam[],
                           char *pcValue[]) {
  DPRINTF("cgi_saveparams called with index %d\n", iIndex);
  for (size_t i = 0; i < iNumParams; i++) {
    /* check if parameter is "json"; "?json" without a value is no json */
    if (strcmp(pcParam[i], "json") == 0 && pcValue[i] != NULL) {
      DPRINTF("JSON encoded value: %s\n", pcValue[i]);
      int ret;
      size_t len = 0;
      char output_buffer[4096];
      ret = mngr_httpd_base64_decode(
          (unsigned char *)output_buffer, sizeof(output_buffer) - 1, &len,
          (const unsigned char *)pcValue[i], strlen(pcValue[i]));
      // Place a null terminator at the end of the string
      output_buffer[len] = '\0';
      if (ret != 0) {
        DPRINTF("Error decoding base64: %d\n", ret);
        response_status = MNGR_HTTPD_RESPONSE_BAD_REQUEST;
        char detail[64] = {0};
        snprintf(detail, sizeof(detail), "Error decoding base64: %d", ret);
        snprintf(httpd_response_message, sizeof(httpd_response_message), "%s",
                 get_status_message(response_status, detail));
      } else {
        DPRINTF("Decoded value: %s\n", output_buffer);
        // Parse the JSON object
        bool valid_json = true;
        cJSON *root = cJSON_Parse(output_buffer);
        if (root == NULL) {
          DPRINTF("Error parsing JSON\n");
          response_status = MNGR_HTTPD_RESPONSE_BAD_REQUEST;
          snprintf(httpd_response_message, sizeof(httpd_response_message),
                   "Error parsing JSON");
          valid_json = false;
        } else {
          // Refuse the keys /.wificonf owns while the file is on the card,
          // before anything is applied, so a mixed payload changes nothing
          // rather than half of itself. The page grays these out, but the
          // firmware is the one that has to say no.
          if (mngr_httpd_wificonfLocks()) {
            cJSON *lockedItem = NULL;
            cJSON_ArrayForEach(lockedItem, root) {
              cJSON *lockedName = cJSON_GetObjectItem(lockedItem, "name");
              if (cJSON_IsString(lockedName) &&
                  mngr_httpd_isWificonfKey(lockedName->valuestring)) {
                DPRINTF("Refusing %s: %s takes precedence\n",
                        lockedName->valuestring, WIFICONF_FILE);
                response_status = MNGR_HTTPD_RESPONSE_BAD_REQUEST;
                snprintf(
                    httpd_response_message, sizeof(httpd_response_message),
                    "A .wificonf file on the microSD card takes precedence. "
                    "Remove or rename it and reboot to change this here.");
                cJSON_Delete(root);
                return "/response.shtml";
              }
            }
          }
          network_static_config_t staticConfig;
          bool isStatic = false;
          const char *badField =
              mngr_httpd_checkStaticTcpip(root, &staticConfig, &isStatic);
          if (badField != NULL) {
            DPRINTF("Refusing the TCP/IP settings: invalid %s\n", badField);
            response_status = MNGR_HTTPD_RESPONSE_BAD_REQUEST;
            snprintf(httpd_response_message, sizeof(httpd_response_message),
                     "Invalid %s. Nothing was saved.", badField);
            cJSON_Delete(root);
            return "/response.shtml";
          }
          // Iterate over the JSON array
          cJSON *item = NULL;
          cJSON_ArrayForEach(item, root) {
            cJSON *name = cJSON_GetObjectItem(item, "name");
            cJSON *type = cJSON_GetObjectItem(item, "type");
            cJSON *value = cJSON_GetObjectItem(item, "value");

            // Print each parameter
            if (cJSON_IsString(name) && cJSON_IsString(type) &&
                cJSON_IsString(value)) {
              // The value is not logged: this also carries the WiFi password.
              DPRINTF("Param Name: %s, Type: %s\n", name->valuestring,
                      type->valuestring);
              if (strcasecmp(type->valuestring, "STRING") == 0) {
                const char *canonical =
                    isStatic ? mngr_httpd_canonicalTcpip(name->valuestring,
                                                         &staticConfig)
                             : NULL;
                settings_put_string(
                    gconfig_getContext(), name->valuestring,
                    canonical != NULL ? canonical : value->valuestring);
                DPRINTF("Setting %s saved.\n", name->valuestring);
              } else if (strcasecmp(type->valuestring, "INT") == 0) {
                settings_put_integer(gconfig_getContext(), name->valuestring,
                                     atoi(value->valuestring));
                DPRINTF("Setting %s to %d saved.\n", name->valuestring,
                        atoi(value->valuestring));
              } else if (strcasecmp(type->valuestring, "BOOL") == 0) {
                // Check if the value starts with "Y", "y", "T", or "t"
                int bool_value =
                    starts_with_case_insensitive(value->valuestring, "YyTt");
                settings_put_bool(gconfig_getContext(), name->valuestring,
                                  bool_value);
                DPRINTF("Setting %s to %s saved.\n", name->valuestring,
                        bool_value ? "true" : "false");
              } else {
                DPRINTF("Invalid parameter type in JSON\n");
                response_status = MNGR_HTTPD_RESPONSE_BAD_REQUEST;
                snprintf(httpd_response_message, sizeof(httpd_response_message),
                         "Invalid parameter type in JSON");
                valid_json = false;
              }
            } else {
              DPRINTF("Invalid parameter structure in JSON\n");
              response_status = MNGR_HTTPD_RESPONSE_BAD_REQUEST;
              snprintf(httpd_response_message, sizeof(httpd_response_message),
                       "Invalid parameter structure in JSON");
              valid_json = false;
            }
          }

          // Free the parsed JSON object
          cJSON_Delete(root);

          if (valid_json) {
            settings_save(gconfig_getContext(), true);
            DPRINTF("Settings saved\n");
            response_status = MNGR_HTTPD_RESPONSE_OK;
            snprintf(httpd_response_message, sizeof(httpd_response_message),
                     "");
          }
        }
      }
      return "/response.shtml";
    }
  }

  // If no "json" parameter is found
  response_status = MNGR_HTTPD_RESPONSE_BAD_REQUEST;
  snprintf(httpd_response_message, sizeof(httpd_response_message),
           "Missing 'json' parameter");
  return "/response.shtml";
}

/**
 * @brief Show the first app found in the apps folder
 *
 * @param iIndex The index of the CGI handler.
 * @param iNumParams The number of parameters passed to the CGI handler.
 * @param pcParam An array of parameter names.
 * @param pcValue An array of parameter values.
 * @return The URL of the page to redirect to after the floppy disk image is
 * selected.
 */
static const char *cgi_fsfirst(int iIndex, int iNumParams, char *pcParam[],
                               char *pcValue[]) {
  DPRINTF("FSFIRST CGI handler called with index %d\n", iIndex);
  apps_installed_found = appmngr_ffirst(apps_installed_json);
  if (!apps_installed_found) {
    DPRINTF("No apps found\n");
    return "/jsonempty.shtml";
  } else {
    DPRINTF("Apps found\n");
  }
  return "/json.shtml";
}

/**
 * @brief Show the next app found in the apps folder
 *
 * @param iIndex The index of the CGI handler.
 * @param iNumParams The number of parameters passed to the CGI handler.
 * @param pcParam An array of parameter names.
 * @param pcValue An array of parameter values.
 * @return The URL of the page to redirect to after the floppy disk image is
 * selected.
 */
static const char *cgi_fsnext(int iIndex, int iNumParams, char *pcParam[],
                              char *pcValue[]) {
  DPRINTF("FSNEXT CGI handler called with index %d\n", iIndex);
  apps_installed_found = appmngr_fnext(apps_installed_json);
  if (!apps_installed_found) {
    DPRINTF("No more apps found\n");
    return "/jsonempty.shtml";
  } else {
    DPRINTF("Apps found\n");
  }
  return "/json.shtml";
}

/**
 * @brief Delete the app info file from the SD card
 *
 *
 * @param iIndex The index of the CGI handler.
 * @param iNumParams The number of parameters passed to the CGI handler.
 * @param pcParam An array of parameter names.
 * @param pcValue An array of parameter values.
 * @return The URL of the page to redirect to wait for the download process
 */
const char *cgi_deleteapp(int iIndex, int iNumParams, char *pcParam[],
                          char *pcValue[]) {
  DPRINTF("cgi_deleteapp called with index %d\n", iIndex);
  download_delete_err_t err = DOWNLOAD_DELETEAPP_NOTUUID_ERROR;
  const char *uuid = cgiparam_get("uuid", iNumParams, pcParam, pcValue);
  if (uuid != NULL) {
    DPRINTF("APP to delete with UUID: %s\n", uuid);
    err = appmngr_delete_app(uuid);
    if (err == DOWNLOAD_DELETEAPP_OK) {
      DPRINTF("App deleted\n");
      return "/deleting.shtml";
    }
  }
  return mngr_httpd_refuse("Deleting error", err);
}

/**
 * @brief Launch the app with the uuid given
 *
 *
 * @param iIndex The index of the CGI handler.
 * @param iNumParams The number of parameters passed to the CGI handler.
 * @param pcParam An array of parameter names.
 * @param pcValue An array of parameter values.
 * @return The URL of the page to redirect to wait for the download process
 */
const char *cgi_launchapp(int iIndex, int iNumParams, char *pcParam[],
                          char *pcValue[]) {
  DPRINTF("cgi_launchapp called with index %d\n", iIndex);
  const char *uuid = cgiparam_get("uuid", iNumParams, pcParam, pcValue);
  if (uuid_is_exact4(uuid)) {
    DPRINTF("APP to launch with UUID: %s\n", uuid);
    appmngr_schedule_launch_app(uuid);
    return "/launching.html";
  }
  return mngr_httpd_refuse("Launching error", DOWNLOAD_LAUNCHAPP_NOTUUID_ERROR);
}

/**
 * @brief "Restore and launch" the development app (EPIC-11).
 *
 * Deletes the deploy API upload, erases the development app's config and
 * flashes the binary downloaded from the catalog, then launches it: a clean
 * slate after a bad deploy. Only the Apps page calls this, for the DEV APP
 * card. It is deliberately not part of the documented deploy API.
 *
 * Refused for any other UUID, and when the catalog binary is missing, because
 * the restore would then have nothing to write. Both checks happen here so the
 * page gets an error instead of a launch that cannot do what was asked.
 */
const char *cgi_restoreapp(int iIndex, int iNumParams, char *pcParam[],
                           char *pcValue[]) {
  DPRINTF("cgi_restoreapp called with index %d\n", iIndex);
  const char *uuid = cgiparam_get("uuid", iNumParams, pcParam, pcValue);
  if (uuid != NULL) {
    if (strcmp(uuid, DEVAPI_DEV_APP_UUID) != 0) {
      DPRINTF("Restore refused for %s: not the development app\n", uuid);
      return mngr_httpd_refuse("Only the DEV APP can be restored",
                               DOWNLOAD_LAUNCHAPP_NOTUUID_ERROR);
    }
    if (!appmngr_get_sdcard_info()->ready) {
      DPRINTF("Restore refused: SD card not ready\n");
      return mngr_httpd_refuse("SD card not ready",
                               DOWNLOAD_LAUNCHAPP_SDCARDNOTREADY_ERROR);
    }
    if (!devapi_hasCatalogBinary()) {
      // No launch error code describes this; 0 is the generic one, as in
      // cgi_launchapp. The message carries the meaning.
      DPRINTF("Restore refused: the DEV APP catalog binary is missing\n");
      return mngr_httpd_refuse(
          "The DEV APP binary is missing. Install the DEV APP again", 0);
    }
    DPRINTF("Restore and launch scheduled for %s\n", uuid);
    appmngr_schedule_restore_launch_app(uuid);
    return "/launching.html";
  }
  return mngr_httpd_refuse("Missing uuid", DOWNLOAD_LAUNCHAPP_NOTUUID_ERROR);
}

/**
 * @brief "Restore config only" for the development app (EPIC-11).
 *
 * Erases the development app's config sector and nothing else: the
 * microfirmware in the slot and the deploy API upload stay, and nothing is
 * launched. Answers with JSON through /response.shtml so the Apps page can show
 * the result in its dialog. Only the Apps page calls it.
 *
 * The erase runs here, in the web server's context, the way cgi_saveparams
 * saves settings: appmngr_delete_config_sector() parks core 1 and disables
 * interrupts around it.
 */
const char *cgi_restoreconfig(int iIndex, int iNumParams, char *pcParam[],
                              char *pcValue[]) {
  DPRINTF("cgi_restoreconfig called with index %d\n", iIndex);
  response_status = MNGR_HTTPD_RESPONSE_BAD_REQUEST;
  snprintf(httpd_response_message, sizeof(httpd_response_message),
           "Missing uuid");
  const char *uuid = cgiparam_get("uuid", iNumParams, pcParam, pcValue);
  if (uuid != NULL) {
    if (strcmp(uuid, DEVAPI_DEV_APP_UUID) != 0) {
      DPRINTF("Config restore refused for %s: not the development app\n", uuid);
      snprintf(httpd_response_message, sizeof(httpd_response_message),
               "Only the DEV APP settings can be restored");
      return "/response.shtml";
    }
    switch (appmngr_erase_app_config(uuid)) {
      case APPMNGR_CONFIG_ERASED:
        response_status = MNGR_HTTPD_RESPONSE_OK;
        snprintf(httpd_response_message, sizeof(httpd_response_message),
                 "DEV APP settings erased. It starts with its default "
                 "settings the next time it runs.");
        break;
      case APPMNGR_CONFIG_NOT_ASSIGNED:
        response_status = MNGR_HTTPD_RESPONSE_OK;
        snprintf(httpd_response_message, sizeof(httpd_response_message),
                 "The DEV APP has no saved settings to erase.");
        break;
      case APPMNGR_CONFIG_ERASE_FAILED:
      default:
        response_status = MNGR_HTTPD_RESPONSE_INTERNAL_SERVER_ERROR;
        snprintf(httpd_response_message, sizeof(httpd_response_message),
                 "Could not erase the DEV APP settings.");
        break;
    }
    return "/response.shtml";
  }
  return "/response.shtml";
}

/**
 * @brief Reboot the device
 *
 *
 * @param iIndex The index of the CGI handler.
 * @param iNumParams The number of parameters passed to the CGI handler.
 * @param pcParam An array of parameter names.
 * @param pcValue An array of parameter values.
 * @return The URL of the page to redirect to wait for the download process
 */
const char *cgi_reboot(int iIndex, int iNumParams, char *pcParam[],
                       char *pcValue[]) {
  DPRINTF("cgi_reboot called with index %d\n", iIndex);
  mngr_schedule_reset(5);  // Reboot in 5 seconds
  return "/ap_step3.shtml";
}

/**
 * @brief Factory reset the device
 *
 *
 * @param iIndex The index of the CGI handler.
 * @param iNumParams The number of parameters passed to the CGI handler.
 * @param pcParam An array of parameter names.
 * @param pcValue An array of parameter values.
 * @return The URL of the page to redirect to wait for the download process
 */
const char *cgi_factoryreset(int iIndex, int iNumParams, char *pcParam[],
                             char *pcValue[]) {
  DPRINTF("cgi_factoryreset called with index %d\n", iIndex);
  mngr_schedule_factory_reset(5);  // Factory reset in 5 seconds
  return "/ap_step3.shtml";
}

const char *cgi_firmware_upgrade_start(int iIndex, int iNumParams,
                                       char *pcParam[], char *pcValue[]) {
  DPRINTF("cgi_firmware_upgrade_start called with index %d\n", iIndex);
  if (appmngr_download_in_flight()) {
    response_status = MNGR_HTTPD_RESPONSE_BAD_REQUEST;
    snprintf(httpd_response_message, sizeof(httpd_response_message),
             "Another download is in progress");
    return "/response.shtml";
  }
  mngr_firmwareUpgradeStart();
  response_status = MNGR_HTTPD_RESPONSE_OK;
  snprintf(httpd_response_message, sizeof(httpd_response_message), "");
  return "/response.shtml";
}

const char *cgi_firmware_upgrade_downloaded(int iIndex, int iNumParams,
                                            char *pcParam[], char *pcValue[]) {
  DPRINTF("cgi_firmware_upgrade_downloaded called with index %d\n", iIndex);
  DPRINTF("Firmware upgrade state: %d\n", mngr_get_firmwareUpgradeState());
  if (mngr_get_firmwareUpgradeState() == FIRMWARE_UPGRADE_DOWNLOADED) {
    response_status = MNGR_HTTPD_RESPONSE_OK;
    snprintf(httpd_response_message, sizeof(httpd_response_message), "");
  } else {
    response_status = MNGR_HTTPD_RESPONSE_BAD_REQUEST;
    snprintf(httpd_response_message, sizeof(httpd_response_message),
             "Download firmware failed");
  }
  return "/response.shtml";
}

const char *cgi_firmware_upgrade_confirm(int iIndex, int iNumParams,
                                         char *pcParam[], char *pcValue[]) {
  DPRINTF("cgi_firmware_upgrade_confirm called with index %d\n", iIndex);
  if (mngr_get_firmwareUpgradeState() == FIRMWARE_UPGRADE_DOWNLOADED) {
    mngr_firmwareUpgradeInstall();
    response_status = MNGR_HTTPD_RESPONSE_OK;
    snprintf(httpd_response_message, sizeof(httpd_response_message), "");
  } else {
    response_status = MNGR_HTTPD_RESPONSE_BAD_REQUEST;
    snprintf(httpd_response_message, sizeof(httpd_response_message),
             "Firmware upgrade failed");
  }
  return "/response.shtml";
}

/**
 * @brief Array of CGI handlers for floppy select and eject operations.
 *
 * This array contains the mappings between the CGI paths and the corresponding
 * handler functions for selecting and ejecting floppy disk images for drive A
 * and drive B.
 */
// Re-issue the install that just failed. Nothing needs re-posting: the failure
// path leaves app_info and /apps/tmp.json intact, so the download engine can be
// pointed at the same target again.
const char *cgi_download_retry(int iIndex, int iNumParams, char *pcParam[],
                               char *pcValue[]) {
  LWIP_UNUSED_ARG(iIndex);
  LWIP_UNUSED_ARG(iNumParams);
  LWIP_UNUSED_ARG(pcParam);
  LWIP_UNUSED_ARG(pcValue);
  DPRINTF("cgi_download_retry called\n");
  if (appmngr_download_in_flight()) {
    return mngr_httpd_refuse("Another download is in progress",
                             DOWNLOAD_BUSY_ERROR);
  }
  appmngr_set_download_phase(APPMNGR_PHASE_CONNECT);
  appmngr_download_error(DOWNLOAD_OK);
  appmngr_set_download_phase(APPMNGR_PHASE_CONNECT);
  appmngr_download_status(DOWNLOAD_STATUS_REQUESTED);
  return "/downloading.shtml";
}

// Abandon a failed install and drop the partial file.
const char *cgi_download_cancel(int iIndex, int iNumParams, char *pcParam[],
                                char *pcValue[]) {
  LWIP_UNUSED_ARG(iIndex);
  LWIP_UNUSED_ARG(iNumParams);
  LWIP_UNUSED_ARG(pcParam);
  LWIP_UNUSED_ARG(pcValue);
  DPRINTF("cgi_download_cancel called\n");
  // Cancel clears a failed download. A live one keeps its file: closing it
  // under lwIP's receive callback would corrupt the card.
  if (appmngr_download_in_flight()) {
    return mngr_httpd_refuse("Another download is in progress",
                             DOWNLOAD_BUSY_ERROR);
  }
  appmngr_cleanup_download();
  appmngr_set_download_phase(APPMNGR_PHASE_IDLE);
  appmngr_download_status(DOWNLOAD_STATUS_IDLE);
  return "/mngr_home.shtml";
}

static const tCGI cgi_handlers[] = {
    {"/test.cgi", cgi_test},
    {"/download.cgi", cgi_download},
    {"/download_retry.cgi", cgi_download_retry},
    {"/download_cancel.cgi", cgi_download_cancel},
    {"/saveparams.cgi", cgi_saveparams},
    {"/reboot.cgi", cgi_reboot},
    {"/factoryreset.cgi", cgi_factoryreset},
    {"/mngr_fsfirst.cgi", cgi_fsfirst},
    {"/mngr_fsnext.cgi", cgi_fsnext},
    {"/mngr_deleteapp.cgi", cgi_deleteapp},
    {"/mngr_launchapp.cgi", cgi_launchapp},
    {"/mngr_restoreapp.cgi", cgi_restoreapp},
    {"/mngr_restoreconfig.cgi", cgi_restoreconfig},
    {"/firmware_upgrade_start.cgi", cgi_firmware_upgrade_start},
    {"/firmware_upgrade_downloaded.cgi", cgi_firmware_upgrade_downloaded},
    {"/firmware_upgrade_confirm.cgi", cgi_firmware_upgrade_confirm},
};

/**
 * @brief Initializes the HTTP server with optional SSI tags, CGI handlers, and
 * an SSI handler function.
 *
 * This function initializes the HTTP server and sets up the provided Server
 * Side Include (SSI) tags, Common Gateway Interface (CGI) handlers, and SSI
 * handler function. It first calls the httpd_init() function to initialize the
 * HTTP server.
 *
 * The filesystem for the HTTP server is in the 'fs' directory in the project
 * root.
 *
 * @param ssi_tags An array of strings representing the SSI tags to be used in
 * the server-side includes.
 * @param num_tags The number of SSI tags in the ssi_tags array.
 * @param ssi_handler_func A pointer to the function that handles SSI tags.
 * @param cgi_handlers An array of tCGI structures representing the CGI handlers
 * to be used.
 * @param num_cgi_handlers The number of CGI handlers in the cgi_handlers array.
 */
static void httpd_server_init(const char *ssi_tags[], size_t num_tags,
                              tSSIHandler ssi_handler_func,
                              const tCGI *cgi_handlers,
                              size_t num_cgi_handlers) {
  httpd_init();

  // SSI Initialization
  if (num_tags > 0) {
    for (size_t i = 0; i < num_tags; i++) {
      LWIP_ASSERT("tag too long for LWIP_HTTPD_MAX_TAG_NAME_LEN",
                  strlen(ssi_tags[i]) <= LWIP_HTTPD_MAX_TAG_NAME_LEN);
    }
    http_set_ssi_handler(ssi_handler_func, ssi_tags, num_tags);
  } else {
    DPRINTF("No SSI tags defined.\n");
  }

  // CGI Initialization
  if (num_cgi_handlers > 0) {
    http_set_cgi_handlers(cgi_handlers, num_cgi_handlers);
  } else {
    DPRINTF("No CGI handlers defined.\n");
  }

  DPRINTF("HTTP server initialized.\n");
}

/**
 * @brief Server Side Include (SSI) handler for the HTTPD server.
 *
 * This function is called when the server needs to dynamically insert content
 * into web pages using SSI tags. It handles different SSI tags and generates
 * the corresponding content to be inserted into the web page.
 *
 * @param iIndex The index of the SSI handler.
 * @param pcInsert A pointer to the buffer where the generated content should be
 * inserted.
 * @param iInsertLen The length of the buffer.
 * @param current_tag_part The current part of the SSI tag being processed (used
 * for multipart SSI tags).
 * @param next_tag_part A pointer to the next part of the SSI tag to be
 * processed (used for multipart SSI tags).
 * @return The length of the generated content.
 */
static u16_t ssi_handler(int iIndex, char *pcInsert, int iInsertLen
#if LWIP_HTTPD_SSI_MULTIPART
                         ,
                         u16_t current_tag_part, u16_t *next_tag_part
#endif /* LWIP_HTTPD_SSI_MULTIPART */
) {
  // DPRINTF("SSI handler called with index %d\n", iIndex);
  size_t printed;
  switch (iIndex) {
    case 0: /* "HOMEPAGE" */
      // Always to the first step of the configuration
      printed = snprintf(
          pcInsert, iInsertLen, "%s",
          "<meta http-equiv='refresh' content='0;url=/mngr_home.shtml'>");
      break;
    case 1: /* "SDCARDST" */
    {
      if (!appmngr_get_sdcard_info()->ready) {
        printed =
            snprintf(pcInsert, iInsertLen,
                     "<span class=\"text-warning\">No SD card found</span>");
      } else {
        printed = snprintf(pcInsert, iInsertLen, "%d MB",
                           appmngr_get_sdcard_info()->free_space);
      }
      break;
    }
    case 2: /* "APPSFLDR" */
    {
      if (!appmngr_get_sdcard_info()->ready) {
        printed =
            snprintf(pcInsert, iInsertLen,
                     "<span class=\"text-warning\">No SD card found</span>");
      } else {
        char *apps_folder =
            settings_find_entry(gconfig_getContext(), PARAM_APPS_FOLDER)->value;
        if (!appmngr_get_sdcard_info()->apps_folder_found) {
          printed = snprintf(pcInsert, iInsertLen,
                             "<span class=\"text-warning\">%s not found</span>",
                             apps_folder);
        } else {
          printed = snprintf(pcInsert, iInsertLen, "%s", apps_folder);
        }
      }
      break;
    }
    case 3: /* "SSID" */
    {
      char *ssid =
          settings_find_entry(gconfig_getContext(), PARAM_WIFI_SSID)->value;
      if (ssid != NULL) {
        // The name of the network the device joined, chosen by whoever runs
        // it: text, not markup.
        printed = (int)textescape_html(pcInsert, (size_t)iInsertLen, ssid);
      } else {
        printed =
            snprintf(pcInsert, iInsertLen,
                     "<span class=\"text-error\">No network selected</span>");
      }
      break;
    }
    case 4: /* IPADDR */
    {
      ip_addr_t ipaddr = network_getCurrentIp();
      if (&ipaddr != NULL) {
        printed = snprintf(pcInsert, iInsertLen, "%s", ip4addr_ntoa(&ipaddr));
      } else {
        printed = snprintf(pcInsert, iInsertLen,
                           "<span class=\"text-error\">No IP address</span>");
      }
      break;
    }
    case 5: /* SDCARDB */
    {
      if (!appmngr_get_sdcard_info()->ready) {
        printed = snprintf(pcInsert, iInsertLen, "false");
      } else {
        printed = snprintf(pcInsert, iInsertLen, "true");
      }
      break;
    }
    case 6: /* APPSFLDB */
    {
      if (!appmngr_get_sdcard_info()->ready) {
        printed = snprintf(pcInsert, iInsertLen, "false");
      } else {
        if (!appmngr_get_sdcard_info()->apps_folder_found) {
          printed = snprintf(pcInsert, iInsertLen, "false");
        } else {
          printed = snprintf(pcInsert, iInsertLen, "true");
        }
      }
      break;
    }
    case 7: /* JSONPLD */
    {
      // DPRINTF("SSI JSONPLD handler called with index %d\n", iIndex);
      int chunk_size = 128;
      /* The offset into json based on current tag part */
      size_t offset = current_tag_part * chunk_size;
      size_t json_len = strlen(apps_installed_json);

      /* If offset is beyond the end, we have no more data */
      if (offset >= json_len) {
        /* No more data, so no next part */
        printed = 0;
        break;
      }

      /* Calculate how many bytes remain from offset */
      size_t remain = json_len - offset;
      /* We want to send up to chunk_size bytes per part, or what's left if
       * <chunk_size */
      size_t chunk_len = (remain < chunk_size) ? remain : chunk_size;

      /* Also ensure we don't exceed iInsertLen - 1, to leave room for '\0' */
      if (chunk_len > (size_t)(iInsertLen - 1)) {
        chunk_len = iInsertLen - 1;
      }

      /* Copy that chunk into pcInsert */
      memcpy(pcInsert, &apps_installed_json[offset], chunk_len);
      pcInsert[chunk_len] = '\0'; /* null-terminate */

      printed = (u16_t)chunk_len;

      /* If there's more data after this chunk, increment next_tag_part */
      if ((offset + chunk_len) < json_len) {
        *next_tag_part = current_tag_part + 1;
      }
      break;
    }
    case 8: /* DWNLDSTS */
    {
      download_status_t status = appmngr_get_download_status();
      download_err_t err = appmngr_get_download_error();

      switch (status) {
        case DOWNLOAD_STATUS_IDLE:
        case DOWNLOAD_STATUS_COMPLETED:
          printed = snprintf(
              pcInsert, iInsertLen, "%s",
              "<meta http-equiv='refresh' content='0;url=/mngr_home.shtml'>");
          break;
          break;
        case DOWNLOAD_STATUS_FAILED:
          printed = snprintf(
              pcInsert, iInsertLen,
              "<meta http-equiv='refresh' "
              "content='0;url=/"
              "error.shtml?error=%d&error_msg=Download%%20error:%%20%s'>",
              err, appmngr_get_download_error_str());
          break;
          break;
        default:
          printed = snprintf(
              pcInsert, iInsertLen, "%s",
              "<meta http-equiv='refresh' content='5;url=/downloading.shtml'>");
          break;
      }
      break;
    }
    case 9: /* TITLEHDR */
    {
#if _DEBUG == 0
      printed = snprintf(pcInsert, iInsertLen, "%s (%s)", BOOSTER_TITLE,
                         RELEASE_VERSION);
#else
      printed = snprintf(pcInsert, iInsertLen, "%s (%s-%s)", BOOSTER_TITLE,
                         RELEASE_VERSION, RELEASE_DATE);
#endif
      break;
    }
    case 10: /* WIFILST */
    {
      if (current_tag_part == 0) {
        mngr_enable_network_scan();
        networks = mngr_get_networks();
      }
      // Loop through networks->count;
      if (networks != NULL) {
        if (current_tag_part < networks->count) {
          wifi_network_info_t *network = &networks->networks[current_tag_part];
          char last_char =
              (current_tag_part == networks->count - 1) ? ' ' : ',';
          // Network names are whatever nearby access points broadcast: a
          // quote in one would end its JSON string.
          char ssid[sizeof(network->ssid) * 6 + 1];
          textescape_js(ssid, sizeof(ssid), network->ssid);
          printed = snprintf(pcInsert, iInsertLen,
                             "{\"SSID\": \"%s\",\"BSSID\": \"%s\",\"RSSI\": "
                             "\"%d\",\"AUTH\": \"%d\"}%c",
                             ssid, network->bssid, network->rssi,
                             network->auth_mode, last_char);
          *next_tag_part = current_tag_part + 1;
        } else {
          printed = 0;
          networks = NULL;
        }
      } else {
        printed = 0;
      }
      break;
    }
    case 11: /* WLSTSTP */
    {
      // Stop the network scan and print an empty string
      mngr_disable_network_scan();
      printed = snprintf(pcInsert, iInsertLen, "");
      break;
    }
    case 12: /* RSPSTS */
    {
      printed = snprintf(pcInsert, iInsertLen, "%d", response_status);
      break;
    }
    case 13: /* RSPMSG */
    {
      printed = snprintf(pcInsert, iInsertLen, "%s", httpd_response_message);
      break;
    }
    case 14: /* BTFTR */
    {
      printed = snprintf(
          pcInsert, iInsertLen, "%s",
          settings_find_entry(gconfig_getContext(), PARAM_BOOT_FEATURE)->value);
      break;
    }
    case 15: /* SFCNFGRB */
    {
      printed =
          snprintf(pcInsert, iInsertLen, "%s",
                   strcasecmp(settings_find_entry(gconfig_getContext(),
                                                  PARAM_SAFE_CONFIG_REBOOT)
                                  ->value,
                              "true") == 0
                       ? "Yes"
                       : "No");
      break;
    }
    case 16: /* SDBDRTKB */
    {
      printed = snprintf(
          pcInsert, iInsertLen, "%s",
          settings_find_entry(gconfig_getContext(), PARAM_SD_BAUD_RATE_KB)
              ->value);
      break;
    }
    case 17: /* APPSURL */
    {
      // Inserted into script strings and JSON: escaped so the URL cannot end
      // the string it is in.
      printed = (int)textescape_js(
          pcInsert, (size_t)iInsertLen,
          settings_find_entry(gconfig_getContext(), PARAM_APPS_CATALOG_URL)
              ->value);
      break;
    }
    case 18: /* NVERSION */
    {
      printed = snprintf(pcInsert, iInsertLen, "%s",
                         version_isNewer() ? "Yes" : "No");
      break;
    }
    case 19: /* NVERSTR */
    {
      printed = snprintf(pcInsert, iInsertLen, "%s", version_get_string());
      break;
    }
    case 20: /* DEVMODE */
    {
      // Reports the REAL gate, evaluated by the firmware: the development app
      // is installed AND the Development channel is selected. The page must
      // not infer either half for itself -- a banner that disagrees with what
      // the device accepts is worse than no banner.
      printed = snprintf(pcInsert, iInsertLen, "%s",
                         devapi_isEnabled() ? "Yes" : "No");
      break;
    }
    case 40: /* WDHCP */
    {
      printed = snprintf(
          pcInsert, iInsertLen, "%s",
          strcasecmp(
              settings_find_entry(gconfig_getContext(), PARAM_WIFI_DHCP)->value,
              "true") == 0
              ? "Yes"
              : "No");
      break;
    }
    case 41: /* WIP */
    {
      printed = snprintf(
          pcInsert, iInsertLen, "%s",
          settings_find_entry(gconfig_getContext(), PARAM_WIFI_IP)->value);
      break;
    }
    case 42: /* WNTMSK */
    {
      printed = snprintf(
          pcInsert, iInsertLen, "%s",
          settings_find_entry(gconfig_getContext(), PARAM_WIFI_NETMASK)->value);
      break;
    }
    case 43: /* WGTWY */
    {
      printed = snprintf(
          pcInsert, iInsertLen, "%s",
          settings_find_entry(gconfig_getContext(), PARAM_WIFI_GATEWAY)->value);
      break;
    }
    case 44: /* WDNS */
    {
      printed = snprintf(
          pcInsert, iInsertLen, "%s",
          settings_find_entry(gconfig_getContext(), PARAM_WIFI_DNS)->value);
      break;
    }
    case 45: /* WCNTRY */
    {
      printed = snprintf(
          pcInsert, iInsertLen, "%s",
          settings_find_entry(gconfig_getContext(), PARAM_WIFI_COUNTRY)->value);
      break;
    }
    case 46: /* WHSTNM */
    {
      printed = snprintf(
          pcInsert, iInsertLen, "%s",
          settings_find_entry(gconfig_getContext(), PARAM_HOSTNAME)->value);
      break;
    }
    case 47: /* WPWR */
    {
      printed = snprintf(
          pcInsert, iInsertLen, "%s",
          settings_find_entry(gconfig_getContext(), PARAM_WIFI_POWER)->value);
      break;
    }
    case 48: /* WRSS */
    {
      printed = snprintf(
          pcInsert, iInsertLen, "%s",
          strcasecmp(
              settings_find_entry(gconfig_getContext(), PARAM_WIFI_RSSI)->value,
              "true") == 0
              ? "Yes"
              : "No");
      break;
    }
    case 49: /* MACADDR */
    {
      const char *mac = network_getCyw43MacStr();
      printed = snprintf(pcInsert, iInsertLen, "%s", mac != NULL ? mac : "");
      break;
    }
    case 50: /* WSIGNAL */
    {
      int32_t rssi = 0;
      if (network_getCurrentRssi(&rssi)) {
        printed = snprintf(pcInsert, iInsertLen, "%" PRId32 " dBm (%s)", rssi,
                           network_getSignalQualityLabel(rssi));
      } else {
        printed = snprintf(pcInsert, iInsertLen, "N/A");
      }
      break;
    }
    case 51: /* WIFICONF */
    {
      // Same rule as DEVMODE: report the firmware's own check, so the page
      // grays out exactly what cgi_saveparams will refuse.
      const char *lockedText = "No";
      if (mngr_httpd_wificonfLocks()) {
        lockedText = "Yes";
      }
      printed = snprintf(pcInsert, iInsertLen, "%s", lockedText);
      break;
    }
    case 21: /* DLSTAT - download progress, polled by downloading.shtml */
    {
      uint32_t received = 0;
      uint32_t total = 0;
      appmngr_get_download_progress(&received, &total);
      appmngr_phase_t phase = appmngr_get_download_phase();

      // Percent only means anything with a Content-Length; 0 total tells the
      // page to show an indeterminate bar instead of a fake number.
      int pct = 0;
      if (total > 0) {
        pct = (int)((uint64_t)received * 100U / total);
        if (pct > 100) {
          pct = 100;
        }
      } else if (phase > APPMNGR_PHASE_DOWNLOAD) {
        pct = 100;
      }

      bool is_firmware =
          (appmngr_get_download_firmware_status() != DOWNLOAD_STATUS_IDLE);
      download_err_t err = is_firmware ? appmngr_get_download_firmware_error()
                                       : appmngr_get_download_error();
      // Use the code for the flow that actually failed. The app and firmware
      // downloads keep separate error variables, and reading the wrong one
      // reported a failed firmware upgrade as "No error".
      const char *msg = "";
      if (phase == APPMNGR_PHASE_FAILED) {
        msg = (err == DOWNLOAD_OK) ? "The download did not complete"
                                   : appmngr_download_error_to_str(err);
      }

      printed = snprintf(pcInsert, iInsertLen,
                         // %.100s, not %.48s: the scaffolding above is 76
                         // bytes and the insert limit is 192, so 100 fits with
                         // margin. 48 cut "Checksum mismatch: the download is
                         // corrupt or incomplete" mid-word. Error strings are
                         // fixed literals with no quotes or backslashes, so
                         // they need no JSON escaping; keep it that way.
                         "{\"ph\":%d,\"pct\":%d,\"rx\":%lu,\"tot\":%lu,"
                         "\"fw\":%d,\"err\":%d,\"msg\":\"%.100s\"}",
                         (int)phase, pct, (unsigned long)received,
                         (unsigned long)total, is_firmware ? 1 : 0, (int)err,
                         msg);
      break;
    }
    case 22: /* DLNAME - which app is installing */
    {
      // The catalog's app name, inside a script string on downloading.shtml:
      // escaped, or a name such as "Diego's Floppy" ends the string.
      char name[65];
      snprintf(name, sizeof(name), "%s", appmngr_get_app_info()->name);
      printed = (int)textescape_js(pcInsert, (size_t)iInsertLen, name);
      break;
    }
    case 23: /* TITLEH1 */
    {
#if _DEBUG == 0
      printed = snprintf(pcInsert, iInsertLen, "%s <small>(%s)</small>",
                         BOOSTER_TITLE, RELEASE_VERSION);
#else
      printed = snprintf(pcInsert, iInsertLen, "%s <small>(%s-%s)</small>",
                         BOOSTER_TITLE, RELEASE_VERSION, RELEASE_DATE);
#endif
      break;
    }
    default: /* unknown tag */
      printed = 0;
      break;
  }
  // snprintf returns what it WOULD have written. Returning a value larger than
  // the buffer makes lwIP write that many bytes out of tag_insert[], reading
  // past the end. Clamp before returning.
  if (printed > (iInsertLen - 1)) {
    printed = iInsertLen - 1;
  }
  LWIP_ASSERT("sane length", printed <= 0xFFFF);
  return (u16_t)printed;
}

// The main function should be as follows:
void mngr_httpd_start() {
  // Initialize the HTTP server with SSI tags and CGI handlers
  httpd_server_init(ssi_tags, LWIP_ARRAYSIZE(ssi_tags), ssi_handler,
                    cgi_handlers, LWIP_ARRAYSIZE(cgi_handlers));
}
