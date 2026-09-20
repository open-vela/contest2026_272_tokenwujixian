/****************************************************************************
 * chips/bk7258/temp_detect/bk7258_temp_detect.c
 *
 * BK7258 on-chip temperature sampling and periodic PHY compensation.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

#include <nuttx/clock.h>
#include <nuttx/spinlock.h>
#include <nuttx/wqueue.h>

#include <common/bk_err.h>
#include <common/bk_typedef.h>
#include <components/log.h>
#include <components/sensor.h>
#include <modules/pm.h>
#include <arm_internal.h>
#include <bk7258_memorymap.h>
#include <arch/chip/bk7258_sysctrl.h>

#include <driver/adc.h>
#include <bk_private/bk_phy.h>
#include <temp_detect.h>
#include <temp_detect_pub.h>

#define TEMPD_TAG "BK7258 TEMPD"

struct bk7258_tempd_state
{
  struct work_s work;
  spinlock_t lock;
  bool active;
  bool sensor_ready;
  bool temperature_valid;
  bool voltage_enabled;
  bool voltage_valid;
  uint16_t last_temperature;
  float temperature;
  float voltage;
  uint32_t sample_count;
};

static struct bk7258_tempd_state g_tempd =
{
  .lock = SP_UNLOCKED,
};

static void bk7258_tempd_worker(void *arg);

/* Authority BK7236-family voltage detection samples ADC0. */
#define BK7258_VOLT_SENSOR_CHANNEL ADC_0

static int bk7258_tempd_sample(uint32_t *temperature)
{
  uint16_t samples[ADC_TEMP_BUFFER_SIZE] = {0};
  adc_config_t config = {0};
  uint32_t sum = 0;
  uint32_t count = 0;
  bool acquired = false;
  bool initialized = false;
  bool temp_enabled = false;
  bk_err_t ret;
  unsigned int index;

  ret = bk_adc_acquire();
  if (ret != BK_OK)
    {
      return ret;
    }
  acquired = true;

  ret = bk7258_analog_update_bits(5, UINT32_C(1) << 4, UINT32_C(1) << 4);
  if (ret < 0)
    {
      ret = BK_FAIL;
      goto cleanup;
    }
  temp_enabled = true;

  ret = bk_adc_init(ADC_TEMP_SENSOR_CHANNEL);
  if (ret != BK_OK)
    {
      goto cleanup;
    }
  initialized = true;

  config.chan = ADC_TEMP_SENSOR_CHANNEL;
  config.adc_mode = ADC_CONTINUOUS_MODE;
  config.clk = TEMP_DETEC_ADC_CLK;
  config.src_clk = ADC_SCLK_XTAL_26M;
  config.saturate_mode = ADC_TEMP_SATURATE_MODE;
  config.sample_rate = TEMP_DETEC_ADC_SAMPLE_RATE;
  config.steady_ctrl = TEMP_DETEC_ADC_STEADY_CTRL;

  /* Authority temp_detect.c uses the regular configuration path so channel 7
   * receives ANA5's normal 1/3 divider setup. PHY calibration intentionally
   * uses the distinct no-divider path. */
  ret = bk_adc_set_config(&config);
  if (ret != BK_OK)
    {
      goto cleanup;
    }
  ret = bk_adc_enable_bypass_clalibration();
  if (ret != BK_OK)
    {
      goto cleanup;
    }
  ret = bk_adc_start();
  if (ret != BK_OK)
    {
      goto cleanup;
    }
  ret = bk_adc_read_raw(samples, ADC_TEMP_BUFFER_SIZE, 1000);
  if (ret != BK_OK)
    {
      ret = BK_ERR_TEMPD_BASE - 1;
      goto cleanup;
    }

  for (index = 5; index < ADC_TEMP_BUFFER_SIZE; index++)
    {
      if (samples[index] != 0 && samples[index] != 2048)
        {
          sum += samples[index];
          count++;
        }
    }
  *temperature = count == 0 ? 0 : (sum / count) / 4;
  ret = BK_OK;

cleanup:
  if (temp_enabled && bk7258_analog_update_bits(5, UINT32_C(1) << 4, 0) < 0 &&
      ret == BK_OK)
    {
      ret = BK_FAIL;
    }
  if (initialized)
    {
      (void)bk_adc_stop();
      (void)bk_adc_deinit(ADC_TEMP_SENSOR_CHANNEL);
    }
  if (acquired)
    {
      (void)bk_adc_release();
    }
  return ret;
}

static int bk7258_voltd_sample(uint32_t *voltage)
{
  uint16_t samples[ADC_TEMP_BUFFER_SIZE] = {0};
  adc_config_t config = {0};
  uint32_t sum = 0;
  uint32_t count = 0;
  bool acquired = false;
  bool initialized = false;
  bk_err_t ret;
  unsigned int index;

  if (voltage == NULL)
    {
      return BK_ERR_NULL_PARAM;
    }

  ret = bk_adc_acquire();
  if (ret != BK_OK)
    {
      return ret;
    }

  acquired = true;
  ret = bk_adc_init(BK7258_VOLT_SENSOR_CHANNEL);
  if (ret != BK_OK)
    {
      goto cleanup;
    }

  initialized = true;
  config.chan = BK7258_VOLT_SENSOR_CHANNEL;
  config.adc_mode = ADC_CONTINUOUS_MODE;
  config.clk = TEMP_DETEC_ADC_CLK;
  config.src_clk = ADC_SCLK_XTAL_26M;
  config.saturate_mode = ADC_TEMP_SATURATE_MODE;
  config.sample_rate = TEMP_DETEC_ADC_SAMPLE_RATE;
  config.steady_ctrl = TEMP_DETEC_ADC_STEADY_CTRL;
  config.adc_filter = 0;

  ret = bk_adc_set_config(&config);
  if (ret != BK_OK)
    {
      goto cleanup;
    }

  ret = bk_adc_enable_bypass_clalibration();
  if (ret != BK_OK)
    {
      goto cleanup;
    }

  ret = bk_adc_start();
  if (ret != BK_OK)
    {
      goto cleanup;
    }

  ret = bk_adc_read_raw(samples, ADC_TEMP_BUFFER_SIZE, 1000);
  if (ret != BK_OK)
    {
      ret = BK_ERR_TEMPD_BASE - 1;
      goto cleanup;
    }

  for (index = 5; index < ADC_TEMP_BUFFER_SIZE; index++)
    {
      if (samples[index] != 0 && samples[index] != 2048)
        {
          sum += samples[index];
          count++;
        }
    }

  if (count == 0)
    {
      ret = BK_ERR_TRY_AGAIN;
      goto cleanup;
    }

  *voltage = (sum / count) / 4;
  ret = *voltage > ADC_TEMP_VAL_MIN ? BK_OK : BK_ERR_TRY_AGAIN;

cleanup:
  if (initialized)
    {
      (void)bk_adc_stop();
      (void)bk_adc_deinit(BK7258_VOLT_SENSOR_CHANNEL);
    }

  if (acquired)
    {
      (void)bk_adc_release();
    }

  return ret;
}

int temp_detect_init(uint32_t init_val)
{
  irqstate_t flags;
  int ret;

  flags = spin_lock_irqsave(&g_tempd.lock);
  if (g_tempd.active)
    {
      spin_unlock_irqrestore(&g_tempd.lock, flags);
      return BK_OK;
    }

  g_tempd.active = true;
  g_tempd.sensor_ready = true;
  g_tempd.temperature_valid = false;
  g_tempd.voltage_enabled = true;
  g_tempd.voltage_valid = false;
  g_tempd.last_temperature = (uint16_t)init_val;
  g_tempd.sample_count = 0;
  ret = work_queue(LPWORK, &g_tempd.work, bk7258_tempd_worker, &g_tempd, 0);
  if (ret < 0)
    {
      g_tempd.active = false;
      g_tempd.sensor_ready = false;
      spin_unlock_irqrestore(&g_tempd.lock, flags);
      BK_LOGE(TEMPD_TAG, "schedule failed=%d\n", ret);
      return BK_FAIL;
    }

  spin_unlock_irqrestore(&g_tempd.lock, flags);
  return BK_OK;
}

int temp_detect_deinit(void)
{
  irqstate_t flags;
  bool active;

  flags = spin_lock_irqsave(&g_tempd.lock);
  active = g_tempd.active;
  g_tempd.active = false;
  g_tempd.sensor_ready = false;
  g_tempd.temperature_valid = false;
  g_tempd.voltage_valid = false;
  spin_unlock_irqrestore(&g_tempd.lock, flags);

  if (active)
    {
      work_cancel_sync(LPWORK, &g_tempd.work);
      manual_cal_temp_pwr_unint();
    }

  return BK_OK;
}

bool temp_detect_is_init(void)
{
  irqstate_t flags;
  bool active;

  flags = spin_lock_irqsave(&g_tempd.lock);
  active = g_tempd.active;
  spin_unlock_irqrestore(&g_tempd.lock, flags);
  return active;
}

static void bk7258_tempd_worker(void *arg)
{
  struct bk7258_tempd_state *state = arg;
  uint32_t temperature;
  uint32_t voltage;
  uint32_t delay_ms;
  irqstate_t flags;
  bool active;
  int ret;

  flags = spin_lock_irqsave(&state->lock);
  active = state->active;
  spin_unlock_irqrestore(&state->lock, flags);
  if (!active)
    {
      return;
    }

  ret = temp_detect_get_temperature(&temperature);
  if (ret == BK_OK)
    {
      rwnx_cal_do_temp_detect((uint16_t)temperature,
                              ADC_TMEP_LSB_PER_10DEGREE *
                              ADC_TMEP_10DEGREE_PER_DBPWR,
                              &state->last_temperature);
      (void)bk_sensor_set_current_temperature((float)temperature);
    }
  else
    {
      BK_LOGW(TEMPD_TAG, "temperature sample unavailable=%d\n", ret);
    }

  flags = spin_lock_irqsave(&state->lock);
  active = state->active && state->voltage_enabled;
  spin_unlock_irqrestore(&state->lock, flags);
  if (active)
    {
      /* Match the authority VOLT_TIMER_EXPIRED ordering: allow SARADC to
       * wake, sample ADC0, notify the PHY calibration routine, then release
       * its sleep vote. */
      (void)bk_pm_module_vote_sleep_ctrl(PM_SLEEP_MODULE_NAME_SARADC, 0, 0);
      ret = bk7258_voltd_sample(&voltage);
      if (ret == BK_OK)
        {
          rwnx_cal_do_volt_detect((uint16_t)voltage);
          (void)bk_sensor_set_current_voltage((float)voltage);
        }
      else
        {
          BK_LOGW(TEMPD_TAG, "voltage sample unavailable=%d\n", ret);
        }
      (void)bk_pm_module_vote_sleep_ctrl(PM_SLEEP_MODULE_NAME_SARADC, 1, 0);
    }

  flags = spin_lock_irqsave(&state->lock);
  if (state->active)
    {
      state->sample_count++;
      delay_ms = state->sample_count >=
                 ADC_TMEP_DETECT_INTERVAL_CHANGE /
                 ADC_TMEP_DETECT_INTERVAL_INIT ?
                 ADC_TMEP_DETECT_INTERVAL * 1000U :
                 ADC_TMEP_DETECT_INTERVAL_INIT * 1000U;
      ret = work_queue(LPWORK, &state->work, bk7258_tempd_worker, state,
                       MSEC2TICK(delay_ms));
      if (ret < 0)
        {
          state->active = false;
          BK_LOGE(TEMPD_TAG, "reschedule failed=%d\n", ret);
        }
    }
  spin_unlock_irqrestore(&state->lock, flags);
}

int temp_detect_get_temperature(uint32_t *temperature)
{
  uint32_t result = 0;
  bk_err_t ret = BK_ERR_TRY_AGAIN;
  unsigned int attempt;

  if (temperature == NULL)
    {
      return BK_ERR_NULL_PARAM;
    }

  /* Authority retries the complete transaction up to TEMPD_MAX_RETRY_NUM
   * times if sampling fails or the processed code is out of sensor range. */
  for (attempt = 0; attempt < 3; attempt++)
    {
      ret = bk7258_tempd_sample(&result);
      if (ret == BK_OK && result > ADC_TEMP_VAL_MIN &&
          result < ADC_TEMP_VAL_MAX)
        {
          break;
        }
      if (ret == BK_OK)
        {
          ret = BK_ERR_TRY_AGAIN;
        }
    }
  *temperature = result;
  return ret;
}

int volt_single_get_current_voltage(UINT32 *volt_value)
{
  uint32_t voltage;
  bk_err_t ret;

  if (volt_value == NULL)
    {
      return BK_ERR_NULL_PARAM;
    }

  ret = bk7258_voltd_sample(&voltage);
  if (ret == BK_OK)
    {
      *volt_value = (UINT32)voltage;
    }

  return ret;
}

int volt_detect_start(void)
{
  irqstate_t flags = spin_lock_irqsave(&g_tempd.lock);
  bool active = g_tempd.active;

  g_tempd.voltage_enabled = true;
  spin_unlock_irqrestore(&g_tempd.lock, flags);
  return active ? BK_OK : BK_ERR_NOT_INIT;
}

int volt_detect_stop(void)
{
  irqstate_t flags = spin_lock_irqsave(&g_tempd.lock);
  bool active = g_tempd.active;

  g_tempd.voltage_enabled = false;
  spin_unlock_irqrestore(&g_tempd.lock, flags);
  return active ? BK_OK : BK_ERR_NOT_INIT;
}

bk_err_t bk_sensor_init(void)
{
  irqstate_t flags = spin_lock_irqsave(&g_tempd.lock);

  g_tempd.sensor_ready = true;
  g_tempd.temperature_valid = false;
  g_tempd.voltage_valid = false;
  spin_unlock_irqrestore(&g_tempd.lock, flags);
  return BK_OK;
}

bk_err_t bk_sensor_deinit(void)
{
  irqstate_t flags = spin_lock_irqsave(&g_tempd.lock);

  g_tempd.sensor_ready = false;
  g_tempd.temperature_valid = false;
  g_tempd.voltage_valid = false;
  spin_unlock_irqrestore(&g_tempd.lock, flags);
  return BK_OK;
}

bk_err_t bk_sensor_set_current_temperature(float temperature)
{
  irqstate_t flags = spin_lock_irqsave(&g_tempd.lock);

  if (!g_tempd.sensor_ready)
    {
      spin_unlock_irqrestore(&g_tempd.lock, flags);
      return BK_ERR_NOT_INIT;
    }

  g_tempd.temperature = temperature;
  g_tempd.temperature_valid = true;
  spin_unlock_irqrestore(&g_tempd.lock, flags);
  return BK_OK;
}

bk_err_t bk_sensor_set_current_voltage(float voltage)
{
  irqstate_t flags = spin_lock_irqsave(&g_tempd.lock);

  if (!g_tempd.sensor_ready)
    {
      spin_unlock_irqrestore(&g_tempd.lock, flags);
      return BK_ERR_NOT_INIT;
    }

  g_tempd.voltage = voltage;
  g_tempd.voltage_valid = true;
  spin_unlock_irqrestore(&g_tempd.lock, flags);
  return BK_OK;
}

bk_err_t bk_sensor_get_current_temperature(float *temperature)
{
  irqstate_t flags;

  if (temperature == NULL)
    {
      return BK_ERR_PARAM;
    }

  flags = spin_lock_irqsave(&g_tempd.lock);
  if (!g_tempd.sensor_ready)
    {
      spin_unlock_irqrestore(&g_tempd.lock, flags);
      return BK_ERR_NOT_INIT;
    }

  if (!g_tempd.temperature_valid)
    {
      spin_unlock_irqrestore(&g_tempd.lock, flags);
      return BK_ERR_TRY_AGAIN;
    }

  *temperature = g_tempd.temperature;
  spin_unlock_irqrestore(&g_tempd.lock, flags);
  return BK_OK;
}

bk_err_t bk_sensor_get_current_voltage(float *voltage)
{
  irqstate_t flags;

  if (voltage == NULL)
    {
      return BK_ERR_PARAM;
    }

  flags = spin_lock_irqsave(&g_tempd.lock);
  if (!g_tempd.sensor_ready)
    {
      spin_unlock_irqrestore(&g_tempd.lock, flags);
      return BK_ERR_NOT_INIT;
    }

  if (!g_tempd.voltage_valid)
    {
      spin_unlock_irqrestore(&g_tempd.lock, flags);
      return BK_ERR_TRY_AGAIN;
    }

  *voltage = g_tempd.voltage;
  spin_unlock_irqrestore(&g_tempd.lock, flags);
  return BK_OK;
}
