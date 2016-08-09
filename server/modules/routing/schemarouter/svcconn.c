/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <shardrouter.h>

void
subsvc_set_state(SUBSERVICE* svc,subsvc_state_t state)
{
    if(state & SUBSVC_WAITING_RESULT)
    {

        /** Increase waiter count */
       atomic_add(&svc->n_res_waiting, 1);
    }
    
    svc->state |= state;
}

void
subsvc_clear_state(SUBSERVICE* svc,subsvc_state_t state)
{
    

    if(state & SUBSVC_WAITING_RESULT)
    {
        /** Decrease waiter count */
        atomic_add(&svc->n_res_waiting, -1);
    }
    
    svc->state &= ~state;
}

bool 
get_shard_subsvc(SUBSERVICE** subsvc,ROUTER_CLIENT_SES* session,char* target)
{
    int i;
    
    if(subsvc == NULL || session == NULL || target == NULL)
	return false;

    for(i = 0;i<session->n_subservice;i++)
    {
        if(strcmp(session->subservice[i]->service->name,target) == 0)
        {
            
            if (SUBSVC_IS_OK(session->subservice[i]))
            {
                if(subsvc_is_valid(session->subservice[i])){
                    *subsvc = session->subservice[i];
                    return true;
                }
                
                /**
                 * The service has failed 
                 */
                
                subsvc_set_state(session->subservice[i],SUBSVC_FAILED);
            }
        } 
    }
    
    return false;
}
