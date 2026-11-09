/**
 * File: devapi.c
 * Author: Diego Parrilla Santamaría
 * Date: July 2026
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: Minimal deploy API for microfirmware developers
 */

#include "devapi.h"

#include "appmngr.h"

// One upload at a time. lwIP's httpd serves POST bodies one connection at a
// time anyway, but the state has to be explicit so a second request is
// refused rather than interleaved into the first one's file.
static bool uploadInProgress = false;
static FIL uploadFile;
static uint32_t uploadBytes = 0;
static uint32_t lastUploadBytes = 0;
// Declared Content-Length, or 0 when the client did not say. This is what
// makes a truncated transfer detectable: lwIP calls httpd_post_finished() when
// a connection dies mid-body just as it does on success, so "finished" alone
// says nothing about whether all the bytes arrived.
static uint32_t expectedBytes = 0;

/**
 * @brief Build "<apps folder>/<name>", the way appmngr does everywhere else.
 */
static void devapi_buildAppsPath(char *out, size_t outSize, const char *name) {
  const SettingsConfigEntry *folder =
      settings_find_entry(gconfig_getContext(), PARAM_APPS_FOLDER);
  snprintf(out, outSize, "%s/%s", folder != NULL ? folder->value : "/apps",
           name);
}

static void devapi_buildDevAppPath(char *out, size_t outSize,
                                   const char *extension) {
  char name[64] = {0};
  snprintf(name, sizeof(name), "%s%s", DEVAPI_DEV_APP_UUID, extension);
  devapi_buildAppsPath(out, outSize, name);
}

/**
 * @brief Second half of the gate: the configured catalog is EXACTLY the
 * canonical development catalog.
 *
 * Exact comparison on purpose (the catalog URL migration uses the same rule).
 * No trimming, no case folding, no ignoring a query string: "the Development
 * channel is selected" has to mean one specific string, or it cannot be
 * described accurately in the developer docs.
 */
static bool devapi_isDevChannelSelected(void) {
  const SettingsConfigEntry *entry =
      settings_find_entry(gconfig_getContext(), PARAM_APPS_CATALOG_URL);
  if (entry == NULL || entry->value == NULL) {
    return false;
  }
  return strcmp(entry->value, DEVAPI_DEV_CATALOG_URL) == 0;
}

/**
 * @brief First half of the gate: the development app is installed.
 *
 * Its .json is written by the normal install path, from the real catalog
 * entry, so its presence is evidence the developer deliberately installed the
 * DEV APP rather than a value that could be changed by a stray click.
 */
static bool devapi_isDevAppInstalled(void) {
  if (!appmngr_get_sdcard_info()->ready) {
    return false;
  }
  char path[256] = {0};
  devapi_buildDevAppPath(path, sizeof(path), ".json");
  FILINFO info = {0};
  return f_stat(path, &info) == FR_OK;
}

bool devapi_isEnabled(void) {
  return devapi_isDevChannelSelected() && devapi_isDevAppInstalled();
}

bool devapi_hasCatalogBinary(void) {
  if (!appmngr_get_sdcard_info()->ready) {
    return false;
  }
  char path[DEVAPI_PATH_MAX] = {0};
  devapi_buildDevAppPath(path, sizeof(path), ".uf2");
  FILINFO info = {0};
  if (f_stat(path, &info) != FR_OK || info.fsize == 0) {
    return false;
  }
  return true;
}

void devapi_buildUploadPath(char *out, size_t outSize) {
  devapi_buildDevAppPath(out, outSize, DEVAPI_UPLOAD_EXTENSION);
}

bool devapi_hasUploadedBinary(void) {
  if (!appmngr_get_sdcard_info()->ready) {
    return false;
  }
  char path[DEVAPI_PATH_MAX] = {0};
  devapi_buildUploadPath(path, sizeof(path));
  FILINFO info = {0};
  if (f_stat(path, &info) != FR_OK || info.fsize == 0) {
    return false;
  }
  return true;
}

uint32_t devapi_lastUploadSize(void) { return lastUploadBytes; }

devapi_err_t devapi_uploadBegin(int contentLength) {
  if (!devapi_isEnabled()) {
    DPRINTF("Deploy API is disabled; upload refused\n");
    return DEVAPI_DISABLED;
  }
  if (uploadInProgress) {
    // Do not refuse forever. If a previous transfer never reached
    // httpd_post_finished the state would otherwise stay stuck until reboot,
    // and the developer would have no way to tell why. The stale transfer only
    // ever owns the temporary file, so abandoning it costs nothing.
    DPRINTF("Abandoning a stale upload before starting a new one\n");
    devapi_uploadFinish(false);
  }
  // Refuse on the declared length before a single byte reaches the card. A
  // .uf2 larger than the microfirmware slot can never be flashed, so there is
  // no reason to spend the SD write and the developer's time discovering that
  // at the end of a transfer.
  if (contentLength > 0 && (uint32_t)contentLength > MAXIMUM_APP_UF2_SIZE) {
    DPRINTF("Upload of %d bytes exceeds the %u byte cap\n", contentLength,
            (unsigned)MAXIMUM_APP_UF2_SIZE);
    return DEVAPI_TOO_LARGE;
  }

  char tmpPath[256] = {0};
  devapi_buildAppsPath(tmpPath, sizeof(tmpPath), DEVAPI_UPLOAD_TMP_NAME);

  FRESULT res = f_open(&uploadFile, tmpPath, FA_WRITE | FA_CREATE_ALWAYS);
  if (res != FR_OK) {
    DPRINTF("Cannot open %s for writing: %i\n", tmpPath, res);
    return DEVAPI_WRITE_ERROR;
  }

  uploadInProgress = true;
  uploadBytes = 0;
  expectedBytes = contentLength > 0 ? (uint32_t)contentLength : 0;
  DPRINTF("Upload started, declared length %d\n", contentLength);
  return DEVAPI_OK;
}

devapi_err_t devapi_uploadData(struct pbuf *p) {
  if (!uploadInProgress) {
    return DEVAPI_DISABLED;
  }
  // Stream the chain straight to the card, mirroring the download path
  // (appmngr.c:472). No malloc, no accumulation: the body is far larger than
  // the heap and always will be.
  for (struct pbuf *q = p; q != NULL; q = q->next) {
    UINT written = 0;
    FRESULT res = f_write(&uploadFile, q->payload, q->len, &written);
    if (res != FR_OK || written != q->len) {
      DPRINTF("Upload write failed: %i (wrote %u of %u)\n", res,
              (unsigned)written, (unsigned)q->len);
      devapi_uploadFinish(false);
      return DEVAPI_WRITE_ERROR;
    }
    uploadBytes += q->len;
  }

  // The cap is enforced again here because Content-Length is a claim, not a
  // guarantee: a chunked or lying client could otherwise fill the card.
  if (uploadBytes > MAXIMUM_APP_UF2_SIZE) {
    DPRINTF("Upload exceeded the cap mid-transfer (%u bytes)\n",
            (unsigned)uploadBytes);
    devapi_uploadFinish(false);
    return DEVAPI_TOO_LARGE;
  }
  return DEVAPI_OK;
}

devapi_err_t devapi_uploadFinish(bool commit) {
  if (!uploadInProgress) {
    return DEVAPI_NOT_UPLOADED;
  }
  uploadInProgress = false;
  f_close(&uploadFile);

  char tmpPath[256] = {0};
  devapi_buildAppsPath(tmpPath, sizeof(tmpPath), DEVAPI_UPLOAD_TMP_NAME);

  // A dropped connection reaches this function exactly like a successful one,
  // so the byte count is what separates them. Committing a short file would
  // rename a truncated .uf2 over a working one and brick the slot on the next
  // launch, which is the single worst outcome this whole feature could have.
  bool complete = (expectedBytes == 0) ? (uploadBytes > 0)
                                       : (uploadBytes == expectedBytes);
  if (!commit || !complete) {
    DPRINTF("Upload not committed (%u of %u bytes); removing %s\n",
            (unsigned)uploadBytes, (unsigned)expectedBytes, tmpPath);
    f_unlink(tmpPath);
    return (commit && !complete) ? DEVAPI_WRITE_ERROR : DEVAPI_OK;
  }

  // Rename last. Until this succeeds the previous upload is untouched, so a
  // failed upload leaves it in place rather than a partial file. Same shape
  // as the firmware upgrade's tmp.download -> upgrade.uf2. The target is the
  // upload file, never the catalog's <uuid>.uf2.
  char finalPath[256] = {0};
  devapi_buildUploadPath(finalPath, sizeof(finalPath));
  f_unlink(finalPath);
  FRESULT res = f_rename(tmpPath, finalPath);
  if (res != FR_OK) {
    DPRINTF("Cannot rename %s to %s: %i\n", tmpPath, finalPath, res);
    f_unlink(tmpPath);
    return DEVAPI_WRITE_ERROR;
  }

  lastUploadBytes = uploadBytes;
  DPRINTF("Upload committed: %u bytes to %s\n", (unsigned)uploadBytes,
          finalPath);
  return DEVAPI_OK;
}
