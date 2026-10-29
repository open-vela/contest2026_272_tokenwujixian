/****************************************************************************
 * board/bk7258-devkit/src/bk7258_status_led.c
 *
 * Two-pin status indicator driven by the remote-ctrl observer plug.
 *
 *   GPIO40  red    a person has to act:   solid = a request is waiting,
 *                                          slow blink = last turn failed
 *   GPIO41  green  progress:              slow blink = idle, fast = working,
 *                                          solid 3 s = done
 *
 * The split keeps the two colours from ever fighting for meaning: green is
 * "how is it going", red is "come and look".  Red and green are never on
 * together.
 *
 * This module registers as a remote_ctrl observer and knows nothing about
 * the channel beyond the state vocabulary it is handed.  A product with a
 * screen would register a different observer and leave this one alone.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "board.h"
#include "include/bk7258_gpio.h"

/* The remote_ctrl observer surface.  Declared here rather than included:
 * the board layer must not reach into an application package's internals.
 */

#ifdef CONFIG_AI_AGENT_REMOTE_CTRL
struct remote_observer_ops_s
{
  const char *name;
  void (*on_state)(const char *text, void *ctx);
  void (*on_reply)(const char *text, void *ctx);
  void *ctx;
};

int remote_ctrl_register_observer(const struct remote_observer_ops_s *ops);
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* GPIO40 is the red half and GPIO41 the green one.  The slow green blink
 * seen at boot is this module's IDLE pattern on GPIO41, not the retired
 * heartbeat; the red flicker that used to appear over it was the old
 * heartbeat writing GPIO40 once a second from a second owner.
 */

#define LED_RED_PIN       40
#define LED_GREEN_PIN     41
#define LED_ACTIVE_HIGH   1

#define BLINK_SLOW_MS     1000
#define BLINK_FAST_MS     300
#define DONE_HOLD_MS      3000
#define TICK_MS           50

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Two colours, two shapes: green is "the board is yours", red is "a human
 * is needed".  Solid = settled, blinking = moving -- so idle is solid and
 * working blinks, and the red pair follows the same rule: approval waits
 * solid, an error blinks.
 */
enum led_phase_e
{
  PHASE_IDLE = 0,   /* green solid */
  PHASE_WORKING,    /* green blink */
  PHASE_APPROVAL,   /* red solid */
  PHASE_DONE,       /* green fast blink for DONE_HOLD_MS, then IDLE */
  PHASE_ERROR,      /* red blink */
};

static volatile enum led_phase_e s_phase = PHASE_IDLE;
static uint32_t s_phase_left_ms;

static void led_write(unsigned int pin, bool on)
{
  bool high = LED_ACTIVE_HIGH ? on : !on;

  bk7258_gpio_write(pin, high);
}

static const char *phase_name(enum led_phase_e p)
{
  switch (p)
    {
      case PHASE_IDLE:    return "IDLE";
      case PHASE_WORKING: return "WORKING";
      case PHASE_APPROVAL:return "APPROVAL";
      case PHASE_DONE:    return "DONE";
      case PHASE_ERROR:   return "ERROR";
    }
  return "?";
}

static void led_phase_enter(enum led_phase_e p)
{
  if (p != s_phase)
    {
      printf("[led] %s -> %s\n", phase_name(s_phase), phase_name(p));
    }

  s_phase = p;
  s_phase_left_ms = (p == PHASE_DONE) ? DONE_HOLD_MS : 0;
}

/* Classify the bridge's free-form status text into a phase.  The strings
 * are the vocabulary remote_ctrl forwards; matching on the two that a
 * person has to react to and treating the rest as progress keeps this
 * robust when the bridge adds new lines.
 */

#ifdef CONFIG_AI_AGENT_REMOTE_CTRL
static void led_on_state(const char *text, void *ctx)
{
  (void)ctx;

  if (text == NULL)
    {
      return;
    }

  /* DONE is sticky: the reply is followed by more status text
   * ("Agent turn completed", the mirrored reply line), and each of those
   * would otherwise knock a finished turn back into WORKING and eat the
   * three seconds of green the caller is meant to see.
   */

  if (s_phase == PHASE_DONE)
    {
      return;
    }

  if (strncmp(text, "approval required", 17) == 0)
    {
      printf("[led] by approval: %.60s\n", text);
      led_phase_enter(PHASE_APPROVAL);
    }
  else if (strncmp(text, "Agent turn failed", 17) == 0)
    {
      printf("[led] by failed: %.60s\n", text);
      led_phase_enter(PHASE_ERROR);
    }
  else if (strncmp(text, "Agent turn", 10) == 0)
    {
      led_phase_enter(PHASE_DONE);
    }
  else
    {
      led_phase_enter(PHASE_WORKING);
    }
}

static void led_on_reply(const char *text, void *ctx)
{
  (void)text;
  (void)ctx;

  led_phase_enter(PHASE_DONE);
}

static const struct remote_observer_ops_s g_led_observer =
{
  "led", led_on_state, led_on_reply, NULL
};
#endif

/* Driven from the single board-services thread, not one of its own: a
 * 50 ms blink loop does not deserve a 2 KB stack and a TCB.  tick_ms is
 * how long since the previous step.
 */

void bk7258_status_led_step(uint32_t tick_ms)
{
  static uint32_t elapsed;
  enum led_phase_e p = s_phase;
  uint32_t slow = (elapsed / BLINK_SLOW_MS) & 1u;
  uint32_t fast = (elapsed / BLINK_FAST_MS) & 1u;
  bool green = false;
  bool red = false;

  elapsed += tick_ms;

  switch (p)
    {
      case PHASE_IDLE:
        green = true;
        break;
      case PHASE_WORKING:
        green = slow != 0;
        break;
      case PHASE_APPROVAL:
        red = true;
        break;
      case PHASE_DONE:
        /* Blink, not solid: under the new scheme solid green means idle, so
         * a solid DONE would vanish into it.  Fast blink for the hold, then
         * settle back.
         */

        green = fast != 0;
        if (s_phase_left_ms > tick_ms)
          {
            s_phase_left_ms -= tick_ms;
          }
        else
          {
            led_phase_enter(PHASE_IDLE);
          }
        break;
      case PHASE_ERROR:
        red = slow != 0;
        break;
    }

  led_write(LED_GREEN_PIN, green);
  led_write(LED_RED_PIN, red);
}

int bk7258_status_led_initialize(void)
{
  bk7258_gpio_config_output(LED_RED_PIN, false);
  bk7258_gpio_config_output(LED_GREEN_PIN, false);
#ifdef CONFIG_AI_AGENT_REMOTE_CTRL
  remote_ctrl_register_observer(&g_led_observer);
#endif
  return 0;
}
