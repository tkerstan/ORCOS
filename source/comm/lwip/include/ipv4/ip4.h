/*
 * Copyright (c) 2001-2004 Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 *
 * Author: Adam Dunkels <adam@sics.se>
 *
 */
#ifndef __LWIP_IP_H__
#define __LWIP_IP_H__

#include "lwip/opt.h"

#include "lwip/def.h"
#include "lwip/pbuf.h"
#include "ipv4/ip4_addr.h"
#include "lwip/err.h"


#ifdef __cplusplus
extern "C"
{
#endif

/* forward definition */
struct netif;

    /** Currently, the function ip_output_if_opt() is only used with IGMP */
#define IP_OPTIONS_SEND   LWIP_IGMP

#define IP4_HLEN 20

#define IP4_PROTO_ICMP    1
#define IP4_PROTO_UDP     17
#define IP4_PROTO_UDPLITE 136
#define IP4_PROTO_TCP     6

    /* This is passed as the destination address to ip_output_if (not
     to ip_output), meaning that an IP header already is constructed
     in the pbuf. This is used when TCP retransmits. */
#ifdef IP4_HDRINCL
#undef IP4_HDRINCL
#endif /* IP_HDRINCL */
#define IP4_HDRINCL  NULL

#if LWIP_NETIF_HWADDRHINT
#define IP4_PCB_ADDRHINT ;u8_t addr_hint
#else
#define IP4_PCB_ADDRHINT
#endif /* LWIP_NETIF_HWADDRHINT */

    /* This is the common part of all PCB types. It needs to be at the
     beginning of a PCB type definition. It is located here so that
     changes to this common part are made in one location instead of
     having to change all PCB structs. */
#define IP4_PCB \
  /* ip addresses in network byte order */ \
  struct ip4_addr local_ip; \
  struct ip4_addr remote_ip; \
   /* Socket options */  \
  u16_t so_options;      \
   /* Type Of Service */ \
  u8_t tos;              \
  /* Time To Live */     \
  u8_t ttl               \
  /* link layer address resolution hint */ \
  IP4_PCB_ADDRHINT

    struct ip4_pcb
    {
        /* Common members of all PCB types */
        IP4_PCB;
    };

    /*
     * Option flags per-socket. These are the same like SO_XXX.
     */
    /*
     * Option flags per-socket. These are the same like SO_XXX.
     */
    /*#define SOF_DEBUG       0x01U     Unimplemented: turn on debugging info recording */
    #define SOF_ACCEPTCONN    0x02U  /* socket has had listen() */
    #define SOF_REUSEADDR     0x04U  /* allow local address reuse */
    #define SOF_KEEPALIVE     0x08U  /* keep connections alive */
    /*#define SOF_DONTROUTE   0x10U     Unimplemented: just use interface addresses */
    #define SOF_BROADCAST     0x20U  /* permit to send and to receive broadcast messages (see IP_SOF_BROADCAST option) */
    /*#define SOF_USELOOPBACK 0x40U     Unimplemented: bypass hardware when possible */
    #define SOF_LINGER        0x80U  /* linger on close if data present */
    /*#define SOF_OOBINLINE   0x0100U   Unimplemented: leave received OOB data in line */
    /*#define SOF_REUSEPORT   0x0200U   Unimplemented: allow local address & port reuse */

    /* These flags are inherited (e.g. from a listen-pcb to a connection-pcb): */
    #define SOF_INHERITED   (SOF_REUSEADDR|SOF_KEEPALIVE|SOF_LINGER/*|SOF_DEBUG|SOF_DONTROUTE|SOF_OOBINLINE*/)
    /** Copy IP address - faster than ip_addr_set: no NULL check */


#ifdef PACK_STRUCT_USE_INCLUDES
#  include "arch/bpstruct.h"
#endif

#define IP4_RF 0x8000        /* reserved fragment flag */
#define IP4_DF 0x4000        /* dont fragment flag */
#define IP4_MF 0x2000        /* more fragments flag */
#define IP4_OFFMASK 0x1fff   /* mask for fragmenting bits */

    PACK_STRUCT_BEGIN
    struct ip4_hdr
    {
        /* version / header length / type of service */
        PACK_STRUCT_FIELD(u16_t _v_hl_tos);
        /* total length */
        PACK_STRUCT_FIELD(u16_t _len);
        /* identification */
        PACK_STRUCT_FIELD(u16_t _id);
        /* fragment offset field */
        PACK_STRUCT_FIELD(u16_t _offset);
        /* time to live / protocol*/
        PACK_STRUCT_FIELD(u16_t _ttl_proto);
        /* checksum */
        PACK_STRUCT_FIELD(u16_t _chksum);
        /* source and destination IP addresses */
        PACK_STRUCT_FIELD(struct ip4_addr src);
        PACK_STRUCT_FIELD(struct ip4_addr dest);
    }PACK_STRUCT_STRUCT;
    PACK_STRUCT_END
#ifdef PACK_STRUCT_USE_INCLUDES
#  include "arch/epstruct.h"
#endif

#define IP4H_V(hdr)  (ntohs((hdr)->_v_hl_tos) >> 12)
#define IP4H_HL(hdr) ((ntohs((hdr)->_v_hl_tos) >> 8) & 0x0f)
#define IP4H_TOS(hdr) (ntohs((hdr)->_v_hl_tos) & 0xff)
#define IP4H_LEN(hdr) ((hdr)->_len)
#define IP4H_ID(hdr) ((hdr)->_id)
#define IP4H_OFFSET(hdr) ((hdr)->_offset)
#define IP4H_TTL(hdr) (ntohs((hdr)->_ttl_proto) >> 8)
#define IP4H_PROTO(hdr) (ntohs((hdr)->_ttl_proto) & 0xff)
#define IP4H_CHKSUM(hdr) ((hdr)->_chksum)

#define IP4H_VHLTOS_SET(hdr, v, hl, tos) (hdr)->_v_hl_tos = (htons(((v) << 12) | ((hl) << 8) | (tos)))
#define IP4H_LEN_SET(hdr, len) (hdr)->_len = (len)
#define IP4H_ID_SET(hdr, id) (hdr)->_id = (id)
#define IP4H_OFFSET_SET(hdr, off) (hdr)->_offset = (off)
#define IP4H_TTL_SET(hdr, ttl) (hdr)->_ttl_proto = (htons(IP4H_PROTO(hdr) | ((u16_t)(ttl) << 8)))
#define IP4H_PROTO_SET(hdr, proto) (hdr)->_ttl_proto = (htons((proto) | (IP4H_TTL(hdr) << 8)))
#define IP4H_CHKSUM_SET(hdr, chksum) (hdr)->_chksum = (chksum)

    /** The interface that provided the packet for the current callback invocation. */
    extern struct netif *current_netif;
    /** Header of the input packet currently being processed. */
    extern const struct ip4_hdr *current_header;

#define ip4_init() /* Compatibility define, not init needed. */
    struct netif *ip4_route(struct ip4_addr *dest);
    err_t ip4_input(struct pbuf *p, struct netif *inp);
    err_t ip4_output(struct pbuf *p, struct ip4_addr *src, struct ip4_addr *dest,
            u8_t ttl, u8_t tos, u8_t proto);
    err_t ip4_output_if(struct pbuf *p, struct ip4_addr *src, struct ip4_addr *dest,
            u8_t ttl, u8_t tos, u8_t proto,
            struct netif *netif);
#if LWIP_NETIF_HWADDRHINT
    err_t ip4_output_hinted(struct pbuf *p, struct ip4_addr *src, struct ip4_addr *dest,
            u8_t ttl, u8_t tos, u8_t proto, u8_t *addr_hint);
#endif /* LWIP_NETIF_HWADDRHINT */
#if IP_OPTIONS_SEND
    err_t ip4_output_if_opt(struct pbuf *p, struct ip4_addr *src, struct ip4_addr *dest,
            u8_t ttl, u8_t tos, u8_t proto, struct netif *netif, void *ip_options,
            u16_t optlen);
#endif /* IP_OPTIONS_SEND */
    /** Get the interface that received the current packet.
     * This function must only be called from a receive callback (udp_recv,
     * raw_recv, tcp_accept). It will return NULL otherwise. */
#define ip4_current_netif()  (current_netif)
    /** Get the IP header of the current packet.
     * This function must only be called from a receive callback (udp_recv,
     * raw_recv, tcp_accept). It will return NULL otherwise. */
#define ip4_current_header() (current_header)
#if IP_DEBUG
    void ip4_debug_print(struct pbuf *p);
#else
#define ip4_debug_print(p)
#endif /* IP_DEBUG */

#ifdef __cplusplus
}
#endif

#endif /* __LWIP_IP_H__ */

