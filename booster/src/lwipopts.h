#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

// Common settings used in most of the pico_w examples
// (see https://www.nongnu.org/lwip/2_1_x/group__lwip__opts.html for details)

// allow override in some examples
#ifndef NO_SYS
#define NO_SYS 1
#endif

// lwIP's statistics cost roughly 3KB of flash and do not fit alongside the
// rest of a debug build, so they are opt-in. Set to 1 (here or as a compile
// definition) to bring back stats_display() on a failed download -- the
// instrument that identified the silent send-heap exhaustion documented at
// MEM_SIZE below. Debug builds only; ignored in release.
#ifndef BOOSTER_LWIP_STATS
#define BOOSTER_LWIP_STATS 0
#endif

// allow override in some examples
#ifndef LWIP_SOCKET
#define LWIP_SOCKET 0
#endif
#if PICO_CYW43_ARCH_POLL
#define MEM_LIBC_MALLOC 1
#else
// MEM_LIBC_MALLOC is incompatible with non polling versions
#define MEM_LIBC_MALLOC 0
#endif

#define MEM_ALIGNMENT 4
// lwIP's own heap (static bss, separate from the C heap where mbedTLS lives).
// This holds outgoing data, and an allocation failure here is SILENT: mem_malloc
// returns NULL, tcp_write transmits nothing, the peer never answers, and the
// connection dies 15s later on the poll timeout as result 5 / server_response 0
// / err 0, with no error reported anywhere. That was the cause of the redirect
// download failures, measured as MEM HEAP .err 353 == TCP .memerr 353 at 12288.
// Measured peak is 8,196 once TCP_SND_BUF was halved, so 16384 is ~2x headroom.
// Paid for by cutting PBUF_POOL_SIZE below: this heap and the C heap that
// mbedTLS allocates from compete for the same RAM, which is why an earlier
// attempt to raise it without freeing pool memory panicked the TLS handshake.
#define MEM_SIZE 16384

#if defined(_DEBUG) && (_DEBUG != 0)
#define MEM_SANITY_CHECK 1
#define MEM_OVERFLOW_CHECK 2
#else
#define MEM_SANITY_CHECK 0
#define MEM_OVERFLOW_CHECK 0
#endif

#define MEMP_NUM_PBUF 32
// NOTE: MEMP_NUM_TCP_PCB is defined further down, in the "Custom flags"
// section, and that later definition is the effective one. A dead
// "#define MEMP_NUM_TCP_PCB 10" used to sit here and was pure trap value --
// the stats dump reported avail 12, matching the lower definition, not 10.
#define MEMP_NUM_TCP_SEG 32
#define MEMP_NUM_ARP_QUEUE 10
// Down from 32, which cost 49,027 bytes of bss -- 37% of the whole RAM budget
// -- and starved both heaps. lwIP's sanity check sets the floor: TCP_WND
// (20*MSS = 29,200) must fit in PBUF_POOL_SIZE * (PBUF_POOL_BUFSIZE - headers),
// so 21 is the minimum. Measured peak use hit exactly 21, so 24 keeps headroom.
#define PBUF_POOL_SIZE 24
#define LWIP_ARP 1
#define LWIP_ETHERNET 1
#define LWIP_ICMP 1
#define LWIP_RAW 1
#define TCP_MSS 1460
// TLS peers send records up to 16KB, and altcp_tls only acks a record's bytes
// once the whole record has arrived, so the receive window must exceed a full
// record plus lwIP's window-update thresholds. With 12*MSS the sender stalls a
// few hundred bytes short of completing a 16KB record and the transfer dies in
// zero-window probes; 20*MSS leaves ~12KB of margin.
#define TCP_WND (20 * TCP_MSS)
// 4*MSS, halved from 8*MSS. This is the per-connection send buffer, drawn
// from MEM_SIZE above, so one connection could previously claim 11,680 of a
// 12,288 heap on its own. Downloads only ever send a request -- the largest
// is the ~1KB signed storage URL -- and static web pages are sent straight
// from flash without copying, so nothing needs the larger value.
#define TCP_SND_BUF (4 * TCP_MSS)

// #define TCP_WND (6 * TCP_MSS)
// #define TCP_SND_BUF (4 * TCP_MSS)

#define TCP_SND_QUEUELEN ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))
#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_LINK_CALLBACK 1
#define LWIP_NETIF_HOSTNAME 1
#define LWIP_NETCONN 0
// stats_display() needs these to report the "err" counters that reveal a
// silently failed allocation -- how the send-heap exhaustion above was finally
// identified. Debug builds only; they cost RAM and flash.
#if defined(_DEBUG) && (_DEBUG != 0) && (BOOSTER_LWIP_STATS != 0)
#define MEM_STATS 1
#define MEMP_STATS 1
#else
#define MEM_STATS 0
#define MEMP_STATS 0
#endif
#define LINK_STATS 0
#define SYS_STATS 0
// #define ETH_PAD_SIZE                2
#define LWIP_CHKSUM_ALGORITHM 3
#define LWIP_DHCP 1
#define LWIP_IPV4 1
#define LWIP_TCP 1
#define LWIP_UDP 1
#define LWIP_DNS 1
#define LWIP_TCP_KEEPALIVE 0
#define LWIP_NETIF_TX_SINGLE_PBUF 1
#define DHCP_DOES_ARP_CHECK 0
#define LWIP_DHCP_DOES_ACD_CHECK 0
#define LWIP_DHCP_GET_NTP_SRV 0

// Keyed off _DEBUG, NOT NDEBUG. Debug builds compile as MinSizeRel, which
// defines NDEBUG, so the old "#ifndef NDEBUG" left LWIP_STATS off in every
// build and silently compiled out stats_display() along with it.
#if defined(_DEBUG) && (_DEBUG != 0) && (BOOSTER_LWIP_STATS != 0)
#define LWIP_DEBUG 1
#define LWIP_STATS 1
#define LWIP_STATS_DISPLAY 1
#endif

#define ETHARP_DEBUG LWIP_DBG_OFF
#define NETIF_DEBUG LWIP_DBG_OFF
#define PBUF_DEBUG LWIP_DBG_OFF
#define API_LIB_DEBUG LWIP_DBG_OFF
#define API_MSG_DEBUG LWIP_DBG_OFF
#define SOCKETS_DEBUG LWIP_DBG_OFF
#define ICMP_DEBUG LWIP_DBG_OFF
#define INET_DEBUG LWIP_DBG_OFF
#define IP_DEBUG LWIP_DBG_OFF
#define IP_REASS_DEBUG LWIP_DBG_OFF
#define RAW_DEBUG LWIP_DBG_OFF
#define MEM_DEBUG LWIP_DBG_OFF
#define MEMP_DEBUG LWIP_DBG_OFF
#define SYS_DEBUG LWIP_DBG_OFF
#define TCP_DEBUG LWIP_DBG_OFF
#define TCP_INPUT_DEBUG LWIP_DBG_OFF
#define TCP_OUTPUT_DEBUG LWIP_DBG_OFF
#define TCP_RTO_DEBUG LWIP_DBG_OFF
#define TCP_CWND_DEBUG LWIP_DBG_OFF
#define TCP_WND_DEBUG LWIP_DBG_OFF
#define TCP_FR_DEBUG LWIP_DBG_OFF
#define TCP_QLEN_DEBUG LWIP_DBG_OFF
#define TCP_RST_DEBUG LWIP_DBG_OFF
#define UDP_DEBUG LWIP_DBG_OFF
#define TCPIP_DEBUG LWIP_DBG_OFF
#define PPP_DEBUG LWIP_DBG_OFF
#define SLIP_DEBUG LWIP_DBG_OFF
#define DHCP_DEBUG LWIP_DBG_OFF

// Custom flags

// The following is needed to test mDns
#define LWIP_MDNS_RESPONDER 1
#define LWIP_IGMP 1
#define LWIP_NUM_NETIF_CLIENT_DATA 1
#define MDNS_RESP_USENETIF_EXTCALLBACK 1
// #define MEMP_NUM_SYS_TIMEOUT (LWIP_NUM_SYS_TIMEOUT_INTERNAL + 3)
#define MEMP_NUM_SYS_TIMEOUT (32)
// 20. 12 was fully exhausted, and 16 still reached its ceiling (used 16 /
// max 16) after six 3-hop downloads plus the browser polling the status page.
#define MEMP_NUM_TCP_PCB 20

// TIME_WAIT is 2*TCP_MSL, so lwIP's 60s default pins a closed connection's pcb
// for two full minutes. A 3-hop redirect chain burns three pcbs per download
// and the web UI polls continuously, which fills the pool far faster than it
// drains; once full, every new connection silently evicts a TIME_WAIT pcb via
// tcp_alloc's fallback and no error counter records it. 10s (20s TIME_WAIT) is
// ample for the short round-trips this device sees and drains 6x faster.
#define TCP_MSL 10000

// #define TCP_FAST_INTERVAL 50
#define TCP_NODELAY 0

#define LWIP_NETIF_API \
  0  //  Not needed. Sequential API, and therefore for platforms with OSes only.
#define LWIP_SOCKET \
  0  //  Not needed. Sequential API, and therefore for platforms with OSes only.

#define LWIP_TIMERS 1  // Enable timers (needed for HTTPD)
#define LWIP_HTTPD 0
#define LWIP_HTTPD_SSI 1
#define LWIP_HTTPD_CGI 1
// don't include the tag comment - less work for the CPU, but may be harder to
// debug
#define LWIP_HTTPD_SSI_INCLUDE_TAG 0
#define LWIP_HTTPD_SSI_MULTIPART 1
#define LWIP_HTTPD_DYNAMIC_HEADERS 0
#define LWIP_HTTPD_SUPPORT_POST 1
#define LWIP_HTTPD_SUPPORT_11_KEEPALIVE 1

#define LWIP_HTTPD_FS_ASYNC_READ 1
#define HTTPD_POLL_INTERVAL 1
#define HTTPD_PRECALCULATED_CHECKSUM 1
#define HTTPD_USE_MEM_POOL 1

#define MEMP_NUM_PARALLEL_HTTPD_CONNS 4
#define MEMP_NUM_PARALLEL_HTTPD_SSI_CONNS 4

#define LWIP_HTTPD_ABORT_ON_CLOSE_MEM_ERROR 1

#define HTTPD_FSDATA_FILE "fsdata_srv.c"

// ALTCP + TLS are always compiled in. Plain http still works: an http request
// simply runs through ALTCP with no TLS layer attached, so one build serves
// both schemes and the transport is chosen per request from the URL.
#define LWIP_ALTCP 1
#define MEMP_NUM_ALTCP_PCB 10
#define LWIP_ALTCP_TLS 1
#define LWIP_ALTCP_TLS_MBEDTLS 1
// TRUST MODEL: encryption only, NOT authentication.
// VERIFY_NONE means no certificate chain is checked, so an HTTPS download is
// protected against passive eavesdropping but NOT against an active
// man-in-the-middle. Two things block verification today: there is no CA
// bundle on the device, and there is no wall clock
// (MBEDTLS_HAVE_TIME_DATE off) so certificate validity periods are uncheckable.
// Verifying would need a CA store for sidecartridge.com hosts, VERIFY_REQUIRED,
// and a time source. Do not describe this as "secure" in user-facing text.
#define ALTCP_MBEDTLS_AUTHMODE MBEDTLS_SSL_VERIFY_NONE
// #define ALTCP_MBEDTLS_DEBUG  LWIP_DBG_ON
// #define ALTCP_MBEDTLS_LIB_DEBUG LWIP_DBG_ON

// Note bug in lwip with LWIP_ALTCP and LWIP_DEBUG
// https://savannah.nongnu.org/bugs/index.php?62159
// #define LWIP_DEBUG 1
// #undef LWIP_DEBUG
// #define LWIP_DEBUG                  1
// #define MEMP_OVERFLOW_CHECK         2
// #define MEMP_SANITY_CHECK           1

#endif /* __LWIPOPTS_H__ */
