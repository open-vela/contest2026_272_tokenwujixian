    return BK_OK;
}

int saradc_hal_is_fifo_empty(void)
{
    return (!adc_hal_is_fifo_empty(&s_adc.hal));
}

bk_err_t bk_saradc_start_int_disable(void)
{
    if(adc_hal_check_adc_busy(&s_adc.hal))
        return BK_ERR_ADC_BUSY;
    if (adc_hal_check_adc_enable(&s_adc.hal))
        return BK_OK;
    sys_drv_sadc_int_disable();
    return BK_OK;
}

void saradc_hal_start_enable(void)
{
    adc_hal_start_commom(&s_adc.hal);
}

uint32_t bk_saradc_read_raw_data(uint32_t timeout)
{
    uint32_t raw_data = 0;
    raw_data = adc_hal_get_adc_data(&s_adc.hal);
    return raw_data;
}

#if SARADC_AUTOTEST
bk_err_t bk_saradc_set_config(adc_config_t *config, uint32_t div)
{
    BK_RETURN_ON_NULL(config);
    ADC_RETURN_ON_INVALID_MODE(config->adc_mode);
    ADC_RETURN_ON_INVALID_SRC_CLK(config->src_clk);
    ADC_RETURN_ON_INVALID_CHAN(config->chan);
    adc_hal_set_div(&s_adc.hal, div);
    adc_hal_set_mode(&s_adc.hal, config->adc_mode);
    adc_hal_set_steady_ctrl(&s_adc.hal, config->steady_ctrl);
    adc_hal_set_saturate_mode(&s_adc.hal, config->saturate_mode);
    if(config->adc_mode == ADC_CONTINUOUS_MODE)
    {
        BK_LOGD(NULL,"config->sample_rate = %d\r\n",config->sample_rate);
        adc_hal_set_sample_rate(&s_adc.hal, config->sample_rate);
        adc_hal_set_adc_filter(&s_adc.hal, config->adc_filter);
    }
    return BK_OK;
}
#endif

bk_err_t bk_adc_read(uint16_t* data, uint32_t timeout)
{
	uint32_t sum = 0;
	int ret = BK_OK;
	UINT32 num;

	//TODO init check

	if (s_adc_buf.size == 0)
		return BK_ERR_ADC_NOT_INIT;

	ret = adc_block_read(timeout);
	if (BK_OK != ret)
		return ret;

	num = s_adc_buf.size/2 + s_adc_buf.size%2;
	for (uint16_t i = num; i < s_adc_buf.size; i++) {
		sum += s_adc_buf.buf[i];
	}

	sum = sum / (s_adc_buf.size / 2);

#if CONFIG_SARADC_RANGE_DIVIDE
	sum = sum >> 1;
#endif

	*data = sum;

	return ret;
}


static void adc_isr(void *arg)
{
	(void)arg;

	adc_hal_clear_int_status(&s_adc.hal);

	ADC_STATIS_INC(s_adc_statis->adc_isr_cnt);

	adc_read_fifo();

	if (s_adc_buf.sample_cnt >= s_adc_buf.size) {
		rtos_set_semaphore(&(s_adc_dev.adc_read_sema));
		bk_adc_stop();
	}

	if (s_adc_read_isr.callback) {
		s_adc_read_isr.callback(s_adc_read_isr.param);
	}

    if (s_adc_read_isr.iot_callback) {
		s_adc_read_isr.iot_callback(s_adc_buf.buf, s_adc_read_isr.p_iot_context);
	}
}

bk_err_t bk_adc_is_valid_ch(uint32_t ch)
{
    if (!adc_hal_is_valid_channel(&s_adc.hal, ch)) 
    {
        ADC_LOGE("ADC id number(%d) is invalid\r\n", (ch));
        return BK_ERR_ADC_INVALID_CHAN;
    }

    return BK_OK;
}

bk_err_t bk_adc_register_isr_iot_callback(    void* iot_callback, void      * p_iot_context)
{
	ADC_RETURN_ON_NOT_INIT();

	GLOBAL_INT_DECLARATION();
	GLOBAL_INT_DISABLE();
	s_adc_read_isr.iot_callback = (IotAdcCallback_t)iot_callback;
	s_adc_read_isr.p_iot_context = p_iot_context;
	GLOBAL_INT_RESTORE();

	return BK_OK;
}

bk_err_t bk_adc_unregister_isr_iot_callback(void)
{
	ADC_RETURN_ON_NOT_INIT();

	GLOBAL_INT_DECLARATION();
	GLOBAL_INT_DISABLE();
	s_adc_read_isr.iot_callback = NULL;
	s_adc_read_isr.p_iot_context = NULL;
	GLOBAL_INT_RESTORE();

	return BK_OK;
}

bk_err_t bk_adc_en(void)
{
    if(adc_hal_check_adc_busy(&s_adc.hal))
    {
        BK_LOGD(NULL,"adc_start:adc busy\n");
        return BK_ERR_ADC_BUSY;
    }

    adc_hal_start_commom(&s_adc.hal);

	extern bk_err_t bk7258_saradc_wait_complete(beken_semaphore_t *semaphore,
									 uint32_t timeout_ms);
	int ret = bk7258_saradc_wait_complete(&(s_adc_dev.adc_read_sema), 1000);
    if(ret != kNoErr)
    {
        BK_LOGD(NULL,"adc_start:rtos_get_semaphore fail\n");
        return BK_ERR_ADC_GET_READ_SEMA;
    }

    return BK_OK;
}

void saradc_config_param_init_for_temp(saradc_desc_t* adc_config)
{
    os_memset(adc_config, 0x00, sizeof(saradc_desc_t));
    adc_config->channel = 1;
    adc_config->current_read_data_cnt = 0;
    adc_config->current_sample_data_cnt = 0;
    adc_config->filter = 0;
    adc_config->has_data = 0;
    adc_config->all_done = 0;
    adc_config->mode = (ADC_CONFIG_MODE_CONTINUE << 0)
                      |(ADC_CONFIG_MODE_4CLK_DELAY << 2)
                      |(ADC_CONFIG_MODE_SHOULD_OFF);
    adc_config->pre_div = 0x10;
    adc_config->samp_rate = 0x20;
}

float saradc_calculate(UINT16 adc_val)
{
    float practic_voltage;

    /* (adc_val - low) / (practic_voltage - 1Volt) = (high - low) / 1Volt */
    /* practic_voltage = (adc_val - low) / (high - low) + 1Volt */
    practic_voltage = (float)(adc_val - saradc_val.low);
    practic_voltage = (practic_voltage / (float)(saradc_val.high - saradc_val.low)) + 1;

    if (practic_voltage < 0) {
        practic_voltage = 0.0f;
    }

    return practic_voltage;
}

float bk_adc_data_calculate(UINT16 adc_val, UINT8 adc_chan)
{
    float cali_value = 0;

    // CHx: 0:1/4 1:1/3 2:1/2 3:1/1
    // CH0: 0:1/7 1:1/5 2:1/3 3:1/2
    if (adc_chan == ADC_0)
    {
        switch (g_adc_cfg.vol_div)
        {
            case ADC_VOL_DIV_7:
                adc_val = adc_val * 7 / 3;
                break;
            case ADC_VOL_DIV_3:
                break;
            case ADC_VOL_DIV_2:
                adc_val = adc_val * 2 / 3;
                break;
            case ADC_VOL_DIV_5:
            default:
                adc_val = adc_val * 5 / 3;
                break;
        }
    }
    else
    {
        switch (g_adc_cfg.vol_div)
        {
            case ADC_VOL_DIV_4:
                adc_val = adc_val * 4 / 3;
                break;
            case ADC_VOL_DIV_2:
                adc_val = adc_val * 2 / 3;
                break;
            case ADC_VOL_DIV_1:
                adc_val = adc_val * 1 / 3;
                break;
            case ADC_VOL_DIV_3:
            default:
                break;
        }
    }

    if(adc_chan == 7 || adc_chan == 8 || adc_chan == 9 || adc_chan == 11)
    {
        ADC_LOGD("adc_chan %d has been used\r\n", adc_chan);
    }
    else
    {
        cali_value = saradc_calculate(adc_val);
    }
    return cali_value;
}

UINT32 saradc_set_calibrate_val(uint16_t *value, SARADC_MODE mode)
{
    uint32_t irq_level = rtos_disable_int();

    #ifdef CONFIG_FREERTOS_SMP
    spin_lock(&adc_spin_lock);
    #endif // CONFIG_FREERTOS_SMP

    if(SARADC_CALIBRATE_LOW == mode)
    {
        saradc_val.low = *value;
    }
    else if(SARADC_CALIBRATE_HIGH == mode)
    {
        saradc_val.high = *value;
    }
    else
    {
        #ifdef CONFIG_FREERTOS_SMP
        spin_unlock(&adc_spin_lock);
        #endif // CONFIG_FREERTOS_SMP
        rtos_enable_int(irq_level);
        return SARADC_FAILURE;
    }

    #ifdef CONFIG_FREERTOS_SMP
    spin_unlock(&adc_spin_lock);
    #endif // CONFIG_FREERTOS_SMP
    rtos_enable_int(irq_level);
    return SARADC_SUCCESS;
}

void bk_adc_read_for_ate(uint32_t saradc_num, uint16_t *saradc_buf)
{
    uint32_t i = 0;
    int irq_level = 0;

    BK_LOG_ON_ERR(bk_saradc_start_int_disable());
    //saradc enable
    saradc_hal_start_enable();
    //need check saradc fifo empty
    //BK_LOGD(NULL,"saradc_hal_is_fifo_empty=%x\r\n",saradc_hal_is_fifo_empty());
    //BK_LOGD(NULL,"sardata_start\r\n");
    irq_level = rtos_disable_int();

    #ifdef CONFIG_FREERTOS_SMP
    spin_lock(&adc_spin_lock);
    #endif // CONFIG_FREERTOS_SMP

    for(i = 0; i < saradc_num; i++)
    {
        if(saradc_hal_is_fifo_empty())
        {
            saradc_buf[i] = bk_saradc_read_raw_data(1000);
            //saradc_buf[i] = *((volatile unsigned long *) (BASEADDR_SADC+0x4*4));
        }
        else
        {
            i--;
            continue;
        }

    }
    #ifdef CONFIG_FREERTOS_SMP
    spin_unlock(&adc_spin_lock);
    #endif // CONFIG_FREERTOS_SMP
    rtos_enable_int(irq_level);
}

void test_adc_for_ate(adc_chan_t channel, adc_mode_t mode,
					  uint32_t pre_div, uint32_t samp_rate,
					  uint32_t filter, uint32_t sta_ctrl,
					  uint32_t usCount, uint16_t *pDataBuffer)
{
    adc_config_t config = {0};
    uint32_t adc_clk;

    adc_clk = 26000000/2/(pre_div + 1);
    BK_LOG_ON_ERR(bk_adc_acquire());
    BK_LOG_ON_ERR(bk_adc_init(channel));

    config.chan = channel;
    config.adc_mode = mode;
    config.clk = adc_clk;
    config.src_clk = 1;
    config.saturate_mode = 4;
    config.sample_rate = samp_rate;
    config.steady_ctrl= sta_ctrl;
    config.adc_filter = filter;

    BK_LOG_ON_ERR(bk_adc_set_config(&config));
    BK_LOG_ON_ERR(bk_adc_enable_bypass_clalibration());
    BK_LOG_ON_ERR(bk_adc_start());

    bk_adc_read_for_ate(usCount, pDataBuffer);
    BK_LOG_ON_ERR(bk_adc_stop());
    BK_LOG_ON_ERR(bk_adc_deinit(channel));
    BK_LOG_ON_ERR(bk_adc_release());
}
