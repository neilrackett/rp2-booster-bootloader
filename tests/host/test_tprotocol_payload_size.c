// units:
/* The parser drops a frame whose payload size is larger than its buffer
 * (MAX_PROTOCOL_PAYLOAD_SIZE) instead of writing past it, and the next frame
 * still works. Any program that reads ROM3 sequentially sends one: its reads
 * are $ABCD, $ABCE, $ABCF, a header, a command and a size of 43,983 bytes.
 * The parser runs in the DMA interrupt, so on the device the write would land
 * in whatever follows the buffer in RAM; under the address sanitizer it fails
 * the test. */
#include "test.h"
#include "tprotocol.h"

host_timer_hw_t host_timer;

static int commands;
static uint16_t lastSize;
static uint16_t lastWord;

static void onCommand(const TransmissionProtocol *p) {
  commands++;
  lastSize = p->payload_size;
  if (p->payload_size >= 2) {
    lastWord = (uint16_t)(p->payload[p->payload_size - 2] |
                          (p->payload[p->payload_size - 1] << 8));
  }
}
static void onChecksumError(const TransmissionProtocol *p) { (void)p; }

static void pause(void) {
  host_timer.timerawl += PROTOCOL_READ_RESTART_MICROSECONDS + 1;
}

static void sendWord(uint16_t word) {
  host_timer.timerawl += 3;
  tprotocol_parse(word, onCommand, onChecksumError);
}

// A complete frame: header, command, size, size / 2 payload words counting
// up from 1, and the checksum.
static void sendFrame(uint16_t command, uint16_t size) {
  pause();
  uint16_t sum = (uint16_t)(command + size);
  sendWord(PROTOCOL_HEADER);
  sendWord(command);
  sendWord(size);
  for (uint16_t i = 1; i <= size / 2; i++) {
    sendWord(i);
    sum = (uint16_t)(sum + i);
  }
  sendWord(sum);
}

int main(void) {
  // A sequential read of ROM3 from $FB0000 + $ABCD: every word is the next.
  pause();
  uint16_t word = PROTOCOL_HEADER;
  for (int i = 0; i < 30000; i++) {
    sendWord(word++);
  }
  CHECK_EQ(commands, 0);
  CHECK_EQ(tprotocol_oversized_frames, 1);

  // The largest payload the buffer holds is accepted whole.
  sendFrame(1, MAX_PROTOCOL_PAYLOAD_SIZE);
  CHECK_EQ(commands, 1);
  CHECK_EQ(lastSize, MAX_PROTOCOL_PAYLOAD_SIZE);
  CHECK_EQ(lastWord, MAX_PROTOCOL_PAYLOAD_SIZE / 2);

  // One byte more, and the largest size there is, are dropped.
  sendFrame(2, MAX_PROTOCOL_PAYLOAD_SIZE + 1);
  sendFrame(3, 0xFFFF);
  CHECK_EQ(commands, 1);
  CHECK_EQ(tprotocol_oversized_frames, 3);

  // The next valid frame after a dropped one works.
  sendFrame(4, 4);
  CHECK_EQ(commands, 2);
  CHECK_EQ(lastSize, 4);
  CHECK_EQ(lastWord, 2);
  TEST_END();
}
