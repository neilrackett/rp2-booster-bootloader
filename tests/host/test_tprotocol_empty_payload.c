// units:
/* A frame with no payload reports a payload size of 0, not the previous
 * frame's size. */
#include "test.h"
#include "tprotocol.h"

host_timer_hw_t host_timer;

static uint16_t lastSize;

static void onCommand(const TransmissionProtocol *p) {
  lastSize = p->payload_size;
}
static void onChecksumError(const TransmissionProtocol *p) { (void)p; }

static void sendFrame(const uint16_t *words, size_t count) {
  host_timer.timerawl += PROTOCOL_READ_RESTART_MICROSECONDS + 1;
  for (size_t i = 0; i < count; i++) {
    host_timer.timerawl += 3;
    tprotocol_parse(words[i], onCommand, onChecksumError);
  }
}

int main(void) {
  // header, command 1, 4 bytes of payload, checksum
  const uint16_t withPayload[] = {
      PROTOCOL_HEADER, 1,      4,
      0x1111,          0x2222, (uint16_t)(1 + 4 + 0x1111 + 0x2222)};
  sendFrame(withPayload, 6);
  CHECK_EQ(lastSize, 4);

  // header, command 2, no payload, checksum
  const uint16_t empty[] = {PROTOCOL_HEADER, 2, 0, 2};
  sendFrame(empty, 4);
  CHECK_EQ(lastSize, 0);
  TEST_END();
}
