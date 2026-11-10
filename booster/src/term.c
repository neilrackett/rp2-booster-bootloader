/**
 * File: term.c
 * Author: Diego Parrilla Santamaría
 * Date: January 2025
 * Copyright: 2025 - GOODDATA LABS
 * Description: Online terminal
 */

#include "include/term.h"

#include <ctype.h>
#include <stdarg.h>

#include "appmngr.h"
#include "display_mngr.h"
#include "romemul.h"  // lookup_data_rom_dma_channel

#define TERM_MENU_TITLE "Downloaded apps\n"
#define TERM_MENU_INSTRUCTIONS "Type app number and press Enter\n"
#define TERM_MENU_RETURN_OPTION "0. Return to connection menu\n"
#define TERM_MENU_PROMPT "App #: "
// Apps listed per page: the 25-row screen also holds the three header lines,
// the page line, the prompt and one status line.
#define TERM_APPS_PER_PAGE 18

static TransmissionProtocol last_protocol;
// Set by the DMA interrupt when a command arrives, cleared by term_loop().
static volatile bool last_protocol_valid = false;
static bool term_active = false;

static uint32_t memory_shared_address = 0;
static uint32_t memory_random_token_address = 0;
static uint32_t memory_random_token_seed_address = 0;
static appmngr_installed_app_t installed_apps[APPMNGR_MAX_INSTALLED_APPS];
static uint16_t installed_app_count = 0;
static bool installed_apps_ready = false;
static uint16_t term_page = 0;

static uint16_t term_page_count(void) {
  return (uint16_t)((installed_app_count + TERM_APPS_PER_PAGE - 1) /
                    TERM_APPS_PER_PAGE);
}

static char screen[TERM_SCREEN_SIZE];
static uint8_t cursor_x = 0;
static uint8_t cursor_y = 0;
static uint8_t prev_cursor_x = 0;
static uint8_t prev_cursor_y = 0;
static char input_buffer[TERM_INPUT_BUFFER_SIZE];
static size_t input_length = 0;

/**
 * @brief Callback that handles the protocol command received.
 *
 * This callback copies the protocol into the last_protocol structure. The
 * last_protocol_valid flag is set to true to indicate that the structure
 * contains a valid command to be processed in the main loop.
 *
 * @param protocol The TransmissionProtocol structure containing the protocol
 * information.
 */
static void __not_in_flash_func(handle_protocol_command)(
    const TransmissionProtocol *protocol) {
  memcpy(&last_protocol, protocol, sizeof(TransmissionProtocol));
  last_protocol_valid = true;
}

static void __not_in_flash_func(handle_protocol_checksum_error)(
    const TransmissionProtocol *protocol) {
  DPRINTF("Checksum error detected (ID=%u, Size=%u)\n", protocol->command_id,
          protocol->payload_size);
}

#if defined(_DEBUG) && (_DEBUG != 0)
bool term_injectProtocol(uint16_t commandId, const uint16_t *payload,
                         uint16_t payloadSize) {
  if (last_protocol_valid || payloadSize > sizeof(last_protocol.payload)) {
    return false;
  }
  last_protocol.command_id = commandId;
  last_protocol.payload_size = payloadSize;
  last_protocol.bytes_read = payloadSize;
  memcpy(last_protocol.payload, payload, payloadSize);
  last_protocol_valid = true;
  return true;
}

uint32_t term_injectBusWords(const uint16_t *words, uint16_t count) {
  // Interrupts off, so the ROM3 interrupt cannot interleave its own words.
  uint32_t ints = save_and_disable_interrupts();
  for (uint16_t i = 0; i < count; i++) {
    tprotocol_parse(words[i], handle_protocol_command,
                    handle_protocol_checksum_error);
  }
  restore_interrupts(ints);
  return tprotocol_oversized_frames;
}
#endif

static void term_reset_input(void) {
  memset(input_buffer, 0, sizeof(input_buffer));
  input_length = 0;
}

static void term_leave_to_manager(void) {
  term_active = false;
  installed_app_count = 0;
  installed_apps_ready = false;
  term_reset_input();
  SEND_COMMAND_TO_DISPLAY(DISPLAY_COMMAND_NOP);
  display_mngr_redraw_current();
}

static uint16_t term_load_installed_apps(void) {
  return appmngr_get_installed_apps(installed_apps, APPMNGR_MAX_INSTALLED_APPS);
}

static void term_refresh_apps_cache(void) {
  installed_apps_ready = appmngr_get_sdcard_info()->ready;
  installed_app_count = installed_apps_ready ? term_load_installed_apps() : 0;
}

void __not_in_flash_func(term_dma_irq_handler_lookup)(void) {
  bool rom3_gpio = (1ul << ROM3_GPIO) & sio_hw->gpio_in;

  // Use the channel init_rom_emulator() actually claimed. This used to be a
  // hardcoded 2, which only worked by accident: COPY_FIRMWARE_TO_RAM_DMA leaked
  // a claimed DMA channel, so romemul's two claims landed on 1 and 2. Once that
  // copy could take the memcpy path (misaligned source, see memfunc.h) nothing
  // leaked channel 0 any more, romemul got 0 and 1, and this handler
  // acknowledged a channel that never fires -- so the real interrupt was never
  // cleared, re-fired forever, and the boot hung with no fault.
  dma_hw->ints1 = 1u << lookup_data_rom_dma_channel;

  if (!rom3_gpio) {
    uint16_t addr_lsb =
        dma_hw->ch[lookup_data_rom_dma_channel].al3_read_addr_trig ^ 0x8000;
    tprotocol_parse(addr_lsb, handle_protocol_command,
                    handle_protocol_checksum_error);
  }
}

void term_init(void) {
  memory_shared_address = (unsigned int)&__rom_in_ram_start__;
  memory_random_token_address =
      memory_shared_address + TERM_RANDOM_TOKEN_OFFSET;
  memory_random_token_seed_address =
      memory_shared_address + TERM_RANDON_TOKEN_SEED_OFFSET;
  SET_SHARED_VAR(TERM_HARDWARE_TYPE, 0, memory_shared_address,
                 TERM_SHARED_VARIABLES_OFFSET);
  SET_SHARED_VAR(TERM_HARDWARE_VERSION, 0, memory_shared_address,
                 TERM_SHARED_VARIABLES_OFFSET);

  srand(time(NULL));
  uint32_t new_random_seed_token = rand();
  TPROTO_SET_RANDOM_TOKEN(memory_random_token_seed_address,
                          new_random_seed_token);

  term_active = false;
  installed_app_count = 0;
  installed_apps_ready = false;
  term_reset_input();
}

bool term_isActive(void) { return term_active; }

static void term_clear_screen(void) {
  memset(screen, 0, TERM_SCREEN_SIZE);
  cursor_x = 0;
  cursor_y = 0;
  prev_cursor_x = 0;
  prev_cursor_y = 0;
  display_term_clear();
}

static void term_scroll_up(void) {
  memmove(screen, screen + TERM_SCREEN_SIZE_X,
          TERM_SCREEN_SIZE - TERM_SCREEN_SIZE_X);
  memset(screen + TERM_SCREEN_SIZE - TERM_SCREEN_SIZE_X, 0, TERM_SCREEN_SIZE_X);
  display_scrollup(TERM_DISPLAY_ROW_BYTES);
}

static void term_put_char(char c) {
  screen[cursor_y * TERM_SCREEN_SIZE_X + cursor_x] = c;
  display_term_char(cursor_x, cursor_y, c);
  cursor_x++;
  if (cursor_x >= TERM_SCREEN_SIZE_X) {
    cursor_x = 0;
    cursor_y++;
    if (cursor_y >= TERM_SCREEN_SIZE_Y) {
      term_scroll_up();
      cursor_y = TERM_SCREEN_SIZE_Y - 1;
    }
  }
}

static void term_render_char(char c) {
  display_term_char(prev_cursor_x, prev_cursor_y, ' ');
  if (c == '\n' || c == '\r') {
    cursor_x = 0;
    cursor_y++;
    if (cursor_y >= TERM_SCREEN_SIZE_Y) {
      term_scroll_up();
      cursor_y = TERM_SCREEN_SIZE_Y - 1;
    }
  } else {
    term_put_char(c);
  }

  display_term_cursor(cursor_x, cursor_y);
  prev_cursor_x = cursor_x;
  prev_cursor_y = cursor_y;
}

static void term_print_string(const char *str) {
  while (*str) {
    term_render_char(*str);
    str++;
  }
  display_term_refresh();
}

static void term_printf(const char *fmt, ...) {
  va_list args;
  char buffer[256];

  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  term_print_string(buffer);
}

static void term_print_prompt(void) { term_print_string(TERM_MENU_PROMPT); }

static void term_print_menu(void) {
  term_clear_screen();
  term_print_string(TERM_MENU_TITLE);
  term_print_string(TERM_MENU_INSTRUCTIONS);
  term_print_string(TERM_MENU_RETURN_OPTION);

  if (!installed_apps_ready) {
    term_print_string("SD card not ready\n");
  } else if (installed_app_count == 0) {
    term_print_string("No downloaded apps found\n");
  } else {
    // One page at a time, numbered across pages, so an app keeps its number
    // whichever page shows it.
    uint16_t pages = term_page_count();
    if (term_page >= pages) {
      term_page = (uint16_t)(pages - 1);
    }
    uint16_t first = (uint16_t)(term_page * TERM_APPS_PER_PAGE);
    uint16_t last = (uint16_t)(first + TERM_APPS_PER_PAGE);
    if (last > installed_app_count) {
      last = installed_app_count;
    }
    for (uint16_t i = first; i < last; i++) {
      if (installed_apps[i].version[0] != '\0') {
        term_printf("%u. %s (%s)\n", (unsigned)(i + 1), installed_apps[i].name,
                    installed_apps[i].version);
      } else {
        term_printf("%u. %s\n", (unsigned)(i + 1), installed_apps[i].name);
      }
    }
    if (pages > 1) {
      term_printf("Page %u/%u. N: next, P: previous\n",
                  (unsigned)(term_page + 1), (unsigned)pages);
    }
  }

  term_print_prompt();
}

static void term_enter_menu(void) {
  term_active = true;
  term_reset_input();
  term_refresh_apps_cache();
  term_page = 0;
  display_term_start(TERM_SCREEN_SIZE_X, TERM_SCREEN_SIZE_Y);
  term_print_menu();
  SEND_COMMAND_TO_DISPLAY(DISPLAY_COMMAND_TERM);
  DPRINTF("Send command to display: DISPLAY_COMMAND_TERM\n");
}

static void term_show_status(const char *status) {
  term_print_string(status);
  term_print_string("\n");
  term_print_prompt();
}

static void term_handle_selection(void) {
  char *endptr = NULL;
  long selected_index = 0;

  if (input_length == 0) {
    term_show_status("Enter an app number");
    return;
  }

  selected_index = strtol(input_buffer, &endptr, 10);
  term_reset_input();

  if (endptr == input_buffer || *endptr != '\0' || selected_index < 0) {
    term_show_status("Invalid app number");
    return;
  }

  if (selected_index == 0) {
    term_leave_to_manager();
    return;
  }

  if (!installed_apps_ready) {
    term_show_status("SD card not ready");
    return;
  }

  if (installed_app_count == 0) {
    term_show_status("No downloaded apps found");
    return;
  }

  // Compare the whole number: cast to 16 bits, 65536 would pass as 0.
  if (selected_index > installed_app_count) {
    term_show_status("Invalid app number");
    return;
  }

  appmngr_schedule_launch_app(installed_apps[selected_index - 1].uuid);
  term_leave_to_manager();
}

static void term_input_char(char c) {
  if (!term_active) {
    return;
  }

  if (c == '\b' || c == 0x7f) {
    display_term_char(prev_cursor_x, prev_cursor_y, ' ');

    if (input_length > 0) {
      input_length--;
      input_buffer[input_length] = '\0';

      if (cursor_x == 0) {
        if (cursor_y > 0) {
          cursor_y--;
          cursor_x = TERM_SCREEN_SIZE_X - 1;
        } else {
          display_term_refresh();
          return;
        }
      } else {
        cursor_x--;
      }

      screen[cursor_y * TERM_SCREEN_SIZE_X + cursor_x] = 0;
      display_term_char(cursor_x, cursor_y, ' ');
    }

    display_term_cursor(cursor_x, cursor_y);
    prev_cursor_x = cursor_x;
    prev_cursor_y = cursor_y;
    display_term_refresh();
    return;
  }

  if (c == '\n' || c == '\r') {
    term_render_char('\n');
    term_handle_selection();
    display_term_refresh();
    return;
  }

  // N and P turn the page while nothing has been typed.
  if (input_length == 0 && (c == 'n' || c == 'N' || c == 'p' || c == 'P')) {
    uint16_t pages = term_page_count();
    bool next = (c == 'n' || c == 'N');
    if (next && term_page + 1 < pages) {
      term_page++;
    } else if (!next && term_page > 0) {
      term_page--;
    } else {
      return;
    }
    term_print_menu();
    display_term_refresh();
    return;
  }

  if (!isdigit((unsigned char)c)) {
    return;
  }

  if (input_length < TERM_INPUT_BUFFER_SIZE - 1) {
    input_buffer[input_length++] = c;
    input_buffer[input_length] = '\0';
    term_render_char(c);
    display_term_refresh();
  }
}

bool __not_in_flash_func(term_loop)(void) {
  bool handled = last_protocol_valid;
  if (handled) {
    uint32_t random_token = TPROTO_GET_RANDOM_TOKEN(last_protocol.payload);
    uint16_t *payloadPtr = ((uint16_t *)(last_protocol).payload);

    DPRINTF(
        "Command ID: %d. Size: %d. Random token: 0x%08X, Checksum: 0x%04X\n",
        last_protocol.command_id, last_protocol.payload_size, random_token,
        last_protocol.final_checksum);

    TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);

    if ((last_protocol.payload_size > 4) &&
        (last_protocol.payload_size <= 20)) {
      DPRINTF("Payload D3: 0x%04X\n", TPROTO_GET_PAYLOAD_PARAM32(payloadPtr));
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);
    }
    if ((last_protocol.payload_size > 8) &&
        (last_protocol.payload_size <= 20)) {
      DPRINTF("Payload D4: 0x%04X\n", TPROTO_GET_PAYLOAD_PARAM32(payloadPtr));
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);
    }
    if ((last_protocol.payload_size > 12) &&
        (last_protocol.payload_size <= 20)) {
      DPRINTF("Payload D5: 0x%04X\n", TPROTO_GET_PAYLOAD_PARAM32(payloadPtr));
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);
    }
    if ((last_protocol.payload_size > 16) &&
        (last_protocol.payload_size <= 20)) {
      DPRINTF("Payload D6: 0x%04X\n", TPROTO_GET_PAYLOAD_PARAM32(payloadPtr));
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);
    }

    switch (last_protocol.command_id) {
      case APP_TERMINAL_START: {
        if (!term_active) {
          term_enter_menu();
        }
        break;
      }
      case APP_TERMINAL_KEYSTROKE: {
        uint16_t *payload = ((uint16_t *)(last_protocol).payload);
        TPROTO_NEXT32_PAYLOAD_PTR(payload);

        uint32_t payload32 = TPROTO_GET_PAYLOAD_PARAM32(payload);
        char keystroke = (char)(payload32 & 0xFF);
        uint8_t shift_key = (payload32 & 0xFF000000) >> 24;
        uint8_t scan_code = (payload32 & 0xFF0000) >> 16;

        if (keystroke >= 0x20 && keystroke <= 0x7E) {
          DPRINTF("Keystroke: %c. Shift key: %d, Scan code: %d\n", keystroke,
                  shift_key, scan_code);
        } else {
          DPRINTF("Keystroke: %d. Shift key: %d, Scan code: %d\n", keystroke,
                  shift_key, scan_code);
        }

        term_input_char(keystroke);
        break;
      }
      default:
        DPRINTF("Unknown command\n");
        break;
    }

    // Clear the flag before the token goes out: the token tells the Atari to
    // send its next command, which can arrive before this function returns.
    // Cleared afterwards, that command would be dropped and the Atari would
    // wait for a token that never comes.
    last_protocol_valid = false;
    if (memory_random_token_address != 0) {
      TPROTO_SET_RANDOM_TOKEN(memory_random_token_address, random_token);

      uint32_t new_random_seed_token = rand();
      TPROTO_SET_RANDOM_TOKEN(memory_random_token_seed_address,
                              new_random_seed_token);
    }
  }
  return handled;
}
