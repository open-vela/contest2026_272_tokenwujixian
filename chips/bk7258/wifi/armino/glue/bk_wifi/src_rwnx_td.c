
/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include <assert.h>
#include <nuttx/spinlock.h>
#include <string.h>

#include <common/sys_config.h>
#include "rwnx_td.h"

#if CONFIG_RWNX_TD
/*
 * VARIABLES
 ****************************************************************************************
 */

struct rwnx_td_env_tag rwnx_td_env_tab[NX_VIRT_DEV_MAX];

/*
 * PUBLIC FUNCTIONS
 ****************************************************************************************
 */

void rwnx_td_init(void)
{
    uint8_t counter;

    // Reset all rwnx td environments
    for (counter = 0; counter < NX_VIRT_DEV_MAX; counter++)
    {
        rwnx_td_reset(counter);
    }
}

void rwnx_td_reset(uint8_t vif_index)
{
    struct rwnx_td_env_tag *rwnx_td_env = &rwnx_td_env_tab[vif_index];

    // Initialize memory
    memset(rwnx_td_env, 0, sizeof(struct rwnx_td_env_tag));
}

void rwnx_td_pkt_ind(uint8_t vif_index,uint8_t access_category)
{
    struct rwnx_td_env_tag *rwnx_td_env = &rwnx_td_env_tab[vif_index];

    irqstate_t flags = enter_critical_section();
    rwnx_td_env->pkt_cnt++;
    rwnx_td_env->ac_pkt_cnt[access_category]++;
    leave_critical_section(flags);

    #if 0
    BK_LOGD(NULL,"ind vif %x ac %x cnt %d ac cnt %d\r\n",vif_index,access_category,
             rwnx_td_env->pkt_cnt,
             rwnx_td_env->ac_pkt_cnt[access_category]);
    #endif

}

void rwnx_td_pkt_dec(uint8_t vif_index,uint8_t access_category)
{
    struct rwnx_td_env_tag *rwnx_td_env = &rwnx_td_env_tab[vif_index];

    if ((rwnx_td_env->pkt_cnt) == 0)
    {
        DEBUGASSERT(false);
        return;
    }

    irqstate_t flags = enter_critical_section();
    rwnx_td_env->pkt_cnt--;
    rwnx_td_env->ac_pkt_cnt[access_category]--;
    leave_critical_section(flags);

    #if 0
    BK_LOGD(NULL,"dec vif %x ac %x cnt %d ac cnt %d\r\n",vif_index,access_category,
             rwnx_td_env->pkt_cnt,
             rwnx_td_env->ac_pkt_cnt[access_category]);
    #endif
}

void rwnx_get_td_info(uint8_t vif_index, uint16_t *pkt_cnt, bool *vivo)
{
    struct rwnx_td_env_tag *rwnx_td_env = &rwnx_td_env_tab[vif_index];

    irqstate_t flags = enter_critical_section();
    *vivo = (rwnx_td_env->ac_pkt_cnt[AC_VI] + rwnx_td_env->ac_pkt_cnt[AC_VO]) ? true : false;
    *pkt_cnt = rwnx_td_env->pkt_cnt;
    leave_critical_section(flags);
}
#else
void rwnx_td_pkt_dec(uint8_t vif_index,uint8_t access_category)
{
}

void rwnx_get_td_info(uint8_t vif_index, uint16 *pkt_cnt, bool *vivo)
{
}
#endif
