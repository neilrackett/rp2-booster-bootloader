// units:
/* A frame's payload buffer holds MAX_PROTOCOL_PAYLOAD_SIZE bytes, 2,112. */
#include "test.h"
#include "tprotocol.h"

int main(void) {
  CHECK_EQ(sizeof(((TransmissionProtocol *)0)->payload), 2112);
  CHECK_EQ(sizeof(((TransmissionProtocol *)0)->payload),
           MAX_PROTOCOL_PAYLOAD_SIZE);
  TEST_END();
}
