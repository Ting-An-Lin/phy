/******************************************************************************
*
*   Copyright (c) 2019 Intel.
*
*   Licensed under the Apache License, Version 2.0 (the "License");
*   you may not use this file except in compliance with the License.
*   You may obtain a copy of the License at
*
*       http://www.apache.org/licenses/LICENSE-2.0
*
*   Unless required by applicable law or agreed to in writing, software
*   distributed under the License is distributed on an "AS IS" BASIS,
*   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*   See the License for the specific language governing permissions and
*   limitations under the License.
*
*******************************************************************************/

/**
 * @file
 * This file consist of implementation of FAPI STOP.indication message.
 *
 **/

#include "nr5g_fapi_framework.h"
#include "gnb_l1_l2_api.h"
#include "nr5g_fapi_fapi2mac_api.h"
#include "nr5g_fapi_fapi2mac_p5_proc.h"

/** @ingroup group_source_api_p5_fapi2mac_proc
 *
 *  @param[in]  p_phy_ctx Pointer to PHY context.
 *  @param[in]  p_fapi_resp Pointer to IAPI START.response message structure.
 *  @return     Returns ::SUCCESS and ::FAILURE.
 *
 *  @description
 *  This message allows PHY to indicate that it has successfully started PHY
 *  
 *
**/
uint8_t nr5g_fapi_start_resp(
    p_nr5g_fapi_phy_ctx_t p_phy_ctx,
    PSTARTRESPONSEStruct p_iapi_resp)
{
    uint8_t phy_id;
#ifdef DEBUG_MODE
    fapi_vendor_ext_start_response_t *p_fapi_resp;
#endif
    fapi_error_ind_t *p_fapi_error_ind;
    p_fapi_api_queue_elem_t p_list_elem;
    p_nr5g_fapi_phy_instance_t p_phy_instance = NULL;
    nr5g_fapi_stats_t *p_stats;

    if (NULL == p_phy_ctx) {
        NR5G_FAPI_LOG(ERROR_LOG, ("[START.response] Invalid " "phy context"));
        return FAILURE;
    }

    if (NULL == p_iapi_resp) {
        NR5G_FAPI_LOG(ERROR_LOG, ("[START.response] Invalid "
                "start response message"));
        return FAILURE;
    }

    phy_id = p_iapi_resp->sSFN_Slot.nCarrierIdx;
    p_phy_instance = &p_phy_ctx->phy_instance[phy_id];
    if (p_phy_instance->phy_id != phy_id) {
        NR5G_FAPI_LOG(ERROR_LOG, ("[START.response] Invalid  " "phy instance"));
        return FAILURE;
    }

    p_stats = &p_phy_instance->stats;
    p_stats->iapi_stats.iapi_start_res++;
    if (0 == p_iapi_resp->nStatus) {
        if (FAPI_STATE_CONFIGURED == p_phy_instance->state) {
            p_phy_instance->state = FAPI_STATE_RUNNING;
        }
#ifdef DEBUG_MODE
        p_list_elem =
            nr5g_fapi_fapi2mac_create_api_list_elem
            (FAPI_VENDOR_EXT_START_RESPONSE, 1,
            sizeof(fapi_vendor_ext_start_response_t));
        if (!p_list_elem) {
            NR5G_FAPI_LOG(ERROR_LOG, ("[START.Response] Unable to create "
                    "list element. Out of memory!!!"));
            return FAILURE;
        }

        p_fapi_resp = (fapi_vendor_ext_start_response_t *) (p_list_elem + 1);
        p_fapi_resp->header.msg_id = FAPI_VENDOR_EXT_START_RESPONSE;
        p_fapi_resp->header.length =
            (uint16_t) sizeof(fapi_vendor_ext_start_response_t);

        /* Add element to send list */
        nr5g_fapi_fapi2mac_add_api_to_list(phy_id, p_list_elem);
        p_stats->fapi_stats.fapi_vext_start_res++;
        NR5G_FAPI_LOG(INFO_LOG, ("[START.response][%d]", phy_id));
#endif
    } else if (1 == p_iapi_resp->nStatus) {
        p_list_elem =
            nr5g_fapi_fapi2mac_create_api_list_elem(FAPI_ERROR_INDICATION, 1,
            sizeof(fapi_error_ind_t));
        if (!p_list_elem) {
            NR5G_FAPI_LOG(ERROR_LOG, ("[START.response] Unable to create "
                    "list element. Out of memory!!!"));
            return FAILURE;
        }

        /* PHY STOP Failed. Sending Error Indication to MAC */
        p_fapi_error_ind = (fapi_error_ind_t *) (p_list_elem + 1);
        p_fapi_error_ind->header.msg_id = FAPI_ERROR_INDICATION;
        p_fapi_error_ind->header.length = (uint16_t) sizeof(fapi_error_ind_t);
        p_fapi_error_ind->sfn = p_iapi_resp->sSFN_Slot.nSFN;
        p_fapi_error_ind->slot = p_iapi_resp->sSFN_Slot.nSlot;
        p_fapi_error_ind->message_id = FAPI_START_REQUEST;
        p_fapi_error_ind->error_code = p_iapi_resp->nStatus;

        /* Add element to send list */
        nr5g_fapi_fapi2mac_add_api_to_list(phy_id, p_list_elem);
        p_stats->fapi_stats.fapi_error_ind++;
        NR5G_FAPI_LOG(INFO_LOG, ("[ERROR.Indication][%d]", phy_id));
    } else {
        NR5G_FAPI_LOG(ERROR_LOG, ("[START.response] Invalid status "
                "from PHY"));
        return FAILURE;
    }

    return SUCCESS;
}
