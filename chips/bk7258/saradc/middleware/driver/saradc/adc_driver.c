// Copyright 2020-2021 Beken
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <common/bk_include.h>
#include <common/bk_compiler.h>
#include <os/mem.h>
#include <driver/gpio.h>
#include "gpio_driver.h"
#include <os/os.h>
#include "adc_hal.h"
#include "adc_statis.h"
#include "adc_driver.h"
#include <driver/int.h>
#include "icu_driver.h"
#include "power_driver.h"
#include "clock_driver.h"
#include <driver/adc.h>
#include "bk_drv_model.h"
#include "bk_sys_ctrl.h"
#include "sys_driver.h"
#include "iot_adc.h"
#include "bk_saradc.h"
#include "common/bk_err.h"
#include <modules/pm.h>
#ifdef CONFIG_FREERTOS_SMP
#include "spinlock.h"
#endif // CONFIG_FREERTOS_SMP

/* Armino's native ICU callback has a void(void) signature.  The NuttX ICU
 * bridge passes the registered opaque argument, so adapt the ISR at this
 * boundary instead of invoking a mismatched function pointer. */
static void adc_isr(void *arg) __BK_SECTION(".itcm");
static bk_err_t adc_read_fifo(void) __BK_SECTION(".itcm");
bk_err_t bk_adc_stop(void) __BK_SECTION(".itcm");

typedef struct
{
	adc_isr_t callback;
	uint32_t param;
    IotAdcCallback_t     iot_callback;
    void *               p_iot_context; 
} adc_callback_t;

typedef struct {
	adc_hal_t hal;
	uint16_t chan_init_bits;
} adc_driver_t;

typedef struct {
	beken_semaphore_t adc_read_sema;
	beken_mutex_t adc_mutex;
} adc_dev_t;

typedef struct {
	uint16_t *buf;
	/* The size is in unit of uint16_t */
	uint16_t size;
	uint16_t sample_cnt;
} adc_buf_t;

static adc_driver_t s_adc = {0};
static bool s_adc_driver_is_init = false;
static adc_dev_t s_adc_dev = {NULL};
static adc_buf_t s_adc_buf = {0};
static adc_callback_t s_adc_read_isr = {NULL};
static adc_statis_t* s_adc_statis = NULL;
UINT8  g_saradc_flag = 0x0;

saradc_calibrate_val saradc_val = {
#if (CONFIG_SOC_BK7256XX)
    0xD40, 0x1A72 /* 1Volt, 2Volt*/
#elif (CONFIG_SOC_BK7236XX)
    0x9C7,0x1358 /* 1Volt, 2Volt*/
#else
    0x55, 0x354
#endif
};

#ifdef CONFIG_FREERTOS_SMP
static volatile spinlock_t adc_spin_lock = SPIN_LOCK_INIT;
#endif // CONFIG_FREERTOS_SMP

adc_config_t g_adc_cfg = {0};

extern bk_err_t mb_saradc_ipc_init(void);
extern bk_err_t mb_saradc_op_prepare(void);
extern bk_err_t mb_saradc_op_finish(void);


//TODO - by Frank
//1. ADC id range confirm
//2. Add API for ADC saturate mode, hide it for application
//3. Considering how/when to use cli_done/calib_trig bits?
//4. adc_hal_init to reset the ADC to default value
//5. Currently ADC ISR is always enabled once ADC is started, can we provide ADC
//   API that don't need to enable ADC ISR?
//6. Make adc_buf_t.buf configurable in bk_adc_init() since different application may
//   use different buffer size.
//7. If necessary, provide ADC API specific for calibration that don't need ADC ISR.
//8. Hide the difference of ADC RAW data
//9. Update doc accordingly
//10. adc_chan_t to adc_chan_t

#define ADC_SAMPLE_CNT_DEFAULT 32

#define ADC_RETURN_ON_NOT_INIT() do {\
		if (!s_adc_driver_is_init) {\
			ADC_LOGE("adc driver not init\r\n");\
			return BK_ERR_ADC_NOT_INIT;\
		}\
	} while(0)

#define ADC_RETURN_ON_INVALID_CHAN(id) do {\
		if (!adc_hal_is_valid_channel(&s_adc.hal, (id))) {\
			ADC_LOGE("ADC id number(%d) is invalid\r\n", (id));\
			return BK_ERR_ADC_INVALID_CHAN;\
		}\
	} while(0)

#define ADC_RETURN_ON_INVALID_MODE(mode) do {\
		if ((mode) >= ADC_NONE_MODE) {\
			return BK_ERR_ADC_INVALID_MODE;\
		}\
	} while(0)

#define ADC_RETURN_ON_INVALID_SRC_CLK(src_clk) do {\
		if ((src_clk) >= ADC_SCLK_NONE) {\
			return BK_ERR_ADC_INVALID_SCLK_MODE;\
		}\
	} while(0)


static void adc_isr(void *arg);
static void adc_flush(void);

static void adc_init_gpio(adc_chan_t chan)
{
	if (adc_hal_is_analog_channel(&s_adc.hal, chan))
		return;

	adc_gpio_map_t adc_map_table[] = ADC_DEV_MAP;
	adc_gpio_map_t *adc_map = &adc_map_table[chan];

	//TODO optimize it
	if(chan == 0) {
		uint32_t param = PARAM_SARADC_BT_TXSEL_BIT;

#if CONFIG_SYSTEM_CTRL
		sys_drv_analog_reg4_bits_or(param);// to do,need remove old interface after all adaption is finished
#else
		sddev_control(DD_DEV_TYPE_SCTRL, CMD_SCTRL_ANALOG_CTRL4_SET, &param);
#endif
	}

	gpio_dev_map(adc_map->gpio_id, adc_map->gpio_dev);
	bk_gpio_disable_pull(adc_map->gpio_id);
	bk_gpio_disable_input(adc_map->gpio_id);
	bk_gpio_disable_output(adc_map->gpio_id);
}

static void adc_deinit_gpio(adc_chan_t chan)
{
	if (adc_hal_is_analog_channel(&s_adc.hal, chan))
		return;

	adc_gpio_map_t adc_map_table[] = ADC_DEV_MAP;
	adc_gpio_map_t *adc_map = &adc_map_table[chan];

	gpio_dev_unmap(adc_map->gpio_id);
}

static void adc_enable_block(void)
{
#if (!CONFIG_SOC_BK7231N) && (!CONFIG_SOC_BK7256XX)
	//TODO - optimize it after sysctrl driver optimized!
	uint32_t param = BLK_BIT_SARADC;
	sddev_control(DD_DEV_TYPE_SCTRL, CMD_SCTRL_BLK_ENABLE, &param);
#endif
}

static bk_err_t adc_chan_init_common(adc_chan_t chan)
{
	bk_err_t ret = 0;

	sys_drv_sadc_pwr_up();
	adc_enable_block();
	adc_hal_init(&s_adc.hal);
	//adc_init_gpio(chan);
	adc_hal_sel_channel(&s_adc.hal, chan);

	s_adc.chan_init_bits |= BIT(chan);

	return ret;
}

bk_err_t bk_adc_chan_init_gpio(adc_chan_t chan)
{
	adc_init_gpio(chan);
	return BK_OK;
}

static bk_err_t adc_chan_deinit_common(adc_chan_t chan)
{
	s_adc.chan_init_bits &= ~(BIT(chan));

	adc_hal_stop_commom(&s_adc.hal);
	sys_drv_sadc_int_disable();

	adc_flush();

	sys_drv_sadc_pwr_down();
	//adc_deinit_gpio(chan);
	return BK_OK;
}

bk_err_t bk_adc_chan_deinit_gpio(adc_chan_t chan)
{
	adc_deinit_gpio(chan);
	return BK_OK;
}

static bk_err_t adc_read_fifo(void)
{

	while(!adc_hal_is_fifo_empty(&s_adc.hal)) {
		ADC_STATIS_INC(s_adc_statis->adc_rx_total_cnt);

		if(s_adc_buf.sample_cnt < s_adc_buf.size) {
			s_adc_buf.buf[s_adc_buf.sample_cnt++] = adc_hal_get_adc_data(&s_adc.hal);

			ADC_STATIS_INC(s_adc_statis->adc_rx_succ_cnt);
		} else {
			if((adc_hal_get_mode(&s_adc.hal) == ADC_CONTINUOUS_MODE)) {
				adc_hal_get_adc_data(&s_adc.hal);

				ADC_STATIS_INC(s_adc_statis->adc_rx_drop_cnt);
			} else {
				//software mode is NOT used!
			}
		}
	}


	return BK_OK;
}

static void adc_flush(void)
{
	adc_hal_clear_int_status(&s_adc.hal);

	while(!adc_hal_is_fifo_empty(&s_adc.hal)) {
		adc_hal_get_adc_data(&s_adc.hal);
	}
}

#if CONFIG_SARADC_PM_CB_SUPPORT
static int adc_restore(uint64_t sleep_time, void *args)
{
	int ret;

	ret = bk_adc_set_config(&g_adc_cfg);
	if (BK_OK != ret) {
		return ret;
	}

	if (g_adc_cfg.is_open) {
		ret = bk_adc_start();
	}

	return ret;
}
#endif

bk_err_t bk_adc_driver_init(void)
{
	int ret;

	if (s_adc_driver_is_init) {
		return BK_OK;
	}

	extern bk_err_t mb_saradc_ipc_init(void);
	ret = mb_saradc_ipc_init();
	if(ret != BK_OK)
	{
		BK_LOGE("adc_driver", "mb_saradc_ipc_init failed %d.\r\n", ret);
		return ret;
	}

#if (CONFIG_CPU_CNT > 1)
	extern bk_err_t bk_saradc_server_init(void);
	ret = bk_saradc_server_init();
	if(ret != BK_OK)
	{
		BK_LOGE("adc_driver", "saradc svr create failed %d.\r\n", ret);
	}
#endif

	os_memset(&s_adc, 0, sizeof(s_adc));
	os_memset(&g_adc_cfg, 0, sizeof(g_adc_cfg));

	if (s_adc_buf.buf) {
		os_free(s_adc_buf.buf);
	}

	s_adc_buf.size = CONFIG_ADC_BUF_SIZE;
	s_adc_buf.buf = (uint16_t*)os_zalloc(CONFIG_ADC_BUF_SIZE<<1);
	if (!s_adc_buf.buf) {
		return BK_ERR_NO_MEM;
	}

	if (!s_adc_dev.adc_mutex) {
		ret = rtos_init_mutex(&s_adc_dev.adc_mutex);
		if (kNoErr != ret) {
			os_free(s_adc_buf.buf);
			return BK_ERR_ADC_INIT_MUTEX;
		}
	}

	if (!s_adc_dev.adc_read_sema) {
		ret = rtos_init_semaphore(&(s_adc_dev.adc_read_sema), 1);
		if (BK_OK != ret) {
			os_free(s_adc_buf.buf);
			rtos_deinit_mutex(&s_adc_dev.adc_mutex);
			return BK_ERR_ADC_INIT_READ_SEMA;
		}
	}

	//bk_int_isr_register(INT_SRC_SARADC, adc_isr, NULL);

	adc_statis_init();
	s_adc_statis = adc_statis_get_statis();

	g_adc_cfg.clk = DEFAULT_ADC_CLK;
	g_adc_cfg.sample_rate = DEFAULT_ADC_SAMPLE_RATE;
	g_adc_cfg.adc_filter = 0;
	g_adc_cfg.steady_ctrl = DEFAULT_ADC_STEADY_TIME;
	g_adc_cfg.adc_mode = DEFAULT_ADC_MODE;
	g_adc_cfg.chan = ADC_MAX;
	g_adc_cfg.src_clk = DEFAULT_ADC_SCLK;
	g_adc_cfg.saturate_mode = DEFAULT_SATURATE_MODE;
	g_adc_cfg.output_buf = NULL;

#include "adc_driver_part2.inc"
