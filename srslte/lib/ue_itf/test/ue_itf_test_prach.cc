/**
 *
 * \section COPYRIGHT
 *
 * Copyright 2013-2014 The srsLTE Developers. See the
 * COPYRIGHT file at the top-level directory of this distribution.
 *
 * \section LICENSE
 *
 * This file is part of the srsLTE library.
 *
 * srsLTE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsLTE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * A copy of the GNU Lesser General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

#include <unistd.h>

#include "srslte/utils/debug.h"
#include "srslte/ue_itf/phy.h"
#include "srslte/ue_itf/tti_sync_cv.h"
#include "srslte/cuhd/radio_uhd.h"

/**********************************************************************
 *  Program arguments processing
 ***********************************************************************/
typedef struct {
  float uhd_rx_freq;
  float uhd_tx_freq; 
  float uhd_rx_gain;
  float uhd_tx_gain;
}prog_args_t;

void args_default(prog_args_t *args) {
  args->uhd_rx_freq = -1.0;
  args->uhd_tx_freq = -1.0;
  args->uhd_rx_gain = 60.0;
  args->uhd_tx_gain = 40.0; 
}

void usage(prog_args_t *args, char *prog) {
  printf("Usage: %s [gGv] -f rx_frequency -F tx_frequency (in Hz)\n", prog);
  printf("\t-g UHD RX gain [Default %.2f dB]\n", args->uhd_rx_gain);
  printf("\t-G UHD TX gain [Default %.2f dB]\n", args->uhd_tx_gain);
  printf("\t-v [increase verbosity, default none]\n");
}

void parse_args(prog_args_t *args, int argc, char **argv) {
  int opt;
  args_default(args);
  while ((opt = getopt(argc, argv, "gGfFv")) != -1) {
    switch (opt) {
    case 'g':
      args->uhd_rx_gain = atof(argv[optind]);
      break;
    case 'G':
      args->uhd_tx_gain = atof(argv[optind]);
      break;
    case 'f':
      args->uhd_rx_freq = atof(argv[optind]);
      break;
    case 'F':
      args->uhd_tx_freq = atof(argv[optind]);
      break;
    case 'v':
      srslte_verbose++;
      break;
    default:
      usage(args, argv[0]);
      exit(-1);
    }
  }
  if (args->uhd_rx_freq < 0 || args->uhd_tx_freq < 0) {
    usage(args, argv[0]);
    exit(-1);
  }
}



typedef enum{
    rar_tpc_n6dB = 0,
    rar_tpc_n4dB,
    rar_tpc_n2dB,
    rar_tpc_0dB,
    rar_tpc_2dB,
    rar_tpc_4dB,
    rar_tpc_6dB,
    rar_tpc_8dB,
    rar_tpc_n_items,
}rar_tpc_command_t;
static const char tpc_command_text[rar_tpc_n_items][8] = {"-6dB", "-4dB", "-2dB",  "0dB", "2dB",  "4dB",  "6dB",  "8dB"};
typedef enum{
    rar_header_type_bi = 0,
    rar_header_type_rapid,
    rar_header_type_n_items,
}rar_header_t;
static const char rar_header_text[rar_header_type_n_items][8] = {"BI", "RAPID"};

typedef struct {
  rar_header_t      hdr_type;
  bool              hopping_flag;
  rar_tpc_command_t tpc_command;
  bool              ul_delay;
  bool              csi_req;
  uint16_t          rba; 
  uint16_t          timing_adv_cmd;
  uint16_t          temp_c_rnti;
  uint8_t           mcs; 
  uint8_t           RAPID;
  uint8_t           BI;
}rar_msg_t; 

char *bool_to_string(bool x) {
  if (x) {
    return (char*) "Enabled";
  } else {
    return (char*) "Disabled";
  }
}

void rar_msg_fprint(FILE *stream, rar_msg_t *msg) 
{
  fprintf(stream, "Header type:  %s\n", rar_header_text[msg->hdr_type]);
  fprintf(stream, "Hopping flag: %s\n", bool_to_string(msg->hopping_flag));
  fprintf(stream, "TPC command:  %s\n", tpc_command_text[msg->tpc_command]);
  fprintf(stream, "UL delay:     %s\n", bool_to_string(msg->ul_delay));
  fprintf(stream, "CSI required: %s\n", bool_to_string(msg->csi_req));
  fprintf(stream, "RBA:          %d\n", msg->rba);
  fprintf(stream, "TA:           %d\n", msg->timing_adv_cmd);
  fprintf(stream, "T-CRNTI:      %d\n", msg->temp_c_rnti);
  fprintf(stream, "MCS:          %d\n", msg->mcs);
  fprintf(stream, "RAPID:        %d\n", msg->RAPID);
  fprintf(stream, "BI:           %d\n", msg->BI);
}

int rar_unpack(uint8_t *buffer, rar_msg_t *msg)
{
    int ret = SRSLTE_ERROR;
    uint8_t *ptr = buffer; 
    
    if(buffer != NULL &&
          msg != NULL)
    {
      ptr++;
      msg->hdr_type = (rar_header_t) *ptr++;
      if(msg->hdr_type == rar_header_type_bi) {
        ptr += 2; 
        msg->BI = srslte_bit_unpack(&ptr, 4);
        ret = SRSLTE_SUCCESS; 
      } else if (msg->hdr_type == rar_header_type_rapid) {
        msg->RAPID = srslte_bit_unpack(&ptr, 6);
        ptr++;
        
        msg->timing_adv_cmd = srslte_bit_unpack(&ptr, 11);
        msg->hopping_flag   = *ptr++;
        msg->rba            = srslte_bit_unpack(&ptr, 10); 
        msg->mcs            = srslte_bit_unpack(&ptr, 4);
        msg->tpc_command    = (rar_tpc_command_t) srslte_bit_unpack(&ptr, 3);
        msg->ul_delay       = *ptr++;
        msg->csi_req        = *ptr++;
        msg->temp_c_rnti    = srslte_bit_unpack(&ptr, 16);
        ret = SRSLTE_SUCCESS;
      } 
    }

    return(ret);
}



srslte::ue::phy phy;

uint8_t payload[1024]; 
const uint8_t conn_request_msg[] = {0x20, 0x06, 0x1F, 0x5C, 0x2C, 0x04, 0xB2, 0xAC, 0xF6, 0x00, 0x00, 0x00};

enum mac_state {RA, RAR, CONNSETUP} state = RA; 

uint32_t ra_tti = 0, conreq_tti = 0;  
uint32_t preamble_idx = 0; 
rar_msg_t rar_msg;
uint32_t nof_rx_connsetup = 0, nof_rx_rar = 0, nof_tx_ra = 0; 

void config_phy() {
  phy.set_param(srslte::ue::params::PRACH_CONFIG_INDEX, 0);
  phy.set_param(srslte::ue::params::PRACH_FREQ_OFFSET, 0);
  phy.set_param(srslte::ue::params::PRACH_HIGH_SPEED_FLAG, 0);
  phy.set_param(srslte::ue::params::PRACH_ROOT_SEQ_IDX, 0);
  phy.set_param(srslte::ue::params::PRACH_ZC_CONFIG, 1);

  phy.set_param(srslte::ue::params::PUSCH_BETA, 10);
  phy.set_param(srslte::ue::params::PUSCH_RS_GROUP_HOPPING_EN, 0);
  phy.set_param(srslte::ue::params::PUSCH_RS_SEQUENCE_HOPPING_EN, 0);
  phy.set_param(srslte::ue::params::PUSCH_RS_CYCLIC_SHIFT, 0);
  phy.set_param(srslte::ue::params::PUSCH_HOPPING_OFFSET, 0);
  
}

// This is the MAC implementation
void run_tti(uint32_t tti) {
  INFO("MAC running tti: %d\n", tti);

  // Get buffer 
  srslte::ue::dl_buffer *dl_buffer = phy.get_dl_buffer(tti); 

  if (dl_buffer) {
    fprintf(stderr, "Error getting DL buffer for tti %d\n", tti);
    return; 
  }
  
  if (state == RA) { 
    // Indicate PHY to transmit the PRACH when possible 
    if (phy.send_prach(preamble_idx)) {
      ra_tti = tti; 
      nof_tx_ra++;
      state = RAR; 
    } else {
      fprintf(stderr, "Error sending PRACH\n");
      exit(-1);
    }
  } else if (state == RAR) { 
    srslte::ue::sched_grant rar_grant(srslte::ue::sched_grant::DOWNLINK, 2); 
    // Assume the maximum RA-window
    uint32_t interval = (tti-ra_tti)%10240;
    if (interval >= 3 && interval <= 13) {        
      // Get DL grant for RA-RNTI=2
      if (dl_buffer->get_dl_grant(srslte::ue::dl_buffer::PDCCH_DL_SEARCH_RARNTI, &rar_grant)) 
      {
        // Decode packet
        if (dl_buffer->decode_pdsch(rar_grant, payload)) {
          INFO("RAR received tti: %d\n", tti);            
          rar_unpack(payload, &rar_msg);

          INFO("Received RAR for preamble %d\n", rar_msg.RAPID);              
          if (rar_msg.RAPID == preamble_idx) {

            nof_rx_rar++;
            
            if (SRSLTE_VERBOSE_ISINFO()) {
              rar_msg_fprint(stdout, &rar_msg);              
            }
            
            // Set time advance
            phy.set_timeadv_rar(rar_msg.timing_adv_cmd);

            // Generate Msg3 grant
            srslte::ue::sched_grant connreq_grant(srslte::ue::sched_grant::UPLINK, rar_msg.temp_c_rnti); 
            phy.rar_ul_grant(rar_msg.rba, rar_msg.mcs, rar_msg.hopping_flag, &connreq_grant);
            
            // Pack Msg3 bits
            srslte_bit_pack_vector((uint8_t*) conn_request_msg, payload, connreq_grant.get_tbs());

            // Get UL buffer 
            srslte::ue::ul_buffer *ul_buffer = phy.get_ul_buffer(tti+6); 
            
            // Generate PUSCH
            if (ul_buffer) {
              ul_buffer->generate_pusch(connreq_grant, payload);

              // Save transmission time
              conreq_tti = tti;        
              state = CONNSETUP;                              
            } else {
              fprintf(stderr, "Error getting UL buffer for TTI %d\n", tti);
              state = RA; 
            }
          } 
        }        
      }       
    }
  } else {
    srslte::ue::sched_grant conn_setup_grant(srslte::ue::sched_grant::DOWNLINK, rar_msg.temp_c_rnti); 
    if ((tti - conreq_tti)%10240 == 4) {
      // Get DL grant for RA-RNTI=2
      if (dl_buffer->get_dl_grant(srslte::ue::dl_buffer::PDCCH_DL_SEARCH_TEMPORAL, &conn_setup_grant)) 
      {
        // Decode packet
        if (dl_buffer->decode_pdsch(conn_setup_grant, payload)) {
          INFO("ConnSetup received tti=%d\n", tti);
          nof_rx_connsetup++;
          state = RA; 
        }
      }
    }
  }
  if (srslte_verbose == SRSLTE_VERBOSE_NONE) {
    printf("RECV RAR %.1f \%% RECV ConnSetup %.1f \%% (total RA: %5u) \r", 
         (float) 100*nof_rx_rar/nof_tx_ra, 
         (float) 100*nof_rx_connsetup/nof_tx_ra, 
         nof_tx_ra);    
  }

  dl_buffer->ready();
}

int main(int argc, char *argv[])
{
  srslte_cell_t cell; 
  uint8_t bch_payload[SRSLTE_BCH_PAYLOAD_LEN];
  prog_args_t prog_args; 
  srslte::ue::tti_sync_cv ttisync(10240); 
  srslte::radio_uhd radio_uhd; 
  
  parse_args(&prog_args, argc, argv);

  // Init Radio 
  radio_uhd.init();
  
  // Init PHY 
  phy.init(&radio_uhd, &ttisync);
  
  // Give it time to create thread 
  sleep(1);
  
  // Setup PHY parameters
  config_phy();
  
  // Set RX freq and gain
  phy.get_radio()->set_rx_freq(prog_args.uhd_rx_freq);
  phy.get_radio()->set_tx_freq(prog_args.uhd_tx_freq);
  phy.get_radio()->set_rx_gain(prog_args.uhd_rx_gain);
  phy.get_radio()->set_tx_gain(prog_args.uhd_tx_gain);
  
  /* Instruct the PHY to decode BCH */
  if (!phy.decode_mib_best(&cell, bch_payload)) {
    exit(-1);
  }
  // Print MIB 
  srslte_cell_fprint(stdout, &cell, phy.get_current_tti()/10);
  
  // Set the current PHY cell to the detected cell
  if (!phy.set_cell(cell)) {
    printf("Error setting cell\n");
    exit(-1);
  }
  
  /* Instruct the PHY to start RX streaming and synchronize */
  if (!phy.start_rxtx()) {
    printf("Could not start RX\n");
    exit(-1);
  }
  /* go to idle and process each tti */
  while(1) {
    uint32_t tti = ttisync.wait();
    run_tti(tti);
  }
}



