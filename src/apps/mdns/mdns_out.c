/**
 * @file
 * MDNS responder implementation - output related functionalities
 */

/*
 * Copyright (c) 2015 Verisure Innovation AB
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
 * Author: Erik Ekman <erik@kryo.se>
 * Author: Jasper Verschueren <jasper.verschueren@apart-audio.com>
 *
 */

#include "lwip/apps/mdns_out.h"
#include "lwip/apps/mdns_priv.h"
#include "lwip/apps/mdns_domain.h"
#include "lwip/prot/dns.h"
#include "lwip/prot/iana.h"
#include "lwip/udp.h"


#if LWIP_IPV6
#include "lwip/prot/ip6.h"
#endif


#if LWIP_MDNS_RESPONDER

/* Payload size allocated for each outgoing UDP packet */
#define OUTPACKET_SIZE 500

/**
 * Call user supplied function to setup TXT data
 * @param service The service to build TXT record for
 */
void
mdns_prepare_txtdata(struct mdns_service *service)
{
  memset(&service->txtdata, 0, sizeof(struct mdns_domain));
  if (service->txt_fn) {
    service->txt_fn(service, service->txt_userdata);
  }
}

/**
 * Write a question to an outpacket
 * A question contains domain, type and class. Since an answer also starts with these fields this function is also
 * called from mdns_add_answer().
 * @param outpkt The outpacket to write to
 * @param domain The domain name the answer is for
 * @param type The DNS type of the answer (like 'AAAA', 'SRV')
 * @param klass The DNS type of the answer (like 'IN')
 * @param unicast If highest bit in class should be set, to instruct the responder to
 *                reply with a unicast packet
 * @return ERR_OK on success, an err_t otherwise
 */
static err_t
mdns_add_question(struct mdns_outpacket *outpkt, struct mdns_domain *domain,
                  u16_t type, u16_t klass, u16_t unicast)
{
  u16_t question_len;
  u16_t field16;
  err_t res;

  if (!outpkt->pbuf) {
    /* If no pbuf is active, allocate one */
    outpkt->pbuf = pbuf_alloc(PBUF_TRANSPORT, OUTPACKET_SIZE, PBUF_RAM);
    if (!outpkt->pbuf) {
      return ERR_MEM;
    }
    outpkt->write_offset = SIZEOF_DNS_HDR;
  }

  /* Worst case calculation. Domain string might be compressed */
  question_len = domain->length + sizeof(type) + sizeof(klass);
  if (outpkt->write_offset + question_len > outpkt->pbuf->tot_len) {
    /* No space */
    return ERR_MEM;
  }

  /* Write name */
  res = mdns_write_domain(outpkt, domain);
  if (res != ERR_OK) {
    return res;
  }

  /* Write type */
  field16 = lwip_htons(type);
  res = pbuf_take_at(outpkt->pbuf, &field16, sizeof(field16), outpkt->write_offset);
  if (res != ERR_OK) {
    return res;
  }
  outpkt->write_offset += sizeof(field16);

  /* Write class */
  if (unicast) {
    klass |= 0x8000;
  }
  field16 = lwip_htons(klass);
  res = pbuf_take_at(outpkt->pbuf, &field16, sizeof(field16), outpkt->write_offset);
  if (res != ERR_OK) {
    return res;
  }
  outpkt->write_offset += sizeof(field16);

  return ERR_OK;
}

/**
 * Write answer to reply packet.
 * buf or answer_domain can be null. The rd_length written will be buf_length +
 * size of (compressed) domain. Most uses will need either buf or answer_domain,
 * special case is SRV that starts with 3 u16 and then a domain name.
 * @param reply The outpacket to write to
 * @param domain The domain name the answer is for
 * @param type The DNS type of the answer (like 'AAAA', 'SRV')
 * @param klass The DNS type of the answer (like 'IN')
 * @param cache_flush If highest bit in class should be set, to instruct receiver that
 *                    this reply replaces any earlier answer for this domain/type/class
 * @param ttl Validity time in seconds to send out for IP address data in DNS replies
 * @param buf Pointer to buffer of answer data
 * @param buf_length Length of variable data
 * @param answer_domain A domain to write after any buffer data as answer
 * @return ERR_OK on success, an err_t otherwise
 */
static err_t
mdns_add_answer(struct mdns_outpacket *reply, struct mdns_domain *domain,
                u16_t type, u16_t klass, u16_t cache_flush, u32_t ttl,
                const u8_t *buf, size_t buf_length, struct mdns_domain *answer_domain)
{
  u16_t answer_len;
  u16_t field16;
  u16_t rdlen_offset;
  u16_t answer_offset;
  u32_t field32;
  err_t res;

  if (!reply->pbuf) {
    /* If no pbuf is active, allocate one */
    reply->pbuf = pbuf_alloc(PBUF_TRANSPORT, OUTPACKET_SIZE, PBUF_RAM);
    if (!reply->pbuf) {
      return ERR_MEM;
    }
    reply->write_offset = SIZEOF_DNS_HDR;
  }

  /* Worst case calculation. Domain strings might be compressed */
  answer_len = domain->length + sizeof(type) + sizeof(klass) + sizeof(ttl) + sizeof(field16)/*rd_length*/;
  if (buf) {
    answer_len += (u16_t)buf_length;
  }
  if (answer_domain) {
    answer_len += answer_domain->length;
  }
  if (reply->write_offset + answer_len > reply->pbuf->tot_len) {
    /* No space */
    return ERR_MEM;
  }

  /* Answer starts with same data as question, then more fields */
  mdns_add_question(reply, domain, type, klass, cache_flush);

  /* Write TTL */
  field32 = lwip_htonl(ttl);
  res = pbuf_take_at(reply->pbuf, &field32, sizeof(field32), reply->write_offset);
  if (res != ERR_OK) {
    return res;
  }
  reply->write_offset += sizeof(field32);

  /* Store offsets and skip forward to the data */
  rdlen_offset = reply->write_offset;
  reply->write_offset += sizeof(field16);
  answer_offset = reply->write_offset;

  if (buf) {
    /* Write static data */
    res = pbuf_take_at(reply->pbuf, buf, (u16_t)buf_length, reply->write_offset);
    if (res != ERR_OK) {
      return res;
    }
    reply->write_offset += (u16_t)buf_length;
  }

  if (answer_domain) {
    /* Write name answer (compressed if possible) */
    res = mdns_write_domain(reply, answer_domain);
    if (res != ERR_OK) {
      return res;
    }
  }

  /* Write rd_length after when we know the answer size */
  field16 = lwip_htons(reply->write_offset - answer_offset);
  res = pbuf_take_at(reply->pbuf, &field16, sizeof(field16), rdlen_offset);

  return res;
}

/** Write an ANY host question to outpacket */
static err_t
mdns_add_any_host_question(struct mdns_outpacket *outpkt,
                           struct mdns_outmsg *msg,
                           u16_t request_unicast_reply)
{
  struct mdns_domain host;
  mdns_build_host_domain(&host, netif_mdns_data(msg->netif));
  LWIP_DEBUGF(MDNS_DEBUG, ("MDNS: Adding host question for ANY type\n"));
  return mdns_add_question(outpkt, &host, DNS_RRTYPE_ANY, DNS_RRCLASS_IN,
                           request_unicast_reply);
}

/** Write an ANY service instance question to outpacket */
static err_t
mdns_add_any_service_question(struct mdns_outpacket *outpkt,
                              struct mdns_service *service,
                              u16_t request_unicast_reply)
{
  struct mdns_domain domain;
  mdns_build_service_domain(&domain, service, 1);
  LWIP_DEBUGF(MDNS_DEBUG, ("MDNS: Adding service instance question for ANY type\n"));
  return mdns_add_question(outpkt, &domain, DNS_RRTYPE_ANY, DNS_RRCLASS_IN,
                           request_unicast_reply);
}

#if LWIP_IPV4
/** Write an IPv4 address (A) RR to outpacket */
static err_t
mdns_add_a_answer(struct mdns_outpacket *reply, struct mdns_outmsg *msg)
{
  err_t res;
  struct mdns_domain host;
  mdns_build_host_domain(&host, netif_mdns_data(msg->netif));
  /* When answering to a legacy querier, we need to repeat the question.
   * But this only needs to be done for the question asked (max one question),
   * not for the additional records. */
  if(msg->legacy_query && reply->questions < 1) {
    LWIP_DEBUGF(MDNS_DEBUG, ("MDNS: Add question for legacy query\n"));
    res = mdns_add_question(reply, &host, DNS_RRTYPE_A, DNS_RRCLASS_IN, 0);
    if (res != ERR_OK) {
      return res;
    }
    reply->questions = 1;
  }
  LWIP_DEBUGF(MDNS_DEBUG, ("MDNS: Responding with A record\n"));
  return mdns_add_answer(reply, &host, DNS_RRTYPE_A, DNS_RRCLASS_IN, msg->cache_flush,
                         (netif_mdns_data(msg->netif))->dns_ttl,
                         (const u8_t *) netif_ip4_addr(msg->netif), sizeof(ip4_addr_t), NULL);
}

/** Write a 4.3.2.1.in-addr.arpa -> hostname.local PTR RR to outpacket */
static err_t
mdns_add_hostv4_ptr_answer(struct mdns_outpacket *reply, struct mdns_outmsg *msg)
{
  err_t res;
  struct mdns_domain host, revhost;
  mdns_build_host_domain(&host, netif_mdns_data(msg->netif));
  mdns_build_reverse_v4_domain(&revhost, netif_ip4_addr(msg->netif));
  /* When answering to a legacy querier, we need to repeat the question.
   * But this only needs to be done for the question asked (max one question),
   * not for the additional records. */
  if(msg->legacy_query && reply->questions < 1) {
    LWIP_DEBUGF(MDNS_DEBUG, ("MDNS: Add question for legacy query\n"));
    res = mdns_add_question(reply, &revhost, DNS_RRTYPE_PTR, DNS_RRCLASS_IN, 0);
    if (res != ERR_OK) {
      return res;
    }
    reply->questions = 1;
  }
  LWIP_DEBUGF(MDNS_DEBUG, ("MDNS: Responding with v4 PTR record\n"));
  return mdns_add_answer(reply, &revhost, DNS_RRTYPE_PTR, DNS_RRCLASS_IN, msg->cache_flush,
                         (netif_mdns_data(msg->netif))->dns_ttl, NULL, 0, &host);
}
#endif

#if LWIP_IPV6
/** Write an IPv6 address (AAAA) RR to outpacket */
static err_t
mdns_add_aaaa_answer(struct mdns_outpacket *reply, struct mdns_outmsg *msg, int addrindex)
{
  err_t res;
  struct mdns_domain host;
  mdns_build_host_domain(&host, netif_mdns_data(msg->netif));
  /* When answering to a legacy querier, we need to repeat the question.
   * But this only needs to be done for the question asked (max one question),
   * not for the additional records. */
  if(msg->legacy_query && reply->questions < 1) {
    LWIP_DEBUGF(MDNS_DEBUG, ("MDNS: Add question for legacy query\n"));
    res = mdns_add_question(reply, &host, DNS_RRTYPE_AAAA, DNS_RRCLASS_IN, 0);
    if (res != ERR_OK) {
      return res;
    }
    reply->questions = 1;
  }
  LWIP_DEBUGF(MDNS_DEBUG, ("MDNS: Responding with AAAA record\n"));
  return mdns_add_answer(reply, &host, DNS_RRTYPE_AAAA, DNS_RRCLASS_IN, msg->cache_flush,
                         (netif_mdns_data(msg->netif))->dns_ttl,
                         (const u8_t *) netif_ip6_addr(msg->netif, addrindex), sizeof(ip6_addr_p_t), NULL);
}

/** Write a x.y.z.ip6.arpa -> hostname.local PTR RR to outpacket */
static err_t
mdns_add_hostv6_ptr_answer(struct mdns_outpacket *reply, struct mdns_outmsg *msg, int addrindex)
{
  err_t res;
  struct mdns_domain host, revhost;
  mdns_build_host_domain(&host, netif_mdns_data(msg->netif));
  mdns_build_reverse_v6_domain(&revhost, netif_ip6_addr(msg->netif, addrindex));
  /* When answering to a legacy querier, we need to repeat the question.
   * But this only needs to be done for the question asked (max one question),
   * not for the additional records. */
  if(msg->legacy_query && reply->questions < 1) {
    LWIP_DEBUGF(MDNS_DEBUG, ("MDNS: Add question for legacy query\n"));
    res = mdns_add_question(reply, &revhost, DNS_RRTYPE_PTR, DNS_RRCLASS_IN, 0);
    if (res != ERR_OK) {
      return res;
    }
    reply->questions = 1;
  }
  LWIP_DEBUGF(MDNS_DEBUG, ("MDNS: Responding with v6 PTR record\n"));
  return mdns_add_answer(reply, &revhost, DNS_RRTYPE_PTR, DNS_RRCLASS_IN, msg->cache_flush,
                         (netif_mdns_data(msg->netif))->dns_ttl, NULL, 0, &host);
}
#endif

/** Write an all-services -> servicetype PTR RR to outpacket */
static err_t
mdns_add_servicetype_ptr_answer(struct mdns_outpacket *reply, struct mdns_outmsg *msg,
                                struct mdns_service *service)
{
  err_t res;
  struct mdns_domain service_type, service_dnssd;
  mdns_build_service_domain(&service_type, service, 0);
  mdns_build_dnssd_domain(&service_dnssd);
  /* When answering to a legacy querier, we need to repeat the question.
   * But this only needs to be done for the question asked (max one question),
   * not for the additional records. */
  if(msg->legacy_query && reply->questions < 1) {
    LWIP_DEBUGF(MDNS_DEBUG, ("MDNS: Add question for legacy query\n"));
    res = mdns_add_question(reply, &service_dnssd, DNS_RRTYPE_PTR, DNS_RRCLASS_IN, 0);
    if (res != ERR_OK) {
      return res;
    }
    reply->questions = 1;
  }
  LWIP_DEBUGF(MDNS_DEBUG, ("MDNS: Responding with service type PTR record\n"));
  return mdns_add_answer(reply, &service_dnssd, DNS_RRTYPE_PTR, DNS_RRCLASS_IN,
                         0, service->dns_ttl, NULL, 0, &service_type);
}

/** Write a servicetype -> servicename PTR RR to outpacket */
static err_t
mdns_add_servicename_ptr_answer(struct mdns_outpacket *reply, struct mdns_outmsg *msg,
                                struct mdns_service *service)
{
  err_t res;
  struct mdns_domain service_type, service_instance;
  mdns_build_service_domain(&service_type, service, 0);
  mdns_build_service_domain(&service_instance, service, 1);
  /* When answering to a legacy querier, we need to repeat the question.
   * But this only needs to be done for the question asked (max one question),
   * not for the additional records. */
  if(msg->legacy_query && reply->questions < 1) {
    LWIP_DEBUGF(MDNS_DEBUG, ("MDNS: Add question for legacy query\n"));
    res = mdns_add_question(reply, &service_type, DNS_RRTYPE_PTR, DNS_RRCLASS_IN, 0);
    if (res != ERR_OK) {
      return res;
    }
    reply->questions = 1;
  }
  LWIP_DEBUGF(MDNS_DEBUG, ("MDNS: Responding with service name PTR record\n"));
  return mdns_add_answer(reply, &service_type, DNS_RRTYPE_PTR, DNS_RRCLASS_IN,
                         0, service->dns_ttl, NULL, 0, &service_instance);
}

/** Write a SRV RR to outpacket */
static err_t
mdns_add_srv_answer(struct mdns_outpacket *reply, struct mdns_outmsg *msg,
                    struct mdns_host *mdns, struct mdns_service *service)
{
  err_t res;
  struct mdns_domain service_instance, srvhost;
  u16_t srvdata[3];
  mdns_build_service_domain(&service_instance, service, 1);
  mdns_build_host_domain(&srvhost, mdns);
  if (msg->legacy_query) {
    /* RFC 6762 section 18.14:
     * In legacy unicast responses generated to answer legacy queries,
     * name compression MUST NOT be performed on SRV records.
     */
    srvhost.skip_compression = 1;
    /* When answering to a legacy querier, we need to repeat the question.
     * But this only needs to be done for the question asked (max one question),
     * not for the additional records. */
    if(reply->questions < 1) {
      LWIP_DEBUGF(MDNS_DEBUG, ("MDNS: Add question for legacy query\n"));
      res = mdns_add_question(reply, &service_instance, DNS_RRTYPE_SRV, DNS_RRCLASS_IN, 0);
      if (res != ERR_OK) {
        return res;
      }
      reply->questions = 1;
    }
  }
  srvdata[0] = lwip_htons(SRV_PRIORITY);
  srvdata[1] = lwip_htons(SRV_WEIGHT);
  srvdata[2] = lwip_htons(service->port);
  LWIP_DEBUGF(MDNS_DEBUG, ("MDNS: Responding with SRV record\n"));
  return mdns_add_answer(reply, &service_instance, DNS_RRTYPE_SRV, DNS_RRCLASS_IN,
                         msg->cache_flush, service->dns_ttl,
                         (const u8_t *) &srvdata, sizeof(srvdata), &srvhost);
}

/** Write a TXT RR to outpacket */
static err_t
mdns_add_txt_answer(struct mdns_outpacket *reply, struct mdns_outmsg *msg,
                    struct mdns_service *service)
{
  err_t res;
  struct mdns_domain service_instance;
  mdns_build_service_domain(&service_instance, service, 1);
  mdns_prepare_txtdata(service);
  /* When answering to a legacy querier, we need to repeat the question.
   * But this only needs to be done for the question asked (max one question),
   * not for the additional records. */
  if(msg->legacy_query && reply->questions < 1) {
    LWIP_DEBUGF(MDNS_DEBUG, ("MDNS: Add question for legacy query\n"));
    res = mdns_add_question(reply, &service_instance, DNS_RRTYPE_TXT, DNS_RRCLASS_IN, 0);
    if (res != ERR_OK) {
      return res;
    }
    reply->questions = 1;
  }
  LWIP_DEBUGF(MDNS_DEBUG, ("MDNS: Responding with TXT record\n"));
  return mdns_add_answer(reply, &service_instance, DNS_RRTYPE_TXT, DNS_RRCLASS_IN,
                         msg->cache_flush, service->dns_ttl,
                         (u8_t *) &service->txtdata.name, service->txtdata.length, NULL);
}


static err_t
mdns_add_probe_questions_to_outpacket(struct mdns_outpacket *outpkt, struct mdns_outmsg *msg)
{
  err_t res;
  int i;
  struct mdns_host *mdns = netif_mdns_data(msg->netif);

  /* Write host questions (probing or legacy query) */
  if(msg->host_questions & QUESTION_PROBE_HOST_ANY) {
    res = mdns_add_any_host_question(outpkt, msg, 1);
    if (res != ERR_OK) {
      return res;
    }
    outpkt->questions++;
  }
  /* Write service questions (probing or legacy query) */
  for (i = 0; i < MDNS_MAX_SERVICES; i++) {
    struct mdns_service* service = mdns->services[i];
    if (!service) {
      continue;
    }
    if(msg->serv_questions[i] & QUESTION_PROBE_SERVICE_NAME_ANY) {
      res = mdns_add_any_service_question(outpkt, service, 1);
      if (res != ERR_OK) {
        return res;
      }
      outpkt->questions++;
    }
  }
  return ERR_OK;
}

/**
 * Send chosen answers as a reply
 *
 * Add all selected answers (first write will allocate pbuf)
 * Add additional answers based on the selected answers
 * Send the packet
 */
err_t
mdns_send_outpacket(struct mdns_outmsg *msg)
{
  struct mdns_service *service;
  struct mdns_outpacket outpkt;
  err_t res = ERR_ARG;
  int i;
  struct mdns_host *mdns = netif_mdns_data(msg->netif);
  u16_t answers = 0;

  memset(&outpkt, 0, sizeof(outpkt));

  res = mdns_add_probe_questions_to_outpacket(&outpkt, msg);
  if (res != ERR_OK) {
    goto cleanup;
  }

  /* Write answers to host questions */
#if LWIP_IPV4
  if (msg->host_replies & REPLY_HOST_A) {
    res = mdns_add_a_answer(&outpkt, msg);
    if (res != ERR_OK) {
      goto cleanup;
    }
    answers++;
  }
  if (msg->host_replies & REPLY_HOST_PTR_V4) {
    res = mdns_add_hostv4_ptr_answer(&outpkt, msg);
    if (res != ERR_OK) {
      goto cleanup;
    }
    answers++;
  }
#endif
#if LWIP_IPV6
  if (msg->host_replies & REPLY_HOST_AAAA) {
    int addrindex;
    for (addrindex = 0; addrindex < LWIP_IPV6_NUM_ADDRESSES; addrindex++) {
      if (ip6_addr_isvalid(netif_ip6_addr_state(msg->netif, addrindex))) {
        res = mdns_add_aaaa_answer(&outpkt, msg, addrindex);
        if (res != ERR_OK) {
          goto cleanup;
        }
        answers++;
      }
    }
  }
  if (msg->host_replies & REPLY_HOST_PTR_V6) {
    u8_t rev_addrs = msg->host_reverse_v6_replies;
    int addrindex = 0;
    while (rev_addrs) {
      if (rev_addrs & 1) {
        res = mdns_add_hostv6_ptr_answer(&outpkt, msg, addrindex);
        if (res != ERR_OK) {
          goto cleanup;
        }
        answers++;
      }
      addrindex++;
      rev_addrs >>= 1;
    }
  }
#endif

  /* Write answers to service questions */
  for (i = 0; i < MDNS_MAX_SERVICES; i++) {
    service = mdns->services[i];
    if (!service) {
      continue;
    }

    if (msg->serv_replies[i] & REPLY_SERVICE_TYPE_PTR) {
      res = mdns_add_servicetype_ptr_answer(&outpkt, msg, service);
      if (res != ERR_OK) {
        goto cleanup;
      }
      answers++;
    }

    if (msg->serv_replies[i] & REPLY_SERVICE_NAME_PTR) {
      res = mdns_add_servicename_ptr_answer(&outpkt, msg, service);
      if (res != ERR_OK) {
        goto cleanup;
      }
      answers++;
    }

    if (msg->serv_replies[i] & REPLY_SERVICE_SRV) {
      res = mdns_add_srv_answer(&outpkt, msg, mdns, service);
      if (res != ERR_OK) {
        goto cleanup;
      }
      answers++;
    }

    if (msg->serv_replies[i] & REPLY_SERVICE_TXT) {
      res = mdns_add_txt_answer(&outpkt, msg, service);
      if (res != ERR_OK) {
        goto cleanup;
      }
      answers++;
    }
  }

  /* if this is a response, the data above is anwers, else this is a probe and
   * the answers above goes into auth section */
  if (msg->flags & DNS_FLAG1_RESPONSE) {
    outpkt.answers += answers;
  } else {
    outpkt.authoritative += answers;
  }

  /* All answers written, add additional RRs */
  for (i = 0; i < MDNS_MAX_SERVICES; i++) {
    service = mdns->services[i];
    if (!service) {
      continue;
    }

    if (msg->serv_replies[i] & REPLY_SERVICE_NAME_PTR) {
      /* Our service instance requested, include SRV & TXT
       * if they are already not requested. */
      if (!(msg->serv_replies[i] & REPLY_SERVICE_SRV)) {
        res = mdns_add_srv_answer(&outpkt, msg, mdns, service);
        if (res != ERR_OK) {
          goto cleanup;
        }
        outpkt.additional++;
      }

      if (!(msg->serv_replies[i] & REPLY_SERVICE_TXT)) {
        res = mdns_add_txt_answer(&outpkt, msg, service);
        if (res != ERR_OK) {
          goto cleanup;
        }
        outpkt.additional++;
      }
    }

    /* If service instance, SRV, record or an IP address is requested,
     * supply all addresses for the host
     */
    if ((msg->serv_replies[i] & (REPLY_SERVICE_NAME_PTR | REPLY_SERVICE_SRV)) ||
        (msg->host_replies & (REPLY_HOST_A | REPLY_HOST_AAAA))) {
#if LWIP_IPV6
      if (!(msg->host_replies & REPLY_HOST_AAAA)) {
        int addrindex;
        for (addrindex = 0; addrindex < LWIP_IPV6_NUM_ADDRESSES; addrindex++) {
          if (ip6_addr_isvalid(netif_ip6_addr_state(msg->netif, addrindex))) {
            res = mdns_add_aaaa_answer(&outpkt, msg, addrindex);
            if (res != ERR_OK) {
              goto cleanup;
            }
            outpkt.additional++;
          }
        }
      }
#endif
#if LWIP_IPV4
      if (!(msg->host_replies & REPLY_HOST_A) &&
          !ip4_addr_isany_val(*netif_ip4_addr(msg->netif))) {
        res = mdns_add_a_answer(&outpkt, msg);
        if (res != ERR_OK) {
          goto cleanup;
        }
        outpkt.additional++;
      }
#endif
    }
  }

  if (outpkt.pbuf) {
    struct dns_hdr hdr;

    /* Write header */
    memset(&hdr, 0, sizeof(hdr));
    hdr.flags1 = msg->flags;
    hdr.numquestions = lwip_htons(outpkt.questions);
    hdr.numanswers = lwip_htons(outpkt.answers);
    hdr.numauthrr = lwip_htons(outpkt.authoritative);
    hdr.numextrarr = lwip_htons(outpkt.additional);
    hdr.id = lwip_htons(msg->tx_id);
    pbuf_take(outpkt.pbuf, &hdr, sizeof(hdr));

    /* Shrink packet */
    pbuf_realloc(outpkt.pbuf, outpkt.write_offset);

    /* Send created packet */
    LWIP_DEBUGF(MDNS_DEBUG, ("MDNS: Sending packet, len=%d, unicast=%d\n",
                outpkt.write_offset, msg->unicast_reply));

    res = udp_sendto_if(get_mdns_pcb(), outpkt.pbuf, &msg->dest_addr, msg->dest_port, msg->netif);
  }

cleanup:
  if (outpkt.pbuf) {
    pbuf_free(outpkt.pbuf);
    outpkt.pbuf = NULL;
  }
  return res;
}

#endif /* LWIP_MDNS_RESPONDER */
