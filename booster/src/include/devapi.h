/**
 * File: devapi.h
 * Author: Diego Parrilla Santamaría
 * Date: July 2026
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: Minimal deploy API for microfirmware developers
 *
 * Lets a developer push a .uf2 over WiFi and run it, instead of needing a
 * debug probe or the USB/BOOTSEL dance.
 *
 * THE GATE
 * --------
 * Both conditions must hold, and both are checked on EVERY request:
 *
 *   1. /apps/44444444-4444-4444-8444-444444444444.json exists on the SD card,
 *      which only happens if the developer installed the DEV APP from the
 *      Development channel like any other microfirmware.
 *   2. APPS_CATALOG_URL is exactly the canonical development catalog URL.
 *
 * They fail differently on purpose. The json makes the API impossible to turn
 * on by accident: it takes switching channel, finding the app, and installing
 * it. The channel makes it trivial to turn off again, from a dropdown, without
 * deleting anything.
 *
 * A developer who switches to another channel loses the API until they switch
 * back. That is deliberate, and it is why the banner exists: the visible state
 * is what stops a working curl suddenly failing from looking like a bug.
 *
 * THIS IS NOT AUTHENTICATION. Anyone who can reach the device while the gate
 * is open can write code that runs on the cartridge, and the UI is served over
 * plain http so there is nothing to sniff-proof it. The gate makes the
 * exposure deliberate and visible; it does not make it safe. Do not describe
 * it as secure in user-facing text (the notes on TLS in lwipopts.h state its
 * trust model the same way, plainly rather than overclaiming).
 */

#ifndef DEVAPI_H
#define DEVAPI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "constants.h"
#include "debug.h"
#include "gconfig.h"
#include "lwip/err.h"
#include "lwip/pbuf.h"
#include "sdcard.h"

/**
 * @brief The development microfirmware's fixed UUID.
 *
 * appmngr.c special-cases this UUID on launch. The catalog's <uuid>.uf2 is
 * never copied to flash: it is the placeholder, and the real binary got into
 * the slot by debug probe or BOOTSEL. The one exception is an upload from
 * this API, which lives in its own file (DEVAPI_UPLOAD_EXTENSION).
 */
#define DEVAPI_DEV_APP_UUID "44444444-4444-4444-8444-444444444444"

/**
 * @brief Suffix of the uploaded binary, beside the app's own .json and .uf2.
 *
 * Uploads used to be renamed over <uuid>.uf2, the file installing the DEV APP
 * from the catalog writes. The two could not be told apart, so launching with
 * the DEV APP installed flashed the catalog placeholder over a probe-flashed
 * microfirmware. A name of its own keeps the upload and the
 * placeholder distinct, and a catalog reinstall, which only replaces the .json
 * and the .uf2, leaves the upload alone.
 */
#define DEVAPI_UPLOAD_EXTENSION ".dev.uf2"

/**
 * @brief The only catalog URL that opens the gate.
 *
 * Exact match, deliberately, the same rule the catalog URL migration uses. A
 * custom catalog URL does not open the gate even if it serves the DEV APP,
 * because "the Development channel is selected" has to mean one specific
 * thing to be stateable in the docs.
 */
#define DEVAPI_DEV_CATALOG_URL \
  "https://md-store.sidecartridge.com/atari-st/apps-dev.json"

/** @brief URI that accepts a .uf2 body. */
#define DEVAPI_UPLOAD_URI "/dev_upload.cgi"

/** @brief Temporary name an in-flight upload is written under. */
#define DEVAPI_UPLOAD_TMP_NAME "devupl.tmp"

/** @brief Buffer size for an apps folder path, as appmngr uses everywhere. */
#define DEVAPI_PATH_MAX 256

typedef enum {
  DEVAPI_OK = 0,
  DEVAPI_DISABLED,      // gate shut
  DEVAPI_BUSY,          // an upload is already in flight
  DEVAPI_TOO_LARGE,     // body exceeds MAXIMUM_APP_UF2_SIZE
  DEVAPI_NO_SDCARD,     // card missing or not mounted
  DEVAPI_WRITE_ERROR,   // f_write / f_close / rename failed
  DEVAPI_NOT_UPLOADED,  // asked to flash with nothing uploaded
} devapi_err_t;

/**
 * @brief Is the deploy API available right now?
 *
 * Evaluates both gate conditions. Cheap enough to call per request and per SSI
 * render: one settings lookup and one f_stat.
 *
 * @return true when both conditions hold.
 */
bool devapi_isEnabled(void);

/**
 * @brief Begin an upload. Rejects when the gate is shut, a transfer is already
 * in flight, or the declared length cannot fit the microfirmware slot.
 *
 * @param contentLength Declared body length, or 0 when unknown.
 * @return DEVAPI_OK when the caller may start feeding data.
 */
devapi_err_t devapi_uploadBegin(int contentLength);

/**
 * @brief Feed one pbuf chain to the in-flight upload.
 *
 * Writes straight through to the SD card and never accumulates: the heap is
 * about 44KB and a microfirmware .uf2 runs past 1MB, so buffering the
 * body is not an option. The caller still owns @p p.
 */
devapi_err_t devapi_uploadData(struct pbuf *p);

/**
 * @brief Finish an upload.
 *
 * On success the temporary file is renamed over the development app's upload
 * file (DEVAPI_UPLOAD_EXTENSION), so a transfer that dies midway can never
 * leave something flashable behind.
 *
 * @param commit false to abandon the transfer and delete the temporary file.
 */
devapi_err_t devapi_uploadFinish(bool commit);

/**
 * @brief Has a .uf2 been uploaded for the development app?
 *
 * Used by the launch path to decide whether to program flash or to leave the
 * slot alone for a developer working with a debug probe. Only the upload file
 * counts; the catalog's .uf2 never does.
 */
bool devapi_hasUploadedBinary(void);

/**
 * @brief Is the binary downloaded from the catalog for the development app on
 * the card, and not empty?
 *
 * "Restore and launch" flashes that file, so it refuses when it is missing.
 */
bool devapi_hasCatalogBinary(void);

/**
 * @brief Path of the development app's upload file,
 * "<apps folder>/<DEVAPI_DEV_APP_UUID>DEVAPI_UPLOAD_EXTENSION".
 *
 * The launch path flashes this file, and deleting the app removes it.
 */
void devapi_buildUploadPath(char *out, size_t outSize);

/**
 * @brief Bytes received by the last completed upload. Diagnostics only.
 */
uint32_t devapi_lastUploadSize(void);

#endif  // DEVAPI_H
