/**********************************************************************
 * file:  sr_router.c
 * date:  Mon Feb 18 12:50:42 PST 2002
 * Contact: casado@stanford.edu
 *
 * Description:
 *
 * This file contains all the functions that interact directly
 * with the routing table, as well as the main entry method
 * for routing.
 *
 **********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#include "sr_if.h"
#include "sr_rt.h"
#include "sr_router.h"
#include "sr_protocol.h"
#include "sr_arpcache.h"
#include "sr_utils.h"

/* TODO: Add constant definitions here... */
struct info{
  uint32_t ip;
  char interface[sr_IFACE_NAMELEN];
};
typedef struct info info_t;

/* TODO: Add helper functions here... */

info_t longest_prefix_match(struct sr_rt* routing_table,uint32_t query_ip){
  info_t max_match = {0,{0}};
  struct sr_rt* rt_entry = routing_table;
  while(rt_entry!=NULL){
    if((query_ip & ntohl(rt_entry->mask.s_addr)) == 
      (ntohl(rt_entry->gw.s_addr) & ntohl(rt_entry->mask.s_addr))){
      if(max_match.ip < ntohl(rt_entry->gw.s_addr)){
        max_match.ip = ntohl(rt_entry->gw.s_addr);
        strcpy(max_match.interface,rt_entry->interface);
        print_addr_ip_int(max_match.ip);
      }
    }
    rt_entry = rt_entry->next;
  }
  return max_match;
}

/* See pseudo-code in sr_arpcache.h */
void handle_arpreq(struct sr_instance* sr, struct sr_arpreq *req){
  time_t now = time(0);
  if(difftime(now, req->sent) > 1.0){
    if(req->times_sent >= 5){
      printf("More than 5\n");  fflush(stdout);
      struct sr_packet *packet = req->packets;
      while(packet){
        /*echo request */

        int len = sizeof(sr_ethernet_hdr_t)+sizeof(sr_ip_hdr_t)+sizeof(sr_icmp_t3_hdr_t);
        uint8_t* icmp_reply = (uint8_t*)malloc(len);
        memcpy(icmp_reply,packet->buf,sizeof(sr_ethernet_hdr_t)+sizeof(sr_ip_hdr_t));

        /*Edit ethernet header*/
        sr_ethernet_hdr_t* ethernet_header = (sr_ethernet_hdr_t*)icmp_reply;
        int pos;
        char tmp[ETHER_ADDR_LEN];
        for (pos = 0; pos < ETHER_ADDR_LEN; pos++) 
          tmp[pos] = ethernet_header->ether_dhost[pos];
        for (pos = 0; pos < ETHER_ADDR_LEN; pos++) 
          ethernet_header->ether_dhost[pos] = ethernet_header->ether_shost[pos];
        for (pos = 0; pos < ETHER_ADDR_LEN; pos++) 
          ethernet_header->ether_shost[pos] = tmp[pos];

        char* interface = NULL;
        uint32_t ip;
        struct sr_if* if_list = sr->if_list;
        while(if_list){
          int match = 1;
          for (pos = 0; pos < ETHER_ADDR_LEN; pos++){
            if(if_list->addr[pos] != ethernet_header->ether_shost[pos])
              match = 0;
          }
          if(match){
            interface = if_list->name;
            ip = if_list->ip;
            break;
          }
          if_list = if_list->next;
        }

        /* Edit IP Header */
        sr_ip_hdr_t* ip_header = (sr_ip_hdr_t*)(icmp_reply+sizeof(sr_ethernet_hdr_t));
        ip_header->ip_len = htons(sizeof(sr_icmp_t3_hdr_t)+sizeof(sr_ip_hdr_t));
        ip_header->ip_dst = ip_header->ip_src;
        ip_header->ip_src = ip;
        ip_header->ip_sum = 0;
        ip_header->ip_sum = cksum(ip_header,sizeof(sr_ip_hdr_t));        

        /* Edit ICMP3 Header */
        sr_icmp_t3_hdr_t* icmp_header = (sr_icmp_t3_hdr_t*)(icmp_reply+sizeof(sr_ethernet_hdr_t)+sizeof(sr_ip_hdr_t));
        icmp_header->icmp_type = 3;
        icmp_header->icmp_code = 1;
        icmp_header->unused = 0;
        icmp_header->next_mtu = 0;
        memcpy(icmp_header->data,packet->buf+sizeof(sr_ethernet_hdr_t),sizeof(sr_ip_hdr_t)+8);
        icmp_header->icmp_sum = 0;
        icmp_header->icmp_sum = cksum(icmp_header,sizeof(sr_icmp_t3_hdr_t));

        sr_send_packet(sr,icmp_reply,len,interface);
        packet = packet->next;
      }      
      sr_arpreq_destroy(&(sr->cache),req);
    }else{
      /*send arp request*/
      uint8_t* arp_request = (uint8_t*)malloc(sizeof(sr_ethernet_hdr_t)+sizeof(sr_arp_hdr_t));
      char* req_interface = req->packets->iface;
      struct sr_if* if_details = sr_get_interface(sr,req_interface);

      /*Edit Ethernet header*/
      sr_ethernet_hdr_t* ethernet_header = (sr_ethernet_hdr_t*)arp_request;
      int pos;
      for (pos = 0; pos < ETHER_ADDR_LEN; pos++) 
        ethernet_header->ether_shost[pos] = if_details->addr[pos]; 
      for (pos = 0; pos < ETHER_ADDR_LEN; pos++) 
        ethernet_header->ether_dhost[pos] = 255;     
      ethernet_header->ether_type = htons(ethertype_arp); 

      /* Edit ARP header */
      sr_arp_hdr_t* arp_header = (sr_arp_hdr_t*)(arp_request+sizeof(sr_ethernet_hdr_t));
      arp_header->ar_hrd = htons(arp_hrd_ethernet);
      arp_header->ar_pro = htons(2048);
      arp_header->ar_hln = 6;
      arp_header->ar_pln = 4;
      arp_header->ar_op = htons(arp_op_request);
      for (pos = 0; pos < ETHER_ADDR_LEN; pos++) 
        arp_header->ar_tha[pos] = 255;
      for (pos = 0; pos < ETHER_ADDR_LEN; pos++) 
        arp_header->ar_sha[pos] = ethernet_header->ether_shost[pos];
      arp_header->ar_tip = htonl(req->ip);
      arp_header->ar_sip = if_details->ip;
      printf("ARP Request header\n"); fflush(stdout);
      /*print_hdrs(arp_request,sizeof(sr_ethernet_hdr_t)+sizeof(sr_arp_hdr_t));*/
      sr_send_packet(sr,arp_request,sizeof(sr_ethernet_hdr_t)+sizeof(sr_arp_hdr_t),if_details->name);
      req->sent = time(0);
      req->times_sent++;
    }
  }
}

/*---------------------------------------------------------------------
 * Method: sr_init(void)
 * Scope:  Global
 *
 * Initialize the routing subsystem
 *
 *---------------------------------------------------------------------*/

void sr_init(struct sr_instance* sr)
{
    /* REQUIRES */
    assert(sr);

    /* Initialize cache and cache cleanup thread */
    sr_arpcache_init(&(sr->cache));

    pthread_attr_init(&(sr->attr));
    pthread_attr_setdetachstate(&(sr->attr), PTHREAD_CREATE_JOINABLE);
    pthread_attr_setscope(&(sr->attr), PTHREAD_SCOPE_SYSTEM);
    pthread_attr_setscope(&(sr->attr), PTHREAD_SCOPE_SYSTEM);
    pthread_t thread;
    pthread_create(&thread, &(sr->attr), sr_arpcache_timeout, sr);
    
    /* TODO: (opt) Add initialization code here */

} /* -- sr_init -- */

/*---------------------------------------------------------------------
 * Method: sr_handlepacket(uint8_t* p,char* interface)
 * Scope:  Global
 *
 * This method is called each time the router receives a packet on the
 * interface.  The packet buffer, the packet length and the receiving
 * interface are passed in as parameters. The packet is complete with
 * ethernet headers.
 *
 * Note: Both the packet buffer and the character's memory are handled
 * by sr_vns_comm.c that means do NOT free either (signified by "lent" comment).  
 * Make a copy of the
 * packet instead if you intend to keep it around beyond the scope of
 * the method call.
 *
 *---------------------------------------------------------------------*/

void sr_handlepacket(struct sr_instance* sr,
        uint8_t * packet/* lent */,
        unsigned int len,
        char* interface/* lent */){
  /* REQUIRES */

  assert(sr);
  assert(packet);
  assert(interface);                                                                             

  printf("*** -> Received packet of length %d\n",len);  fflush(stdout);
  print_hdrs(packet,len);
  /* min-length check*/
  int minlength = sizeof(sr_ethernet_hdr_t);
  if (len < minlength) {
    fprintf(stderr, "Failed to print ETHERNET header, insufficient length\n");
    return;
  }

  /* Check protocol*/
  uint16_t ethtype = ethertype(packet);

  if(ethtype == ethertype_ip){
    /* ip packet */
    minlength += sizeof(sr_ip_hdr_t);
    if (len < minlength) {
      fprintf(stderr, "Failed to print IP header, insufficient length\n"); 
      return;
    }

    sr_ip_hdr_t* ip_hdr =  (sr_ip_hdr_t*)(packet+sizeof(sr_ethernet_hdr_t));

    /* checksum check */
    uint16_t checksum_orig = ip_hdr->ip_sum;
    ip_hdr->ip_sum = 0;
    ip_hdr->ip_sum = cksum(ip_hdr,sizeof(sr_ip_hdr_t));
    if(checksum_orig != ip_hdr->ip_sum){
      fprintf(stderr, "Corrupt package. Checksum mismatch\n"); 
      return;
    }

    uint32_t query_ip = ntohl(ip_hdr->ip_dst);
    struct sr_if* if_details = sr_get_interface(sr,interface);

    printf("An IP Request for IP:"); fflush(stdout);
    print_addr_ip_int(query_ip);

    struct sr_if* if_list = sr->if_list;
    while(if_list!=NULL){
      if(ntohl(if_list->ip)==query_ip){
        break;
      }else{
        if_list = if_list->next;
      }
    }

    if(if_list!=NULL){
      /* For router's interface */
      if(ip_hdr->ip_p == 1){
        /* If ICMP Packet */
        minlength += sizeof(sr_icmp_hdr_t);
        if (len < minlength) {
          fprintf(stderr, "Failed to print ARP header, insufficient length\n"); 
          return;
        }
        sr_icmp_hdr_t* icmp_hdr =  (sr_icmp_hdr_t*)(packet+sizeof(sr_ethernet_hdr_t)+sizeof(sr_ip_hdr_t));
        if(icmp_hdr->icmp_type==8){
          /*echo request */
          uint8_t* icmp_reply = (uint8_t*)malloc(len);
          memcpy(icmp_reply,packet,len);

          /*Edit ethernet header*/
          sr_ethernet_hdr_t* ethernet_header = (sr_ethernet_hdr_t*)icmp_reply;
          int pos;
          for (pos = 0; pos < ETHER_ADDR_LEN; pos++) 
            ethernet_header->ether_dhost[pos] = ethernet_header->ether_shost[pos];
          for (pos = 0; pos < ETHER_ADDR_LEN; pos++) 
            ethernet_header->ether_shost[pos] = if_details->addr[pos];

          /*Edit IP header*/
          sr_ip_hdr_t* ip_header = (sr_ip_hdr_t*)(icmp_reply+sizeof(sr_ethernet_hdr_t));
          ip_header->ip_dst = ip_header->ip_src;
          ip_header->ip_src = if_list->ip;
          ip_header->ip_sum = 0;
          ip_header->ip_sum = cksum(ip_header,sizeof(sr_ip_hdr_t)); 

          /* Edit ICMP Header */
          sr_icmp_hdr_t* icmp_header = (sr_icmp_hdr_t*)(icmp_reply+sizeof(sr_ethernet_hdr_t)+sizeof(sr_ip_hdr_t));
          icmp_header->icmp_type = 0;
          icmp_header->icmp_sum = 0;
          icmp_header->icmp_sum = cksum(icmp_header,sizeof(sr_icmp_hdr_t));

          printf("Final Header\n"); fflush(stdout);

          sr_send_packet(sr,icmp_reply,len,interface);
        }else{
          /* If contains TCP */
          fprintf(stderr,"Non ping request\n");
          return;
        }
      }else{
        printf("TCP/UDP payload\n");  fflush(stdout);
        /* ICMP Port unreachable */
        int icmp_len = sizeof(sr_ethernet_hdr_t)+sizeof(sr_ip_hdr_t)+sizeof(sr_icmp_t3_hdr_t);
        uint8_t* icmp_reply = (uint8_t*)malloc(icmp_len);
        memcpy(icmp_reply,packet,sizeof(sr_ethernet_hdr_t)+sizeof(sr_ip_hdr_t));

        /* Edit ethernet header */
        sr_ethernet_hdr_t* ethernet_header = (sr_ethernet_hdr_t*)icmp_reply;
        int pos;
        char tmp[ETHER_ADDR_LEN];
        for (pos = 0; pos < ETHER_ADDR_LEN; pos++) 
          tmp[pos] = ethernet_header->ether_dhost[pos];
        for (pos = 0; pos < ETHER_ADDR_LEN; pos++) 
          ethernet_header->ether_dhost[pos] = ethernet_header->ether_shost[pos];
        for (pos = 0; pos < ETHER_ADDR_LEN; pos++) 
          ethernet_header->ether_shost[pos] = tmp[pos];

        /* Edit IP Header */
        sr_ip_hdr_t* ip_header = (sr_ip_hdr_t*)(icmp_reply+sizeof(sr_ethernet_hdr_t));
        ip_header->ip_len = htons(sizeof(sr_icmp_t3_hdr_t)+sizeof(sr_ip_hdr_t));
        uint32_t tmp_ip = ip_header->ip_dst;
        ip_header->ip_dst = ip_header->ip_src;
        ip_header->ip_src = tmp_ip;
        ip_header->ip_p = ip_protocol_icmp;
        ip_header->ip_sum = 0;
        ip_header->ip_sum = cksum(ip_header,sizeof(sr_ip_hdr_t));        

        /* Edit ICMP3 Header */
        sr_icmp_t3_hdr_t* icmp_header = (sr_icmp_t3_hdr_t*)(icmp_reply+sizeof(sr_ethernet_hdr_t)+sizeof(sr_ip_hdr_t));
        icmp_header->icmp_type = 3;
        icmp_header->icmp_code = 3;
        icmp_header->unused = 0;
        icmp_header->next_mtu = 0;
        memcpy(icmp_header->data,packet+sizeof(sr_ethernet_hdr_t),sizeof(sr_ip_hdr_t)+8);
        icmp_header->icmp_sum = 0;
        icmp_header->icmp_sum = cksum(icmp_header,sizeof(sr_icmp_t3_hdr_t));

        sr_send_packet(sr,icmp_reply,icmp_len,interface);
      } 
    }else{
      /* Forwarding Logic */
      ip_hdr->ip_ttl--;
      if(ip_hdr->ip_ttl==0){
        printf("ttl 0\n");  fflush(stdout);
        /*  ICMP Port unreachable */
        int icmp_len = sizeof(sr_ethernet_hdr_t)+sizeof(sr_ip_hdr_t)+sizeof(sr_icmp_t3_hdr_t);
        uint8_t* icmp_reply = (uint8_t*)malloc(icmp_len);
        memcpy(icmp_reply,packet,sizeof(sr_ethernet_hdr_t)+sizeof(sr_ip_hdr_t));

        /*Edit ethernet header*/
        sr_ethernet_hdr_t* ethernet_header = (sr_ethernet_hdr_t*)icmp_reply;
        int pos;
        char tmp[ETHER_ADDR_LEN];
        for (pos = 0; pos < ETHER_ADDR_LEN; pos++) 
          tmp[pos] = ethernet_header->ether_dhost[pos];
        for (pos = 0; pos < ETHER_ADDR_LEN; pos++) 
          ethernet_header->ether_dhost[pos] = ethernet_header->ether_shost[pos];
        for (pos = 0; pos < ETHER_ADDR_LEN; pos++) 
          ethernet_header->ether_shost[pos] = tmp[pos];

        /* Edit IP Header */
        sr_ip_hdr_t* ip_header = (sr_ip_hdr_t*)(icmp_reply+sizeof(sr_ethernet_hdr_t));
        ip_header->ip_len = htons(sizeof(sr_icmp_t3_hdr_t)+sizeof(sr_ip_hdr_t));
        ip_header->ip_dst = ip_header->ip_src;
        ip_header->ip_src = sr_get_interface(sr,interface)->ip;
        ip_header->ip_p = ip_protocol_icmp;
        ip_header->ip_sum = 0;
        ip_header->ip_sum = cksum(ip_header,sizeof(sr_ip_hdr_t));        

        /* Edit ICMP3 Header */
        sr_icmp_t3_hdr_t* icmp_header = (sr_icmp_t3_hdr_t*)(icmp_reply+sizeof(sr_ethernet_hdr_t)+sizeof(sr_ip_hdr_t));
        icmp_header->icmp_type = 11;
        icmp_header->icmp_code = 0;
        icmp_header->unused = 0;
        icmp_header->next_mtu = 0;
        memcpy(icmp_header->data,packet+sizeof(sr_ethernet_hdr_t),sizeof(sr_ip_hdr_t)+8);
        icmp_header->icmp_sum = 0;
        icmp_header->icmp_sum = cksum(icmp_header,sizeof(sr_icmp_t3_hdr_t));

        sr_send_packet(sr,icmp_reply,icmp_len,interface);
        return;
      }
      /* check value of ttl 0 or non-zero*/
      if(ip_hdr->ip_p==1){
        /* If ICMP Packet */
        minlength += sizeof(sr_icmp_hdr_t);
        if (len < minlength) {
          fprintf(stderr, "Failed to print ICMP header, insufficient length\n"); 
          return;
        }
      }
      printf("Query for other IP:\n");  fflush(stdout);
      info_t match = longest_prefix_match(sr->routing_table,query_ip);
      if(match.ip==0){
        /* Net Unreachable */
        printf("Net unreachable\n");
        int icmp_len = sizeof(sr_ethernet_hdr_t)+sizeof(sr_ip_hdr_t)+sizeof(sr_icmp_t3_hdr_t);
        uint8_t* icmp_reply = (uint8_t*)malloc(icmp_len);
        memcpy(icmp_reply,packet,sizeof(sr_ethernet_hdr_t)+sizeof(sr_ip_hdr_t));

        /*Edit ethernet header*/
        sr_ethernet_hdr_t* ethernet_header = (sr_ethernet_hdr_t*)icmp_reply;
        int pos;
        char tmp[ETHER_ADDR_LEN];
        for (pos = 0; pos < ETHER_ADDR_LEN; pos++) 
          tmp[pos] = ethernet_header->ether_dhost[pos];
        for (pos = 0; pos < ETHER_ADDR_LEN; pos++) 
          ethernet_header->ether_dhost[pos] = ethernet_header->ether_shost[pos];
        for (pos = 0; pos < ETHER_ADDR_LEN; pos++) 
          ethernet_header->ether_shost[pos] = tmp[pos];

        /* Edit IP Header */
        sr_ip_hdr_t* ip_header = (sr_ip_hdr_t*)(icmp_reply+sizeof(sr_ethernet_hdr_t));
        ip_header->ip_len = htons(sizeof(sr_icmp_t3_hdr_t)+sizeof(sr_ip_hdr_t));
        ip_header->ip_dst = ip_header->ip_src;
        ip_header->ip_src = if_details->ip;
        ip_header->ip_p = ip_protocol_icmp;
        ip_header->ip_sum = 0;
        ip_header->ip_sum = cksum(ip_header,sizeof(sr_ip_hdr_t));        

        /* Edit ICMP3 Header */
        sr_icmp_t3_hdr_t* icmp_header = (sr_icmp_t3_hdr_t*)(icmp_reply+sizeof(sr_ethernet_hdr_t)+sizeof(sr_ip_hdr_t));
        icmp_header->icmp_type = 3;
        icmp_header->icmp_code = 0;
        icmp_header->unused = 0;
        icmp_header->next_mtu = 0;
        /* ip_hdr->ip_ttl++;*/
        memcpy(icmp_header->data,packet+sizeof(sr_ethernet_hdr_t),sizeof(sr_ip_hdr_t)+8);
        icmp_header->icmp_sum = 0;
        icmp_header->icmp_sum = cksum(icmp_header,sizeof(sr_icmp_t3_hdr_t));
        print_hdrs(icmp_reply,icmp_len);
        sr_send_packet(sr,icmp_reply,icmp_len,interface);
      }else{
        ip_hdr->ip_sum = 0;
        ip_hdr->ip_sum = cksum(ip_hdr,sizeof(sr_ip_hdr_t));
        struct sr_arpentry *entry = sr_arpcache_lookup(&(sr->cache),match.ip);

        if(entry!=NULL){
          uint8_t* icmp_reply = (uint8_t*)malloc(len);
          memcpy(icmp_reply,packet,len);

          /*Edit ethernet header*/
          sr_ethernet_hdr_t* ethernet_header = (sr_ethernet_hdr_t*)icmp_reply;
          int pos;
          for (pos = 0; pos < ETHER_ADDR_LEN; pos++) 
            ethernet_header->ether_dhost[pos] = entry->mac[pos];
          for (pos = 0; pos < ETHER_ADDR_LEN; pos++) 
            ethernet_header->ether_shost[pos] = sr_get_interface(sr,match.interface)->addr[pos];

          sr_send_packet(sr,icmp_reply,len,match.interface);
          free(entry);
        }else{
          printf("Put in Queue\n");
          uint8_t* packet_copy = (uint8_t*) malloc(len);
          memcpy(packet_copy,packet,len);
          struct sr_arpreq *req = sr_arpcache_queuereq(&(sr->cache),match.ip,packet_copy,len,match.interface);
          handle_arpreq(sr,req);
        }
      }
    }
  }else if(ethtype == ethertype_arp){
    /*min-length check*/
    minlength += sizeof(sr_arp_hdr_t);
    if (len < minlength) {
      fprintf(stderr, "Failed to print ARP header, insufficient length\n"); 
      return;
    }

    sr_arp_hdr_t* arp_header =  (sr_arp_hdr_t*)(packet+sizeof(sr_ethernet_hdr_t));
    struct sr_if* if_details = sr_get_interface(sr,interface);

    if(ntohs(arp_header->ar_op) == arp_op_request){
      /* ARP Request */
      uint32_t query_ip = ntohl(arp_header->ar_tip);

      printf("An ARP Request for IP:"); fflush(stdout);
      print_addr_ip_int(query_ip);

      struct sr_if* if_list = sr->if_list;
      while(if_list!=NULL){
        if(ntohl(if_list->ip)==query_ip){
          break;
        }else{
          if_list = if_list->next;
        }
      }

      if(if_list!=NULL){
        /* Query for router interface's IP */
        printf("Query for router interface's IP:");  fflush(stdout);
        uint8_t* arp_reply = (uint8_t*)malloc(len);
        memcpy(arp_reply,packet,len);

        /*Edit ethernet header*/
        sr_ethernet_hdr_t* ethernet_header = (sr_ethernet_hdr_t*)arp_reply;
        int pos;
        for (pos = 0; pos < ETHER_ADDR_LEN; pos++) 
          ethernet_header->ether_dhost[pos] = ethernet_header->ether_shost[pos];
        for (pos = 0; pos < ETHER_ADDR_LEN; pos++) 
          ethernet_header->ether_shost[pos] = if_details->addr[pos];

        /* Edit ARP header */
        sr_arp_hdr_t* arp_header = (sr_arp_hdr_t*)(arp_reply+sizeof(sr_ethernet_hdr_t));
        for (pos = 0; pos < ETHER_ADDR_LEN; pos++) 
          arp_header->ar_tha[pos] = ethernet_header->ether_dhost[pos];
        for (pos = 0; pos < ETHER_ADDR_LEN; pos++) 
          arp_header->ar_sha[pos] = if_list->addr[pos];
        arp_header->ar_tip = arp_header->ar_sip;
        arp_header->ar_sip = if_list->ip;
        arp_header->ar_op = htons(arp_op_reply);

        sr_send_packet(sr,arp_reply,len,interface);

      }else{
        /* Drop packet */
        fprintf(stderr, "ARP Request for non-router interface\n");
        return;
      }
    }else if(ntohs(arp_header->ar_op) == arp_op_reply){
      /* ARP Response */
      printf("An ARP Response\n"); fflush(stdout);
      struct sr_arpreq *req = sr_arpcache_insert(&(sr->cache),arp_header->ar_sha,ntohl(arp_header->ar_sip));
      if(req){
        struct sr_packet *packet = req->packets;
        while(packet){
          /*echo request */
          uint8_t* icmp_reply = (uint8_t*)malloc(packet->len);
          memcpy(icmp_reply,packet->buf,packet->len);

          /*Edit ethernet header*/
          sr_ethernet_hdr_t* ethernet_header = (sr_ethernet_hdr_t*)icmp_reply;
          int pos;
          for (pos = 0; pos < ETHER_ADDR_LEN; pos++) 
            ethernet_header->ether_dhost[pos] = arp_header->ar_sha[pos];
          for (pos = 0; pos < ETHER_ADDR_LEN; pos++) 
            ethernet_header->ether_shost[pos] = if_details->addr[pos];

          sr_send_packet(sr,icmp_reply,packet->len,interface);
          packet = packet->next;
          free(icmp_reply);
        }
        sr_arpreq_destroy(&(sr->cache),req);
      }
    }else{
      fprintf(stderr, "Unknown ARP opcode\n");
      return;
    }

  }else{
    fprintf(stderr, "Packet doesn't fall under any protocol\n");
    return;
  }

}/* -- sr_handlepacket -- */
