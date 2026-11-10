#include "gconfig.h"

#include <stddef.h>

#include "constants.h"
#include "debug.h"

enum {
  GCONFIG_FLASH_SIZE = 4096,
  GCONFIG_KEY_LENGTH = 30,
  GCONFIG_VALUE_LENGTH = 96
};

#define GCONFIG_DEFAULT_APPS_FOLDER "/apps"
#define GCONFIG_MAGIC_VERSION_KEY "MAGICVERSION"
#define GCONFIG_MAGIC_VERSION_VALUE "305397761"

typedef enum {
  SETTINGS_TYPE_INT = 0,
  SETTINGS_TYPE_STRING = 1,
  SETTINGS_TYPE_BOOL = 2
} SettingsDataType;

typedef struct {
  char key[GCONFIG_KEY_LENGTH];
  SettingsDataType dataType;
  char value[GCONFIG_VALUE_LENGTH];
} SettingsConfigEntry;

_Static_assert(sizeof(SettingsDataType) == 1, "Unexpected settings enum size");
_Static_assert(offsetof(SettingsConfigEntry, dataType) == GCONFIG_KEY_LENGTH,
               "Unexpected settings key layout");
_Static_assert(offsetof(SettingsConfigEntry, value) ==
                   GCONFIG_KEY_LENGTH + sizeof(SettingsDataType),
               "Unexpected settings value layout");

static char g_apps_folder[GCONFIG_VALUE_LENGTH] = GCONFIG_DEFAULT_APPS_FOLDER;
static unsigned int g_sd_baud_rate_kb = 0;

static int field_equals(const char *field, size_t field_len,
                        const char *expected) {
  size_t i = 0;

  while ((i < field_len) && (expected[i] != '\0')) {
    if (field[i] != expected[i]) {
      return 0;
    }
    i++;
  }

  return (i < field_len) && (field[i] == '\0') && (expected[i] == '\0');
}

static int is_valid_key(const char *key) {
  size_t i = 0;

  while (i < GCONFIG_KEY_LENGTH) {
    const char chr = key[i];

    if (chr == '\0') {
      return i > 0;
    }
    if (((chr < 'A') || (chr > 'Z')) && ((chr < '0') || (chr > '9')) &&
        (chr != '_')) {
      return 0;
    }
    i++;
  }

  return 0;
}

static size_t copy_field(char *dst, size_t dst_len, const char *src,
                         size_t src_len) {
  size_t i = 0;

  if (dst_len == 0) {
    return 0;
  }

  while ((i + 1 < dst_len) && (i < src_len) && (src[i] != '\0')) {
    dst[i] = src[i];
    i++;
  }
  dst[i] = '\0';

  return i;
}

int gconfig_init(const char *currentAppName) {
  const SettingsConfigEntry *entries =
      (const SettingsConfigEntry *)&_global_config_flash_start;
  const size_t max_entries = GCONFIG_FLASH_SIZE / sizeof(SettingsConfigEntry);
  size_t i = 0;
  int boot_feature_match = (currentAppName == NULL);

  copy_field(g_apps_folder, sizeof(g_apps_folder), GCONFIG_DEFAULT_APPS_FOLDER,
             sizeof(GCONFIG_DEFAULT_APPS_FOLDER));

  if (max_entries == 0) {
    return GCONFIG_INIT_ERROR;
  }

  if (!field_equals(entries[0].key, sizeof(entries[0].key),
                    GCONFIG_MAGIC_VERSION_KEY) ||
      !field_equals(entries[0].value, sizeof(entries[0].value),
                    GCONFIG_MAGIC_VERSION_VALUE)) {
    DPRINTF("Global config magic/version mismatch\n");
    return GCONFIG_INIT_ERROR;
  }

  for (i = 1; i < max_entries; i++) {
    const SettingsConfigEntry *entry = &entries[i];

    if (entry->key[0] == '\0') {
      break;
    }
    if (!is_valid_key(entry->key)) {
      break;
    }

    if (field_equals(entry->key, sizeof(entry->key), PARAM_APPS_FOLDER)) {
      if (entry->value[0] != '\0') {
        copy_field(g_apps_folder, sizeof(g_apps_folder), entry->value,
                   sizeof(entry->value));
      }
      continue;
    }

    if (field_equals(entry->key, sizeof(entry->key), PARAM_SD_BAUD_RATE_KB)) {
      // Stored as a decimal string; anything else leaves the default speed.
      unsigned int kb = 0;
      size_t j = 0;
      while (j < sizeof(entry->value) && entry->value[j] >= '0' &&
             entry->value[j] <= '9' && kb < 1000000U) {
        kb = kb * 10U + (unsigned int)(entry->value[j] - '0');
        j++;
      }
      g_sd_baud_rate_kb =
          (j > 0 && (j == sizeof(entry->value) || entry->value[j] == '\0')) ? kb
                                                                            : 0;
      continue;
    }

    if ((currentAppName != NULL) &&
        field_equals(entry->key, sizeof(entry->key), PARAM_BOOT_FEATURE)) {
      boot_feature_match =
          field_equals(entry->value, sizeof(entry->value), currentAppName);
    }
  }

  return boot_feature_match ? GCONFIG_SUCCESS : GCONFIG_MISMATCHED_APP;
}

const char *gconfig_get_apps_folder(void) { return g_apps_folder; }

unsigned int gconfig_get_sd_baud_rate_kb(void) { return g_sd_baud_rate_kb; }
