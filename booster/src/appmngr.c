#include "appmngr.h"

#include <ctype.h>
#include <stdlib.h>
#include <strings.h>

#include "devapi.h"
#include "lwip/stats.h"

#include "select.h"
#include "upgrader_firmware.h"

// Create an enum for the type of download
typedef enum { DOWNLOAD_TYPE_APP, DOWNLOAD_TYPE_FIRMWARE } download_type_t;

static download_type_t download_type = DOWNLOAD_TYPE_APP;
static sdcard_info_t sdcard_info = {false, 0, 0, false};
static app_info_t app_info = {0};
static FIL file;
static download_status_t download_status = DOWNLOAD_STATUS_IDLE;
static download_status_t download_firmware_status = DOWNLOAD_STATUS_IDLE;
static download_err_t download_error = DOWNLOAD_OK;
static download_err_t download_firmware_error = DOWNLOAD_OK;
static HTTPC_REQUEST_T request = {0};
// Persistent storage for async HTTP request strings to avoid dangling pointers
static char request_host_buf[256];
static char request_uri_buf[1536];

// --- Redirect following (EPIC-08) -------------------------------------
// A 30x is detected in the headers callback, which aborts the body so no
// redirect page is ever written to the destination file, and records the
// target. appmngr_poll_download_app() re-issues from the main loop, once the
// aborted connection is fully closed -- starting a new request from inside
// the lwIP callback would reenter the stack.
static bool redirect_pending = false;
static bool following_redirect = false;
static int redirect_hops = 0;
static char redirect_url[APPMNGR_MAX_URL_SIZE] = {0};
// Re-issuing in the same tick as the abort collides with lwIP still tearing
// the previous connection down. See appmngr_poll_download_app().
static bool redirect_settling = false;

// A hop that times out is retried before the whole download is failed. lwIP
// reports every resource satisfied and TCP has already retransmitted the SYN
// internally, so a transfer that receives nothing for 15 seconds means the
// peer did not answer -- an ordinary internet event that any download client
// is expected to ride out rather than surface as a hard failure.
static bool retry_pending = false;
static int hop_retries = 0;
// Retry budget for the hop in flight. Lowered to 0 for optional fetches such as
// upgrade.md5, where a 404 is a legitimate answer and retrying it just delays
// the upgrade on every device whose release never published the file.
static int max_hop_retries = APPMNGR_MAX_HOP_RETRIES;
// The URL of the hop currently in flight, so a retry can re-issue it.
static char current_url[APPMNGR_MAX_URL_SIZE] = {0};

// Live progress for the web UI. Written from the lwIP callbacks, read from an
// SSI handler (also lwIP context) and from the main loop, hence volatile.
// download_bytes_total is 0 when the server sent no Content-Length, which the
// UI renders as an indeterminate bar rather than a bogus percentage.
static volatile uint32_t download_bytes_received = 0;
static volatile uint32_t download_bytes_total = 0;
// Coarse stage, for the progress page. DOWNLOAD_STATUS_* cannot express this:
// it returns to IDLE on success (indistinguishable from "never started"), and
// the verify/install work runs synchronously inside appmngr_finish_download_app
// and appmngr_confirm_download_app where no status is published at all.
static volatile appmngr_phase_t download_phase = APPMNGR_PHASE_IDLE;
// Cleared around internal fetches the user should not see as a download, such
// as the 32-byte upgrade.md5 companion file, which would otherwise flash up as
// a completed 0 KB transfer before the real image starts.
static volatile bool progress_reporting = true;

static absolute_time_t redirect_retry_time;

// FatFS is not reentrant (FF_FS_REENTRANT 0) yet is reached from two
// contexts: httpd CGI/SSI handlers and the download's own f_write run inside
// the cyw43 background worker, which preempts the main loop -- including in
// the middle of a main-loop f_open or f_read. One preemption inside a FAT
// update corrupts the shared sector window. The async-context lock is the
// mutex the callbacks already run under, so main-loop filesystem sections
// take it too; it is recursive, so a path reached from a callback (which
// already holds it) is unaffected.
static inline void appmngr_fs_lock(void) {
  async_context_acquire_lock_blocking(cyw43_arch_async_context());
}
static inline void appmngr_fs_unlock(void) {
  async_context_release_lock(cyw43_arch_async_context());
}
// The components of the request currently in flight, needed to resolve a
// relative Location against the URL that produced it.
static url_components_t active_components = {0};

// Defined further down, next to get_tmp_filename_path().
static void appmngr_cleanup_tmp_download_file(void);
static download_err_t calculate_md5_of_tmp_file(MD5Context *md5_ctx);


static DIR s_dir;
static bool s_dir_opened = false;
static char s_folder[256];
static download_launch_err_t launch_status = DOWNLOAD_LAUNCHAPP_IDLE;
static char launch_app_uuid[37] = {0};
// Set by appmngr_schedule_restore_launch_app(), cleared by a plain schedule
// and when the launch consumes it. Only honoured for the development app.
static bool launch_restore = false;
static bool download_update = false;

static int appmngr_hex_nibble(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  c = (char)tolower((unsigned char)c);
  if (c >= 'a' && c <= 'f') {
    return 10 + (c - 'a');
  }
  return -1;
}

static bool appmngr_parse_md5_hex(const char *md5_str, uint8_t *out_md5) {
  for (int i = 0; i < 16; ++i) {
    int high = appmngr_hex_nibble(md5_str[i * 2]);
    int low = appmngr_hex_nibble(md5_str[i * 2 + 1]);
    if (high < 0 || low < 0) {
      return false;
    }
    out_md5[i] = (uint8_t)((high << 4) | low);
  }
  return true;
}

#if defined(_DEBUG) && (_DEBUG != 0)
static void appmngr_debug_log_md5(const char *label, const uint8_t *digest,
                                  size_t digest_len) {
  char digest_hex[(2 * 16) + 1];

  if (digest_len > 16) {
    digest_len = 16;
  }

  for (size_t i = 0; i < digest_len; ++i) {
    sprintf(&digest_hex[i * 2], "%02X", digest[i]);
  }
  digest_hex[digest_len * 2] = '\0';
  DPRINTF("%s: %s\n", label, digest_hex);
}
#else
#define appmngr_debug_log_md5(label, digest, digest_len) ((void)0)
#endif

static void appmngr_prepare_launch_runtime(void) {
  // Stop the SELECT watcher core before any flash writes.
  select_coreWaitPushDisable();

  // Tear down Wi-Fi/LWIP so background activity cannot race the launch copy.
  network_deInit();
}

void appmngr_set_download_update(bool update) { download_update = update; }

// Write the sdcard_write_file function
static int sdcard_write_file(const char *filename, const char *data,
                             size_t size) {
  FIL file;
  FRESULT res;
  UINT bytes_written;

  res = f_open(&file, filename, FA_WRITE | FA_CREATE_ALWAYS);
  if (res != FR_OK) {
    DPRINTF("Error opening file %s: %i\n", filename, res);
    return -1;
  }
  // Write the data to the file
  res = f_write(&file, data, size, &bytes_written);
  if (res != FR_OK) {
    DPRINTF("Error writing to file %s: %i\n", filename, res);
    f_close(&file);
    return -1;
  }
  // Close the file
  res = f_close(&file);
  if (res != FR_OK) {
    DPRINTF("Error closing file %s: %i\n", filename, res);
    return -1;
  }
  return 0;
}

static download_err_t set_app_info(const char *json_str) {
  cJSON *root = cJSON_Parse(json_str);
  if (root == NULL) {
    DPRINTF("Error parsing JSON\n");
    return DOWNLOAD_PARSEJSON_ERROR;  // Error parsing JSON
  }

  // Retrieve JSON fields
  cJSON *uuid = cJSON_GetObjectItem(root, "uuid");
  cJSON *name = cJSON_GetObjectItem(root, "name");
  cJSON *description = cJSON_GetObjectItem(root, "description");
  cJSON *image = cJSON_GetObjectItem(root, "image");
  cJSON *tags = cJSON_GetObjectItem(root, "tags");
  cJSON *devices = cJSON_GetObjectItem(root, "devices");
  cJSON *binary = cJSON_GetObjectItem(root, "binary");
  cJSON *md5 = cJSON_GetObjectItem(root, "md5");
  cJSON *version = cJSON_GetObjectItem(root, "version");

  // For each field, confirm it's a string before copying
  if (uuid && cJSON_IsString(uuid)) {
    snprintf(app_info.uuid, sizeof(app_info.uuid), "%s", uuid->valuestring);
  }
  if (name && cJSON_IsString(name)) {
    snprintf(app_info.name, sizeof(app_info.name), "%s", name->valuestring);
  }
  if (description && cJSON_IsString(description)) {
    snprintf(app_info.description, sizeof(app_info.description), "%s",
             description->valuestring);
  }
  if (image && cJSON_IsString(image)) {
    snprintf(app_info.image, sizeof(app_info.image), "%s", image->valuestring);
  }

  // tags is expected to be an array of strings
  if (tags && cJSON_IsArray(tags)) {
    cJSON *tag = NULL;
    int i = 0;
    cJSON_ArrayForEach(tag, tags) {
      if (cJSON_IsString(tag)) {
        snprintf(app_info.tags[i], sizeof(app_info.tags[i]), "%s",
                 tag->valuestring);
        i++;
        if (i >= MAX_TAGS) break;
      }
    }
    app_info.tags_count = i;
  } else {
    app_info.tags_count = 0;  // No valid tags array
  }

  // devices is expected to be an array of strings
  if (devices && cJSON_IsArray(devices)) {
    cJSON *device = NULL;
    int i = 0;
    cJSON_ArrayForEach(device, devices) {
      if (cJSON_IsString(device)) {
        snprintf(app_info.devices[i], sizeof(app_info.devices[i]), "%s",
                 device->valuestring);
        i++;
        if (i >= MAX_DEVICES) break;
      }
    }
    app_info.devices_count = i;
  } else {
    app_info.devices_count = 0;  // No valid devices array
  }

  if (binary && cJSON_IsString(binary)) {
    snprintf(app_info.binary, sizeof(app_info.binary), "%s",
             binary->valuestring);
  }
  if (md5 && cJSON_IsString(md5) && md5->valuestring) {
    // Convert MD5 string to 16-byte binary array
    const char *md5_str = md5->valuestring;
    size_t md5_len = strlen(md5_str);
    if (md5_len != 32) {  // MD5 string should be exactly 32 hex characters
      DPRINTF("Invalid MD5 length in JSON: %zu\n", md5_len);
      cJSON_Delete(root);
      return DOWNLOAD_PARSEMD5_ERROR;
    }
    if (!appmngr_parse_md5_hex(md5_str, app_info.md5)) {
      DPRINTF("Error parsing MD5 hex string\n");
      cJSON_Delete(root);
      return DOWNLOAD_PARSEMD5_ERROR;
    }
  } else {
    DPRINTF("MD5 field is missing or is not a valid string\n");
    cJSON_Delete(root);
    return DOWNLOAD_PARSEMD5_ERROR;
  }

  if (version && cJSON_IsString(version)) {
    snprintf(app_info.version, sizeof(app_info.version), "%s",
             version->valuestring);
  }

  // Always free the cJSON root object to avoid memory leaks
  cJSON_Delete(root);

  // Finally, copy the JSON string into the app_info struct in the json field
  snprintf(app_info.json, sizeof(app_info.json), "%s", json_str);

  DPRINTF("App info set:\n");
  DPRINTF("UUID: %s\n", app_info.uuid);
  DPRINTF("Name: %s\n", app_info.name);
  DPRINTF("Description: %s\n", app_info.description);
  DPRINTF("Image: %s\n", app_info.image);
  DPRINTF("Tags: ");
  for (int i = 0; i < app_info.tags_count; i++) {
    DPRINTF("+- %s\n", app_info.tags[i]);
  }
  DPRINTF("Devices: ");
  for (int i = 0; i < app_info.devices_count; i++) {
    DPRINTF("+- %s\n", app_info.devices[i]);
  }
  DPRINTF("Binary: %s\n", app_info.binary);
  appmngr_debug_log_md5("MD5", app_info.md5, sizeof(app_info.md5));
  DPRINTF("Version: %s\n", app_info.version);
  DPRINTF("JSON: %s\n", app_info.json);
  DPRINTF("App info set successfully\n");
  return DOWNLOAD_OK;
}

static bool appmngr_parse_installed_app(
    const char *json_str, const char *expected_uuid,
    appmngr_installed_app_t *installed_app) {
  cJSON *root = cJSON_Parse(json_str);
  if (root == NULL) {
    return false;
  }

  cJSON *uuid = cJSON_GetObjectItem(root, "uuid");
  cJSON *name = cJSON_GetObjectItem(root, "name");
  cJSON *version = cJSON_GetObjectItem(root, "version");

  bool valid = false;
  if (uuid && cJSON_IsString(uuid) && uuid->valuestring && name &&
      cJSON_IsString(name) && name->valuestring &&
      strcasecmp(uuid->valuestring, expected_uuid) == 0 &&
      uuid_is_valid4(expected_uuid)) {
    snprintf(installed_app->uuid, sizeof(installed_app->uuid), "%s",
             expected_uuid);
    snprintf(installed_app->name, sizeof(installed_app->name), "%s",
             name->valuestring);
    if (version && cJSON_IsString(version) && version->valuestring) {
      snprintf(installed_app->version, sizeof(installed_app->version), "%s",
               version->valuestring);
    } else {
      snprintf(installed_app->version, sizeof(installed_app->version), "%s",
               "unknown");
    }
    valid = true;
  }

  cJSON_Delete(root);
  return valid;
}

static int appmngr_compare_installed_apps(const void *lhs, const void *rhs) {
  const appmngr_installed_app_t *left = lhs;
  const appmngr_installed_app_t *right = rhs;

  int cmp = strcasecmp(left->name, right->name);
  if (cmp != 0) {
    return cmp;
  }

  return strcmp(left->uuid, right->uuid);
}

static void appmngr_sort_installed_apps(appmngr_installed_app_t *apps,
                                        uint16_t count) {
  for (uint16_t i = 1; i < count; ++i) {
    appmngr_installed_app_t current = apps[i];
    uint16_t j = i;

    while (j > 0 &&
           appmngr_compare_installed_apps(&apps[j - 1], &current) > 0) {
      apps[j] = apps[j - 1];
      --j;
    }

    apps[j] = current;
  }
}

download_err_t appmngr_save_app_info(const char *json_str) {
  // Save the app info to the SD card as a JSON file

  int err = set_app_info(json_str);
  if (err == DOWNLOAD_PARSEJSON_ERROR) {
    DPRINTF("Error setting app info: %i\n", err);
    return DOWNLOAD_PARSEJSON_ERROR;
  }
  // We have the app info in the app_info struct, now save it to a temp file
  char filename[256] = {0};
  snprintf(filename, sizeof(filename), "%s/tmp.json",
           settings_find_entry(gconfig_getContext(), PARAM_APPS_FOLDER)->value);

  // Let's try to delete first the temp file if it exists
  FRESULT ferr = f_unlink(filename);
  if (ferr == FR_NO_FILE) {
    // If the file doesn't exist, we ignore the error
    DPRINTF("Error deleting file. %s does not exist. Continuing...\n",
            filename);
  }

  // Now write the app info to the temp file
  DPRINTF("Saving app info to file: %s\n", filename);
  DPRINTF("JSON: %s\n", app_info.json);
  ferr = sdcard_write_file(filename, app_info.json, strlen(app_info.json));
  if (ferr != FR_OK) {
    DPRINTF("Error saving app info to file: %i\n", ferr);
  } else {
    DPRINTF("App info saved to file: %s\n", filename);
  }
  return DOWNLOAD_OK;
}

app_info_t *appmngr_get_app_info() { return &app_info; }

sdcard_info_t *appmngr_get_sdcard_info() { return &sdcard_info; }

int appmngr_delete_app_info() {
  // Delete the app info file from the SD card
  // The filename must be the UUID of the app with the .json extension
  char filename[256] = {0};
  snprintf(filename, sizeof(filename), "%s/%s.json",
           settings_find_entry(gconfig_getContext(), PARAM_APPS_FOLDER)->value,
           app_info.uuid);
  DPRINTF("Deleting app info file: %s\n", filename);
  int err = f_unlink(filename);
  if (err != FR_OK) {
    DPRINTF("Error deleting app info file: %i\n", err);
  } else {
    DPRINTF("App info file deleted: %s\n", filename);
  }
  return err;
}

// Save body to file
static err_t http_client_receive_file_fn(__unused void *arg,
                                         __unused struct altcp_pcb *conn,
                                         struct pbuf *p, err_t err) {
  if (p == NULL) {
    DPRINTF("End of data or connection closed by the server.\n");
    if (redirect_pending) {
      // The redirect response ended. Leave the status alone; the result
      // callback holds it IN_PROGRESS and the poll re-issues.
      return ERR_OK;
    }
    if (download_type == DOWNLOAD_TYPE_FIRMWARE) {
      download_firmware_status = DOWNLOAD_STATUS_COMPLETED;
    } else {
      download_status = DOWNLOAD_STATUS_COMPLETED;
    }
    return ERR_OK;
  }

  if (err != ERR_OK) {
    DPRINTF("Error receiving file: %i\n", err);
    if (download_type == DOWNLOAD_TYPE_FIRMWARE)
      download_firmware_status = DOWNLOAD_STATUS_FAILED;
    else
      download_status = DOWNLOAD_STATUS_FAILED;
    pbuf_free(p);
    return err;
  }

  // A redirect response's body is not the file. Discard it: the request will
  // be re-issued against the Location target once this connection closes.
  if (redirect_pending) {
    pbuf_free(p);
    return ERR_OK;
  }

  // Stream the pbuf chain to file: no malloc, no large copies
  FRESULT fres = FR_OK;
  for (struct pbuf *q = p; q != NULL; q = q->next) {
    UINT bw = 0;
    download_bytes_received += q->len;
    fres = f_write(&file, q->payload, q->len, &bw);
    if (fres != FR_OK || bw != q->len) {
      DPRINTF("Error writing to file: %i (wrote %u of %u)\n", fres,
              (unsigned)bw, q->len);
      if (download_type == DOWNLOAD_TYPE_FIRMWARE)
        download_firmware_status = DOWNLOAD_STATUS_FAILED;
      else
        download_status = DOWNLOAD_STATUS_FAILED;
      pbuf_free(p);
      return ERR_ABRT;
    }
  }

  // ALTCP is always enabled; altcp_recved is correct for plain http too
  // (an http connection is just an ALTCP layer with no TLS attached).
  altcp_recved(conn, p->tot_len);
  pbuf_free(p);

  if (download_type == DOWNLOAD_TYPE_FIRMWARE)
    download_firmware_status = DOWNLOAD_STATUS_IN_PROGRESS;
  else
    download_status = DOWNLOAD_STATUS_IN_PROGRESS;
  return ERR_OK;
}

/**
 * @brief Find a header's value by name, case-insensitively.
 *
 * Ported from md-browser's download.c. Walks CRLF-delimited lines rather than
 * using strstr on the whole block, so a header name appearing inside another
 * header's value cannot match.
 */
static bool appmngr_find_header_value(const char *headers, const char *name,
                                      char *out, size_t out_len) {
  size_t name_len = strlen(name);
  const char *line = headers;
  while (line != NULL && *line != '\0') {
    if (strncasecmp(line, name, name_len) == 0) {
      const char *value = line + name_len;
      while (*value == ' ' || *value == '\t') {
        value++;
      }
      size_t len = strcspn(value, "\r\n");
      if (len == 0 || len >= out_len) {
        return false;  // absent, or too long to use safely
      }
      memcpy(out, value, len);
      out[len] = '\0';
      return true;
    }
    line = strstr(line, "\r\n");
    if (line != NULL) {
      line += 2;
    }
  }
  return false;
}

/**
 * @brief Turn a Location value into an absolute URL.
 *
 * Location may be absolute, host-relative ("/path"), or path-relative
 * ("file.uf2"). Resolved against the request that produced it, which is why
 * active_components is kept.
 */
static void appmngr_resolve_redirect_url(const char *location, char *out,
                                         size_t out_len) {
  char host_port[160];
  if (active_components.port != 0) {
    snprintf(host_port, sizeof(host_port), "%s:%u", active_components.host,
             active_components.port);
  } else {
    snprintf(host_port, sizeof(host_port), "%s", active_components.host);
  }

  if (strstr(location, "://") != NULL) {
    snprintf(out, out_len, "%s", location);
  } else if (location[0] == '/') {
    snprintf(out, out_len, "%s://%s%s", active_components.protocol, host_port,
             location);
  } else {
    const char *last_slash = strrchr(active_components.uri, '/');
    int base_len =
        last_slash ? (int)(last_slash - active_components.uri) + 1 : 0;
    snprintf(out, out_len, "%s://%s%.*s%s", active_components.protocol,
             host_port, base_len, active_components.uri, location);
  }
}

// Function to parse headers and check Content-Length
static err_t http_client_header_check_size_fn(
    __unused httpc_state_t *connection, __unused void *arg, struct pbuf *hdr,
    u16_t hdr_len, __unused u32_t content_len) {
  // Both branches download a .uf2: the firmware path fetches the full-image
  // upgrade.bin, the app path fetches a single microfirmware. (Before v2.3.0
  // this ternary read APP_UF2_SIZE / APP_INFO_SIZE, which capped the firmware
  // at a microfirmware's size and every app at 4KB of JSON. It never ran.)
  size_t max_allowed = (download_type == DOWNLOAD_TYPE_FIRMWARE)
                           ? MAXIMUM_FIRMWARE_UF2_SIZE
                           : MAXIMUM_APP_UF2_SIZE;

  // Static, and large enough for a real redirect chain. A GitHub release
  // asset's Location header alone runs past 900 characters, so the old 512
  // stack buffer would have truncated the header block before the Location
  // could be read -- and 2KB on an lwIP callback stack is not something to
  // take.
  static char buf[2048];
  u16_t copy = hdr_len < sizeof(buf) - 1 ? hdr_len : (sizeof(buf) - 1);
  pbuf_copy_partial(hdr, buf, copy, 0);
  buf[copy] = '\0';

  // If the block was truncated, cut back to the last complete CRLF. Without
  // this a header split by the cut would be parsed as if the remainder were
  // its whole value -- a truncated Location would then be followed as a real
  // URL. Dropping the partial line makes the failure clean instead: the
  // header is simply not found. Real chains put Location early (byte 94 of
  // 5189 on the GitHub release path), so this only matters for a pathological
  // server.
  if (hdr_len > copy) {
    char *last_crlf = strrchr(buf, '\n');
    if (last_crlf != NULL) {
      last_crlf[1] = '\0';
    }
  }

  // Parse the status line ("HTTP/1.x NNN ..."); lwIP hands us the headers
  // pbuf from the first byte of the response.
  int http_status = 0;
  if (strncmp(buf, "HTTP/", 5) == 0) {
    const char *status_start = strchr(buf, ' ');
    if (status_start != NULL) {
      http_status = atoi(status_start + 1);
    }
  }

  // Redirects: record the target and abort the body. The re-issue happens in
  // appmngr_poll_download_app() from the main loop (EPIC-08).
  if (http_status == 301 || http_status == 302 || http_status == 303 ||
      http_status == 307 || http_status == 308) {
    if (redirect_hops >= APPMNGR_MAX_REDIRECT_HOPS) {
      DPRINTF("Redirect limit (%d) exceeded\n", APPMNGR_MAX_REDIRECT_HOPS);
    } else {
      char location[APPMNGR_MAX_URL_SIZE];
      if (appmngr_find_header_value(buf, "Location:", location,
                                    sizeof(location))) {
        appmngr_resolve_redirect_url(location, redirect_url,
                                     sizeof(redirect_url));
        DPRINTF("HTTP %d redirect to: %s\n", http_status, redirect_url);
        redirect_pending = true;
        // Deliberately NOT ERR_ABRT. Aborting made lwIP tear the connection
        // down hard, and after two of those the stack stopped serving
        // anything at all: the third request never completed a handshake and
        // the device's own web server stopped responding too. A 302 body is a
        // few bytes or nothing, so letting the response finish and the
        // connection close normally costs nothing and keeps lwIP's state
        // machine on the path it is designed for. The body is discarded in
        // http_client_receive_file_fn.
        return ERR_OK;
      }
      DPRINTF("HTTP %d without a usable Location header\n", http_status);
    }
    // fall through to the non-2xx abort below
  }

  // Only 2xx carries the content we asked for. Anything else must be aborted
  // before the body arrives, or an error page gets written to the file and
  // treated as firmware. The microfirmware path would catch that with its
  // MD5, but the OTA path has no hash at all.
  if (http_status < 200 || http_status >= 300) {
    DPRINTF("HTTP status %d: aborting before the body\n", http_status);
    if (download_type == DOWNLOAD_TYPE_FIRMWARE)
      download_firmware_status = DOWNLOAD_STATUS_FAILED;
    else
      download_status = DOWNLOAD_STATUS_FAILED;
    return ERR_ABRT;
  }

  const char *label = "Content-Length:";
  char *p = buf;
  while (p && *p) {
    char *eol = strstr(p, "\r\n");
    size_t line_len = eol ? (size_t)(eol - p) : strlen(p);
    if (line_len >= strlen(label) &&
        strncasecmp(p, label, strlen(label)) == 0) {
      const char *num = p + strlen(label);
      while (*num == ' ' || *num == '\t') ++num;
      unsigned long cl = strtoul(num, NULL, 10);
      if (cl > max_allowed) {
        DPRINTF("Content-Length too large: %lu > %u\n", cl,
                (unsigned)max_allowed);
        if (download_type == DOWNLOAD_TYPE_FIRMWARE)
          download_firmware_status = DOWNLOAD_STATUS_FAILED;
        else
          download_status = DOWNLOAD_STATUS_FAILED;
        return ERR_VAL;
      }
      // Only a 2xx reaches here, so this is the real payload size.
      download_bytes_total = (uint32_t)cl;
      break;
    }
    p = eol ? (eol + 2) : NULL;
  }

  // A 2xx with headers parsed: the payload transfer starts now.
  download_phase = APPMNGR_PHASE_DOWNLOAD;
  if (download_type == DOWNLOAD_TYPE_FIRMWARE)
    download_firmware_status = DOWNLOAD_STATUS_IN_PROGRESS;
  else
    download_status = DOWNLOAD_STATUS_IN_PROGRESS;
  return ERR_OK;
}

static void http_client_result_complete_fn(void *arg,
                                           httpc_result_t httpc_result,
                                           u32_t rx_content_len, u32_t srv_res,
                                           err_t err) {
  HTTPC_REQUEST_T *req = (HTTPC_REQUEST_T *)arg;
  DPRINTF("Requet complete: result %d len %u server_response %u err %d\n",
          httpc_result, rx_content_len, srv_res, err);
  req->complete = true;

  // A redirect reaches here as a failure: we aborted the body deliberately, so
  // err is ERR_ABRT and srv_res is the 30x. Neither is an error, and marking
  // the download FAILED here would be fatal in a way that is easy to miss --
  // mngr.c only calls appmngr_poll_download_app() while the status is
  // IN_PROGRESS, so the re-issue in that function would never run at all.
  // Hold the status at IN_PROGRESS and let the poll follow the redirect.
  if (redirect_pending) {
    DPRINTF("Redirect pending; holding the download open for the re-issue\n");
    // Drop whatever the aborted transfer left behind. appmngr_start_download()
    // truncates this file on its way through anyway, so this is belt and
    // braces -- but relying on that ordering is how a partial body survives a
    // future refactor. Safe here: the result callback runs in the same
    // cooperative context as the main loop, so FatFs access is allowed.
    appmngr_cleanup_tmp_download_file();
    if (download_type == DOWNLOAD_TYPE_FIRMWARE) {
      download_firmware_status = DOWNLOAD_STATUS_IN_PROGRESS;
      download_firmware_error = DOWNLOAD_OK;
    } else {
      download_status = DOWNLOAD_STATUS_IN_PROGRESS;
      download_error = DOWNLOAD_OK;
    }
    return;
  }

  if (err == ERR_OK) {
    if (download_type == DOWNLOAD_TYPE_FIRMWARE) {
      download_firmware_status = DOWNLOAD_STATUS_COMPLETED;
      download_firmware_error = DOWNLOAD_OK;
    } else {
      download_status = DOWNLOAD_STATUS_COMPLETED;
      download_error = DOWNLOAD_OK;
    }
  } else {
    if (download_type == DOWNLOAD_TYPE_FIRMWARE) {
      download_firmware_status = DOWNLOAD_STATUS_FAILED;
      download_firmware_error = DOWNLOAD_HTTP_ERROR;
    } else {
      download_status = DOWNLOAD_STATUS_FAILED;
      download_error = DOWNLOAD_HTTP_ERROR;
    }
  }
  // httpc_result is the authoritative outcome and must be checked FIRST. A
  // timeout arrives as HTTPC_RESULT_ERR_TIMEOUT with err == ERR_OK and
  // srv_res == 0, so judging on err and srv_res alone marks a transfer that
  // received nothing as COMPLETED -- which is exactly how a zero-byte file
  // reached the MD5 check and failed there instead of at the download.
  if (httpc_result != HTTPC_RESULT_OK) {
    DPRINTF("Transfer did not complete: httpc_result %d\n", httpc_result);
    if (hop_retries < max_hop_retries) {
      // Hold the download open; appmngr_poll_download_app() re-issues it.
      retry_pending = true;
      if (download_type == DOWNLOAD_TYPE_FIRMWARE) {
        download_firmware_status = DOWNLOAD_STATUS_IN_PROGRESS;
      } else {
        download_status = DOWNLOAD_STATUS_IN_PROGRESS;
      }
      return;
    }
#if defined(_DEBUG) && (_DEBUG != 0) && LWIP_STATS && LWIP_STATS_DISPLAY
    // lwIP's own accounting, rather than another guess. The interesting rows
    // are the "err" and "avail" columns: MEMP_TCP_PCB exhaustion, PBUF_POOL
    // starvation, MEM errors or TCP drops each point at a different cause for
    // a connection that sends SYN and never receives anything.
    DPRINTF("---- lwIP stats after a failed transfer ----\n");
    stats_display();
#endif
    if (download_type == DOWNLOAD_TYPE_FIRMWARE) {
      download_firmware_status = DOWNLOAD_STATUS_FAILED;
      download_firmware_error = DOWNLOAD_HTTP_ERROR;
    } else {
      download_status = DOWNLOAD_STATUS_FAILED;
      download_error = DOWNLOAD_HTTP_ERROR;
    }
    appmngr_cleanup_tmp_download_file();
    return;
  }

  // Belt and braces, in case the headers callback never ran or missed the
  // status line. Any 2xx is success: this used to demand exactly 200, which
  // would have failed a legitimate 204 or 206. srv_res == 0 means the server
  // response was never seen, so there is nothing to judge and err above
  // already decided it.
  bool status_ok = (srv_res >= 200 && srv_res < 300);
  if (!status_ok && srv_res != 0) {
    DPRINTF("Server response %u is not 2xx\n", (unsigned)srv_res);
    if (download_type == DOWNLOAD_TYPE_FIRMWARE) {
      download_firmware_status = DOWNLOAD_STATUS_FAILED;
      download_firmware_error = DOWNLOAD_HTTP_ERROR;
    } else {
      download_status = DOWNLOAD_STATUS_FAILED;
      download_error = DOWNLOAD_HTTP_ERROR;
    }
  }
}

static void get_tmp_filename_path(char filename[256]) {
  snprintf(filename, 256, "%s/tmp.download",
           settings_find_entry(gconfig_getContext(), PARAM_APPS_FOLDER)->value);
}

/**
 * @brief Close and delete the in-progress download file.
 *
 * Used when a transfer is abandoned -- a redirect being followed, or a
 * failure -- so a partial body is never left where a later step could read it
 * as a complete .uf2.
 */
static void appmngr_cleanup_tmp_download_file(void) {
  char filename[256] = {0};
  get_tmp_filename_path(filename);
  appmngr_fs_lock();
  f_close(&file);
  f_chmod(filename, 0, AM_RDO);
  f_unlink(filename);
  appmngr_fs_unlock();
}

static bool find_next_json_file(char *json, size_t max_len) {
  // We'll read directory entries until we find a valid .json file
  // or until we exhaust all entries
  FRESULT res;
  FILINFO fno;
  while (true) {
    res = f_readdir(&s_dir, &fno);
    if (res != FR_OK || fno.fname[0] == '\0') {
      // Error or no more files in this directory
      return false;
    }

    // Skip "apps.json"
    if (strcmp(fno.fname, "apps.json") == 0) {
      continue;
    }

    // Check extension
    char *ext = strrchr(fno.fname, '.');
    if (!ext || strcasecmp(ext, ".json") != 0) {
      continue;  // Not a .json file
    }

    // Check if the base name (excluding extension) is 36 characters
    char file_base[64];
    strncpy(file_base, fno.fname, sizeof(file_base));
    file_base[sizeof(file_base) - 1] = '\0';  // Ensure null-termination

    char *dot = strrchr(file_base, '.');
    if (dot) {
      *dot = '\0';  // Remove the .json extension
    }

    // If length is not 36, skip (not a valid UUID)
    if (strlen(file_base) != 36) {
      continue;
    }

    // We have a .json file, let's open and read it
    FIL fil;
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/%s", s_folder, fno.fname);
    DPRINTF("Found JSON file with valid UUID name: %s\n", filepath);

    res = f_open(&fil, filepath, FA_READ);
    if (res != FR_OK) {
      DPRINTF("Error opening file %s: %d\n", filepath, res);
      continue;  // Skip and read the next
    }

    // Read file contents into 'json' buffer
    // Assume json has space for at least max_len bytes
    UINT bytes_read = 0;
    res = f_read(&fil, json, max_len - 1, &bytes_read);
    f_close(&fil);

    if (res != FR_OK) {
      DPRINTF("Error reading file %s: %d\n", filepath, res);
      continue;  // skip this file
    }

    // Null-terminate
    json[bytes_read] = '\0';

    DPRINTF("JSON file contents: %s\n", json);

    // Found a valid file and read it successfully
    return true;
  }
}

// Go to the first UUID.json file in the apps folder
// Return false if no file is found.
// Return true if a file is found and the json parameter is filled with the
// contents of the file.
bool appmngr_ffirst(char *json) {
  // If we already have a directory open, close it first
  if (s_dir_opened) {
    f_closedir(&s_dir);
    s_dir_opened = false;
  }

  // Get the apps folder path
  snprintf(s_folder, sizeof(s_folder), "%s",
           settings_find_entry(gconfig_getContext(), PARAM_APPS_FOLDER)->value);

  // Open the directory
  FRESULT res = f_opendir(&s_dir, s_folder);
  if (res != FR_OK) {
    DPRINTF("appmngr_ffirst: Error opening folder: %s (res=%d)\n", s_folder,
            res);
    return false;
  }
  s_dir_opened = true;

  // Find the first JSON file
  if (!find_next_json_file(json, MAXIMUM_APP_INFO_SIZE)) {
    // No valid .json found or error reading
    f_closedir(&s_dir);
    s_dir_opened = false;
    return false;
  }

  return true;
}

// Go to the next UUID.json file in the apps folder
// Return false if no more files are found.
// Return true if a file is found and the json parameter is filled with the
// contents of the file.
bool appmngr_fnext(char *json) {
  // Must have done ffirst first
  if (!s_dir_opened) {
    DPRINTF("appmngr_fnext called but directory not opened.\n");
    return false;
  }

  // Find the next JSON file
  if (!find_next_json_file(json, MAXIMUM_APP_INFO_SIZE)) {
    // No more .json files
    f_closedir(&s_dir);
    s_dir_opened = false;
    return false;
  }

  return true;
}

uint16_t appmngr_get_installed_apps(appmngr_installed_app_t *apps,
                                    uint16_t max_apps) {
  if (apps == NULL || max_apps == 0) {
    return 0;
  }
  if (!sdcard_info.ready) {
    return 0;
  }

  char folder[256] = {0};
  snprintf(folder, sizeof(folder), "%s",
           settings_find_entry(gconfig_getContext(), PARAM_APPS_FOLDER)->value);

  DIR dir;
  FILINFO fno;
  FRESULT res = f_opendir(&dir, folder);
  if (res != FR_OK) {
    DPRINTF("Error opening apps folder %s: %d\n", folder, res);
    return 0;
  }

  uint16_t count = 0;
  static char json_buf[MAXIMUM_APP_INFO_SIZE];
  while (count < max_apps) {
    res = f_readdir(&dir, &fno);
    if (res != FR_OK || fno.fname[0] == '\0') {
      break;
    }
    if (fno.fattrib & AM_DIR) {
      continue;
    }
    if (strcmp(fno.fname, "apps.json") == 0) {
      continue;
    }

    char *ext = strrchr(fno.fname, '.');
    if (ext == NULL || strcasecmp(ext, ".json") != 0) {
      continue;
    }

    char uuid[37] = {0};
    size_t base_len = (size_t)(ext - fno.fname);
    if (base_len != 36) {
      continue;
    }
    snprintf(uuid, sizeof(uuid), "%.*s", (int)base_len, fno.fname);
    if (!uuid_is_valid4(uuid)) {
      continue;
    }

    char filepath[256] = {0};
    snprintf(filepath, sizeof(filepath), "%s/%s", folder, fno.fname);

    FIL fil;
    UINT bytes_read = 0;
    res = f_open(&fil, filepath, FA_READ);
    if (res != FR_OK) {
      DPRINTF("Error opening installed app json %s: %d\n", filepath, res);
      continue;
    }

    res = f_read(&fil, json_buf, sizeof(json_buf) - 1, &bytes_read);
    f_close(&fil);
    if (res != FR_OK) {
      DPRINTF("Error reading installed app json %s: %d\n", filepath, res);
      continue;
    }
    json_buf[bytes_read] = '\0';

    if (appmngr_parse_installed_app(json_buf, uuid, &apps[count])) {
      count++;
    }
  }

  f_closedir(&dir);

  if (count > 1) {
    appmngr_sort_installed_apps(apps, count);
  }

  return count;
}

void appmngr_set_progress_reporting(bool enabled) {
  progress_reporting = enabled;
}

void appmngr_get_download_progress(uint32_t *received, uint32_t *total) {
  uint32_t rx = progress_reporting ? download_bytes_received : 0;
  uint32_t tot = progress_reporting ? download_bytes_total : 0;
  if (received != NULL) {
    *received = rx;
  }
  if (total != NULL) {
    *total = tot;
  }
}

// Public wrapper: the web UI's Cancel action drops a partial download.
void appmngr_cleanup_download(void) { appmngr_cleanup_tmp_download_file(); }

// Expected MD5 of upgrade.bin, taken from the optional upgrade.md5 companion
// file. Absent means "not published", which is not an error: the upgrade then
// proceeds unverified, as it always did.
static uint8_t expected_fw_md5[16] = {0};
static bool have_expected_fw_md5 = false;

// The download engine leaves the destination file open; nothing is flushed to
// the card until it is closed. Anything that reads the file back (digest parse,
// MD5) must close it first, and may be called before or after the regular
// finish path, so this tolerates an already-closed handle.
static FRESULT appmngr_close_download_file(void) {
  appmngr_fs_lock();
  FRESULT res = f_close(&file);
  appmngr_fs_unlock();
  if (res == FR_INVALID_OBJECT) {
    // Already closed by an earlier step; not an error.
    res = FR_OK;
  }
  return res;
}

void appmngr_set_max_hop_retries(int retries) {
  max_hop_retries = retries < 0 ? 0 : retries;
}

void appmngr_clear_expected_firmware_md5(void) {
  have_expected_fw_md5 = false;
  memset(expected_fw_md5, 0, sizeof(expected_fw_md5));
}

bool appmngr_has_expected_firmware_md5(void) { return have_expected_fw_md5; }

bool appmngr_load_expected_firmware_md5_from_tmp(void) {
  appmngr_clear_expected_firmware_md5();

  // Flush and release the handle the download engine left open, or the read
  // below sees a short/empty file and the digest is silently ignored.
  if (appmngr_close_download_file() != FR_OK) {
    DPRINTF("upgrade.md5: could not close the download file\n");
    return false;
  }

  char filename[256] = {0};
  get_tmp_filename_path(filename);

  // md5sum output is "<32 hex><spaces><name>", but a bare digest is just as
  // common; only the first 32 characters matter either way.
  char head[33] = {0};
  UINT bytes_read = 0;
  FIL md5_file;

  appmngr_fs_lock();
  FRESULT res = f_open(&md5_file, filename, FA_READ);
  if (res == FR_OK) {
    res = f_read(&md5_file, head, sizeof(head) - 1, &bytes_read);
    f_close(&md5_file);
  }
  appmngr_fs_unlock();

  if (res != FR_OK || bytes_read < 32) {
    DPRINTF("upgrade.md5: unusable (fres %d, %u bytes read)\n", (int)res,
            (unsigned)bytes_read);
    return false;
  }
  head[32] = '\0';

  if (!appmngr_parse_md5_hex(head, expected_fw_md5)) {
    DPRINTF("upgrade.md5: not a valid hex digest\n");
    return false;
  }

  have_expected_fw_md5 = true;
  appmngr_debug_log_md5("Expected firmware MD5", expected_fw_md5,
                        sizeof(expected_fw_md5));
  return true;
}

download_err_t appmngr_verify_firmware_md5(void) {
  if (!have_expected_fw_md5) {
    // The checksum is required: refusing is the whole point of the check.
    DPRINTF("No firmware checksum held; refusing to flash\n");
    return DOWNLOAD_MD5UNAVAILABLE_ERROR;
  }

  // Same reason as above: hash the file on the card, not a half-written one.
  if (appmngr_close_download_file() != FR_OK) {
    DPRINTF("Could not close the firmware image before hashing\n");
    return DOWNLOAD_CANNOTCLOSEFILE_ERROR;
  }

  MD5Context md5_ctx;
  download_err_t err = calculate_md5_of_tmp_file(&md5_ctx);
  if (err != DOWNLOAD_OK) {
    DPRINTF("Error calculating MD5 of the firmware image: %d\n", err);
    return err;
  }

  appmngr_debug_log_md5("Expected firmware MD5", expected_fw_md5,
                        sizeof(expected_fw_md5));
  appmngr_debug_log_md5("Downloaded firmware MD5", md5_ctx.digest,
                        sizeof(expected_fw_md5));

  // Integrity, not authenticity: the digest travels from the same host over
  // the same unauthenticated TLS connection as the image (decision D-01), so
  // it catches truncation and corruption, not substitution.
  if (memcmp(expected_fw_md5, md5_ctx.digest, sizeof(expected_fw_md5)) != 0) {
    DPRINTF("Firmware MD5 mismatch: refusing to flash\n");
    return DOWNLOAD_MD5MISMATCH_ERROR;
  }
  DPRINTF("Firmware MD5 match\n");
  return DOWNLOAD_OK;
}

appmngr_phase_t appmngr_get_download_phase() {
  // An internal fetch still reads as "connecting" to the user: the transfer
  // they asked for has not started yet.
  if (!progress_reporting && download_phase == APPMNGR_PHASE_DOWNLOAD) {
    return APPMNGR_PHASE_CONNECT;
  }
  return download_phase;
}

void appmngr_set_download_phase(appmngr_phase_t phase) {
  // CONNECT marks the start of a new transfer, and is set by every entry point
  // (a fresh install, a retry, the firmware flow, and the upgrade.md5 to
  // upgrade.bin handover). Zero the counters here rather than in
  // appmngr_start_download(), which only runs after a 3 second arming delay:
  // the progress page is already polling during that gap and would otherwise
  // report the previous download's size.
  if (phase == APPMNGR_PHASE_CONNECT) {
    download_bytes_received = 0;
    download_bytes_total = 0;
  }
  download_phase = phase;
}

download_status_t appmngr_get_download_status() { return download_status; }

void appmngr_download_status(download_status_t status) {
  download_status = status;
}

download_err_t appmngr_get_download_error() { return download_error; }

void appmngr_download_error(download_err_t err) { download_error = err; }

download_status_t appmngr_get_download_firmware_status() {
  return download_firmware_status;
}

void appmngr_download_firmware_status(download_status_t status) {
  download_firmware_status = status;
}

download_err_t appmngr_get_download_firmware_error() {
  return download_firmware_error;
}

void appmngr_download_firmware_error(download_err_t err) {
  download_firmware_error = err;
}

const char *appmngr_get_download_error_str() {
  return appmngr_download_error_to_str(download_error);
}

const char *appmngr_download_error_to_str(download_err_t err) {
  switch (err) {
    case DOWNLOAD_OK:
      return "No error";
    case DOWNLOAD_BASE64_ERROR:
      return "Error decoding base64";
    case DOWNLOAD_PARSEJSON_ERROR:
      return "Error parsing JSON";
    case DOWNLOAD_PARSEMD5_ERROR:
      return "Error parsing MD5";
    case DOWNLOAD_CANNOTOPENFILE_ERROR:
      return "Cannot open file";
    case DOWNLOAD_CANNOTWRITEFILE_ERROR:
      return "Cannot write file";
    case DOWNLOAD_CANNOTCLOSEFILE_ERROR:
      return "Cannot close file";
    case DOWNLOAD_FORCEDABORT_ERROR:
      return "Download aborted";
    case DOWNLOAD_CANNOTSTARTDOWNLOAD_ERROR:
      return "Cannot start download";
    case DOWNLOAD_CANNOTREADFILE_ERROR:
      return "Cannot read file";
    case DOWNLOAD_CANNOTPARSEURL_ERROR:
      return "Cannot parse URL";
    case DOWNLOAD_MD5MISMATCH_ERROR:
      return "Checksum mismatch: the download is corrupt or incomplete";
    case DOWNLOAD_CANNOTRENAMEFILE_ERROR:
      return "Cannot rename file";
    case DOWNLOAD_CANNOTCREATE_CONFIG:
      return "Cannot create configuration";
    case DOWNLOAD_CANNOTDELETECONFIGSECTOR_ERROR:
      return "Cannot delete configuration sector";
    case DOWNLOAD_HTTP_ERROR:
      return "HTTP error";
    case DOWNLOAD_MD5UNAVAILABLE_ERROR:
      return "Firmware checksum (upgrade.md5) missing or unreadable";
    default:
      return "Unknown error";
  }
}

// How many 4K config sectors the linker script reserves: one per app.
static uint16_t appmngr_config_sector_count(void) {
  return (uint16_t)(((uint32_t)&_global_lookup_flash_start -
                     (uint32_t)&_config_flash_start) /
                    FLASH_SECTOR_SIZE);
}

static int8_t appmngr_delete_config_sector(uint8_t sector) {
  uint32_t flash_start = (uint32_t)&_config_flash_start;
  uint32_t config_flash_length =
      (uint32_t)&_global_lookup_flash_start - (uint32_t)&_config_flash_start;
  uint32_t num_sectors = config_flash_length / FLASH_SECTOR_SIZE;

  if (sector >= num_sectors) {
    DPRINTF("Invalid sector %u (max=%u)\n", sector,
            num_sectors ? (unsigned)(num_sectors - 1) : 0);
    return -1;
  }

  uint32_t offs =
      (flash_start - XIP_BASE) + (uint32_t)sector * FLASH_SECTOR_SIZE;
  DPRINTF("Erase config sector %u at offset 0x%08X\n", sector, offs);

  select_flashLockoutBegin();
  uint32_t ints = save_and_disable_interrupts();
  flash_range_erase(offs, FLASH_SECTOR_SIZE);
  restore_interrupts(ints);
  select_flashLockoutEnd();
  return 0;
}

/**
 * Print the apps lookup table to stdout.
 *
 * @param table  Pointer to the lookup table data.
 * @param length Total number of bytes in the lookup table.
 */
void appmngr_print_apps_lookup_table(uint8_t *table, uint16_t length) {
  if (length % LOOKUP_ENTRY_SIZE != 0) {
    DPRINTF("Warning: table length (%u) is not a multiple of %d bytes.\n",
            length, LOOKUP_ENTRY_SIZE);
  }

  uint16_t num_entries = length / LOOKUP_ENTRY_SIZE;
  DPRINTF("Printing Apps Lookup Table (%u entries):\n", num_entries);

  for (uint16_t i = 0; i < num_entries; i++) {
    uint8_t *entry = table + i * LOOKUP_ENTRY_SIZE;

    // Copy the first 36 bytes into a temporary buffer and null-terminate it.
    char uuid[37];  // 36 chars + 1 for the null terminator.
    memcpy(uuid, entry, 36);
    uuid[36] = '\0';

    // The next 2 bytes represent the sector as a 16-bit integer.
    // Assuming little-endian order.
    uint16_t sector = entry[36] | (entry[37] << 8);

    // Print the entry.
    DPRINTF("Entry %u: UUID = %s, Sector = %u\n", i, uuid, sector);
  }
}

void appmngr_load_apps_lookup_table(uint8_t *table, uint16_t *length) {
  // Calculate flash region start and total lookup region length
  uint32_t flash_start = (uint32_t)&_global_lookup_flash_start;
  uint32_t lookup_flash_length = (uint32_t)&_global_config_flash_start -
                                 (uint32_t)&_global_lookup_flash_start;

  // Determine the maximum number of entries stored in flash
  uint32_t num_entries = lookup_flash_length / LOOKUP_ENTRY_SIZE;
  DPRINTF("Lookup Flash start: 0x%X, length: %u bytes, entries: %u\n",
          flash_start, lookup_flash_length, num_entries);

  uint16_t total_bytes_read = 0;
  uint8_t *dest = table;

  // Read each entry from flash memory
  for (uint32_t i = 0; i < num_entries; i++) {
    uint32_t entry_addr = flash_start + (i * LOOKUP_ENTRY_SIZE);
    uint8_t *entry_ptr = (uint8_t *)entry_addr;

    // Check if this entry is valid by verifying that the first byte of the UUID
    // is nonzero. If it's zero, we assume there's no more valid data.
    if (entry_ptr[0] == 0) {
      break;
    }

    if (!uuid_is_valid4((const char *)entry_ptr)) {
      // Invalid UUID format: assume no further valid data.
      break;
    }

    // Copy the 38-byte entry into the provided table buffer.
    memcpy(dest, entry_ptr, LOOKUP_ENTRY_SIZE);
    dest += LOOKUP_ENTRY_SIZE;
    total_bytes_read += LOOKUP_ENTRY_SIZE;
  }

  // Return the total number of bytes read in the lookup table via the length
  // pointer.
  *length = total_bytes_read;
}

static int8_t appmngr_persist_app_lookup_table(const uint8_t *table,
                                               uint16_t table_length) {
  // The table is one flash sector. Anything longer would be programmed into
  // the next sector, the global settings.
  if (table_length > LOOKUP_TABLE_SIZE) {
    DPRINTF("Lookup table of %u bytes does not fit its %u-byte sector\n",
            table_length, (unsigned)LOOKUP_TABLE_SIZE);
    return -1;
  }
  uint32_t flash_start = (uint32_t)&_global_lookup_flash_start;
  uint32_t config_flash_length = (uint32_t)&_global_config_flash_start -
                                 (uint32_t)&_global_lookup_flash_start;

  // Erase the whole region that stores the table (or at least the first sector)
  select_flashLockoutBegin();
  uint32_t ints = save_and_disable_interrupts();
  flash_range_erase(flash_start - XIP_BASE, FLASH_SECTOR_SIZE);

  const uint32_t PAGE = FLASH_PAGE_SIZE;
  uint32_t wrote = 0;
  uint8_t pagebuf[FLASH_PAGE_SIZE];

  while (wrote < table_length) {
    uint32_t chunk = table_length - wrote;
    if (chunk > PAGE) chunk = PAGE;
    memset(pagebuf, 0x00,
           PAGE);  // choose your erased-fill; 0x00 is fine for table
    memcpy(pagebuf, table + wrote, chunk);
    flash_range_program((flash_start - XIP_BASE) + wrote, pagebuf, PAGE);
    wrote += chunk;
  }
  restore_interrupts(ints);
  select_flashLockoutEnd();
  return 0;
}

// -----------------------------------------------------------------------------
// Repair the lookup table at boot
// -----------------------------------------------------------------------------
// The table is no longer filled from the .json files on the card: an app gets
// a config sector the first time it is launched (appmngr_assign_launch_sector).
// What is left at boot is the repair of a table written by older firmware.
void appmngr_sync_lookup_table() {
  uint8_t table[LOOKUP_TABLE_SIZE];
  memset(table, 0, sizeof(table));
  uint16_t table_len = 0;
  appmngr_load_apps_lookup_table(table, &table_len);
  // Firmware before v2.5.0 handed out sectors past the last config sector
  // once more than 30 apps were installed. Drop those entries, and any that
  // share a sector, so their apps get a sector of their own at their next
  // launch.
  if (lookuptable_sanitize(table, &table_len, appmngr_config_sector_count()) >
      0) {
    appmngr_persist_app_lookup_table(table, table_len);
  } else {
    DPRINTF("sync: no changes, nothing to persist\n");
  }
}

// Give the app being launched its config sector and make it the most recently
// launched entry of the lookup table (D-14 keeps launch order there). An app
// that has a sector keeps it and its settings; one that has none gets the
// lowest free sector, erased, so it starts from its defaults.
static download_launch_err_t appmngr_assign_launch_sector(const char *uuid) {
  uint8_t *table = malloc(LOOKUP_TABLE_SIZE);
  if (table == NULL) {
    DPRINTF("Launch: no memory to read the lookup table\n");
    return DOWNLOAD_LAUNCHAPP_NOCONFIGSECTOR_ERROR;
  }
  memset(table, 0, LOOKUP_TABLE_SIZE);
  uint16_t table_length = 0;
  appmngr_load_apps_lookup_table(table, &table_length);
  uint16_t num_sectors = appmngr_config_sector_count();

  int moved = lookuptable_move_to_end(uuid, table, table_length);
  if (moved >= 0) {
    if (moved > 0 && appmngr_persist_app_lookup_table(table, table_length)) {
      DPRINTF("Launch: could not save the launch order\n");
    }
    DPRINTF("Launch: %s keeps sector %d\n", uuid,
            lookuptable_get_sector(uuid, table, &table_length));
    free(table);
    return DOWNLOAD_LAUNCHAPP_OK;
  }

  int16_t sector =
      lookuptable_find_first_empty_sector(table, &table_length, num_sectors);
  if (sector < 0 ||
      lookuptable_update(uuid, (uint16_t)sector, table, &table_length) != 0 ||
      appmngr_persist_app_lookup_table(table, table_length) != 0) {
    DPRINTF("Launch: no config sector for %s\n", uuid);
    free(table);
    return DOWNLOAD_LAUNCHAPP_NOCONFIGSECTOR_ERROR;
  }
  free(table);
  if (appmngr_delete_config_sector((uint8_t)sector) != 0) {
    DPRINTF("Launch: could not erase sector %d\n", sector);
    return DOWNLOAD_LAUNCHAPP_NOCONFIGSECTOR_ERROR;
  }
  DPRINTF("Launch: %s gets sector %d\n", uuid, sector);
  return DOWNLOAD_LAUNCHAPP_OK;
}

/*
  Reads a UF2 file from the microSD card, ignores each block’s targetAddr,
  and writes each block’s payload linearly into flash starting at flashAddress.
  Before writing, it erases [flashAddress..(flashAddress + flashSize)).

  We no longer write one UF2 block at a time. Instead, we accumulate multiple
  payloads from consecutive UF2 blocks into a dynamic buffer of size
  'userPageSize' until that buffer is full or we run out of data.
  Then we flush that buffer to the flash.

  If the leftover region in flash is smaller than the userPageSize,
  we do partial writes, ensuring we never write beyond the allocated region.

  Parameters:
    - filename: Path to the UF2 file on SD card.
    - flashAddress: Start address in RP2040 flash memory.
    - flashSize: Size of the region to erase/write (must be a multiple of 4096).
    - userPageSize: The chunk size for flash writes (must be a multiple of 256).

  Returns FRESULT error codes (FR_OK if successful, etc.).

  Steps:
    1) Verify userPageSize is multiple of 256.
    2) Open UF2 file.
    3) Erase [flashAddress..flashAddress+flashSize).
    4) Repeatedly read 512-byte UF2 blocks from file, parse out the payload,
       and accumulate that payload in a dynamic buffer up to userPageSize.
    5) Once the buffer is full (or no more data), write it to flash.
    6) If flash region remains but we have partial leftover data < userPageSize,
       do a final partial write.

  NOTE:
    - We do partial writes if leftover flash size is smaller than userPageSize.
    - The user must ensure enough RAM is available to allocate userPageSize
  bytes.
*/

static FRESULT __not_in_flash_func(storeUF2FileToFlash)(const char *filename,
                                                        uint32_t flashAddress,
                                                        uint32_t flashSize,
                                                        uint32_t userPageSize) {
  FIL file;
  FRESULT res;
  FSIZE_t uf2FileSize;
  UINT bytesRead;

  // Check page size
  if (userPageSize == 0 || (userPageSize % FLASH_PAGE_SIZE) != 0) {
    DPRINTF("Error: userPageSize (%u) is not a multiple of %u.\n", userPageSize,
            FLASH_PAGE_SIZE);
    return FR_INVALID_PARAMETER;
  }

  // Open file
  res = f_open(&file, filename, FA_READ);
  if (res != FR_OK) {
    DPRINTF("Error opening file %s: %d\n", filename, res);
    return res;
  }

  // Erase region
  uint32_t offset = flashAddress - XIP_BASE;
  if (flashSize == 0) {
    // If user passed 0, nothing to do?
    f_close(&file);
    return FR_INVALID_PARAMETER;
  }

  DPRINTF("Erasing %u bytes of flash at offset 0x%X\n", flashSize, offset);
  {
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(offset, flashSize);
    restore_interrupts(ints);
  }

  uf2FileSize = f_size(&file);
  DPRINTF("UF2 file size: %u bytes\n", (unsigned int)uf2FileSize);
  DPRINTF("Erased %u bytes of flash at offset 0x%X\n", (unsigned int)flashSize,
          offset);

  // Static staging buffer, deliberately not malloc'd -- see
  // APP_FLASH_COPY_CHUNK_SIZE in appmngr.h for why (heap fragmentation at
  // launch time made a large contiguous allocation unreliable).
  static uint8_t accumBufStorage[APP_FLASH_COPY_CHUNK_SIZE];
  if (userPageSize > sizeof(accumBufStorage)) {
    DPRINTF("Chunk size %u exceeds the %u byte staging buffer\n", userPageSize,
            (unsigned)sizeof(accumBufStorage));
    f_close(&file);
    return FR_INVALID_PARAMETER;
  }
  uint8_t *accumBuf = accumBufStorage;

  // We'll keep track of how many bytes we have in accumBuf.
  uint32_t accumUsed = 0;

  // We'll read the file block by block (512 bytes each) and parse payload.
  uint32_t currentOffset = offset;
  bool done = false;

  while (!done) {
    // Attempt to read 512 bytes for a UF2 block.
    uint8_t uf2block[UF2_BLOCK_SIZE];
    res = f_read(&file, uf2block, sizeof(uf2block), &bytesRead);

    if (res != FR_OK) {
      DPRINTF("Error reading file: %d\n", res);
      break;  // error
    }
    if (bytesRead == 0) {
      // End of file.
      done = true;
    } else if (bytesRead < UF2_BLOCK_SIZE) {
      DPRINTF("Warning: incomplete UF2 block (%u bytes). Ignored.\n",
              bytesRead);
      done = true;
    } else {
      // parse the block
      const uint32_t UF2_MAGIC_START0 = 0x0A324655;
      const uint32_t UF2_MAGIC_START1 = 0x9E5D5157;
      uint32_t magic0 = ((uint32_t *)uf2block)[0];
      uint32_t magic1 = ((uint32_t *)uf2block)[1];

      if (magic0 != UF2_MAGIC_START0 || magic1 != UF2_MAGIC_START1) {
        DPRINTF("Invalid UF2 magic. Skipping block.\n");
        continue;  // skip
      }

      uint32_t payloadSize = ((uint32_t *)uf2block)[4];
      if (payloadSize == 0 || payloadSize > 476) {
        DPRINTF("Invalid UF2 payload size: %u. Skipping.\n", payloadSize);
        continue;
      }

      // The payload is located at offset 32 in the block.
      uint8_t *payload = uf2block + 32;

      // Now we have 'payloadSize' bytes to accumulate in accumBuf.
      // We might have to break it up if there's not enough space left.
      uint32_t payloadPos = 0;

      while (payloadPos < payloadSize) {
        uint32_t leftover = payloadSize - payloadPos;
        uint32_t spaceInBuf = userPageSize - accumUsed;
        uint32_t toCopy = (leftover < spaceInBuf) ? leftover : spaceInBuf;

        // Copy to the accumulation buffer.
        memcpy(accumBuf + accumUsed, payload + payloadPos, toCopy);
        accumUsed += toCopy;
        payloadPos += toCopy;

        // If accumBuf is full, flush it to flash.
        if (accumUsed == userPageSize) {
          // Check leftover region.
          uint32_t leftoverRegion = (offset + flashSize) - currentOffset;
          if (leftoverRegion == 0) {
            // No more space in flash.
            done = true;
            break;
          }

          // We'll write either userPageSize or leftoverRegion if smaller.
          uint32_t chunk =
              (userPageSize <= leftoverRegion) ? userPageSize : leftoverRegion;

          // Program the flash with 'chunk' bytes.
          {
            uint32_t ints = save_and_disable_interrupts();
            flash_range_program(currentOffset, accumBuf, chunk);
            restore_interrupts(ints);
          }

          currentOffset += chunk;
          accumUsed = 0;  // reset for next round

          // If leftoverRegion < userPageSize, we wrote partially.
          if (chunk < userPageSize) {
            done = true;
            break;
          }
        }

        if (done) break;
      }
    }

    if (done) {
      // either end of file or no more flash space
      break;
    }
  }

  // If we have leftover data in accumBuf, flush it partially.
  if (accumUsed > 0 && !res) {
    // leftover region check
    uint32_t leftoverRegion = (offset + flashSize) - currentOffset;
    if (leftoverRegion > 0) {
      uint32_t chunk =
          (accumUsed <= leftoverRegion) ? accumUsed : leftoverRegion;
      // Program partial.
      {
        uint32_t ints = save_and_disable_interrupts();
        flash_range_program(currentOffset, accumBuf, chunk);
        restore_interrupts(ints);
      }
      currentOffset += chunk;
    }
  }

  // accumBuf is static storage, not heap: nothing to free.
  f_close(&file);

  return FR_OK;
}

int8_t appmngr_erase_app_lookup_table() {
  // Delete whole app lookup table
  uint32_t flash_start = (uint32_t)&_global_lookup_flash_start;
  uint32_t flash_length = (unsigned int)&_global_config_flash_start -
                          (unsigned int)&_global_lookup_flash_start;

  // Calculate the number of 4KB sectors
  uint32_t num_sectors = flash_length / FLASH_SECTOR_SIZE;

  // Print the number of sectors
  DPRINTF("Config Flash start: 0x%X, length: %u bytes, sectors: %u\n",
          flash_start, flash_length, num_sectors);

  // Erase the sector
  select_flashLockoutBegin();
  uint32_t ints = save_and_disable_interrupts();
  flash_range_erase(flash_start - XIP_BASE, flash_length);  // 4 Kbytes
  restore_interrupts(ints);
  select_flashLockoutEnd();

  return 0;  // Success
}

download_delete_err_t appmngr_delete_app(const char *uuid) {
  // Delete the app with the uuid given.
  DPRINTF("Deleting app with UUID: %s\n", uuid);

  // First, check that the uuid is a valid uuid
  if (strlen(uuid) != 36) {
    DPRINTF("Invalid UUID: %s\n", uuid);
    return DOWNLOAD_DELETEAPP_NOTUUID_ERROR;
  }
  // Now, check that the sd card is ready
  if (!sdcard_info.ready) {
    DPRINTF("SD card not ready\n");
    return DOWNLOAD_DELETEAPP_SDCARDNOTREADY_ERROR;
  }

  // Get the filename of the app info json
  char json_filename[256] = {0};
  snprintf(json_filename, sizeof(json_filename), "%s/%s.json",
           settings_find_entry(gconfig_getContext(), PARAM_APPS_FOLDER)->value,
           uuid);

  // Get the filename of the app binary in uf2 format
  char binary_filename[256] = {0};
  snprintf(binary_filename, sizeof(binary_filename), "%s/%s.uf2",
           settings_find_entry(gconfig_getContext(), PARAM_APPS_FOLDER)->value,
           uuid);

  DPRINTF("Deleting files %s and %s\n", json_filename, binary_filename);
  // Try to delete the files if they exist
  f_unlink(json_filename);
  f_unlink(binary_filename);

  // The development app can also have a deploy API upload beside its catalog
  // binary (D-10). Deleting the app is the documented way back to the probe
  // workflow, so the upload goes too.
  if (strcmp(uuid, DEVAPI_DEV_APP_UUID) == 0) {
    char upload_filename[sizeof(binary_filename)] = {0};
    devapi_buildUploadPath(upload_filename, sizeof(upload_filename));
    DPRINTF("Deleting development upload %s\n", upload_filename);
    f_unlink(upload_filename);
  }

  // Now delete the entry in the app lookup table
  uint8_t *table = malloc(FLASH_SECTOR_SIZE);
  if (table == NULL) {
    return DOWNLOAD_CANNOTCREATE_CONFIG;
  }
  uint16_t table_length = 0;
  // Read the table from flash memory
  appmngr_load_apps_lookup_table(table, &table_length);

  // Delete and update entry in the table
  int res = lookuptable_delete(uuid, table, &table_length);

  // Persist the table if ok
  if (res == -1) {
    DPRINTF("Something went wrong updating the app lookup table.\n");
  } else {
    res = appmngr_persist_app_lookup_table(table, table_length);
    if (res == -1) {
      DPRINTF("There was something wrong persisting the app lookup table.\n");
    } else {
      DPRINTF("App lookup table pesisted!\n");
    }
  }
  free(table);
  DPRINTF("App deleted from the app lookup table\n");

  DPRINTF("App deleted\n");
  return DOWNLOAD_DELETEAPP_OK;
}

download_launch_err_t appmngr_get_launch_status(void) { return launch_status; }

void appmngr_set_launch_status(download_launch_err_t status) {
  launch_status = status;
}

void appmngr_schedule_launch_app(const char *uuid) {
  if (uuid) {
    launch_status = DOWNLOAD_LAUNCHAPP_SCHEDULED;
    strncpy(launch_app_uuid, uuid, sizeof(launch_app_uuid));
    launch_app_uuid[sizeof(launch_app_uuid) - 1] = '\0';
    launch_restore = false;
  }
}

void appmngr_schedule_restore_launch_app(const char *uuid) {
  appmngr_schedule_launch_app(uuid);
  if (uuid) {
    launch_restore = true;
  }
}

appmngr_config_erase_t appmngr_erase_app_config(const char *uuid) {
  // Not-found from the lookup is -3; -1 and -2 mean the UUID itself is bad.
  enum { LOOKUP_SECTOR_NOT_FOUND = -3 };
  uint8_t *table = malloc(FLASH_SECTOR_SIZE);
  if (table == NULL) {
    DPRINTF("Config erase: no memory to read the lookup table\n");
    return APPMNGR_CONFIG_ERASE_FAILED;
  }
  uint16_t table_length = 0;
  appmngr_load_apps_lookup_table(table, &table_length);
  int sector = lookuptable_get_sector(uuid, table, &table_length);
  free(table);
  if (sector == LOOKUP_SECTOR_NOT_FOUND) {
    DPRINTF("Config erase: no config sector assigned to %s\n", uuid);
    return APPMNGR_CONFIG_NOT_ASSIGNED;
  }
  if (sector < 0) {
    DPRINTF("Config erase: lookup failed for %s (%d)\n", uuid, sector);
    return APPMNGR_CONFIG_ERASE_FAILED;
  }
  if (sector >= appmngr_config_sector_count()) {
    // Never an address: a sector past the last one is the lookup table or
    // the global settings. The next boot's sync drops the entry.
    DPRINTF("Config erase: %s points at sector %d, which does not exist\n",
            uuid, sector);
    return APPMNGR_CONFIG_NOT_ASSIGNED;
  }
  if (appmngr_delete_config_sector((uint8_t)sector) != 0) {
    DPRINTF("Config erase: could not erase sector %d\n", sector);
    return APPMNGR_CONFIG_ERASE_FAILED;
  }
  DPRINTF("Config erase: sector %d of %s erased\n", sector, uuid);
  return APPMNGR_CONFIG_ERASED;
}

// "Restore and launch" for the development app: remove everything a developer
// left behind before the catalog binary is flashed. The deploy API upload is
// deleted, or the next plain launch would flash it again and undo the restore.
// The app's config sector is erased, so the microfirmware starts from its
// defaults, exactly as after a fresh install.
static void appmngr_restore_dev_app(void) {
  char upload_filename[DEVAPI_PATH_MAX] = {0};
  devapi_buildUploadPath(upload_filename, sizeof(upload_filename));
  FRESULT unlinked = f_unlink(upload_filename);
  DPRINTF("Restore: delete upload %s: %s\n", upload_filename,
          unlinked == FR_OK ? "deleted" : "not present");
  appmngr_erase_app_config(DEVAPI_DEV_APP_UUID);
}

download_launch_err_t appmngr_launch_app() {
  // Launch the app with the uuid given.
  DPRINTF("Launch app with UUID: %s\n", launch_app_uuid);

  // First, check that the uuid is a valid uuid
  if (!uuid_is_valid4(launch_app_uuid)) {
    DPRINTF("Invalid UUID: %s\n", launch_app_uuid);
    return DOWNLOAD_LAUNCHAPP_NOTUUID_ERROR;
  }

  // Now, check that the sd card is ready
  if (!sdcard_info.ready) {
    DPRINTF("SD card not ready\n");
    return DOWNLOAD_LAUNCHAPP_SDCARDNOTREADY_ERROR;
  }

  // Settings first: a launch that cannot give the app a config sector stops
  // here, with nothing flashed and the network still up.
  download_launch_err_t sector_err =
      appmngr_assign_launch_sector(launch_app_uuid);
  if (sector_err != DOWNLOAD_LAUNCHAPP_OK) {
    return sector_err;
  }

  DPRINTF("Preparing runtime for app launch\n");
  appmngr_prepare_launch_runtime();

  // Get the filename of the app info json
  char json_filename[256] = {0};
  snprintf(json_filename, sizeof(json_filename), "%s/%s.json",
           settings_find_entry(gconfig_getContext(), PARAM_APPS_FOLDER)->value,
           launch_app_uuid);

  // Get the filename of the app binary in uf2 format
  char binary_filename[256] = {0};
  snprintf(binary_filename, sizeof(binary_filename), "%s/%s.uf2",
           settings_find_entry(gconfig_getContext(), PARAM_APPS_FOLDER)->value,
           launch_app_uuid);

  // The development app (magic UUID 44444444-4444-4444-8444-444444444444) can
  // have three binaries, and at most one of them may be flashed:
  //
  //  1. Probe workflow: the developer put the microfirmware in the slot
  //     themselves, with a debug probe or the USB/BOOTSEL dance. Launching must
  //     leave the slot alone, or it destroys exactly what they wanted to run.
  //  2. Deploy API upload (EPIC-05): <uuid>.dev.uf2. That file IS what the
  //     developer wants to run, so it is flashed like any other app.
  //  3. Catalog binary: <uuid>.uf2, written when the DEV APP is installed from
  //     the Development channel, which the deploy API requires. A plain launch
  //     never flashes it (D-10). "Restore and launch" on the Apps page does, on
  //     purpose, after deleting the upload and erasing the app's config.
  //
  // Uploads used to be written over <uuid>.uf2, so cases 2 and 3 looked the
  // same: from v2.3.0 to v2.4.1, launching with the DEV APP installed flashed
  // the placeholder over a probe-flashed microfirmware, and the placeholder
  // handed straight back to Booster (EPIC-11).
  bool isDevApp = strcmp(launch_app_uuid, DEVAPI_DEV_APP_UUID) == 0;
  bool flashIt = true;
  const char *flash_filename = binary_filename;
  char upload_filename[sizeof(binary_filename)] = {0};
  if (isDevApp && launch_restore) {
    // Restore and launch: the one deliberate exception to case 3. The
    // developer asked for a clean slate from the web page, so the upload and
    // the app's config go, and the catalog binary is flashed.
    DPRINTF("Restoring the development app from the catalog binary\n");
    appmngr_restore_dev_app();
  } else if (isDevApp) {
    flashIt = devapi_hasUploadedBinary();
    if (flashIt) {
      devapi_buildUploadPath(upload_filename, sizeof(upload_filename));
      flash_filename = upload_filename;
    }
  }
  launch_restore = false;

  if (flashIt) {
    DPRINTF("Copying app binary %s to flash memory\n", flash_filename);
    int res = storeUF2FileToFlash(
        flash_filename, (uint32_t)&_storage_flash_start,
        (uint32_t)&__flash_binary_start - (uint32_t)&_storage_flash_start,
        APP_FLASH_COPY_CHUNK_SIZE);
  } else {
    DPRINTF("Development app launched; nothing uploaded, leaving flash alone\n");
  }

  // After copying the executable from the SD card to the flash memory, we need
  // to update the boot feature to the UUID of the app we want to launch
  settings_put_string(gconfig_getContext(), PARAM_BOOT_FEATURE,
                      launch_app_uuid);
  settings_save(gconfig_getContext(), true);

  DPRINTF("App launched\n");

  // After returning from this function, the system should reboot and the app
  // will be launched
  return DOWNLOAD_LAUNCHAPP_OK;
}

download_err_t appmngr_start_download(const char *url) {
  // Download the app binary from the URL in the app_info struct
  // The binary is saved to the SD card in the apps folder
  // The filename must be the UUID of the app with the .bin extension
  // The binary is downloaded using the HTTP client
  // The MD5 hash is checked after the download
  // The binary is saved to the SD card
  // The MD5 hash is checked again after saving
  // If the MD5 hash is correct, the app info is saved to the SD card
  // If the MD5 hash is incorrect, the binary is deleted from the SD card
  // The app info is not saved
  // The function returns 0 on success, -1 on error

  // Open the file for writing to the folder of the apps to the tmp.download
  // file
  char filename[256] = {0};
  get_tmp_filename_path(filename);
  DPRINTF("Downloading app binary to file: %s\n", filename);
  FRESULT res;

  appmngr_fs_lock();
  // Close any previously open handle
  f_close(&file);

  // Clear read-only attribute if necessary
  f_chmod(filename, 0, AM_RDO);

  // Let's try to remove the file if it exists
  res = f_unlink(filename);
  if (res == FR_NO_FILE) {
    // Ignore the error if the file doesn't exist
    DPRINTF("Error deleting file. %s does not exists: %i. Continuing.\n",
            filename, res);
  }

  // Open file for writing or create if it doesn't exist
  res = f_open(&file, filename, FA_WRITE | FA_CREATE_ALWAYS);
  if (res == FR_LOCKED) {
    DPRINTF("File is locked. Attempting to resolve...\n");

    // Try to remove the file and create it again
    res = f_unlink(filename);
    if (res == FR_OK || res == FR_NO_FILE) {
      res = f_open(&file, filename, FA_WRITE | FA_CREATE_ALWAYS);
    }
  }

  if (res != FR_OK) {
    DPRINTF("Error opening file %s: %i\n", filename, res);
    appmngr_fs_unlock();
    return DOWNLOAD_CANNOTOPENFILE_ERROR;
  }
  appmngr_fs_unlock();

  UINT bw = 0;
  const uint8_t probe_byte = 0;
  res = f_write(&file, &probe_byte, sizeof(probe_byte), &bw);
  if (res != FR_OK || bw != sizeof(probe_byte)) {
    DPRINTF("Error probing file write %s: %i (wrote %u of %u)\n", filename, res,
            (unsigned)bw, (unsigned)sizeof(probe_byte));
    f_close(&file);
    return DOWNLOAD_CANNOTWRITEFILE_ERROR;
  }
  res = f_lseek(&file, 0);
  if (res == FR_OK) {
    res = f_truncate(&file);
  }
  if (res != FR_OK) {
    DPRINTF("Error resetting temp file %s: %i\n", filename, res);
    f_close(&file);
    return DOWNLOAD_CANNOTWRITEFILE_ERROR;
  }

  // A fresh download starts the hop count over; a redirect re-issue must not.
  // The pending/settling flags must be cleared too: if a previous attempt died
  // between capturing a Location and consuming it, they stay set, and the first
  // poll of THIS download would chase the previous attempt's stale redirect_url
  // instead of the transfer we just started.
  if (!following_redirect) {
    redirect_hops = 0;
    redirect_pending = false;
    redirect_settling = false;
    retry_pending = false;
    hop_retries = 0;
  }

  // Get the components of a url
  url_components_t components;
  if (following_redirect) {
    // Re-issuing after a 30x. The URL comes from the Location header, and
    // download_type must stay as it was: passing a URL through the branch
    // below would silently reclassify a microfirmware download as a firmware
    // OTA, which changes the size cap and the completion handling.
    if (urlparse_parse(redirect_url, &components) != 0) {
      DPRINTF("Error parsing redirect URL: %s\n", redirect_url);
      return DOWNLOAD_CANNOTPARSEURL_ERROR;
    }
    snprintf(current_url, sizeof(current_url), "%s", redirect_url);
    if (download_type == DOWNLOAD_TYPE_FIRMWARE) {
      download_firmware_status = DOWNLOAD_STATUS_STARTED;
      download_firmware_error = DOWNLOAD_OK;
    } else {
      download_status = DOWNLOAD_STATUS_STARTED;
      download_error = DOWNLOAD_OK;
    }
  } else if (url == NULL || strlen(url) == 0) {
    if (urlparse_parse(app_info.binary, &components) != 0) {
      DPRINTF("Error parsing URL of app_info.binary: %s\n", app_info.binary);
      return DOWNLOAD_CANNOTPARSEURL_ERROR;
    }
    snprintf(current_url, sizeof(current_url), "%s", app_info.binary);
    download_type = DOWNLOAD_TYPE_APP;
    download_status = DOWNLOAD_STATUS_STARTED;
    download_error = DOWNLOAD_OK;
  } else {
    if (urlparse_parse(url, &components) != 0) {
      DPRINTF("Error parsing URL of url arg: %s\n", url);
      return DOWNLOAD_CANNOTPARSEURL_ERROR;
    }
    snprintf(current_url, sizeof(current_url), "%s", url);
    download_type = DOWNLOAD_TYPE_FIRMWARE;
    download_firmware_status = DOWNLOAD_STATUS_STARTED;
    download_firmware_error = DOWNLOAD_OK;
  }
  // Every hop restarts the destination file, so the counters restart with it.
  // This must NOT be limited to a fresh download: a redirect re-issue and a
  // retry both truncate and rewrite the same file.
  download_bytes_received = 0;
  download_bytes_total = 0;

  // Remember what we are about to request: a relative Location is resolved
  // against it (components itself is on the stack).
  active_components = components;

  // Copy host and URI into persistent buffers (components is on stack)
  snprintf(request_host_buf, sizeof(request_host_buf), "%s", components.host);
  snprintf(request_uri_buf, sizeof(request_uri_buf), "%s", components.uri);

  // Decide the transport from the URL scheme, per request. Only http and
  // https are supported.
  int use_https = httpc_scheme_is_https(components.protocol);
  if (use_https < 0) {
    DPRINTF("Unsupported URL scheme: %s\n", components.protocol);
    return DOWNLOAD_CANNOTPARSEURL_ERROR;
  }

  // Rebuilding `request` holds the async-context lock because lwIP keeps
  // pointers INTO this struct for the lifetime of a connection
  // (http_client.c stores conn_settings = &request.settings and
  // callback_arg = &request), and under the threadsafe_background cyw43 arch
  // lwIP runs callbacks and teardown in a background context concurrent with
  // this main-loop code. A redirect re-issues while the previous connection
  // may still be tearing down, so repopulating the struct unlocked is a real
  // race. Note this was NOT the cause of the download failures -- that was
  // lwIP send-heap exhaustion (see MEM_SIZE in lwipopts.h) -- and adding this
  // lock alone changed nothing. It is kept because the race is real, not
  // because it fixed anything.
  //
  // The lock is recursive, so http_client_request_async() taking it again
  // inside is fine.
  async_context_t *ctx = cyw43_arch_async_context();
  async_context_acquire_lock_blocking(ctx);
  request = (HTTPC_REQUEST_T){0};
  request.complete = false;
  request.hostname = request_host_buf;
  request.url = request_uri_buf;
  request.port = components.port;  // 0 lets httpc pick 80 or 443
  request.headers_fn = http_client_header_check_size_fn;
  request.recv_fn = http_client_receive_file_fn;
  request.result_fn = http_client_result_complete_fn;
  if (use_https) {
    request.tls_config = httpc_shared_tls_config();
    if (request.tls_config == NULL) {
      async_context_release_lock(ctx);
      DPRINTF("Cannot initialize HTTPS\n");
      return DOWNLOAD_CANNOTSTARTDOWNLOAD_ERROR;
    }
  } else {
    request.tls_config = NULL;
  }
  DPRINTF("HOST: %s. PORT: %u. URI: %s\n", components.host, components.port,
          components.uri);
  DPRINTF("Downloading app binary: %s\n", request.url);
  DPRINTF("Download with %s\n", use_https ? "HTTPS" : "HTTP");
  int result = http_client_request_async(ctx, &request);
  async_context_release_lock(ctx);
  if (result != 0) {
    DPRINTF("Error initializing the download app binary: %i\n", result);
    res = f_close(&file);
    if (res != FR_OK) {
      DPRINTF("Error closing file %s: %i\n", filename, res);
    }
    request.tls_config = NULL;  // shared config, not ours to free
    return DOWNLOAD_CANNOTSTARTDOWNLOAD_ERROR;
  }
  return DOWNLOAD_OK;
}

download_poll_t appmngr_poll_download_app() {
  if (!request.complete) {
    async_context_poll(cyw43_arch_async_context());
    async_context_wait_for_work_ms(cyw43_arch_async_context(), 10);
    return DOWNLOAD_POLL_CONTINUE;
  }
  if (retry_pending) {
    // Same deferral as the redirect path below: re-issuing from inside an
    // lwIP callback would reenter the stack.
    if (!redirect_settling) {
      redirect_settling = true;
      redirect_retry_time = make_timeout_time_ms(APPMNGR_REDIRECT_SETTLE_MS);
      return DOWNLOAD_POLL_CONTINUE;
    }
    if (absolute_time_diff_us(get_absolute_time(), redirect_retry_time) > 0) {
      async_context_poll(cyw43_arch_async_context());
      async_context_wait_for_work_ms(cyw43_arch_async_context(), 10);
      return DOWNLOAD_POLL_CONTINUE;
    }
    redirect_settling = false;
    retry_pending = false;
    hop_retries++;
    DPRINTF("Retrying hop, attempt %d/%d\n", hop_retries,
            max_hop_retries);
    // The redirect path re-issues from redirect_url, and following_redirect
    // keeps the hop count and download_type intact, which is what a retry of
    // the current hop needs too.
    snprintf(redirect_url, sizeof(redirect_url), "%s", current_url);
    following_redirect = true;
    download_err_t retry_err = appmngr_start_download(NULL);
    following_redirect = false;
    if (retry_err != DOWNLOAD_OK) {
      DPRINTF("Hop retry failed to start: %d\n", retry_err);
      appmngr_cleanup_tmp_download_file();
      if (download_type == DOWNLOAD_TYPE_FIRMWARE)
        download_firmware_status = DOWNLOAD_STATUS_FAILED;
      else
        download_status = DOWNLOAD_STATUS_FAILED;
      return DOWNLOAD_POLL_COMPLETED;
    }
    return DOWNLOAD_POLL_CONTINUE;
  }
  if (redirect_pending) {
    // Followed here rather than in the headers callback: the previous
    // connection is only fully closed once request.complete is set, and
    // starting a new request from inside an lwIP callback reenters the stack.
    //
    // The settle delay is empirical and load-bearing. Opening the next
    // connection within a few milliseconds of the previous close yields a
    // connection that receives nothing and dies on lwIP's 15 second poll
    // timeout. Debug builds get this spacing for free from UART logging,
    // which is why an undelayed build passed with traces and failed without.
    if (!redirect_settling) {
      redirect_settling = true;
      redirect_retry_time = make_timeout_time_ms(APPMNGR_REDIRECT_SETTLE_MS);
      return DOWNLOAD_POLL_CONTINUE;
    }
    if (absolute_time_diff_us(get_absolute_time(), redirect_retry_time) > 0) {
      async_context_poll(cyw43_arch_async_context());
      async_context_wait_for_work_ms(cyw43_arch_async_context(), 10);
      return DOWNLOAD_POLL_CONTINUE;
    }
    redirect_settling = false;
    redirect_pending = false;
    redirect_hops++;
    hop_retries = 0;
    DPRINTF("Following redirect %d/%d\n", redirect_hops,
            APPMNGR_MAX_REDIRECT_HOPS);
    following_redirect = true;
    download_err_t err = appmngr_start_download(NULL);
    following_redirect = false;
    if (err != DOWNLOAD_OK) {
      DPRINTF("Redirect re-issue failed: %d\n", err);
      if (download_type == DOWNLOAD_TYPE_FIRMWARE)
        download_firmware_status = DOWNLOAD_STATUS_FAILED;
      else
        download_status = DOWNLOAD_STATUS_FAILED;
      return DOWNLOAD_POLL_COMPLETED;
    }
    return DOWNLOAD_POLL_CONTINUE;
  }
  return DOWNLOAD_POLL_COMPLETED;
}

static download_err_t calculate_md5_of_tmp_file(MD5Context *md5_ctx) {
  FRESULT res;
  FIL md5_file;
  char filename[256] = {0};
  get_tmp_filename_path(filename);

  // Held for the whole read: ~400KB from SD takes well under a second, and
  // nothing latency-critical runs while a finished download is hashed.
  appmngr_fs_lock();
  res = f_open(&md5_file, filename, FA_READ);
  if (res != FR_OK) {
    DPRINTF("Error opening tmp file for MD5 calculation: %i\n", res);
    appmngr_fs_unlock();
    return DOWNLOAD_CANNOTOPENFILE_ERROR;
  }

  md5Init(md5_ctx);
  char *buffer = malloc(4096);
  if (buffer == NULL) {
    DPRINTF("Error allocating buffer for MD5 calculation\n");
    f_close(&md5_file);
    appmngr_fs_unlock();
    return DOWNLOAD_CANNOTCREATE_CONFIG;
  }
  memset(buffer, 0, 4096);
  UINT bytes_read;

  do {
    res = f_read(&md5_file, buffer, 4096, &bytes_read);
    if (res != FR_OK) {
      DPRINTF("Error reading file %s for MD5 calculation: %i\n", filename, res);
      f_close(&md5_file);
      free(buffer);
      appmngr_fs_unlock();
      return DOWNLOAD_CANNOTREADFILE_ERROR;
    }
    md5Update(md5_ctx, buffer, bytes_read);
  } while (bytes_read > 0);

  md5Finalize(md5_ctx);
  f_close(&md5_file);
  appmngr_fs_unlock();
  free(buffer);
  DPRINTF("MD5 hash calculated\n");
  return DOWNLOAD_OK;
}

download_err_t appmngr_finish_download_app() {
  // Close the file
  appmngr_fs_lock();
  int res = f_close(&file);
  appmngr_fs_unlock();
  if (res != FR_OK) {
    DPRINTF("Error closing tmp file %s: %i\n", res);
    return DOWNLOAD_CANNOTCLOSEFILE_ERROR;
  }
  DPRINTF("Downloaded.\n");

  // The TLS config is shared process-wide and outlives this download; just
  // drop our reference.
  request.tls_config = NULL;

  if (download_status != DOWNLOAD_STATUS_COMPLETED) {
    DPRINTF("Error downloading app binary: %i\n", download_status);
    return DOWNLOAD_FORCEDABORT_ERROR;
  }
  DPRINTF("App binary downloaded\n");

  appmngr_debug_log_md5("MD5 hash of app info", app_info.md5,
                        sizeof(app_info.md5));

  MD5Context md5_ctx;
  download_err_t md5_ret = calculate_md5_of_tmp_file(&md5_ctx);
  if (md5_ret != DOWNLOAD_OK) {
    DPRINTF("Error calculating MD5 of tmp file: %d\n", md5_ret);
    return md5_ret;
  }

  appmngr_debug_log_md5("MD5 hash of app info", app_info.md5,
                        sizeof(app_info.md5));

  memcpy(app_info.file_md5_digest, md5_ctx.digest,
         sizeof(app_info.file_md5_digest));
  appmngr_debug_log_md5("MD5 hash of downloaded file", app_info.file_md5_digest,
                        sizeof(app_info.file_md5_digest));
  appmngr_debug_log_md5("MD5 hash of app info", app_info.md5,
                        sizeof(app_info.md5));

  // Compare the MD5 hash with the one in the app info.
  //
  // This is an INTEGRITY check, not an authenticity one: it proves the bytes we
  // wrote match what the catalog said to expect, so it catches truncation and
  // corruption. It is not a signature. The expected hash arrives from the same
  // catalog, over the same connection, as the binary itself, and TLS here is
  // encryption-only (ALTCP_MBEDTLS_AUTHMODE = VERIFY_NONE, decision D-01), so
  // an attacker able to substitute the download can substitute the hash with
  // it. Real authenticity needs D-01 phase B (verified certificates) or a
  // signed catalog.
  if (memcmp(app_info.md5, app_info.file_md5_digest, sizeof(app_info.md5)) !=
      0) {
    DPRINTF("MD5 hash mismatch\n");
    return DOWNLOAD_MD5MISMATCH_ERROR;
  } else {
    DPRINTF("MD5 hash match\n");
    // Hash confirmed; everything after this is SD and flash work.
    download_phase = APPMNGR_PHASE_INSTALL;
  }

  // An install no longer takes a config sector: an app gets one the first
  // time it is launched (appmngr_assign_launch_sector()), so apps that sit on
  // the card unlaunched do not use any of the 30. An update keeps the app's
  // settings. A fresh install of an app that still has a sector, from an
  // earlier install whose files went missing, starts from clean settings, as
  // a fresh install always did.
  if (download_update) {
    DPRINTF("Download is an update, not deleting the config sector.\n");
    return DOWNLOAD_OK;
  }
  uint8_t *table = malloc(FLASH_SECTOR_SIZE);
  if (table == NULL) {
    return DOWNLOAD_CANNOTCREATE_CONFIG;
  }
  uint16_t table_length = 0;
  appmngr_load_apps_lookup_table(table, &table_length);
  int sector = lookuptable_get_sector(app_info.uuid, table, &table_length);
  free(table);
  if (sector >= 0 && sector < appmngr_config_sector_count()) {
    res = appmngr_delete_config_sector((uint8_t)sector);
    if (res != 0) {
      DPRINTF("Error deleting config sector: %i\n", res);
      return DOWNLOAD_CANNOTDELETECONFIGSECTOR_ERROR;
    }
    DPRINTF("Fresh install: config sector %d of %.*s erased\n", sector, 36,
            app_info.uuid);
  }
  return DOWNLOAD_OK;
}

download_err_t appmngr_confirm_download_app() {
  // First, get the uuid of the app
  char uuid[64] = {0};
  snprintf(uuid, sizeof(uuid), "%s", app_info.uuid);

  // Get the filename of the app info json
  char json_filename[256] = {0};
  snprintf(json_filename, sizeof(json_filename), "%s/%s.json",
           settings_find_entry(gconfig_getContext(), PARAM_APPS_FOLDER)->value,
           uuid);

  // Get the filename of the app binary in uf2 format
  char binary_filename[256] = {0};
  snprintf(binary_filename, sizeof(binary_filename), "%s/%s.uf2",
           settings_find_entry(gconfig_getContext(), PARAM_APPS_FOLDER)->value,
           uuid);

  DPRINTF("Writing files %s and %s\n", json_filename, binary_filename);
  appmngr_fs_lock();
  // Try to delete the files if they exist
  f_unlink(json_filename);
  f_unlink(binary_filename);

  // Now rename the tmp files to the final filenames
  char tmp_json_filename[256] = {0};
  snprintf(tmp_json_filename, sizeof(tmp_json_filename), "%s/tmp.json",
           settings_find_entry(gconfig_getContext(), PARAM_APPS_FOLDER)->value);
  char tmp_binary_filename[256] = {0};
  snprintf(tmp_binary_filename, sizeof(tmp_binary_filename), "%s/tmp.download",
           settings_find_entry(gconfig_getContext(), PARAM_APPS_FOLDER)->value);
  // Rename the json file to the final filename
  FRESULT res = f_rename(tmp_json_filename, json_filename);
  if (res != FR_OK) {
    DPRINTF("Error renaming json file: %i\n", res);
    appmngr_fs_unlock();
    return DOWNLOAD_CANNOTRENAMEFILE_ERROR;
  }
  // Rename the binary file to the final filename
  res = f_rename(tmp_binary_filename, binary_filename);
  if (res != FR_OK) {
    DPRINTF("Error renaming binary file: %i\n", res);
    appmngr_fs_unlock();
    return DOWNLOAD_CANNOTRENAMEFILE_ERROR;
  }
  appmngr_fs_unlock();
  DPRINTF("Written files %s and %s\n", json_filename, binary_filename);
  return DOWNLOAD_OK;
}

download_err_t appmngr_confirm_failed_download_app() {
  return DOWNLOAD_STATUS_FAILED;
}

download_err_t appmngr_finish_download_firmware() {
  // May already be closed by the MD5 verification step; that is fine.
  int res = appmngr_close_download_file();
  if (res != FR_OK) {
    DPRINTF("Error closing tmp file %s: %i\n", res);
    return DOWNLOAD_CANNOTCLOSEFILE_ERROR;
  }
  DPRINTF("Downloaded.\n");

  // The TLS config is shared process-wide and outlives this download; just
  // drop our reference.
  request.tls_config = NULL;

  download_firmware_status = DOWNLOAD_STATUS_COMPLETED;
  download_firmware_error = DOWNLOAD_OK;

  DPRINTF("Firmware binary downloaded\n");
  return DOWNLOAD_OK;
}

static void __not_in_flash_func(program_image_from_flash_src)(
    const uint8_t *src_flash, uint32_t image_size) {
  const uint32_t SECTOR = FLASH_SECTOR_SIZE;
  const uint32_t PAGE = FLASH_PAGE_SIZE;

  uint32_t erase_size = (image_size + (SECTOR - 1)) & ~(SECTOR - 1);

  multicore_lockout_victim_init();  // keep core 1 out (safe to call here if
                                    // linked to RAM)
  uint32_t ints = save_and_disable_interrupts();

  flash_range_erase(0, erase_size);

  uint8_t pagebuf[FLASH_PAGE_SIZE];
  uint32_t wrote = 0;
  while (wrote < image_size) {
    uint32_t chunk = image_size - wrote;
    if (chunk > PAGE) chunk = PAGE;
    memset(pagebuf, 0xFF, PAGE);
    memcpy(pagebuf, src_flash + wrote, chunk);
    flash_range_program(wrote, pagebuf, PAGE);
    wrote += chunk;
  }

  flash_flush_cache();
  restore_interrupts(ints);
}

void appmngr_cleanup_upgrade_image(void) {
  SettingsConfigEntry *folder =
      settings_find_entry(gconfig_getContext(), PARAM_APPS_FOLDER);
  if (folder == NULL) {
    return;
  }

  char upgrade_filename[256] = {0};
  snprintf(upgrade_filename, sizeof(upgrade_filename), "%s/upgrade.bin",
           folder->value);

  // No async-context lock here on purpose: this runs from mngr_init() before
  // the web server is started, so nothing else can be inside FatFs yet, and
  // taking the lock would depend on the cyw43 context being up.
  FRESULT res = f_unlink(upgrade_filename);
  if (res == FR_OK) {
    DPRINTF("Removed the firmware image left by the last upgrade: %s\n",
            upgrade_filename);
  }
}

download_err_t appmngr_confirm_download_firmware() {
  // Now rename the tmp files to the final filenames
  char tmp_binary_filename[256] = {0};
  snprintf(tmp_binary_filename, sizeof(tmp_binary_filename), "%s/tmp.download",
           settings_find_entry(gconfig_getContext(), PARAM_APPS_FOLDER)->value);

  // Rename the tmp.download file to the name "upgrade.uf2"
  char upgrade_filename[256] = {0};
  snprintf(upgrade_filename, sizeof(upgrade_filename), "%s/upgrade.bin",
           settings_find_entry(gconfig_getContext(), PARAM_APPS_FOLDER)->value);

  // Delete it first
  f_unlink(upgrade_filename);

  FRESULT res = f_rename(tmp_binary_filename, upgrade_filename);
  if (res != FR_OK) {
    DPRINTF("Error renaming tmp file: %i\n", res);
    return DOWNLOAD_CANNOTRENAMEFILE_ERROR;
  }

  download_firmware_status = DOWNLOAD_STATUS_COMPLETED;
  download_firmware_error = DOWNLOAD_OK;

  return DOWNLOAD_OK;
}

void appmngr_firmwareUpgradeStart(void) {
  // If you are here, we need to write in the start of the flash memory the
  // upgrader
  const uint8_t *src_flash =
      (const uint8_t *)upgrader_firmware;  // source in flash (safe region)
  const uint32_t image_size =
      (uint32_t)upgrader_firmware_length * 2;  // in bytes

  program_image_from_flash_src(src_flash, image_size);

  // UPGRADER Manager mode
  // Force to set the UPGRADER boot feature
  settings_put_string(gconfig_getContext(), PARAM_BOOT_FEATURE, "UPGRADER");
  settings_save(gconfig_getContext(), true);
  sleep_ms(100);  // Wait for the settings to be saved

  DPRINTF("Boot feature set to UPGRADER\n");

  DPRINTF("Resetting device\n");
  reset_device();

  while (1) {
    sleep_ms(1000);
  }
  DPRINTF("You should never reach this point\n");
}

download_err_t appmngr_confirm_failed_download_firmware() {
  return DOWNLOAD_STATUS_FAILED;
}

void appmngr_init() {
  sdcard_info = (sdcard_info_t){false, 0, 0, false};
  app_info = (app_info_t){0};
  download_status = DOWNLOAD_STATUS_IDLE;
  download_firmware_status = DOWNLOAD_STATUS_IDLE;
  download_type = DOWNLOAD_TYPE_APP;
  request = (HTTPC_REQUEST_T){0};
}

void appmngr_deinit(void) {
  if (file.obj.fs) {
    f_close(&file);
  }
  request.tls_config = NULL;  // shared config, not ours to free

  sdcard_info = (sdcard_info_t){false, 0, 0, false};
  app_info = (app_info_t){0};
  download_status = DOWNLOAD_STATUS_IDLE;
  download_firmware_status = DOWNLOAD_STATUS_IDLE;
  download_type = DOWNLOAD_TYPE_APP;
  request = (HTTPC_REQUEST_T){0};
}
