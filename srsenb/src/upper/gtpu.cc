/**
 *
 * \section COPYRIGHT
 *
 * Copyright 2013-2017 Software Radio Systems Limited
 *
 * \section LICENSE
 *
 * This file is part of srsLTE.
 *
 * srsUE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsUE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * A copy of the GNU Affero General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

#include "upper/gtpu.h"
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <linux/ip.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <sys/ioctl.h>
#include <sys/socket.h>


using namespace srslte;

namespace srsenb {
  
bool gtpu::init(std::string gtp_bind_addr_, std::string mme_addr_, srsenb::pdcp_interface_gtpu* pdcp_, srslte::log* gtpu_log_)
{
  pdcp          = pdcp_;
  gtpu_log      = gtpu_log_;
  gtp_bind_addr = gtp_bind_addr_;
  mme_addr      = mme_addr_;

  pthread_mutex_init(&mutex, NULL); 
  
  pool          = byte_buffer_pool::get_instance();

  if(0 != srslte_netsource_init(&src, gtp_bind_addr.c_str(), GTPU_PORT, SRSLTE_NETSOURCE_UDP)) {
    gtpu_log->error("Failed to create source socket on %s:%d", gtp_bind_addr.c_str(), GTPU_PORT);
    return false;
  }
  if(0 != srslte_netsink_init(&snk, mme_addr.c_str(), GTPU_PORT, SRSLTE_NETSINK_UDP)) {
    gtpu_log->error("Failed to create sink socket on %s:%d", mme_addr.c_str(), GTPU_PORT);
    return false;
  }

  // CDonato's hacker code

  char dev[IFNAMSIZ] = "tun_srsenb";
  char *err_str;

  // Construct the TUN device
  tun_fd = open("/dev/net/tun", O_RDWR);
  gtpu_log->info("eNB TUN file descriptor = %d\n", tun_fd);
  if(0 > tun_fd)
  {
      err_str = strerror(errno);
      gtpu_log->debug("Failed to open TUN device: %s\n", err_str);
      return(ERROR_CANT_START);
  }
  memset(&ifr, 0, sizeof(ifr));
  ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
  strncpy(ifr.ifr_ifrn.ifrn_name, dev, IFNAMSIZ);
  if(0 > ioctl(tun_fd, TUNSETIFF, &ifr))
  {
      err_str = strerror(errno);
      gtpu_log->debug("Failed to set TUN device name: %s\n", err_str);
      close(tun_fd);
      return(ERROR_CANT_START);
  }

  // Bring up the interface
  sock = socket(AF_INET, SOCK_DGRAM, 0);
  if(0 > ioctl(sock, SIOCGIFFLAGS, &ifr))
  {
      err_str = strerror(errno);
      gtpu_log->debug("Failed to bring up socket: %s\n", err_str);
      close(tun_fd);
      return(ERROR_CANT_START);
  }
  ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
  if(0 > ioctl(sock, SIOCSIFFLAGS, &ifr))
  {
      err_str = strerror(errno);
      gtpu_log->debug("Failed to set socket flags: %s\n", err_str);
      close(tun_fd);
      return(ERROR_CANT_START);
  }

  // Setup the IP address
  sock                                                   = socket(AF_INET, SOCK_DGRAM, 0);
  ifr.ifr_addr.sa_family                                 = AF_INET;
  ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr.s_addr = inet_addr("172.21.20.100");
  if(0 > ioctl(sock, SIOCSIFADDR, &ifr))
  {
      err_str = strerror(errno);
      gtpu_log->debug("Failed to set socket address: %s\n", err_str);
      close(tun_fd);
      return(ERROR_CANT_START);
  }
  ifr.ifr_netmask.sa_family                                 = AF_INET;
  ((struct sockaddr_in *)&ifr.ifr_netmask)->sin_addr.s_addr = inet_addr("255.255.255.0");
  if(0 > ioctl(sock, SIOCSIFNETMASK, &ifr))
  {
      err_str = strerror(errno);
      gtpu_log->debug("Failed to set socket netmask: %s\n", err_str);
      close(tun_fd);
      return(ERROR_CANT_START);
  }

  filter_gtpu.init("/tmp/addr_filter");
  // char *err_str;
  // sock = socket (AF_INET, SOCK_RAW, IPPROTO_RAW);
  // int one = 1;

  // if (setsockopt (sock, IPPROTO_IP, IP_HDRINCL, &one, sizeof (one)) < 0)
  // {
  //   err_str = strerror(errno);
  //   gtpu_log->debug("Failed to assign options: %s\n", err_str);
  // }

  // END CDonato's hacker code

  srslte_netsink_set_nonblocking(&snk);
  pthread_t thrd;
  pthread_create(&thrd, NULL, &run_rx_thread, this);

  // Setup a thread to receive packets from the src socket
  start(THREAD_PRIO);

  return true;

}

void* gtpu::run_rx_thread(void *m_)
{
  gtpu *m = (gtpu*)m_;
  m->listen_rx_thread();
  return NULL;
}

void gtpu::stop()
{
  if(run_enable) {
    run_enable = false;
    // Wait thread to exit gracefully otherwise might leave a mutex locked
    int cnt=0;
    while(running && cnt<100) {
      usleep(10000);
      cnt++;
    }
    if (running) {
      thread_cancel();
    }
    wait_thread_finish();
  }

  srslte_netsink_free(&snk);
  srslte_netsource_free(&src);
}

// gtpu_interface_pdcp
void gtpu::write_pdu(uint16_t rnti, uint32_t lcid, srslte::byte_buffer_t* pdu)
{
  gtpu_log->info_hex(pdu->msg, pdu->N_bytes, "TX PDU, RNTI: 0x%x, LCID: %d", rnti, lcid);
  gtpu_header_t header;
  header.flags        = 0x30;
  header.message_type = 0xFF;
  header.length       = pdu->N_bytes;
  header.teid         = rnti_bearers[rnti].teids_out[lcid];

// CDonato's code
  char addr_str[INET_ADDRSTRLEN];
  ip_map temp;
  temp.m_rnti = rnti;
  temp.m_lcid = lcid;

  // We copy the source ip addr
  memcpy((void*)temp.ip_addr, (void*)&(pdu->msg[12]), 4);
  inet_ntop(AF_INET, &(pdu->msg[12]), addr_str, INET_ADDRSTRLEN);

  bool ip_found = false;
  for( unsigned int i = 0; i < m_bearers_ip.size(); i++)
  {
    if (memcmp((void*)m_bearers_ip.at(i).ip_addr, (void*) &(pdu->msg[12]), 4) == 0)
    { 
      gtpu_log->debug("Updating values for: %s, rnti = %d, lcid = %d \n", addr_str,  m_bearers_ip.at(i).m_rnti, m_bearers_ip.at(i).m_lcid);
      ip_found = true;
      m_bearers_ip.at(i).m_rnti = rnti;
      m_bearers_ip.at(i).m_lcid = lcid;
      break;
    }
  }
  if (!ip_found)
  {
    gtpu_log->debug("Adding for: %s, rnti = %d, lcid = %d \n", addr_str, temp.m_rnti, temp.m_lcid);
    m_bearers_ip.push_back(temp);
  }
 
  //struct in_addr network;
  //inet_pton(AF_INET, "172.21.20.0", (void *) &network);
  //inet_ntop(AF_INET, &(pdu->msg[16]), addr_str, INET_ADDRSTRLEN);

  if (filter_gtpu.match_rule(pdu->msg, pdu->N_bytes))
  {
    int n = write (tun_fd, pdu->msg, pdu->N_bytes);
  }
  else
  { 
    gtpu_write_header(&header, pdu);
    srslte_netsink_write(&snk, pdu->msg, pdu->N_bytes);
  }

// End CDonato's code

  //gtpu_write_header(&header, pdu);
  //srslte_netsink_write(&snk, pdu->msg, pdu->N_bytes);
  pool->deallocate(pdu);
}

// gtpu_interface_rrc
void gtpu::add_bearer(uint16_t rnti, uint32_t lcid, uint32_t teid_out, uint32_t *teid_in)
{
  // Allocate a TEID for the incoming tunnel
  rntilcid_to_teidin(rnti, lcid, teid_in);
  gtpu_log->info("Adding bearer for rnti: 0x%x, lcid: %d, teid_out: 0x%x, teid_in: 0x%x\n", rnti, lcid, teid_out, *teid_in);

  // Initialize maps if it's a new RNTI
  if(rnti_bearers.count(rnti) == 0) {
    for(int i=0;i<SRSENB_N_RADIO_BEARERS;i++) {
      rnti_bearers[rnti].teids_in[i]  = 0;
      rnti_bearers[rnti].teids_out[i] = 0;
    }
  }

  rnti_bearers[rnti].teids_in[lcid]  = *teid_in;
  rnti_bearers[rnti].teids_out[lcid] = teid_out;
}

void gtpu::rem_bearer(uint16_t rnti, uint32_t lcid)
{
  gtpu_log->info("Removing bearer for rnti: 0x%x, lcid: %d\n", rnti, lcid);

  rnti_bearers[rnti].teids_in[lcid]  = 0;
  rnti_bearers[rnti].teids_out[lcid] = 0;

  // Remove RNTI if all bearers are removed
  bool rem = true;
  for(int i=0;i<SRSENB_N_RADIO_BEARERS; i++) {
    if(rnti_bearers[rnti].teids_in[i] != 0) {
      rem = false;
    }
  }
  if(rem) {
    rnti_bearers.erase(rnti);
  }
}

void gtpu::rem_user(uint16_t rnti)
{
  pthread_mutex_lock(&mutex); 
  rnti_bearers.erase(rnti);
  pthread_mutex_unlock(&mutex); 
}

// CDonato's code
void gtpu::listen_rx_thread()
{
  
  byte_buffer_t *pdu = pool_allocate;
  run_enable = true;
  uint16_t rnti = 0;
  uint16_t lcid = 0;

  while(run_enable) 
  {
    gtpu_log->debug("Waiting for read from tun_srsenb...\n");
    pdu->N_bytes = read(tun_fd, pdu->msg, SRSLTE_MAX_BUFFER_SIZE_BYTES-SRSLTE_BUFFER_HEADER_OFFSET);

    char addr_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(pdu->msg[16]), addr_str, INET_ADDRSTRLEN);
    for( unsigned int i = 0; i < m_bearers_ip.size(); i++)
    {
      gtpu_log->debug("Map: %s, rnti = %d, lcid = %d \n", m_bearers_ip.at(i).ip_addr,  m_bearers_ip.at(i).m_rnti, m_bearers_ip.at(i).m_lcid);
      if (memcmp((void*) m_bearers_ip.at(i).ip_addr, (void*) &(pdu->msg[16]), 4 ) == 0)
      {
        gtpu_log->debug("Matched IP Map: %s, rnti = 0x%x, lcid = %d \n", addr_str,  m_bearers_ip.at(i).m_rnti, m_bearers_ip.at(i).m_lcid);
        rnti = m_bearers_ip.at(i).m_rnti;
        lcid = m_bearers_ip.at(i).m_lcid;
      }
    }

    pthread_mutex_lock(&mutex); 
    bool user_exists = (rnti_bearers.count(rnti) > 0);
    pthread_mutex_unlock(&mutex); 
    
    if(!user_exists) {
      gtpu_log->error("listen_rx_thread: Unrecognized RNTI for DL PDU: 0x%x - dropping packet\n", rnti);
      continue;
    }

    if(lcid < SRSENB_N_SRB || lcid >= SRSENB_N_RADIO_BEARERS) {
      gtpu_log->error("listen_rx_thread: Invalid LCID for DL PDU: %d - dropping packet\n", lcid);
      continue;
    }
  
    gtpu_log->info_hex(pdu->msg, pdu->N_bytes, "listen_rx_thread: RX GTPU PDU rnti=0x%x, lcid=%d", rnti, lcid);

    pdcp->write_sdu(rnti, lcid, pdu);
    do {
      pdu = pool_allocate;
      if (!pdu) {
        gtpu_log->console("listen_rx_thread: GTPU Buffer pool empty. Trying again...\n");
        usleep(10000);
      }
    } while(!pdu); 
    
  }

}

void gtpu::run_thread()
{

  byte_buffer_t *pdu = pool_allocate;
  run_enable = true;
  running=true;

  uint16_t rnti = 0;
  uint16_t lcid = 0;

  while(run_enable) {
    
    pdu->reset();
    gtpu_log->debug("Waiting for read...\n");

    pdu->N_bytes = srslte_netsource_read(&src, pdu->msg, SRSENB_MAX_BUFFER_SIZE_BYTES - SRSENB_BUFFER_HEADER_OFFSET);
    gtpu_header_t header;
    gtpu_read_header(pdu, &header);
  
    teidin_to_rntilcid(header.teid, &rnti, &lcid);


    pthread_mutex_lock(&mutex); 
    bool user_exists = (rnti_bearers.count(rnti) > 0);
    pthread_mutex_unlock(&mutex); 
    
    if(!user_exists) {
      gtpu_log->error("Unrecognized RNTI for DL PDU: 0x%x - dropping packet\n", rnti);
      continue;
    }

    if(lcid < SRSENB_N_SRB || lcid >= SRSENB_N_RADIO_BEARERS) {
      gtpu_log->error("Invalid LCID for DL PDU: %d - dropping packet\n", lcid);
      continue;
    }
  
    gtpu_log->info_hex(pdu->msg, pdu->N_bytes, "RX GTPU PDU rnti=0x%x, lcid=%d", rnti, lcid);

    pdcp->write_sdu(rnti, lcid, pdu);
    do {
      pdu = pool_allocate;
      if (!pdu) {
        gtpu_log->console("GTPU Buffer pool empty. Trying again...\n");
        usleep(10000);
      }
    } while(!pdu); 
  }
  running=false;
}

/****************************************************************************
 * Header pack/unpack helper functions
 * Ref: 3GPP TS 29.281 v10.1.0 Section 5
 ***************************************************************************/

bool gtpu::gtpu_write_header(gtpu_header_t *header, srslte::byte_buffer_t *pdu)
{
  if(header->flags != 0x30) {
    gtpu_log->error("gtpu_write_header - Unhandled header flags: 0x%x\n", header->flags);
    return false;
  }
  if(header->message_type != 0xFF) {
    gtpu_log->error("gtpu_write_header - Unhandled message type: 0x%x\n", header->message_type);
    return false;
  }
  if(pdu->get_headroom() < GTPU_HEADER_LEN) {
    gtpu_log->error("gtpu_write_header - No room in PDU for header\n");
    return false;
  }

  pdu->msg      -= GTPU_HEADER_LEN;
  pdu->N_bytes  += GTPU_HEADER_LEN;

  uint8_t *ptr = pdu->msg;

  *ptr = header->flags;
  ptr++;
  *ptr = header->message_type;
  ptr++;
  uint16_to_uint8(header->length, ptr);
  ptr += 2;
  uint32_to_uint8(header->teid, ptr);

  return true;
}

bool gtpu::gtpu_read_header(srslte::byte_buffer_t *pdu, gtpu_header_t *header)
{
  uint8_t *ptr  = pdu->msg;

  pdu->msg      += GTPU_HEADER_LEN;
  pdu->N_bytes  -= GTPU_HEADER_LEN;

  header->flags         = *ptr;
  ptr++;
  header->message_type  = *ptr;
  ptr++;
  uint8_to_uint16(ptr, &header->length);
  ptr += 2;
  uint8_to_uint32(ptr, &header->teid);

  if(header->flags != 0x30) {
    gtpu_log->error("gtpu_read_header - Unhandled header flags: 0x%x\n", header->flags);
    return false;
  }
  if(header->message_type != 0xFF) {
    gtpu_log->error("gtpu_read_header - Unhandled message type: 0x%x\n", header->message_type);
    return false;
  }

  return true;
}

/****************************************************************************
 * TEID to RNIT/LCID helper functions
 ***************************************************************************/
void gtpu::teidin_to_rntilcid(uint32_t teidin, uint16_t *rnti, uint16_t *lcid)
{
  *lcid = teidin & 0xFFFF;
  *rnti = (teidin >> 16) & 0xFFFF;
}

void gtpu::rntilcid_to_teidin(uint16_t rnti, uint16_t lcid, uint32_t *teidin)
{
  *teidin = (rnti << 16) | lcid;
}
 
} // namespace srsenb
