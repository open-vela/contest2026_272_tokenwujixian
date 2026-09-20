/*
 * Read-only calibration facade for vendor PHY public APIs.
 *
 * Per-unit OTP/Flash calibration storage is not adapted at this stage. Reads
 * and writes therefore report unsupported.  The BK7258 board vendor-cal table
 * compiled into this image does provide the matching factory crystal trim,
 * which is a safe nonzero fallback for the PHY ABI.
 */

#include <modules/wifi.h>
#include <syslog.h>

/* Defined by the BK7258 vendor-cal board source linked in both build paths.
 * This is a PHY trim code, not an oscillator frequency in Hz. */
extern const UINT32 g_default_xtal;

int manual_cal_get_tx_power(wifi_standard standard, float *power_dbm)
{
  (void)standard;
  (void)power_dbm;
  return BK_ERR_NOT_SUPPORT;
}

int manual_cal_set_tx_power(wifi_standard standard, float power_dbm)
{
  (void)standard;
  (void)power_dbm;
  return BK_ERR_NOT_SUPPORT;
}

UINT32 manual_cal_get_cali_xtal(void)
{
  static bool reported;

  if (!reported)
    {
      reported = true;
      syslog(LOG_INFO, "[BK7258-WIFI] factory xtal trim=%lu\n",
             (unsigned long)g_default_xtal);
    }

  return g_default_xtal;
}
