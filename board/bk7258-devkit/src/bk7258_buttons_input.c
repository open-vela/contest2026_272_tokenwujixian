/****************************************************************************
 * board/bk7258-devkit/src/bk7258_buttons_input.c
 *
 * A /dev/btn0 reader, a gesture engine on top of it, and a context stack
 * of key maps.
 *
 * Why the shape:
 *   - /dev/btn0 takes several readers: the upper half allocates a
 *     btn_open_s per open() with its own pending flag and notifies every
 *     open (drivers/input/button_upper.c).  ai_agent's ptt_button thread
 *     opens it separately for KEY3; this file owns KEY1/KEY2.
 *   - The gesture layer reports TAP/HOLD/DOUBLE/COMBO from that snapshot.
 *     LVGL's indev wants a state not an event stream too, so the two layer
 *     cleanly once it arrives.
 *   - What a key means depends on the screen.  Each context owns its own
 *     key map; a small global layer is consulted first so an escape hatch
 *     (cancel, leave) always works no matter which page is up.
 *
 * Keys (board.h): BUTTON_KEY1/2/3 on GPIO 13/12/8, active low.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include <nuttx/input/buttons.h>

#include <arch/chip/bk7258_memorymap.h>

#include "board.h"

/* The remote_ctrl input-adapter surface.  This file is a keymap adapter:
 * it maps gestures to remote_decision_e and registers itself through
 * remote_ctrl_register_input().  A touch-screen adapter would be a sibling
 * doing the same thing with widget events; remote_ctrl never learns which
 * hardware the product has.
 */

#ifdef CONFIG_AI_AGENT_REMOTE_CTRL
enum remote_decision_e
{
  REMOTE_DECISION_ONCE = 0,
  REMOTE_DECISION_DENY,
};

typedef void (*remote_decide_fn)(enum remote_decision_e decision, void *ctx);

struct remote_input_ops_s
{
  const char *name;
  int (*attach)(remote_decide_fn decide, void *ctx);
  void (*detach)(void);
};

int remote_ctrl_register_input(const struct remote_input_ops_s *ops);

#endif


/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define GESTURE_HOLD_MS     800
#define GESTURE_DOUBLE_MS   400
#define POLL_SLICE_MS       20
#define LED_SLICE_TICKS     2   /* 2 x 20 ms = 40 ms */

#define KEY_COUNT           NUM_BUTTONS
#define KEY_BIT(i)          (1u << (i))

#define GLOBAL_MAP_NAME     "global"
#define IDLE_MAP_NAME       "idle"

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum gesture_e
{
  GESTURE_TAP = 0,   /* press and release inside HOLD */
  GESTURE_HOLD,      /* held past GESTURE_HOLD_MS */
  GESTURE_DOUBLE,    /* two taps inside DOUBLE_MS */
  GESTURE_COMBO,     /* two or more keys pressed together */
  GESTURE_PRESS,     /* key went down -- the front half of press-and-hold */
  GESTURE_RELEASE,   /* key came up -- the back half */
};

typedef void (*key_action_t)(void);

struct keymap_s
{
  uint32_t       keys;   /* button bitmask, one bit for TAP/HOLD/DOUBLE */
  enum gesture_e gesture;
  key_action_t   fn;
};

struct keyctx_s
{
  const char          *name;
  const struct keymap_s *map;
  unsigned int         nmap;
  void               (*enter)(void);
  void               (*leave)(void);
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static int s_fd = -1;
static uint32_t s_held;          /* currently down */
static uint32_t s_prev;          /* previous snapshot, for edges */
static uint32_t s_down_ms[KEY_COUNT];
static bool s_tap_pending[KEY_COUNT];
static bool s_hold_fired[KEY_COUNT];
static uint32_t s_combo_started;
static uint32_t s_irq_held;      /* state as of the last POLLIN */
static uint32_t s_skew_ms;
static bool s_skew;
static bool s_skew_reported;

static const struct keyctx_s *s_ctx_stack[8];
static unsigned int s_ctx_depth;

/****************************************************************************
 * Context stack
 *
 * Lookup walks from the top downwards so a page overrides the global layer.
 * The global map holds only the escape hatches.
 ****************************************************************************/

static void action_cancel(void);
static void action_stat(void);
/* ponytail: diagnostic for the lost KEY3 (GPIO8) release edge.  GPIO8 is
 * also Armino's ADC10 pin, and Armino's gpio_dev_map()/unmap() clear the
 * pin's int_en (bit 12) and rewrite its mux.  Dump CFG and the mux word
 * when an edge goes missing so the board log shows who flips them.  Remove
 * once the writer is found.
 */

static void key3_regs(const char *tag)
{
  printf("[key] %s gpio%d cfg=%08lx func=%08lx\n", tag, BOARD_KEY3_PIN,
         (unsigned long)*(volatile uint32_t *)
           BK7258_GPIO_CFG(BOARD_KEY3_PIN),
         (unsigned long)*(volatile uint32_t *)
           BK7258_SYS_GPIO_FUNC(BOARD_KEY3_PIN));
}

static void action_once(void);
static void action_deny(void);

static const struct keymap_s g_global_map[] =
{
  { KEY_BIT(0) | KEY_BIT(1), GESTURE_COMBO, action_cancel },
};

static const struct keymap_s g_idle_map[] =
{
  { KEY_BIT(0), GESTURE_TAP,    action_once },
  { KEY_BIT(1), GESTURE_TAP,    action_deny  },
  { KEY_BIT(0), GESTURE_HOLD,   action_stat },
  { KEY_BIT(1), GESTURE_HOLD,   action_stat },

  /* KEY3 (GPIO8) is push-to-talk, owned by ai_agent's ptt_button thread
   * (CONFIG_AI_AGENT_PTT_BUTTON_IDX=2) on its own /dev/btn0 open.  The
   * upper half keeps per-open pending state, so both readers see every
   * edge; nothing is bound to it here.
   */
};

static const struct keyctx_s g_global_ctx =
{
  GLOBAL_MAP_NAME, g_global_map,
  sizeof(g_global_map) / sizeof(g_global_map[0]), NULL, NULL
};

static const struct keyctx_s g_idle_ctx =
{
  IDLE_MAP_NAME, g_idle_map,
  sizeof(g_idle_map) / sizeof(g_idle_map[0]), NULL, NULL
};

void keyctx_push(const struct keyctx_s *ctx)
{
  if (s_ctx_depth < sizeof(s_ctx_stack) / sizeof(s_ctx_stack[0]))
    {
      s_ctx_stack[s_ctx_depth++] = ctx;
      if (ctx->enter != NULL)
        {
          ctx->enter();
        }
    }
}

void keyctx_pop(void)
{
  if (s_ctx_depth > 0)
    {
      const struct keyctx_s *ctx = s_ctx_stack[--s_ctx_depth];
      if (ctx->leave != NULL)
        {
          ctx->leave();
        }
    }
}

static const struct keymap_s *keymap_lookup(uint32_t keys,
                                            enum gesture_e g,
                                            const char **which)
{
  unsigned int i;

  for (i = s_ctx_depth; i > 0; i--)
    {
      const struct keyctx_s *ctx = s_ctx_stack[i - 1];
      unsigned int j;

      for (j = 0; j < ctx->nmap; j++)
        {
          if (ctx->map[j].keys == keys && ctx->map[j].gesture == g)
            {
              if (which != NULL)
                {
                  *which = ctx->name;
                }

              return &ctx->map[j];
            }
        }
    }

  for (i = 0; i < g_global_ctx.nmap; i++)
    {
      if (g_global_ctx.map[i].keys == keys &&
          g_global_ctx.map[i].gesture == g)
        {
          if (which != NULL)
            {
              *which = g_global_ctx.name;
            }

          return &g_global_ctx.map[i];
        }
    }

  return NULL;
}

/****************************************************************************
 * Actions
 *
 * The key map reports a decision, not a button press.  The decide entry
 * point arrives through remote_ctrl_register_input() and is the only thing
 * this adapter knows about the application.
 ****************************************************************************/

#ifdef CONFIG_AI_AGENT_REMOTE_CTRL
static remote_decide_fn s_decide;
static void *s_decide_ctx;
#endif

static void action_cancel(void)
{
  printf("[key] cancel\n");
}

static void action_stat(void)
{
#ifdef CONFIG_AI_AGENT_REMOTE_CTRL
  if (s_decide != NULL)
    {
      /* VIEW_PENDING is not a permission decision, so it is not routed
       * through decide(); print the pending request directly when the
       * channel is configured.
       */

      printf("[key] view pending\n");
    }
#endif
}

static void action_once(void)
{
#ifdef CONFIG_AI_AGENT_REMOTE_CTRL
  if (s_decide != NULL)
    {
      s_decide(REMOTE_DECISION_ONCE, s_decide_ctx);
    }
#else
  printf("[key] once (no remote-ctrl)\n");
#endif
}

static void action_deny(void)
{
#ifdef CONFIG_AI_AGENT_REMOTE_CTRL
  if (s_decide != NULL)
    {
      s_decide(REMOTE_DECISION_DENY, s_decide_ctx);
    }
#else
  printf("[key] deny (no remote-ctrl)\n");
#endif
}

#ifdef CONFIG_AI_AGENT_REMOTE_CTRL
static int keymap_attach(remote_decide_fn decide, void *ctx)
{
  s_decide = decide;
  s_decide_ctx = ctx;
  return 0;
}

static void keymap_detach(void)
{
  s_decide = NULL;
  s_decide_ctx = NULL;
}

static const struct remote_input_ops_s g_keymap_ops =
{
  "keymap", keymap_attach, keymap_detach
};
#endif

/****************************************************************************
 * Gesture engine
 *
 * Works on the state snapshot, not on the event stream.  A key held past
 * GESTURE_HOLD_MS fires HOLD once; releasing inside that window and
 * pressing again inside GESTURE_DOUBLE_MS upgrades the first tap to DOUBLE.
 ****************************************************************************/

/* 32-bit arithmetic on purpose: the 64-bit multiply/divide the naive form
 * pulls in (umul64_const / div64_const32) is deep enough to blow a 2 KB
 * thread stack.  Milliseconds from a 32-bit tick count is enough for the
 * gesture deadlines.
 */

static uint32_t now_ms(void)
{
  return (uint32_t)(clock() / (CLOCKS_PER_SEC / 1000u));
}

static void gesture_fire(uint32_t keys, enum gesture_e g)
{
  const char *which = "-";
  const struct keymap_s *m = keymap_lookup(keys, g, &which);

  printf("[key] %s: keys=%lx gesture=%d -> %s\n", which,
         (unsigned long)keys, g,
         (m != NULL && m->fn != NULL) ? "hit" : "unmapped");
  if (m != NULL && m->fn != NULL)
    {
      m->fn();
    }
}

static void gesture_update(uint32_t now)
{
  uint32_t pressed = s_held & ~s_prev;
  uint32_t released = s_prev & ~s_held;
  unsigned int i;

  /* COMBO: two or more keys down at once.  Fires once on the transition
   * and then suppresses the individual TAP/HOLD for every key that took
   * part -- otherwise pressing KEY1 then KEY2 would emit COMBO *and* a
   * stray KEY1 TAP when KEY1 is released.
   */

  if (pressed != 0 && (s_held & (s_held - 1)) != 0 && s_combo_started == 0)
    {
      s_combo_started = s_held;
      gesture_fire(s_held, GESTURE_COMBO);
    }

  for (i = 0; i < KEY_COUNT; i++)
    {
      uint32_t bit = KEY_BIT(i);

      if ((pressed & bit) != 0)
        {
          s_down_ms[i] = now;
          s_hold_fired[i] = false;

          /* Only keys that carry a press-and-hold binding emit the edge
           * pair.  Firing it for every key made each tap of KEY1/KEY2 log
           * "unmapped" for PRESS and RELEASE before the TAP that mattered
           * (board: "我都是单击").  TAP/HOLD/DOUBLE/COMBO keep their
           * unmapped diagnostic -- those are gestures a person can
           * discover by accident, these are not.
           */

          if (keymap_lookup(bit, GESTURE_PRESS, NULL) != NULL)
            {
              gesture_fire(bit, GESTURE_PRESS);
            }
        }

      if ((s_combo_started & bit) != 0)
        {
          /* Consumed by the combo: no single-key gesture for this press, and
           * a tap left over from an earlier press of the same key must go
           * too -- otherwise it fires after the combo and looks like a stray
           * KEY1 tap.
           */

          s_tap_pending[i] = false;
          if ((released & bit) != 0)
            {
              s_combo_started &= ~bit;
            }

          continue;
        }

      /* A key bound to GESTURE_PRESS is a press-and-hold key: the whole
       * gesture is the edge pair, so TAP and HOLD must not also fire or
       * releasing the PTT key would answer a permission request.
       */

      if (keymap_lookup(bit, GESTURE_PRESS, NULL) == NULL)
        {
          if ((s_held & bit) != 0 && !s_hold_fired[i] &&
              (uint32_t)(now - s_down_ms[i]) >= GESTURE_HOLD_MS)
            {
              s_hold_fired[i] = true;
              s_tap_pending[i] = false;
              gesture_fire(bit, GESTURE_HOLD);
            }

          if ((released & bit) != 0 && !s_hold_fired[i])
            {
              gesture_fire(bit, GESTURE_TAP);
            }
        }

      if ((released & bit) != 0 &&
          keymap_lookup(bit, GESTURE_RELEASE, NULL) != NULL)
        {
          gesture_fire(bit, GESTURE_RELEASE);
        }
    }

  /* Reset the combo only after the loop has seen every release edge.
   * Clearing it on s_held == 0 beforehand let the last key released skip
   * the suppression and emit a stray TAP.
   */

  if (s_held == 0)
    {
      s_combo_started = 0;
    }

  s_prev = s_held;
}

/****************************************************************************
 * button_source
 *
 * The only place that touches /dev/btn0.  poll() wakes on any edge; read()
 * returns the current bitmask, so edges are derived by differencing.
 ****************************************************************************/

static int button_source_init(void)
{
  struct btn_pollevents_s ev;

  s_fd = open("/dev/btn0", O_RDONLY);
  if (s_fd < 0)
    {
      printf("[key] open /dev/btn0: %d\n", errno);
      return -errno;
    }

  ev.bp_press = ev.bp_release = (1u << KEY_COUNT) - 1u;
  ioctl(s_fd, BTNIOC_POLLEVENTS, &ev);
  return 0;
}

static void button_source_poll(void)
{
  struct pollfd pfd;
  btn_buttonset_t set;
  ssize_t n;
  int ret;

  pfd.fd = s_fd;
  pfd.events = POLLIN;
  ret = poll(&pfd, 1, POLL_SLICE_MS);
  (void)ret;

  /* gesture_update must run every slice, not only when poll reports data:
   * the deferred TAP and the HOLD timer are deadlines, and skipping the
   * quiet slices pushed them to the next key event -- which is why a tap
   * printed only when something else was pressed afterwards.
   *
   * The level is sampled every slice for the same reason.  poll() only
   * shortens the wait; read() returns the live GPIO level, so a lost edge
   * heals within one slice.  Reading only on POLLIN left KEY4 "held" after
   * a PTT whose release edge never reached us, and its RELEASE then fired
   * on the next KEY1 tap during an approval (board log).
   */

  n = read(s_fd, &set, sizeof(set));
  if (n == sizeof(set))
    {
      s_held = (uint32_t)set;
    }

  /* Report an edge the interrupt path never delivered.  The upper half
   * debounces for 30 ms before notifying, so a short mismatch is normal;
   * only one that outlives 200 ms without POLLIN is a lost edge.
   */

  if (ret > 0)
    {
      s_irq_held = s_held;
    }

  if (s_held != s_irq_held)
    {
      if (!s_skew)
        {
          s_skew = true;
          s_skew_ms = now_ms();
        }
      else if (!s_skew_reported &&
               (uint32_t)(now_ms() - s_skew_ms) >= 200)
        {
          s_skew_reported = true;
          printf("[key] missed edge: irq=%lx level=%lx\n",
                 (unsigned long)s_irq_held, (unsigned long)s_held);
          key3_regs("missed edge");
        }
    }
  else
    {
      s_skew = false;
      s_skew_reported = false;
    }

  gesture_update(now_ms());
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

static FAR void *key_thread(FAR void *arg)
{
  (void)arg;

  if (button_source_init() < 0)
    {
      return NULL;
    }

  extern void bk7258_status_led_step(uint32_t tick_ms);
  unsigned int ticks = 0;

  keyctx_push(&g_idle_ctx);
#ifdef CONFIG_AI_AGENT_REMOTE_CTRL
  remote_ctrl_register_input(&g_keymap_ops);
#endif
  printf("[key] input up (%d keys)\n", KEY_COUNT);

  for (; ; )
    {
      button_source_poll();
      if (++ticks >= LED_SLICE_TICKS)
        {
          ticks = 0;
          bk7258_status_led_step(POLL_SLICE_MS * LED_SLICE_TICKS);
        }
    }

  return NULL;
}

int bk7258_buttons_input_initialize(void)
{
  pthread_t tid;
  pthread_attr_t attr;
  int ret;

  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 4096);
  ret = pthread_create(&tid, &attr, key_thread, NULL);
  pthread_attr_destroy(&attr);
  return ret == 0 ? 0 : -EIO;
}
