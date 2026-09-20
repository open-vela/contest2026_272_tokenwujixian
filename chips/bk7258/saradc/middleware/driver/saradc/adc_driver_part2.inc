	g_adc_cfg.output_buf_len = 0;
	g_adc_cfg.is_open = 0;

	s_adc_driver_is_init = true;

	return BK_OK;
}

bk_err_t bk_adc_acquire(void)
{
    int ret = 0;
    if(!s_adc_driver_is_init)
    {
        return BK_FAIL;
    }

    ret = rtos_lock_mutex(&s_adc_dev.adc_mutex);
    mb_saradc_op_prepare();

    return ret;
}

bk_err_t bk_adc_release(void)
{
    int ret = 0;

    ADC_LOGV("release\n");
    mb_saradc_op_finish();
    ret = rtos_unlock_mutex(&s_adc_dev.adc_mutex);

    return ret;
}

bk_err_t bk_adc_driver_deinit(void)
{
	if (!s_adc_driver_is_init)
		return BK_OK;

	bk_adc_deinit(ADC_1); // Deinit ADC_1 to deinit ADC
	adc_hal_deinit(&s_adc.hal);
	//bk_int_isr_unregister(INT_SRC_SARADC);

	if (s_adc_dev.adc_mutex)
		rtos_deinit_mutex(&s_adc_dev.adc_mutex);

	if (s_adc_dev.adc_read_sema)
		rtos_deinit_semaphore(&(s_adc_dev.adc_read_sema));

	os_free(s_adc_buf.buf);
	s_adc_buf.buf = NULL;
	s_adc_buf.size = 0;

	os_memset(&g_adc_cfg, 0, sizeof(g_adc_cfg));

	s_adc_driver_is_init = false;

	return BK_OK;
}

bk_err_t bk_adc_init(adc_chan_t adc_chan)
{
#if CONFIG_SARADC_PM_CB_SUPPORT
	pm_cb_conf_t resume_cfg;
#endif

	ADC_RETURN_ON_NOT_INIT();
	ADC_RETURN_ON_INVALID_CHAN(adc_chan);

#if CONFIG_SARADC_PM_CB_SUPPORT
	resume_cfg.cb   = (pm_cb)adc_restore;
	resume_cfg.args = NULL;
	bk_pm_sleep_register_cb(PM_MODE_LOW_VOLTAGE, PM_DEV_ID_SARADC, NULL, &resume_cfg);
	bk_pm_module_vote_power_ctrl(PM_POWER_SUB_MODULE_NAME_BAKP_SADC, PM_POWER_MODULE_STATE_ON);
#endif
#if (CONFIG_SARADC_NEED_FLUSH)
	adc_flush();
#endif
	adc_chan_init_common(adc_chan);

	bk_int_isr_register(INT_SRC_SARADC, adc_isr, NULL);

	bk_adc_set_sample_cnt(ADC_SAMPLE_CNT_DEFAULT);
	sys_hal_set_saradc_config();

	return BK_OK;
}

bk_err_t bk_adc_deinit(adc_chan_t chan)
{
	bk_int_isr_unregister(INT_SRC_SARADC);

	ADC_RETURN_ON_NOT_INIT();
	ADC_RETURN_ON_INVALID_CHAN(chan);

#if CONFIG_SARADC_PM_CB_SUPPORT
	bk_pm_module_vote_power_ctrl(PM_POWER_SUB_MODULE_NAME_BAKP_SADC, PM_POWER_MODULE_STATE_OFF);
	bk_pm_sleep_unregister_cb(PM_MODE_LOW_VOLTAGE, PM_DEV_ID_SARADC, false, true);
#endif
	adc_chan_deinit_common(chan);

	return BK_OK;
}

bk_err_t bk_adc_start(void)
{
	if(adc_hal_check_adc_busy(&s_adc.hal))
		return BK_ERR_ADC_BUSY;

	g_adc_cfg.is_open = 1;
	if (adc_hal_check_adc_enable(&s_adc.hal))
		return BK_OK;

	adc_hal_start_commom(&s_adc.hal);

	return BK_OK;
}

bk_err_t bk_adc_stop(void)
{
	uint32_t ret = 0;

	g_adc_cfg.is_open = 0;
	if (!adc_hal_check_adc_enable(&s_adc.hal))
		return BK_OK;

	s_adc_buf.sample_cnt = 0;

	sys_drv_sadc_int_disable();
	adc_flush();

	return ret;
}

static bk_err_t adc_block_read(uint32_t timeout)
{
	adc_flush();
	sys_drv_sadc_int_enable();

	extern bk_err_t bk7258_saradc_wait_complete(beken_semaphore_t *semaphore,
									 uint32_t timeout_ms);
	int ret = bk7258_saradc_wait_complete(&(s_adc_dev.adc_read_sema), timeout);
	if(ret != kNoErr)
		return BK_ERR_ADC_GET_READ_SEMA;

	return BK_OK;
}

bk_err_t bk_adc_single_read(uint16_t* data)
{
	*data = adc_hal_get_adc_data(&s_adc.hal);
	return BK_OK;
}

bk_err_t bk_adc_read_raw(uint16_t* buf, uint32_t size, uint32_t timeout)
{
	uint32_t ret = 0;

	//TODO init check

	if (size > s_adc_buf.size) {
		ADC_LOGE("adc size %d too big\n", size);
		return BK_ERR_ADC_SIZE_TOO_BIG;
	}

	ret = adc_block_read(timeout);
	if (BK_OK != ret)
		return ret;

	os_memcpy(buf, s_adc_buf.buf, (size<<1));

	return ret;
}

bk_err_t bk_adc_register_isr(adc_isr_t isr, uint32_t param)
{
	ADC_RETURN_ON_NOT_INIT();

	GLOBAL_INT_DECLARATION();
	GLOBAL_INT_DISABLE();
	s_adc_read_isr.callback = isr;
	s_adc_read_isr.param = param;
	GLOBAL_INT_RESTORE();

	return BK_OK;
}

bk_err_t bk_adc_set_clk(adc_src_clk_t src_clk, uint32_t clk)
{
	ADC_RETURN_ON_INVALID_SRC_CLK(src_clk);

	adc_hal_set_clk(&s_adc.hal, src_clk, clk);

	return BK_OK;
}

bk_err_t bk_adc_set_channel(adc_chan_t   adc_chan)
{
	ADC_RETURN_ON_INVALID_CHAN(adc_chan);

	adc_hal_sel_channel(&s_adc.hal, adc_chan);

	return BK_OK;
}

bk_err_t bk_adc_set_sample_rate(uint32_t sample_rate)
{
	adc_hal_set_sample_rate(&s_adc.hal, sample_rate);

	return BK_OK;
}

bk_err_t bk_adc_set_filter(uint32_t filter)
{
	adc_hal_set_adc_filter(&s_adc.hal, filter);

	return BK_OK;
}

bk_err_t bk_adc_set_steady_time(uint32_t steady_ctrl)
{
	adc_hal_set_steady_ctrl(&s_adc.hal, steady_ctrl);

	return BK_OK;
}

bk_err_t bk_adc_set_sample_cnt(uint32_t cnt)
{
	if (cnt == 0) {
		cnt = SOC_ADC_SAMPLE_CNT_MAX;
	}

	if (cnt > SOC_ADC_SAMPLE_CNT_MAX) {
		ADC_LOGW("sample cnt %d too big, default to %d\n", cnt, SOC_ADC_SAMPLE_CNT_MAX);
		cnt = SOC_ADC_SAMPLE_CNT_MAX;
	}

	adc_hal_set_fifo_threshold(&s_adc.hal, cnt);
	return BK_OK;
}

bk_err_t bk_adc_set_saturate_mode(adc_saturate_mode_t mode)
{
	return adc_hal_set_saturate_mode(&s_adc.hal, mode);
}

bk_err_t bk_adc_enable_bypass_clalibration(void)
{
	adc_hal_enable_bypass_calib(&s_adc.hal);
	return BK_OK;
}

bk_err_t bk_adc_disable_bypass_clalibration(void)
{
	adc_hal_disable_bypass_calib(&s_adc.hal);
	return BK_OK;
}

bk_err_t bk_adc_get_saturate_mode(adc_saturate_mode_t *mode)
{
	return BK_OK;
	//return adc_hal_get_sat_ctrl(&s_adc.hal);
}

bk_err_t bk_adc_set_mode(adc_mode_t adc_mode)
{
	ADC_RETURN_ON_INVALID_MODE(adc_mode);

	return adc_hal_set_mode(&s_adc.hal, adc_mode);
}

adc_mode_t bk_adc_get_mode(void)
{
	return adc_hal_get_mode(&s_adc.hal);
}

bk_err_t bk_adc_set_config(adc_config_t *config)
{
    BK_RETURN_ON_NULL(config);
	ADC_RETURN_ON_INVALID_MODE(config->adc_mode);
	ADC_RETURN_ON_INVALID_SRC_CLK(config->src_clk);
	ADC_RETURN_ON_INVALID_CHAN(config->chan);

	if (&g_adc_cfg != config) {
		os_memcpy(&g_adc_cfg, config, sizeof(g_adc_cfg));
	}
	if (config->vol_div == ADC_VOL_DIV_NONE)
	{
		if (config->chan == ADC_0)
		{
			config->vol_div = ADC_VOL_DIV_5;
			adc_hal_set_vol_div(config->chan, ADC_VOL_DIV_5);
		}
		else
		{
			config->vol_div = ADC_VOL_DIV_3;
			adc_hal_set_vol_div(config->chan, ADC_VOL_DIV_3);
		}
	}
	else
	{
		adc_hal_set_vol_div(config->chan, config->vol_div);
	}

	adc_hal_set_clk(&s_adc.hal, config->src_clk, config->clk);
	adc_hal_set_mode(&s_adc.hal, config->adc_mode);

	adc_hal_set_steady_ctrl(&s_adc.hal, config->steady_ctrl);
	adc_hal_set_saturate_mode(&s_adc.hal, config->saturate_mode);

	bk_adc_set_channel(config->chan);

	if(config->adc_mode == ADC_CONTINUOUS_MODE) {
		adc_hal_set_sample_rate(&s_adc.hal, config->sample_rate);
		adc_hal_set_adc_filter(&s_adc.hal, config->adc_filter);
	}
	return BK_OK;
}

bk_err_t bk_adc_set_phy_cali_config(adc_config_t *config)
{
    BK_RETURN_ON_NULL(config);
	ADC_RETURN_ON_INVALID_MODE(config->adc_mode);
	ADC_RETURN_ON_INVALID_SRC_CLK(config->src_clk);
	ADC_RETURN_ON_INVALID_CHAN(config->chan);

	if (&g_adc_cfg != config) {
		os_memcpy(&g_adc_cfg, config, sizeof(g_adc_cfg));
	}

	adc_hal_set_clk(&s_adc.hal, config->src_clk, config->clk);
	adc_hal_set_mode(&s_adc.hal, config->adc_mode);

	adc_hal_set_steady_ctrl(&s_adc.hal, config->steady_ctrl);
	adc_hal_set_saturate_mode(&s_adc.hal, config->saturate_mode);

	bk_adc_set_channel(config->chan);

	if(config->adc_mode == ADC_CONTINUOUS_MODE) {
		adc_hal_set_sample_rate(&s_adc.hal, config->sample_rate);
		adc_hal_set_adc_filter(&s_adc.hal, config->adc_filter);
	}
	return BK_OK;
}

bk_err_t bk_adc_get_config(uint32 adc_ch, adc_config_t **config)
{
    if (adc_ch != g_adc_cfg.chan) {
        return BK_ERR_ADC_CHAN_NOT_INIT;
    }

    *config = &g_adc_cfg;

#include "adc_driver_part3.inc"
