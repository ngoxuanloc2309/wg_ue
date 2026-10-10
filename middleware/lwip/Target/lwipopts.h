/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : Target/lwipopts.h
  * Description        : This file overrides LwIP stack default configuration
  *                      done in opt.h file.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion --------------------------------------*/
#ifndef __LWIPOPTS__H__
#define __LWIPOPTS__H__

#include "main.h"
//#include "logger.h"
/*-----------------------------------------------------------------------------*/
/* Current version of LwIP supported by CubeMx: 2.1.2 -*/
/*-----------------------------------------------------------------------------*/

/* Within 'USER CODE' section, code will be kept by default at each generation */
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

#ifdef __cplusplus
 extern "C" {
#endif

/* STM32CubeMX Specific Parameters (not defined in opt.h) ---------------------*/
/* Parameters set in STM32CubeMX LwIP Configuration GUI -*/
/*----- WITH_RTOS enabled (Since FREERTOS is set) -----*/
#define WITH_RTOS 1
/* Let lwIP provide errno by defining LWIP_PROVIDE_ERRNO */
#define LWIP_PROVIDE_ERRNO 1
#include <sys/time.h>
/*----- CHECKSUM_BY_HARDWARE enabled -----*/
#define CHECKSUM_BY_HARDWARE 1
/*-----------------------------------------------------------------------------*/

/* LWIP_SO_RCVBUF is enabled => this requires INT_MAX definition in limits.h --*/
#include "limits.h"

/* LwIP Stack Parameters (modified compared to initialization value in opt.h) -*/
/* Parameters set in STM32CubeMX LwIP Configuration GUI -*/
/*----- Value in opt.h for LWIP_DHCP: 0 -----*/
#define LWIP_DHCP 1
/* Allow netifapi_* wrappers from driver callbacks outside tcpip_thread. */
#define LWIP_NETIF_API 1
/*----- Default Value for LWIP_DNS: 0 ---*/
#define LWIP_DNS 1
/* IPv4 multicast support required by mDNS (224.0.0.251). */
#define LWIP_IGMP 1
/*----- Default Value for MEMP_NUM_UDP_PCB: 4 ---*/
#define MEMP_NUM_UDP_PCB 20
/*----- Default Value for MEMP_NUM_TCP_PCB: 5 ---*/
#define MEMP_NUM_TCP_PCB 20
/*----- Default Value for LWIP_TCPIP_CORE_LOCKING: 0 ---*/
/* Commercial-safe ownership model: only tcpip_thread may mutate raw TCP PCB
 * lists.  Socket/netconn operations from HTTP are marshalled through its
 * mailbox instead of editing tcp_active_pcbs from the caller under a mutex. */
#define LWIP_TCPIP_CORE_LOCKING 0
/*----- Default Value for LWIP_TCPIP_CORE_LOCKING_INPUT: 0 ---*/
/* Keep protocol input single-owned by tcpip_thread.  Processing packets
 * directly in ETH/WiFi RX tasks makes TCP callbacks run in driver task
 * context and allowed a browser connection close/open race to reach
 * recv_tcp() with a stale netconn PCB. */
#define LWIP_TCPIP_CORE_LOCKING_INPUT 0
/*----- Value in opt.h for MEM_ALIGNMENT: 1 -----*/
#define MEM_ALIGNMENT 4
/*----- Default Value for MEM_SIZE: 1600 ---*/
#define MEM_SIZE 16384
/*----- Value in opt.h for LWIP_ETHERNET: LWIP_ARP || PPPOE_SUPPORT -*/
#define LWIP_ETHERNET 1
/*----- Value in opt.h for LWIP_DNS_SECURE: (LWIP_DNS_SECURE_RAND_XID | LWIP_DNS_SECURE_NO_MULTIPLE_OUTSTANDING | LWIP_DNS_SECURE_RAND_SRC_PORT) -*/
#define LWIP_DNS_SECURE 7
/*----- Value in opt.h for TCP_SND_QUEUELEN: (4*TCP_SND_BUF + (TCP_MSS - 1))/TCP_MSS -----*/
//#define TCP_SND_QUEUELEN 16
/*----- Value in opt.h for TCP_SNDLOWAT: LWIP_MIN(LWIP_MAX(((TCP_SND_BUF)/2), (2 * TCP_MSS) + 1), (TCP_SND_BUF) - 1) -*/
#define TCP_SNDLOWAT 1071
/*----- Value in opt.h for TCP_SNDQUEUELOWAT: LWIP_MAX(TCP_SND_QUEUELEN)/2, 5) -*/
#define TCP_SNDQUEUELOWAT 5
/*----- Value in opt.h for TCP_WND_UPDATE_THRESHOLD: LWIP_MIN(TCP_WND/4, TCP_MSS*4) -----*/
#define TCP_WND_UPDATE_THRESHOLD 536
/*----- Value in opt.h for LWIP_NETIF_LINK_CALLBACK: 0 -----*/
#define LWIP_NETIF_LINK_CALLBACK 1
/*----- Value in opt.h for TCPIP_THREAD_STACKSIZE: 0 -----*/
#define TCPIP_THREAD_STACKSIZE 2048
/*----- Value in opt.h for TCPIP_THREAD_PRIO: 1 -----*/
#define TCPIP_THREAD_PRIO 24
/*----- Value in opt.h for TCPIP_MBOX_SIZE: 0 -----*/
#define TCPIP_MBOX_SIZE 32
/*----- Value in opt.h for SLIPIF_THREAD_STACKSIZE: 0 -----*/
#define SLIPIF_THREAD_STACKSIZE 2048
/*----- Value in opt.h for SLIPIF_THREAD_PRIO: 1 -----*/
#define SLIPIF_THREAD_PRIO 3
/*----- Value in opt.h for DEFAULT_THREAD_STACKSIZE: 0 -----*/
#define DEFAULT_THREAD_STACKSIZE 2048
/*----- Value in opt.h for DEFAULT_THREAD_PRIO: 1 -----*/
#define DEFAULT_THREAD_PRIO 3
/*----- Value in opt.h for DEFAULT_UDP_RECVMBOX_SIZE: 0 -----*/
#define DEFAULT_UDP_RECVMBOX_SIZE 6
/*----- Value in opt.h for DEFAULT_TCP_RECVMBOX_SIZE: 0 -----*/
#define DEFAULT_TCP_RECVMBOX_SIZE 6
/*----- Value in opt.h for DEFAULT_ACCEPTMBOX_SIZE: 0 -----*/
#define DEFAULT_ACCEPTMBOX_SIZE 6
/*----- Default Value for LWIP_TCPIP_TIMEOUT: 0 ---*/
#define LWIP_TCPIP_TIMEOUT 1
/*----- Default Value for LWIP_TCP_KEEPALIVE: 0 ---*/
#define LWIP_TCP_KEEPALIVE 1
/*----- Default Value for LWIP_SO_SNDTIMEO: 0 ---*/
#define LWIP_SO_SNDTIMEO 1
/*----- Default Value for LWIP_SO_RCVTIMEO: 0 ---*/
#define LWIP_SO_RCVTIMEO 1
/*----- Default Value for LWIP_SO_SNDRCVTIMEO_NONSTANDARD: 0 ---*/
#define LWIP_SO_SNDRCVTIMEO_NONSTANDARD 1
/*----- Default Value for LWIP_SO_RCVBUF: 0 ---*/
#define LWIP_SO_RCVBUF 1
/*----- Default Value for LWIP_SO_LINGER: 0 ---*/
#define LWIP_SO_LINGER 1
/*----- Value in opt.h for RECV_BUFSIZE_DEFAULT: INT_MAX -----*/
#define RECV_BUFSIZE_DEFAULT 2000000000
/*----- Default Value for LWIP_TCP_CLOSE_TIMEOUT_MS_DEFAULT: 20000 ---*/
#define LWIP_TCP_CLOSE_TIMEOUT_MS_DEFAULT 2000
/*----- Value in opt.h for LWIP_STATS: 1 -----*/
#define LWIP_STATS 0
/*----- Value in opt.h for CHECKSUM_GEN_IP: 1 -----*/
#define CHECKSUM_GEN_IP 1
/*----- Value in opt.h for CHECKSUM_GEN_UDP: 1 -----*/
#define CHECKSUM_GEN_UDP 1
/*----- Value in opt.h for CHECKSUM_GEN_TCP: 1 -----*/
#define CHECKSUM_GEN_TCP 1
/*----- Value in opt.h for CHECKSUM_GEN_ICMP: 1 -----*/
#define CHECKSUM_GEN_ICMP 1
/*----- Value in opt.h for CHECKSUM_GEN_ICMP6: 1 -----*/
#define CHECKSUM_GEN_ICMP6 1
/*----- Value in opt.h for CHECKSUM_CHECK_IP: 1 -----*/
#define CHECKSUM_CHECK_IP 1
/*----- Value in opt.h for CHECKSUM_CHECK_UDP: 1 -----*/
#define CHECKSUM_CHECK_UDP 1
/*----- Value in opt.h for CHECKSUM_CHECK_TCP: 1 -----*/
#define CHECKSUM_CHECK_TCP 1
/*----- Value in opt.h for CHECKSUM_CHECK_ICMP: 1 -----*/
#define CHECKSUM_CHECK_ICMP 1
/*----- Value in opt.h for CHECKSUM_CHECK_ICMP6: 1 -----*/
#define CHECKSUM_CHECK_ICMP6 1
/*-----------------------------------------------------------------------------*/
/*-----------------------------------------------------------------------------*/
/* PPP options                                                                 */
/*-----------------------------------------------------------------------------*/
#define PPP_SUPPORT                     1
#define PPPOS_SUPPORT                   1
#define PAP_SUPPORT                     1
#define CHAP_SUPPORT                    1
#define PPP_IPV4_SUPPORT                1
#define PPP_IPV6_SUPPORT                0
#define PPP_NOTIFY_PHASE                1
/* USER CODE BEGIN 1 */

/* Enable lwIP sockets for Mongoose HTTP server */
#define LWIP_SOCKET 1
#define LWIP_SOCKET_SELECT 1
#define LWIP_SOCKET_POLL 1

#define MEMP_NUM_SYS_TIMEOUT        16
#define LWIP_DEBUG 1
#define MQTT_DEBUG LWIP_DBG_OFF
#undef LWIP_PLATFORM_DIAG
#define LWIP_PLATFORM_DIAG(x) do {printf x; printf("\r\n");} while(0)

#include <stdlib.h>
#define LWIP_RAND() ((u32_t)rand())

#define MEM_LIBC_MALLOC 1
#define MEMP_MEM_MALLOC 1

#define MQTT_CONNECT_TIMOUT  10
// #define MEM_SIZE                (20 * 1024)      // Nếu bạn có RAM đủ, có thể tăng đến 20–24KB
#define MEMP_NUM_PBUF           64
#define MEMP_NUM_TCP_SEG        64
#define PBUF_POOL_SIZE          16
#define PBUF_POOL_BUFSIZE       1024             // Giữ nguyên nếu MTU mạng bạn < 1500
#define TCP_MSS                 512              // PPP thường dùng 512, kiểm tra lại nếu bạn đã đổi
#define TCP_SND_BUF             (4 * TCP_MSS)    // 2048
#define TCP_SND_QUEUELEN        (8 * TCP_SND_BUF / TCP_MSS)  // Đảm bảo TCP không bị nghẽn hàng đợi
#define MEMP_NUM_NETCONN        20
#define SNTP_SERVER_DNS         1
#define TCP_DEBUG                       LWIP_DBG_OFF
#define ETHARP_DEBUG                    LWIP_DBG_OFF
#define PBUF_DEBUG                      LWIP_DBG_OFF
#define IP_DEBUG                        LWIP_DBG_OFF
#define TCPIP_DEBUG                     LWIP_DBG_OFF
#define DHCP_DEBUG                      LWIP_DBG_OFF
#define UDP_DEBUG                       LWIP_DBG_OFF
#define DNS_DEBUG                       LWIP_DBG_OFF
#define MQTT_REQ_MAX_IN_FLIGHT 10
#define MQTT_REQ_TIMEOUT       5
#define MQTT_TOPIC_LENGTH 128
#define MQTT_PAYLOAD_LENGTH 4096
#define MQTT_CYCLIC_TIMER_INTERVAL 1
#define MQTT_VAR_HEADER_BUFFER_LEN (MQTT_TOPIC_LENGTH+MQTT_PAYLOAD_LENGTH+8)
#define MQTT_OUTPUT_RINGBUF_SIZE MQTT_VAR_HEADER_BUFFER_LEN*MQTT_REQ_MAX_IN_FLIGHT
#define LWIP_NETIF_STATUS_CALLBACK 1
#define SNTP_UPDATE_DELAY (30 * 60 * 1000)
#define SNTP_MAX_SERVERS  2
extern void sntp_set_time(uint32_t sntp_time);
#define SNTP_SET_SYSTEM_TIME    sntp_set_time
#define LWIP_ALTCP 1
#define LWIP_USE_EXTERNAL_MBEDTLS 1
#define LWIP_ALTCP_TLS 1
#define LWIP_ALTCP_TLS_MBEDTLS 1
#define ALTCP_MBEDTLS_DEBUG LWIP_DBG_ON
#define LWIP_TIMEVAL_PRIVATE 0

/* Hook to force DHCP BROADCAST flag on Wi-Fi interface (ST67W611 drops unicast before IP) */
#define LWIP_HOOK_DHCP_APPEND_OPTIONS(netif, dhcp, state, msg, msg_type, options_len_ptr) \
  do { \
    if ((netif) != NULL && (netif)->name[0] == 'w' && (netif)->name[1] == 'l') { \
      (msg)->flags = lwip_htons(0x8000); \
    } \
  } while(0)

/* USER CODE END 1 */

#ifdef __cplusplus
}
#endif
#endif /*__LWIPOPTS__H__ */
