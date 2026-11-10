#include "include/gconfig.h"

static SettingsConfigEntry defaultEntries[] = {
    {PARAM_APPS_FOLDER, SETTINGS_TYPE_STRING, "/apps"},
    {PARAM_APPS_CATALOG_URL, SETTINGS_TYPE_STRING,
     "https://md-store.sidecartridge.com/atari-st/apps.json"},
    {PARAM_BOOT_FEATURE, SETTINGS_TYPE_STRING, "FABRIC"},
    {PARAM_HOSTNAME, SETTINGS_TYPE_STRING, "sidecart"},
    {PARAM_SAFE_CONFIG_REBOOT, SETTINGS_TYPE_BOOL, "true"},
    {PARAM_SD_BAUD_RATE_KB, SETTINGS_TYPE_INT, "12500"},
    {PARAM_WIFI_AUTH, SETTINGS_TYPE_INT, "0"},
    {PARAM_WIFI_CONNECT_TIMEOUT, SETTINGS_TYPE_INT, "30"},
    {PARAM_WIFI_COUNTRY, SETTINGS_TYPE_STRING, "XX"},
    {PARAM_WIFI_DHCP, SETTINGS_TYPE_BOOL, "true"},
    {PARAM_WIFI_DNS, SETTINGS_TYPE_STRING, "8.8.8.8"},
    {PARAM_WIFI_GATEWAY, SETTINGS_TYPE_STRING, ""},
    {PARAM_WIFI_IP, SETTINGS_TYPE_STRING, ""},
    {PARAM_WIFI_MODE, SETTINGS_TYPE_INT, "0"},
    {PARAM_WIFI_NETMASK, SETTINGS_TYPE_STRING, ""},
    {PARAM_WIFI_PASSWORD, SETTINGS_TYPE_STRING, ""},
    // 4 = No Powersave: always-on radio, highest power draw. The device is
    // mains-powered through the cartridge port, so trading power for a radio
    // that never sleeps is the right default here.
    {PARAM_WIFI_POWER, SETTINGS_TYPE_INT, "4"},
    {PARAM_WIFI_RSSI, SETTINGS_TYPE_BOOL, "true"},
    {PARAM_WIFI_SCAN_SECONDS, SETTINGS_TYPE_INT, "10"},
    {PARAM_WIFI_SSID, SETTINGS_TYPE_STRING, ""}};

enum {
  CONFIG_BUFFER_SIZE = 4096,
  CONFIG_MAGIC_NUMBER = 0x1234,
  CONFIG_VERSION_NUMBER = 0x0001
};

// Create a global context for our settings
static SettingsContext gSettingsCtx;

/**
 * @brief Initializes the global configuration settings.
 *
 * This function initializes the global configuration settings using the
 * provided default entries. If the settings are not initialized, it initializes
 * them with the default values. If a current application name is provided, it
 * checks if the current application matches the one in the settings.
 *
 * @param current_app_name The name of the current application. If NULL, the
 * function will ignore the application name check.
 * @return int Returns GCONFIG_SUCCESS on success, GCONFIG_INIT_ERROR if there
 * is an error initializing the settings, or GCONFIG_MISMATCHED_APP if the
 * current application does not match the one in the settings.
 */
int gconfig_init(const char *currentAppName) {
  DPRINTF("Initializing settings\n");
  int err = settings_init(&gSettingsCtx, defaultEntries,
                          sizeof(defaultEntries) / sizeof(defaultEntries[0]),
                          (unsigned int)&_global_config_flash_start - XIP_BASE,
                          CONFIG_BUFFER_SIZE, CONFIG_MAGIC_NUMBER,
                          CONFIG_VERSION_NUMBER);

  // If the settings are not initialized, then we must initialize them with the
  // default values in the Booster application
  if (err < 0) {
    DPRINTF("Error initializing settings.\n");
    return GCONFIG_INIT_ERROR;
  }

  // If the current app as argument is not null, check if the current app is the
  // same as the one in the settings Otherwise, ignore and continue
  if (currentAppName != NULL) {
    // If we are here, it means that the settings were initialized correctly
    // We now must read the flash address of the configuration settings of the
    // current application
    SettingsConfigEntry *entry =
        settings_find_entry(&gSettingsCtx, PARAM_BOOT_FEATURE);
    if ((entry == NULL) || (entry->value == NULL) ||
        (strcmp(currentAppName, entry->value) != 0)) {
      // If the entry is found but the content is empty, or not equal to the
      // current app name then go to the Booster application
      DPRINTF(
          "The current app (%s) is not the same as the one in the settings "
          "(%s)\n",
          currentAppName, entry->value);
      return GCONFIG_MISMATCHED_APP;
    }
  } else {
    DPRINTF("The current app is not provided as argument. Booster app?\n");
  }

  DPRINTF("Settings loaded.\n");

  settings_print(&gSettingsCtx, NULL);

  return GCONFIG_SUCCESS;
}

/**
 * @brief Returns a pointer to the global settings context.
 *
 * This function allows other parts of the application to retrieve a pointer
 * to the global settings context at any time.
 *
 * @return SettingsContext* Pointer to the global settings context.
 */
SettingsContext *gconfig_getContext(void) { return &gSettingsCtx; }

// One-shot migration of APPS_CATALOG_URL onto the md-store host.
//
// Migrate if and ONLY if the stored value is byte-for-byte one of the three
// canonical URLs that shipped firmware wrote. Those three strings were written
// by Booster itself and are always literal, so an exact comparison is safe and
// anything differing by a single character is by definition user-supplied and
// must be left alone: custom hosts, the https variants of the old host, and
// anything carrying a query string all stay untouched. No normalisation of
// case, trailing slash or query.
//
// Verified against git: these three values are identical in v2.2.0 and in the
// commit before the repoint, so the table covers every value a shipped firmware
// could have stored.
static const struct {
  const char *from;
  const char *to;
} CATALOG_URL_MIGRATIONS[] = {
    {"http://atarist.sidecartridge.com/apps.json",
     "https://md-store.sidecartridge.com/atari-st/apps.json"},
    // Source stays apps-beta.json: that is the literal string shipped firmware
    // wrote, and it cannot be revised after the fact. The target follows the
    // Testing channel to apps-test.json, so a device migrating off the old
    // host lands on a catalog that exists and that the Config dropdown still
    // offers -- migrating it to apps-beta.json would strand it on a URL that
    // 404s and is no longer selectable.
    {"http://atarist.sidecartridge.com/apps-beta.json",
     "https://md-store.sidecartridge.com/atari-st/apps-test.json"},
    {"http://atarist.sidecartridge.com/apps-dev.json",
     "https://md-store.sidecartridge.com/atari-st/apps-dev.json"},
};

bool gconfig_migrateCatalogUrl(void) {
  SettingsConfigEntry *entry =
      settings_find_entry(gconfig_getContext(), PARAM_APPS_CATALOG_URL);
  if (entry == NULL) {
    // Entry missing entirely, which should not happen: the defaults table
    // always provides one. Nothing to migrate either way.
    return false;
  }

  for (size_t i = 0;
       i < sizeof(CATALOG_URL_MIGRATIONS) / sizeof(CATALOG_URL_MIGRATIONS[0]);
       i++) {
    if (strcmp(entry->value, CATALOG_URL_MIGRATIONS[i].from) != 0) {
      continue;
    }
    DPRINTF("Migrating apps catalog URL:\n  from %s\n  to   %s\n",
            CATALOG_URL_MIGRATIONS[i].from, CATALOG_URL_MIGRATIONS[i].to);
    settings_put_string(gconfig_getContext(), PARAM_APPS_CATALOG_URL,
                        CATALOG_URL_MIGRATIONS[i].to);
    settings_save(gconfig_getContext(), true);
    return true;
  }

  // No match: leave the value alone and, importantly, do NOT write flash. A
  // fresh device (already holding the new default), an already-migrated device,
  // and a user's custom URL all land here.
  return false;
}