/* NuttX port: the prologue below came from a wrapper translation unit
 * at wifi/hal_port/vendor_sources/bk_phy_notify.c, which existed only to include this
 * file.  The build lists this file directly now.
 */
/* Wi-Fi-only translation unit: establish Armino's global build contract. */
#include <bk_prelude.h>

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
#include <os/os.h>
#include "flash_driver.h"
// #include "mb_ipc_cmd.h"

static void (*s_phy_op_notify)(uint32_t param) = NULL;

bk_err_t mb_phy_register_op_notify(void * notify_cb)
{
    s_phy_op_notify = (void (*)(uint32_t))notify_cb;
    return BK_OK;
}

bk_err_t mb_phy_unregister_op_notify(void * notify_cb)
{
	if(s_phy_op_notify == notify_cb)
	{
		s_phy_op_notify = NULL;
		return BK_OK;
	}

	return BK_FAIL;
}

#if !CONFIG_PHY_MB
bk_err_t mb_phy_op_prepare(void)
{
	if(s_phy_op_notify != NULL)
		s_phy_op_notify(0);

	return BK_OK;
}

bk_err_t mb_phy_op_finish(void)
{
	if(s_phy_op_notify != NULL)
		s_phy_op_notify(1);

	return BK_OK;
}
#endif

static volatile flash_op_status_t s_phy_op_status = 0;

__attribute__((section(".itcm_sec_code"))) bk_err_t bk_phy_set_operate_status(flash_op_status_t status)
{
	s_phy_op_status = status;
	return BK_OK;
}

__attribute__((section(".itcm_sec_code"))) flash_op_status_t bk_phy_get_operate_status(void)
{
	return s_phy_op_status;
}
