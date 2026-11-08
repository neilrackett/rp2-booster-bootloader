/**
 * File: appmngr.h
 * Author: Diego Parrilla Santamaría
 * Date: January 2025
 * Copyright: 2025 - GOODDATA LABS SL
 * Description: Header file for the application manager mode.
 */

#ifndef APPMNGR_H
#define APPMNGR_H

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "cjson/cJSON.h"
#include "constants.h"
#include "debug.h"
#include "gconfig.h"
#include "httpc/httpc.h"
#include "lookuptable.h"
#include "lwip/altcp_tls.h"
#include "lwip/apps/httpd.h"
#include "md5/md5.h"
#include "memfunc.h"
#include "network.h"
#include "pico/async_context.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "reset.h"
#include "sdcard.h"
#include "urlparse.h"
#include "uuid.h"

// Sanity caps on Content-Length, checked before we start writing a download to
// the SD card.
//
// A UF2 carries 256 payload bytes in each 512-byte block, so a .uf2 is about
// twice the size of the binary it holds, plus a little framing (the observed
// upgrade.bin is 512 bytes over exactly 2x).
//
// NOTE: these caps were dead code until v2.3.0 -- httpc.c unconditionally
// overwrote the headers callback that enforces them, so no download was ever
// size-checked and both values were wrong without anyone noticing.
#define UF2_OVERHEAD_FACTOR 2
#define UF2_FRAMING_SLACK (64 * 1024)

// A downloaded microfirmware .uf2 can at most fill the 1152K microfirmware
// slot (STORAGE_FLASH in memmap_booster.ld).
#define MAXIMUM_APP_UF2_SIZE \
  ((1152 * 1024 * UF2_OVERHEAD_FACTOR) + UF2_FRAMING_SLACK)

// A downloaded Booster upgrade.bin is the FULL-image .uf2 (placeholder plus
// Booster core, 3,932,672 bytes as of v2.2.0) -- far larger than any single
// microfirmware, so it needs its own cap. Bound it by the whole flash
// expressed as UF2 so the value survives changes to the image layout.
#define MAXIMUM_FIRMWARE_UF2_SIZE \
  ((PICO_FLASH_SIZE_BYTES * UF2_OVERHEAD_FACTOR) + UF2_FRAMING_SLACK)
// Redirects are followed on both download paths, microfirmware and firmware
// OTA. Five matches md-browser and is well past what real hosting chains use:
// the GitHub release path this was written for takes two.
#define APPMNGR_MAX_REDIRECT_HOPS 5

// How long to let lwIP finish tearing down an aborted redirect response
// before opening the next connection. Without this the two overlap and the
// new handshake stalls until the poll timeout.
// Times a single hop is re-issued after receiving nothing, before the whole
// download is failed. A timed-out connection with every lwIP resource
// satisfied means the peer did not answer -- an ordinary internet event.
#define APPMNGR_MAX_HOP_RETRIES 2

// Minimum spacing between closing one hop's connection and opening the next,
// and between a timed-out attempt and its retry. Briefly raised to 2000 while
// chasing the redirect download failures, on the theory that fast turnaround
// corrupted lwIP state. That theory was wrong -- the cause was lwIP send-heap
// exhaustion (see MEM_SIZE in lwipopts.h) -- so this is back to the original
// 500. Cost is paid only on redirected installs.
#define APPMNGR_REDIRECT_SETTLE_MS 500

// Holds a fully-resolved redirect target. Sized for the same worst case as
// url_components.uri plus scheme and host.
#define APPMNGR_MAX_URL_SIZE 1700

#define MAX_TAGS 6
#define MAX_DEVICES 6

#define MAXIMUM_APP_INFO_SIZE 4096

#define UF2_BLOCK_SIZE 512

// Chunk size for the UF2 -> flash copy at app launch (storeUF2FileToFlash).
// Only a batching buffer in front of flash_range_program, which programs
// 256-byte pages: any multiple of FLASH_PAGE_SIZE writes byte-identical flash
// contents. One flash sector is a natural granularity (it matches the erase
// unit) and keeps the buffer small enough to live in bss.
//
// The buffer is STATIC, not malloc'd. History of why:
//   - It used to be the SDK's FLASH_BLOCK_SIZE (64K) from the heap. Once
//   mbedTLS
//     grew bss by ~9KB the allocation ran 536 bytes past 0x20030000, into the
//     ROM_IN_RAM cartridge window, silently corrupting the ROM served to the
//     Atari. memmap_booster.ld now caps the heap at the stack bottom so the
//     heap can never reach that window at all.
//   - With the cap the heap is ~47KB, so 64K could not be allocated anyway, and
//     even 32K failed at launch: by then the session has run the web server,
//     parsed catalog JSON, and done a TLS handshake, so a single CONTIGUOUS 32K
//     block is not obtainable even though the total free heap is larger. At the
//     time malloc panicked rather than returning NULL, so that surfaced as
//     "*** PANIC *** Out of memory". (PICO_MALLOC_PANIC is 0 since v2.4.1, so
//     the same failure would now return NULL -- but a static buffer is still
//     the right answer here.)
// A static buffer removes the whole failure class: no allocation, no
// fragmentation sensitivity, and it leaves the heap free for TLS.
#define APP_FLASH_COPY_CHUNK_SIZE FLASH_SECTOR_SIZE

#define APPMNGR_MAX_INSTALLED_APPS (FLASH_SECTOR_SIZE / LOOKUP_ENTRY_SIZE)
#define APPMNGR_INSTALLED_APP_NAME_LENGTH 64
#define APPMNGR_INSTALLED_APP_VERSION_LENGTH 16

typedef struct {
  bool ready;
  uint32_t total_size;
  uint32_t free_space;
  bool apps_folder_found;
} sdcard_info_t;

typedef struct {
  char uuid[64];
  char name[128];
  char description[512];
  char image[256];

  int tags_count;
  char tags[MAX_TAGS][32];

  int devices_count;
  char devices[MAX_DEVICES][32];

  char binary[256];
  uint8_t md5[16];
  char version[16];
  char json[2048];
  uint8_t file_md5_digest[16];  // Result of algorithm
} app_info_t;

typedef struct {
  char uuid[37];
  char name[APPMNGR_INSTALLED_APP_NAME_LENGTH];
  char version[APPMNGR_INSTALLED_APP_VERSION_LENGTH];
} appmngr_installed_app_t;

typedef enum {
  DOWNLOAD_STATUS_IDLE,
  DOWNLOAD_STATUS_REQUESTED,
  DOWNLOAD_STATUS_NOT_STARTED,
  DOWNLOAD_STATUS_STARTED,
  DOWNLOAD_STATUS_IN_PROGRESS,
  DOWNLOAD_STATUS_COMPLETED,
  DOWNLOAD_STATUS_FAILED
} download_status_t;

// Coarse download stage for the progress UI. Distinct from download_status_t,
// which returns to IDLE on success and says nothing during the synchronous
// verify/install work after the transfer ends.
typedef enum {
  APPMNGR_PHASE_IDLE = 0,
  APPMNGR_PHASE_CONNECT = 1,
  APPMNGR_PHASE_DOWNLOAD = 2,
  APPMNGR_PHASE_VERIFY = 3,
  APPMNGR_PHASE_INSTALL = 4,
  APPMNGR_PHASE_DONE = 5,
  APPMNGR_PHASE_FAILED = 6,
} appmngr_phase_t;

typedef enum {
  DOWNLOAD_POLL_CONTINUE,
  DOWNLOAD_POLL_ERROR,
  DOWNLOAD_POLL_COMPLETED
} download_poll_t;

typedef enum {
  DOWNLOAD_OK,
  DOWNLOAD_BASE64_ERROR,
  DOWNLOAD_PARSEJSON_ERROR,
  DOWNLOAD_PARSEMD5_ERROR,
  DOWNLOAD_CANNOTOPENFILE_ERROR,
  DOWNLOAD_CANNOTWRITEFILE_ERROR,
  DOWNLOAD_CANNOTCLOSEFILE_ERROR,
  DOWNLOAD_FORCEDABORT_ERROR,
  DOWNLOAD_CANNOTSTARTDOWNLOAD_ERROR,
  DOWNLOAD_CANNOTREADFILE_ERROR,
  DOWNLOAD_CANNOTPARSEURL_ERROR,
  DOWNLOAD_MD5MISMATCH_ERROR,
  DOWNLOAD_CANNOTRENAMEFILE_ERROR,
  DOWNLOAD_CANNOTCREATE_CONFIG,
  DOWNLOAD_CANNOTDELETECONFIGSECTOR_ERROR,
  DOWNLOAD_HTTP_ERROR,
  // Appended, not inserted: these values surface in the UI and in logs.
  DOWNLOAD_MD5UNAVAILABLE_ERROR,
  DOWNLOAD_NOCONFIGSECTOR_ERROR,
} download_err_t;

typedef enum {
  DOWNLOAD_CREATECATALOG_OK,
  DOWNLOAD_CREATECATALOG_PROCESSING,
  DOWNLOAD_CREATECATALOG_ERROR,
  DOWNLOAD_CREATECATALOG_NODIR_ERROR,
  DOWNLOAD_CREATECATALOG_JSON_ERROR,
  DOWNLOAD_CREATECATALOG_CANNOTOPENFILE_ERROR,
  DOWNLOAD_CREATECATALOG_CANNOTWRITEFILE_ERROR
} download_catalog_err_t;

typedef enum {
  DOWNLOAD_DELETEAPP_OK,
  DOWNLOAD_DELETEAPP_SDCARDNOTREADY_ERROR,
  DOWNLOAD_DELETEAPP_NOTUUID_ERROR
} download_delete_err_t;

typedef enum {
  DOWNLOAD_LAUNCHAPP_IDLE,
  DOWNLOAD_LAUNCHAPP_SCHEDULED,
  DOWNLOAD_LAUNCHAPP_INPROGRESS,
  DOWNLOAD_LAUNCHAPP_OK,
  DOWNLOAD_LAUNCHAPP_SDCARDNOTREADY_ERROR,
  DOWNLOAD_LAUNCHAPP_NOTUUID_ERROR
} download_launch_err_t;

sdcard_info_t *appmngr_get_sdcard_info();
app_info_t *appmngr_get_app_info();

download_err_t appmngr_save_app_info(const char *json_str);

int appmngr_delete_app_info();

void appmngr_set_download_update(bool update);

download_delete_err_t appmngr_delete_app(const char *uuid);
download_launch_err_t appmngr_launch_app();
download_err_t appmngr_start_download(const char *url);
download_poll_t appmngr_poll_download_app();
download_err_t appmngr_finish_download_app();
/**
 * @brief Hide or show the current transfer in the progress UI.
 *
 * Set false around internal fetches the user did not ask for (upgrade.md5),
 * so they are reported as "connecting" with no byte counts instead of
 * appearing as a finished 0 KB download.
 */
void appmngr_set_progress_reporting(bool enabled);

/**
 * @brief Bytes written so far and the total expected.
 *
 * @param received Bytes written to the destination file for the current hop.
 * @param total Content-Length of the current hop, or 0 if the server did not
 *              send one (render an indeterminate bar in that case).
 */
void appmngr_get_download_progress(uint32_t *received, uint32_t *total);

/**
 * @brief Cap the number of times a hop is retried after receiving nothing.
 *
 * Set to 0 around an optional fetch (upgrade.md5) where a 404 is a valid
 * answer and retrying only adds delay. Restore to APPMNGR_MAX_HOP_RETRIES
 * afterwards.
 */
void appmngr_set_max_hop_retries(int retries);

/**
 * @brief Read the just-downloaded temp file as an upgrade.md5 digest.
 *
 * Accepts a bare 32-character hex digest or md5sum's "<digest>  <name>" form.
 *
 * @return true if a valid digest was stored, false if the file was missing,
 *         short or malformed. False is fatal: the upgrade is refused, since
 *         the checksum is required.
 */
bool appmngr_load_expected_firmware_md5_from_tmp(void);

/** @brief Forget any stored expected firmware digest. */
void appmngr_clear_expected_firmware_md5(void);

/** @brief Whether an expected firmware digest is currently held. */
bool appmngr_has_expected_firmware_md5(void);

/**
 * @brief Hash the downloaded firmware image and compare against upgrade.md5.
 *
 * @return DOWNLOAD_OK when the digests match, DOWNLOAD_MD5MISMATCH_ERROR when
 *         they differ, DOWNLOAD_MD5UNAVAILABLE_ERROR when no digest is held.
 */
download_err_t appmngr_verify_firmware_md5(void);

/**
 * @brief Delete the firmware image left behind by a completed upgrade.
 *
 * The upgrader flashes upgrade.bin and jumps to the new firmware without
 * removing it, so roughly 1.4MB stays on the card indefinitely. Call this from
 * boot only: reaching the booster proves the firmware in flash runs, whereas
 * an interrupted flash never gets that far -- it reboots into the upgrader
 * again with BOOT_FEATURE still UPGRADER and retries from this very file.
 * Deleting it anywhere earlier would throw away that recovery copy.
 */
void appmngr_cleanup_upgrade_image(void);

/** @brief Close and delete the partial download file (web UI Cancel). */
void appmngr_cleanup_download(void);

/** @brief Current coarse download stage, for the progress page. */
appmngr_phase_t appmngr_get_download_phase();

/** @brief Publish the coarse download stage. Driven from the mngr main loop. */
void appmngr_set_download_phase(appmngr_phase_t phase);

download_status_t appmngr_get_download_status();
download_err_t appmngr_get_download_error();

download_status_t appmngr_get_download_firmware_status();
void appmngr_download_firmware_status(download_status_t status);
download_err_t appmngr_get_download_firmware_error();
void appmngr_download_firmware_error(download_err_t err);

const char *appmngr_get_download_error_str();

/**
 * @brief Human-readable text for a specific error code.
 *
 * Needed because appmngr_get_download_error_str() reads the app-download
 * error, which is still DOWNLOAD_OK when it is the FIRMWARE download that
 * failed -- reporting a firmware failure as "No error".
 */
const char *appmngr_download_error_to_str(download_err_t err);
download_launch_err_t appmngr_get_launch_status();
download_err_t appmngr_confirm_download_app();
download_err_t appmngr_confirm_failed_download_app();
void appmngr_download_status(download_status_t status);
void appmngr_download_error(download_err_t err);

download_err_t __not_in_flash_func(appmngr_confirm_download_firmware)();
download_err_t appmngr_finish_download_firmware();
download_err_t appmngr_confirm_failed_download_firmware();
void appmngr_firmwareUpgradeStart(void);
void mngr_firmwareUpgradeClean(void);
void mngr_firmwareUpgradeInstall(void);

void appmngr_set_launch_status(download_launch_err_t status);
download_catalog_err_t appmngr_create_app_catalog();
void appmngr_schedule_launch_app(const char *uuid);

/**
 * @brief Schedule "Restore and launch" for the development app (EPIC-11).
 *
 * Like appmngr_schedule_launch_app(), but when the launch runs it first
 * deletes the deploy API upload and erases the development app's config
 * sector, then flashes the binary downloaded from the catalog, even though a
 * plain launch of the development app never flashes that binary (D-10). It
 * gives a developer a clean slate after a bad deploy. The web page is the
 * only caller, and it only ever passes DEVAPI_DEV_APP_UUID.
 */
void appmngr_schedule_restore_launch_app(const char *uuid);

typedef enum {
  APPMNGR_CONFIG_ERASED = 0,    // the app's config sector was erased
  APPMNGR_CONFIG_NOT_ASSIGNED,  // no sector in the lookup table: nothing to do
  APPMNGR_CONFIG_ERASE_FAILED   // lookup table unreadable, or erase refused
} appmngr_config_erase_t;

/**
 * @brief Erase an installed app's config sector, so the microfirmware starts
 * from its defaults the next time it runs (EPIC-11).
 *
 * The lookup table entry stays: the sector remains assigned to the app. Used
 * by "Restore and launch" and "Restore config only" for the development app.
 * Parks core 1 and disables interrupts around the erase, like every other
 * config write.
 */
appmngr_config_erase_t appmngr_erase_app_config(const char *uuid);

void appmngr_print_apps_lookup_table(uint8_t *table, uint16_t length);
void appmngr_load_apps_lookup_table(uint8_t *table, uint16_t *length);
int8_t appmngr_erase_app_lookup_table();
uint16_t appmngr_get_installed_apps(appmngr_installed_app_t *apps,
                                    uint16_t max_apps);

bool appmngr_ffirst(char *json);
bool appmngr_fnext(char *json);

void appmngr_init();
void appmngr_deinit();

// Synchronize the apps lookup table with JSON files in the apps folder.
// Scans all .json files (excluding apps.json), parses them, and ensures each
// app UUID exists in the lookup table; missing entries are added.
void appmngr_sync_lookup_table();

#endif  // APPMNGR_H
