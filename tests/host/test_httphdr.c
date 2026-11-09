// units: booster/src/httphdr.c
/* httphdr_find() and httphdr_is_chunked(): the headers a download reads
 * from a response, as servers and CDNs send them. */
#include <string.h>

#include "httphdr.h"
#include "test.h"

int main(void) {
  const char *redirect =
      "HTTP/1.1 302 Found\r\n"
      "Content-Type: text/html\r\n"
      "location: https://objects.githubusercontent.com/x?a=1\r\n"
      "X-Note: Location: https://not-this-one\r\n"
      "\r\n";
  char value[128];
  CHECK(httphdr_find(redirect, "Location:", value, sizeof(value)));
  CHECK(strcmp(value, "https://objects.githubusercontent.com/x?a=1") == 0);
  // A name inside another header's value is not a header.
  CHECK(httphdr_find("X-Note: Location: x\r\n", "Location:", value,
                     sizeof(value)) == false);
  // Absent, empty, or longer than the buffer.
  CHECK(!httphdr_find(redirect, "Content-Length:", value, sizeof(value)));
  CHECK(!httphdr_find("Location:   \r\n", "Location:", value, sizeof(value)));
  char tiny[8];
  CHECK(!httphdr_find(redirect, "Location:", tiny, sizeof(tiny)));
  // The last line need not end with CRLF.
  CHECK(httphdr_find("A: 1\r\nB:\t2", "B:", value, sizeof(value)));
  CHECK(strcmp(value, "2") == 0);

  CHECK(
      httphdr_is_chunked("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n"));
  CHECK(
      httphdr_is_chunked("HTTP/1.1 200 OK\r\ntransfer-encoding: Chunked\r\n"));
  CHECK(httphdr_is_chunked(
      "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip, chunked\r\n"));
  CHECK(!httphdr_is_chunked("HTTP/1.1 200 OK\r\nContent-Length: 1024\r\n"));
  CHECK(!httphdr_is_chunked(
      "HTTP/1.1 200 OK\r\nTransfer-Encoding: identity\r\n"));
  CHECK(!httphdr_is_chunked("HTTP/1.1 200 OK\r\nX-Info: chunked\r\n"));
  TEST_END();
}
