#include "version.h"

#include <ctype.h>

#include "pico/rand.h"
#include "urlparse.h"
#include "vercmp.h"

download_version_t status = DOWNLOAD_VERSION_IDLE;
static char version_string[64] = {0};
static size_t version_buf_used = 0;  // <— track appended bytes

// Persistent storage for async HTTP request strings to avoid dangling pointers
static char request_host_buf[256];
static char request_uri_buf[512];
static HTTPC_REQUEST_T request = {0};

// ------------------- helpers -------------------
static int strncasecmp_ascii(const char *a, const char *b, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    unsigned char ca = (unsigned char)a[i];
    unsigned char cb = (unsigned char)b[i];
    if (!ca || !cb) return tolower(ca) - tolower(cb);
    int d = tolower(ca) - tolower(cb);
    if (d) return d;
  }
  return 0;
}

// ------------------- HTTP callbacks -------------------

// Receive body → append to version_string (no malloc; safe on multiple chunks)
static err_t http_client_receive_version_fn(__unused void *arg,
                                            __unused struct altcp_pcb *conn,
                                            struct pbuf *p, err_t err) {
  if (p == NULL) {
    // remote end closed; result callback will follow
    return ERR_OK;
  }
  if (err != ERR_OK) {
    DPRINTF("Error receiving data: %d\n", err);
    status = DOWNLOAD_VERSION_FAILED;
    pbuf_free(p);
    return err;
  }

  // How many bytes can we still store (reserve 1 byte for NUL)
  size_t cap = sizeof(version_string) - 1u - version_buf_used;
  if (cap > 0) {
    // Copy up to 'cap' bytes from the pbuf chain
    u16_t to_copy = (u16_t)((p->tot_len < cap) ? p->tot_len : cap);
    if (to_copy) {
      pbuf_copy_partial(p, version_string + version_buf_used, to_copy, 0);
      version_buf_used += to_copy;
      version_string[version_buf_used] = '\0';
    }
  }
  // Acknowledge all received bytes even if we truncated locally
  // ALTCP is always enabled; altcp_recved is correct for plain http too
  // (an http connection is just an ALTCP layer with no TLS attached).
  altcp_recved(conn, p->tot_len);
  pbuf_free(p);

  // Still in progress until result callback says done
  if (status == DOWNLOAD_VERSION_START || status == DOWNLOAD_VERSION_IDLE) {
    status = DOWNLOAD_VERSION_IN_PROGRESS;
  }
  return ERR_OK;
}

// Parse headers → enforce a small Content-Length that fits our buffer
static err_t http_client_header_check_size_fn(
    __unused httpc_state_t *connection, __unused void *arg, struct pbuf *hdr,
    u16_t hdr_len, __unused u32_t content_len) {
  // Copy header into a small stack buffer if possible; otherwise clamp
  char buf[512];
  u16_t copy = hdr_len < sizeof(buf) - 1 ? hdr_len : (sizeof(buf) - 1);
  pbuf_copy_partial(hdr, buf, copy, 0);
  buf[copy] = '\0';

  // Case-insensitive search for "Content-Length:"
  const char *label = "Content-Length:";
  char *p = buf;
  size_t max_len = sizeof(version_string) - 1;

  while (p && *p) {
    // find end of line
    char *eol = strstr(p, "\r\n");
    size_t line_len = eol ? (size_t)(eol - p) : strlen(p);

    if (line_len >= strlen(label) &&
        strncasecmp_ascii(p, label, strlen(label)) == 0) {
      const char *num = p + strlen(label);
      while (*num == ' ' || *num == '\t') ++num;
      unsigned long cl = strtoul(num, NULL, 10);
      if (cl > max_len) {
        DPRINTF("Content-Length too large: %lu > %u\n", cl, (unsigned)max_len);
        status = DOWNLOAD_VERSION_FAILED;
        return ERR_VAL;  // refuse large bodies
      }
      break;  // ok
    }
    p = eol ? (eol + 2) : NULL;
  }

  // Mark we’re ready to receive body
  if (status == DOWNLOAD_VERSION_START) status = DOWNLOAD_VERSION_IN_PROGRESS;
  return ERR_OK;
}

static void http_client_result_complete_fn(void *arg,
                                           httpc_result_t httpc_result,
                                           u32_t rx_content_len, u32_t srv_res,
                                           err_t err) {
  HTTPC_REQUEST_T *req = (HTTPC_REQUEST_T *)arg;
  DPRINTF("Request complete: result %d len %u server_response %u err %d\n",
          httpc_result, rx_content_len, srv_res, err);
  req->complete = true;

  // Shared process-wide config; just drop the reference, never free it.
  req->tls_config = NULL;

  if (err == ERR_OK && srv_res == 200) {
    status = DOWNLOAD_VERSION_COMPLETED;
  } else {
    status = DOWNLOAD_VERSION_FAILED;
  }
}

// ------------------- driver code -------------------

static download_version_t version_start_download(const char *url) {
  // reset accumulation buffer
  version_buf_used = 0;
  version_string[0] = '\0';

  url_components_t components;

  if (!url || !*url) {
    // No URL: use built-in version
    snprintf(version_string, sizeof(version_string), "%s", RELEASE_VERSION);
    status = DOWNLOAD_VERSION_DEFAULT;
    return status;
  }
  if (urlparse_parse(url, &components) != 0) {
    DPRINTF("Error parsing URL: %s\n", url ? url : "(null)");
    snprintf(version_string, sizeof(version_string), "%s", RELEASE_VERSION);
    status = DOWNLOAD_VERSION_CANNOTPARSEURL;
    return status;
  }
  status = DOWNLOAD_VERSION_START;

  // Persist host and URI (components are on stack)
  snprintf(request_host_buf, sizeof(request_host_buf), "%s", components.host);
  snprintf(request_uri_buf, sizeof(request_uri_buf), "%s", components.uri);
  request.hostname = request_host_buf;
  request.url = request_uri_buf;
  request.complete = false;

  request.callback_arg = &request;
  request.headers_fn = http_client_header_check_size_fn;
  request.recv_fn = http_client_receive_version_fn;
  request.result_fn = http_client_result_complete_fn;

  // The version check is ALWAYS fetched over https, even when the URL says
  // http://. SIDECART_BASE_URL is already https as of v2.3.0, so this is
  // normally a no-op; it stays as a guard for a compile-time override or any
  // future settings-provided URL that arrives as cleartext. The scheme is
  // still parsed first so a genuinely unsupported one is rejected rather than
  // upgraded.
  int use_https = httpc_scheme_is_https(components.protocol);
  if (use_https < 0) {
    DPRINTF("Unsupported URL scheme: %s\n", components.protocol);
    status = DOWNLOAD_VERSION_FAILED;
    return status;
  }

  request.port = components.port;  // 0 lets httpc pick 80 or 443

  if (!use_https) {
    DPRINTF("Forcing version check to https (URL said %s)\n",
            components.protocol);
    // An http URL carrying no port, or an explicit :80, would otherwise send
    // the TLS handshake to the cleartext port. Fall back to the scheme
    // default so httpc picks 443. A non-default explicit port is honoured:
    // that is a deliberate endpoint choice by whoever set the URL.
    if (request.port == 0 || request.port == 80) {
      request.port = 0;
    }
  }

  // Always https past this point, by the force above.
  request.tls_config = httpc_shared_tls_config();
  if (request.tls_config == NULL) {
    DPRINTF("Cannot initialize HTTPS\n");
    status = DOWNLOAD_VERSION_FAILED;
    return status;
  }
  DPRINTF("Download with HTTPS\n");

  int rc = http_client_request_async(cyw43_arch_async_context(), &request);
  if (rc != 0) {
    DPRINTF("http_client_request_async failed: %d\n", rc);
    request.tls_config = NULL;  // shared config, not ours to free
    status = DOWNLOAD_VERSION_FAILED;
    return status;
  }
  status = DOWNLOAD_VERSION_IN_PROGRESS;
  return status;
}

static download_version_poll_t version_poll_download(void) {
  if (!request.complete) {
    async_context_poll(cyw43_arch_async_context());
    async_context_wait_for_work_ms(cyw43_arch_async_context(),
                                   VERSION_POLL_INTERVAL_MS);
    return DOWNLOAD_VERSION_POLL_CONTINUE;
  }
  return DOWNLOAD_VERSION_POLL_COMPLETED;
}

static download_version_t version_finish_download(void) {
  DPRINTF("Download finished with status=%d, content=\"%s\"\n", status,
          version_string);
  return status;
}

void version_loop(void) {
  if (request.url != NULL && !request.complete) {
    // A request that timed out is still owned by lwIP, which closes it on its
    // own timeout and then calls back into it: it must not be reused yet.
    DPRINTF("Previous version request still open\n");
    status = DOWNLOAD_VERSION_FAILED;
    return;
  }
  status = DOWNLOAD_VERSION_IDLE;
  request = (HTTPC_REQUEST_T){0};
  version_buf_used = 0;
  version_string[0] = '\0';

  // A stale copy from the CDN would hide a release that exists.
  static char fresh[URLPARSE_FRESH_URL_MAX];
  download_version_t start_status =
      version_start_download(urlparse_addCacheBuster(VERSION_URL, get_rand_32(),
                                                     fresh, sizeof(fresh)) == 0
                                 ? fresh
                                 : VERSION_URL);
  if (start_status == DOWNLOAD_VERSION_DEFAULT ||
      start_status == DOWNLOAD_VERSION_CANNOTPARSEURL ||
      start_status == DOWNLOAD_VERSION_FAILED) {
    request.complete = true;  // never sent: nothing will call back into it
    return;
  }

  // Real time: a poll pass can take much longer than its interval.
  absolute_time_t deadline = make_timeout_time_ms(VERSION_DOWNLOAD_TIMEOUT_MS);
  while (version_poll_download() == DOWNLOAD_VERSION_POLL_CONTINUE) {
    if (status == DOWNLOAD_VERSION_FAILED) break;
    if (absolute_time_diff_us(get_absolute_time(), deadline) < 0) {
      DPRINTF("VERSION download timed out after %u ms\n",
              (unsigned)VERSION_DOWNLOAD_TIMEOUT_MS);
      status = DOWNLOAD_VERSION_FAILED;
      break;
    }
  }

  // The request is left as it is: if it timed out, lwIP still calls back
  // into it until it closes the connection.
  (void)version_finish_download();
}

download_version_t version_get_status(void) { return status; }

// The fetched body counts only when the request finished with a 200 and the
// body is a version: an error page, a captive portal's login page or a body
// cut short by the timeout never shows up as the latest version.
static bool version_fetched(void) {
  return status == DOWNLOAD_VERSION_COMPLETED &&
         vercmp_isVersion(version_string);
}

const char *version_get_string(void) {
  return version_fetched() ? version_string : RELEASE_VERSION;
}

bool version_isNewer(void) {
  return version_fetched() && vercmp_isNewer(version_string, RELEASE_VERSION);
}
