#include "select.h"

static reset_callback_t __not_in_flash_func(reset_cb) = NULL;
static reset_callback_t __not_in_flash_func(reset_long_cb) = NULL;

static bool __not_in_flash_func(select_is_stable_state)(bool pressed_state) {
  uint32_t stable_ms = 0;

  while (stable_ms < SELECT_DEBOUNCE_MS) {
    if (select_detectPush() != pressed_state) {
      return false;
    }

    tight_loop_contents();
    sleep_ms(SELECT_LOOP_DELAY);
    stable_ms += SELECT_LOOP_DELAY;
  }

  return true;
}

// True while core 1 is running the watcher. Read by the lockout helpers so
// they no-op when core 1 has been stopped (app launch path).
static volatile bool s_core1_watcher_active = false;

static void __not_in_flash_func(select_wait_for_press_loop)(void) {
  // Make this core lockout-able BEFORE anything else: core 0 must be able to
  // park us in the SDK's RAM-resident lockout handler while it erases or
  // programs flash. This loop is __not_in_flash_func, but that is not enough
  // on its own -- sleep_ms and select_detectPush are flash-resident, so
  // without the lockout this core fetches from flash while core 0 has XIP
  // disabled, double-faults, and locks up.
  multicore_lockout_victim_init();
  while (true) {
    DPRINTF("Waiting for SELECT button to be pushed\n");
    while (!select_detectPush()) {
      tight_loop_contents();
      sleep_ms(SELECT_LOOP_DELAY);
    }

    if (!select_is_stable_state(true)) {
      DPRINTF("Ignoring unstable SELECT press\n");
      continue;
    }

    DPRINTF("SELECT button pushed!\n");
    select_waitPush();
  }
}

void __not_in_flash_func(select_waitPush)() {
  DPRINTF("Waiting for SELECT button to be released\n");
  uint64_t press_start_us = time_us_64();
  bool long_press_handled = false;

  while (true) {
    tight_loop_contents();
    sleep_ms(SELECT_LOOP_DELAY);

    if (select_detectPush()) {
      if (!long_press_handled &&
          (time_us_64() - press_start_us >=
           ((uint64_t)SELECT_LONG_RESET * 1000ULL))) {
        DPRINTF("Long press detected. Executing long reset callback\n");
        long_press_handled = true;
        if (reset_long_cb != NULL) {
          reset_long_cb();
        }
      }
      continue;
    }

    if (!select_is_stable_state(false)) {
      continue;
    }

    DPRINTF("SELECT button released after %llu ms\n",
            (unsigned long long)((time_us_64() - press_start_us) / 1000ULL));
    if (!long_press_handled && reset_cb != NULL) {
      DPRINTF("Short press detected. Executing reset callback\n");
      reset_cb();
    }
    break;
  }

  DPRINTF("SELECT button callback returned!\n");
}

void select_configure() {
  // Configure the input ping for SELECT button
  gpio_init(SELECT_GPIO);
  gpio_set_dir(SELECT_GPIO, GPIO_IN);
  gpio_set_pulls(SELECT_GPIO, false, true);  // Pull down (false, true)
  gpio_pull_down(SELECT_GPIO);
}

bool select_detectPush() { return (gpio_get(SELECT_GPIO) != 0); }

void select_coreWaitPush(reset_callback_t reset, reset_callback_t resetLong) {
  DPRINTF("Launching core 1 to wait for SELECT button push\n");
  reset_cb = reset;
  reset_long_cb = resetLong;
  multicore_launch_core1(select_wait_for_press_loop);
  // The watcher runs victim-init as its first statement; the microseconds
  // between this store and that init are safe because the first flash write
  // happens much later in boot.
  s_core1_watcher_active = true;
}

void select_coreWaitPushDisable() {
  DPRINTF("Disabling core 1\n");
  // Park core 1 before resetting it. It spends its time in sleep_ms(), which
  // briefly takes the alarm pool's hardware spin lock; a reset that lands
  // there leaves the lock taken for good, and core 0 then hangs at its next
  // timer operation (seen in network_deInit() during a launch). The lockout
  // handler only runs once core 1 has released its spin locks (they are held
  // with interrupts off), so once the lockout is acknowledged core 1 holds
  // none and the reset is safe.
  if (s_core1_watcher_active) {
    multicore_lockout_start_blocking();
  }
  s_core1_watcher_active = false;
  multicore_reset_core1();
}

void select_flashLockoutBegin(void) {
  if (s_core1_watcher_active) {
    multicore_lockout_start_blocking();
  }
}

void select_flashLockoutEnd(void) {
  if (s_core1_watcher_active) {
    multicore_lockout_end_blocking();
  }
}

void select_checkPushReset() {
  if (select_detectPush()) {
    DPRINTF("SELECT button pushed. Resetting the device\n");
    if (reset_cb != NULL) {
      DPRINTF("SELECT button pushed. Executing reset callback\n");
      reset_cb();
    }
  }
}

void select_setResetCallback(reset_callback_t reset) { reset_cb = reset; }
void select_setLongResetCallback(reset_callback_t resetLong) {
  reset_long_cb = resetLong;
}
