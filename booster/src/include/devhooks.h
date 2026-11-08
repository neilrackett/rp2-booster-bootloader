/**
 * File: devhooks.h
 * Author: Diego Parrilla Santamaría
 * Date: November 2026
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: Debug-only mailbox that host tools write over SWD, so they can
 *              drive the terminal without an Atari at the keyboard.
 *              tools/dev/swd.py is the host side. From
 * md-microfirmware-template.
 */

#ifndef DEVHOOKS_H
#define DEVHOOKS_H

// The mailbox is a struct in RAM found by its symbol, devhooksMailbox. The
// host writes kind, command_id, payload_size and payload, then sets seq to
// ack + 1. devhooks_poll(), called from the main loop, runs the request,
// stores the outcome in result and copies seq to ack.
//
//   DEVHOOKS_KIND_PROTOCOL  Inject a transmission-protocol command as if the
//                           ST had sent it: command_id, payload_size bytes of
//                           payload words (random token first). result is 1
//                           when it was queued, 0 when another command was
//                           pending (retry).
//   DEVHOOKS_KIND_APP       App-defined command. Booster defines none, so
//                           result is always 0.
//   DEVHOOKS_KIND_BUS       Booster only: feed the payload words to the
//                           protocol parser as if the ST had read them from
//                           ROM3, header and checksum included, with
//                           interrupts off. command_id is ignored. result is
//                           the parser's count of dropped oversized frames.
//
// Include this header in one file only (it holds the definitions, as weak
// symbols) and call devhooks_poll() from the main loop. In release builds the
// call compiles to nothing.

#include <stdbool.h>
#include <stdint.h>

#define DEVHOOKS_MAGIC 0x444B4831u  // "DKH1"
#define DEVHOOKS_KIND_PROTOCOL 1u
#define DEVHOOKS_KIND_APP 2u
#define DEVHOOKS_KIND_BUS 3u
#define DEVHOOKS_PAYLOAD_WORDS 16u

#if defined(_DEBUG) && (_DEBUG != 0)

#include "term.h"

typedef struct {
  uint32_t magic;
  uint32_t seq;
  uint32_t ack;
  uint32_t kind;
  uint32_t result;
  uint16_t command_id;
  uint16_t payload_size;  // bytes
  uint16_t payload[DEVHOOKS_PAYLOAD_WORDS];
} DevhooksMailbox;

__attribute__((weak, used)) DevhooksMailbox devhooksMailbox = {
    .magic = DEVHOOKS_MAGIC};

__attribute__((weak, noinline)) void devhooks_poll(void) {
  DevhooksMailbox *m = &devhooksMailbox;
  uint32_t seq = m->seq;
  if (seq == m->ack) {
    return;
  }
  uint16_t size = m->payload_size;
  if (size > DEVHOOKS_PAYLOAD_WORDS * 2u) {
    size = DEVHOOKS_PAYLOAD_WORDS * 2u;
  }
  uint32_t result = 0;
  if (m->kind == DEVHOOKS_KIND_PROTOCOL) {
    result = term_injectProtocol(m->command_id, m->payload, size) ? 1u : 0u;
  } else if (m->kind == DEVHOOKS_KIND_BUS) {
    result = term_injectBusWords(m->payload, size / 2u);
  }
  m->result = result;
  m->ack = seq;
}

#else

#define devhooks_poll() ((void)0)

#endif  // _DEBUG

#endif  // DEVHOOKS_H
