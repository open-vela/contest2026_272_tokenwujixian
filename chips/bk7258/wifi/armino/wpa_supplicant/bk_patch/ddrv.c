/* NuttX port: the prologue below came from a wrapper translation unit
 * at wifi/hal_port/vendor_sources/ddrv.c, which existed only to include this
 * file.  The build lists this file directly now.
 */
/* ioctl_inet(): the transport driver_beken.c uses to reach the WPA/MAC bridge.
 *
 * This file was previously classified as unused in the bk_patch survey. That
 * was wrong: driver_beken.c issues five STA operations through ioctl_inet()
 * (SIOCSIWESSID, SIOCSIWFREQ, SIOCGIWRANGE, PRISM2_IOCTL_PRISM2_PARAM and
 * PRISM2_IOCTL_HOSTAPD), and ioctl_inet() forwards them to hapd_intf_ioctl().
 * Without it the STA driver cannot set an SSID or a channel.
 *
 * It is a small dispatcher, not a socket implementation: it compares the caller
 * socket number against ioctl_get_socket_num() and then calls into the bridge.
 */

#include <bk_prelude.h>

/* Consume the sk_intf/fake_socket guards with the NuttX-safe replacements so
 * the vendored quoted includes cannot re-enter the conflicting bk_patch
 * definitions of struct sockaddr / enum sock_type.
 */

#include <wpa_compat/fake_socket.h>
#include <wpa_compat/sk_intf.h>

#include "driver.h"

#include <common/bk_include.h>
#include "ddrv.h"

#include "sk_intf.h"
#include "ieee802_11_defs.h"
#include "driver_beken.h"
#include "bk_hostapd_intf.h"
#include "bk_wifi_private.h"

int ioctl_host_ap(unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	switch(cmd)
	{
		case PRISM2_IOCTL_HOSTAPD:
			ret = hapd_intf_ioctl(arg);
			break;
			
		case PRISM2_IOCTL_PRISM2_PARAM:
			break;
			
		case SIOCSIWESSID:
			break;
			
		case SIOCGIWRANGE:
			break;
			
		case SIOCSIWFREQ:
			break;

		default:
			break;
	}
	
	return ret;
}

int ioctl_inet(int dev, u8 vif_index, unsigned int cmd, unsigned long arg)
{
    int ret = 0;

    if(ioctl_get_socket_num(vif_index) == dev)
    {
        ret = ioctl_host_ap(cmd, arg);
    }

    return ret;
}

// eof

