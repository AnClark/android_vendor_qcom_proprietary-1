#ifndef DIAG_LSM_PKT_I_H
#define DIAG_LSM_PKT_I_H

/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                Internal Header File for Event Packet Req/Res Service Mapping

GENERAL DESCRIPTION

Copyright (c) 2007-2011, 2013-2014 Qualcomm Technologies, Inc.  All Rights Reserved.
Qualcomm Technologies Proprietary and Confidential
*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

/*===========================================================================
                        EDIT HISTORY FOR FILE

$Header:

when       who     what, where, why
--------   ---     ---------------------------------------------------------
02/27/08   JV     Created File
===========================================================================*/

/*
 * Structure to support Local DCI packets. Defines the header
 * format for DCI packets
 */
typedef PACK(struct) {
	uint8 start;
	uint8 version;
	uint16 length;
	uint8 code;
	uint32 tag;
} diag_dci_pkt_header_t;

/*
 * Structure to support Local DCI Delayed response packets.
 * Defines the header format of DCI Delayed Response Packets.
 */
typedef PACK(struct) diag_dci_delayed_rsp_tbl_t{
	int delayed_rsp_id;
	int dci_tag;
	struct diag_dci_delayed_rsp_tbl_t *next;
	struct diag_dci_delayed_rsp_tbl_t *prev;
} diag_dci_delayed_rsp_tbl_t;

boolean Diag_LSM_Pkt_Init(void);

/* clean up packet Req/Res service before exiting legacy service mapping layer.
Currently does nothing, just returns TRUE */
boolean Diag_LSM_Pkt_DeInit(void);

void diagpkt_LSM_process_request (void *req_pkt, uint16 pkt_len,
				  diag_cmd_rsp rsp_func, void *rsp_func_param,
				  int type);

/*===========================================================================

FUNCTION DIAGPKT_FREE

DESCRIPTION
  This function free the packet allocated by diagpkt_alloc(), which doesn't
  need to 'commit' for sending as a response because it is merely a temporary
  processing packet.

===========================================================================*/
  void diagpkt_free(PACK(void *)pkt);

#endif /* DIAG_LSM_EVENT_I_H */
