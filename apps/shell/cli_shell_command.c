#include "cli_shell.h"
#include "logger.h"
#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os2.h"
#include "main.h"
#include <stdio.h>
#include <string.h>
#include "network_manager.h"
#include "wifi_driver.h"
#if (ST67_ARCH == 2)
#include "wifi_netif.h"
#endif
#include "file_io.h"
#include "lfs.h"
#include "block_device.h"
#include "config_manager.h"
#include "shell_serial.h"
#include "st67w611_port.h"
#include "st67w611.h"
#include "eth_driver.h"
#include "lwip/ip4_addr.h"
#include "lwip/dns.h"
#include "lwip/dhcp.h"

static int parse_ipv4_u32(const char *text, uint32_t *out) {
    ip4_addr_t addr;
    if (text == NULL || out == NULL) {
        return -1;
    }
    if (!ip4addr_aton(text, &addr)) {
        return -1;
    }
    *out = addr.addr;
    return 0;
}

static int shell_ping_handler(ShellContext_t *shell, int argc, char *argv[]) {
    if (argc < 2) {
        cli_shell_put_line(shell, "Usage: ping <ip>");
        return -1;
    }
    cli_shell_printf(shell, "Pinging %s (not fully implemented yet)\r\n", argv[1]);
    return 0;
}

static int shell_eth_handler(ShellContext_t *shell, int argc, char *argv[])
{
    struct netif *netif = eth_driver_get_netif();
    ethernetif_diag_t diag;
    char ip[16], mask[16], gw[16];

    if (argc < 2 || strcmp(argv[1], "status") == 0) {
        if (netif == NULL) return -1;
        ip4addr_ntoa_r(netif_ip4_addr(netif), ip, sizeof(ip));
        ip4addr_ntoa_r(netif_ip4_netmask(netif), mask, sizeof(mask));
        ip4addr_ntoa_r(netif_ip4_gw(netif), gw, sizeof(gw));
        cli_shell_printf(shell, "ETH status: up=%d link=%d IP=%s mask=%s gateway=%s dhcp=%d\r\n",
                         netif_is_up(netif), netif_is_link_up(netif), ip, mask, gw,
                         dhcp_supplied_address(netif));
        return 0;
    }
    if (strcmp(argv[1], "phy") == 0) {
        eth_driver_get_diag(&diag);
        cli_shell_printf(shell, "ETH PHY: addr=%lu id1=0x%04lX id2=0x%04lX BMSR=0x%04lX link=%ld speed=%lu duplex=%lu\r\n",
                         (unsigned long)diag.phy_address,
                         (unsigned long)diag.phy_id1, (unsigned long)diag.phy_id2,
                         (unsigned long)diag.bmsr, (long)diag.link_state,
                         (unsigned long)diag.speed, (unsigned long)diag.duplex);
        cli_shell_printf(shell, "ETH diag: rx=%lu tx=%lu dma=%lu rbu=%lu phy_rd=%lu phy_wr=%lu transitions=%lu\r\n",
                         (unsigned long)diag.rx_complete, (unsigned long)diag.tx_complete,
                         (unsigned long)diag.dma_errors, (unsigned long)diag.rx_buffer_unavailable,
                         (unsigned long)diag.phy_read_errors, (unsigned long)diag.phy_write_errors,
                         (unsigned long)diag.link_transitions);
        cli_shell_printf(shell, "ETH DHCP: tx=%lu rx=%lu discover=%lu offer=%lu request=%lu ack=%lu nack=%lu xid=0x%08lX lwip_xid=0x%08lX state=%u\r\n",
                         (unsigned long)diag.dhcp_tx, (unsigned long)diag.dhcp_rx,
                         (unsigned long)diag.dhcp_discover, (unsigned long)diag.dhcp_offer,
                         (unsigned long)diag.dhcp_request, (unsigned long)diag.dhcp_ack,
                         (unsigned long)diag.dhcp_nack, (unsigned long)diag.dhcp_last_xid,
                         (unsigned long)diag.dhcp_lwip_xid, (unsigned)diag.dhcp_state);
        return 0;
    }
    if (strcmp(argv[1], "dhcp") == 0) {
        if (netif == NULL || !netif_is_up(netif) || !netif_is_link_up(netif)) {
            cli_shell_put_line(shell, "ETH link is down");
            return -1;
        }
        dhcp_stop(netif);
        dhcp_start(netif);
        cli_shell_put_line(shell, "ETH DHCP restarted");
        return 0;
    }
    if (strcmp(argv[1], "static") == 0) {
        if (argc < 5) {
            cli_shell_put_line(shell, "Usage: eth static <ip> <netmask> <gateway>");
            return -1;
        }
        network_interface_config_t cfg;
        if (network_interface_get_config(NET_IF_ETH, &cfg) != 0 ||
            parse_ipv4_u32(argv[2], &cfg.static_ip.ip) != 0 ||
            parse_ipv4_u32(argv[3], &cfg.static_ip.netmask) != 0 ||
            parse_ipv4_u32(argv[4], &cfg.static_ip.gateway) != 0) {
            cli_shell_put_line(shell, "Invalid IPv4 value");
            return -1;
        }
        cfg.enabled = true;
        cfg.dhcp = false;
        cfg.static_ip.dns_primary = cfg.static_ip.gateway;
        cfg.static_ip.dns_secondary = cfg.static_ip.gateway;
        network_interface_set_config(NET_IF_ETH, &cfg);
        if (netif != NULL && netif_is_link_up(netif)) {
            dhcp_stop(netif);
            ip4_addr_t ip = { .addr = cfg.static_ip.ip };
            ip4_addr_t mask = { .addr = cfg.static_ip.netmask };
            ip4_addr_t gw = { .addr = cfg.static_ip.gateway };
            netif_set_addr(netif, &ip, &mask, &gw);
            netif_set_up(netif);
        }
        (void)config_save();
        cli_shell_put_line(shell, "ETH static config applied");
        return 0;
    }
    if (strcmp(argv[1], "start") == 0) return eth_driver_start();
    if (strcmp(argv[1], "stop") == 0) return eth_driver_stop();
    cli_shell_put_line(shell, "Usage: eth <status|phy|dhcp|static|start|stop>");
    return -1;
}

static int shell_reboot_handler(ShellContext_t *shell, int argc, char *argv[]) {
    cli_shell_put_line(shell, "Rebooting system...");
    vTaskDelay(pdMS_TO_TICKS(100));
    NVIC_SystemReset();
    return 0;
}

static int shell_config_handler(ShellContext_t *shell, int argc, char *argv[]) {
    char *json = config_get_json_string();
    if (json) {
        cli_shell_put_line(shell, json);
        vPortFree(json);
    } else {
        cli_shell_put_line(shell, "Error reading config");
    }
    return 0;
}

static int shell_ipconfig_handler(ShellContext_t *shell, int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    cli_shell_printf(shell, "\r\nIP Configuration:\r\n");
    cli_shell_printf(shell, " ETH:  %s\r\n", network_get_ip_address(NET_IF_ETH));
    cli_shell_printf(shell, " WIFI: %s\r\n", network_get_ip_address(NET_IF_WIFI));
    cli_shell_printf(shell, " LTE:  %s\r\n", network_get_ip_address(NET_IF_LTE));
    cli_shell_printf(shell, " USB:  %s\r\n", network_get_ip_address(NET_IF_USB));
    return 0;
}

static int shell_ls_handler(ShellContext_t *shell, int argc, char *argv[]) {
    lfs_dir_t dir;
    struct lfs_info info;
    const char *path = (argc > 1) ? argv[1] : "/";
    int err = lfs_dir_open(&lfs, &dir, path);
    if (err < 0) {
        cli_shell_printf(shell, "Error %d opening dir %s\r\n", err, path);
        return -1;
    }
    cli_shell_printf(shell, "Contents of %s:\r\n", path);
    int res;
    while ((res = lfs_dir_read(&lfs, &dir, &info)) > 0) {
        if (info.type == LFS_TYPE_DIR) {
            cli_shell_printf(shell, " [DIR]  %s\r\n", info.name);
        } else {
            cli_shell_printf(shell, " [FILE] %s (%ld bytes)\r\n", info.name, info.size);
        }
    }
    if (res < 0) {
        cli_shell_printf(shell, "Error %d reading directory\r\n", res);
    }
    lfs_dir_close(&lfs, &dir);
    return 0;
}

static int shell_cat_handler(ShellContext_t *shell, int argc, char *argv[]) {
    if (argc < 2) {
        cli_shell_put_line(shell, "Usage: cat <filename>");
        return -1;
    }
    char buffer[256];
    int read_len = file_io_read(argv[1], buffer, sizeof(buffer) - 1);
    if (read_len >= 0) {
        buffer[read_len] = '\0';
        cli_shell_put_line(shell, buffer);
    } else {
        cli_shell_printf(shell, "Error %d reading file %s\r\n", read_len, argv[1]);
    }
    return 0;
}

static int shell_rm_handler(ShellContext_t *shell, int argc, char *argv[]) {
    if (argc < 2) {
        cli_shell_put_line(shell, "Usage: rm <filename>");
        return -1;
    }
    int err = lfs_remove(&lfs, argv[1]);
    if (err == 0) {
        cli_shell_printf(shell, "Deleted %s\r\n", argv[1]);
    } else {
        cli_shell_printf(shell, "Error %d deleting %s\r\n", err, argv[1]);
    }
    return 0;
}

static int shell_wifi_handler(ShellContext_t *shell, int argc, char *argv[]) {
    (void)shell;
    if (argc < 2) {
        cli_shell_put_line(shell, "Usage: wifi <on|off|init|scan|scan_results|connect|disconnect|dhcp|static|status|ip|ping>");
        return -1;
    }

    if (strcmp(argv[1], "on") == 0) {
        network_interface_config_t cfg;
        network_interface_get_config(NET_IF_WIFI, &cfg);
        cfg.enabled = true;
        network_interface_set_config(NET_IF_WIFI, &cfg);
        cli_shell_printf(shell, "WiFi enabled (reboot or call 'wifi connect' to activate)\r\n");
        return 0;
    }

    if (strcmp(argv[1], "off") == 0) {
        network_interface_config_t cfg;
        network_interface_get_config(NET_IF_WIFI, &cfg);
        cfg.enabled = false;
        network_interface_set_config(NET_IF_WIFI, &cfg);
        cli_shell_printf(shell, "WiFi disabled\r\n");
        return 0;
    }

    if (strcmp(argv[1], "init") == 0) {
#if (ST67_ARCH == 1)
        wifi_state_t state = wifi_driver_get_state();
        cli_shell_printf(shell, "ST67W6X init state: %d\r\n", state);
        return (state == WIFI_STATE_ERROR || state == WIFI_STATE_IDLE) ? -1 : 0;
#else
        cli_shell_printf(shell, "WiFi initialization is automatic in T02 mode\r\n");
        return 0;
#endif
    }

    if (strcmp(argv[1], "connect") == 0) {
        if (argc < 3) {
            cli_shell_put_line(shell, "Usage: wifi connect <ssid> [password]");
            return -1;
        }
        const char *ssid = argv[2];
        const char *pass = (argc >= 4) ? argv[3] : "";

 #if (ST67_ARCH == 1)
        if (wifi_driver_get_state() == WIFI_STATE_IDLE ||
            wifi_driver_get_state() == WIFI_STATE_INITIALIZING) {
            cli_shell_printf(shell, "ST67W6X initialization is not ready\r\n");
            return 0;
        }
        if (wifi_driver_connect(ssid, pass) != 0) {
            cli_shell_printf(shell, "WiFi connect command failed\r\n");
            return -1;
        }
        cli_shell_printf(shell, "WiFi connect started\r\n");
        return 0;
 #else
        network_interface_config_t cfg;
        network_interface_get_config(NET_IF_WIFI, &cfg);
        cfg.enabled = true;
        strncpy(cfg.ssid, ssid, sizeof(cfg.ssid) - 1);
        cfg.ssid[sizeof(cfg.ssid) - 1] = '\0';
        strncpy(cfg.password, pass, sizeof(cfg.password) - 1);
        cfg.password[sizeof(cfg.password) - 1] = '\0';
        network_interface_set_config(NET_IF_WIFI, &cfg);
        if (config_save() != 0) {
            cli_shell_printf(shell, "Failed to persist WiFi credentials\r\n");
            return -1;
        }

        /* W6X_Init() is owned by the boot-created ST67W6X task. */
        if (wifi_driver_get_state() == WIFI_STATE_IDLE) {
            cli_shell_printf(shell, "ST67W6X startup task is not ready\r\n");
            return 0;
        }

        if (wifi_driver_get_state() == WIFI_STATE_INITIALIZING) {
            cli_shell_printf(shell, "ST67W6X initialization is still in progress\r\n");
            return 0;
        }

        network_manager_request_wifi_connect();
        cli_shell_printf(shell, "WiFi connect requested\r\n");
        return 0;
 #endif
    }

    if (strcmp(argv[1], "disconnect") == 0) {
        if (wifi_driver_disconnect() != 0) {
            cli_shell_put_line(shell, "WiFi disconnect failed");
            return -1;
        }
        cli_shell_put_line(shell, "WiFi disconnected");
        return 0;
    }

    if (strcmp(argv[1], "static") == 0) {
        if (argc < 5) {
            cli_shell_put_line(shell, "Usage: wifi static <ip> <netmask> <gateway> [dns]");
            return -1;
        }
        network_interface_config_t cfg;
        network_interface_get_config(NET_IF_WIFI, &cfg);
        cfg.enabled = true;
        cfg.dhcp = false;
        if (parse_ipv4_u32(argv[2], &cfg.static_ip.ip) != 0 ||
            parse_ipv4_u32(argv[3], &cfg.static_ip.netmask) != 0 ||
            parse_ipv4_u32(argv[4], &cfg.static_ip.gateway) != 0) {
            cli_shell_put_line(shell, "Invalid IPv4 value");
            return -1;
        }
        cfg.static_ip.dns_primary = cfg.static_ip.gateway;
        cfg.static_ip.dns_secondary = cfg.static_ip.gateway;
        if (argc >= 6) {
            if (parse_ipv4_u32(argv[5], &cfg.static_ip.dns_primary) != 0) {
                cli_shell_put_line(shell, "Invalid DNS IPv4 value");
                return -1;
            }
            cfg.static_ip.dns_secondary = cfg.static_ip.dns_primary;
        }
        network_interface_set_config(NET_IF_WIFI, &cfg);

        /* T02 owns a host lwIP netif; T01 applies this on reconnect. */
#if (ST67_ARCH == 2)
        struct netif *nif = wifi_driver_get_netif();
        if (nif != NULL && netif_is_up(nif) && netif_is_link_up(nif)) {
            network_config_t net_cfg;
            if (network_manager_get_config(&net_cfg) != 0) {
                cli_shell_put_line(shell, "Failed to read WiFi network config");
                return -1;
            }
            net_cfg.wifi = cfg;
            if (wifi_netif_apply_config(&net_cfg) != 0) {
                cli_shell_put_line(shell, "Failed to apply WiFi static config");
                return -1;
            }
        } else {
            cli_shell_put_line(shell, "WiFi static config saved; will apply when link is UP");
        }
#endif

        (void)config_save();
        cli_shell_put_line(shell, "WiFi static config applied");
        return 0;
    }

    if (strcmp(argv[1], "dhcp") == 0) {
        network_interface_config_t cfg;
        network_interface_get_config(NET_IF_WIFI, &cfg);
        cfg.enabled = true;
        cfg.dhcp = true;
        network_interface_set_config(NET_IF_WIFI, &cfg);

#if (ST67_ARCH == 2)
        struct netif *nif = wifi_driver_get_netif();
        if (nif != NULL && netif_is_up(nif) && netif_is_link_up(nif)) {
            network_config_t net_cfg;
            if (network_manager_get_config(&net_cfg) != 0) {
                cli_shell_put_line(shell, "Failed to read WiFi network config");
                return -1;
            }
            net_cfg.wifi = cfg;
            if (wifi_netif_apply_config(&net_cfg) != 0) {
                cli_shell_put_line(shell, "Failed to start WiFi DHCP");
                return -1;
            }
        } else {
            cli_shell_put_line(shell, "WiFi DHCP config saved; will apply when link is UP");
        }
#endif

        (void)config_save();
        cli_shell_put_line(shell, "WiFi switched to DHCP mode; starting DHCP...");
        return 0;
    }

    if (strcmp(argv[1], "scan") == 0) {
        cli_shell_printf(shell, "WiFi scan started...\r\n");
        return (wifi_driver_scan() == 0) ? 0 : -1;
    }

    if (strcmp(argv[1], "scan_results") == 0) {
        cli_shell_printf(shell, "WiFi scan %s\r\n%s\r\n",
                         wifi_driver_is_scanning() ? "in progress" : "results",
                         wifi_driver_get_scan_results_json());
        return 0;
    }

    if (strcmp(argv[1], "ip") == 0) {
 #if (ST67_ARCH == 1)
        uint8_t ip[4], gateway[4], netmask[4];
        if (wifi_driver_get_ip(ip, gateway, netmask) != 0) {
            cli_shell_printf(shell, "Failed to read WiFi IP\r\n");
            return -1;
        }
        cli_shell_printf(shell, "WiFi IP: %u.%u.%u.%u gateway: %u.%u.%u.%u netmask: %u.%u.%u.%u\r\n",
                         ip[0], ip[1], ip[2], ip[3],
                         gateway[0], gateway[1], gateway[2], gateway[3],
                         netmask[0], netmask[1], netmask[2], netmask[3]);
        return 0;
 #else
        cli_shell_printf(shell, "WiFi IP command is available only in T01 mode\r\n");
        return -1;
 #endif
    }

    if (strcmp(argv[1], "ping") == 0) {
 #if (ST67_ARCH == 1)
        if (argc < 3) {
            cli_shell_put_line(shell, "Usage: wifi ping <host>");
            return -1;
        }
        cli_shell_printf(shell, "WiFi ping started\r\n");
        return (wifi_driver_ping(argv[2]) == 0) ? 0 : -1;
 #else
        cli_shell_printf(shell, "WiFi ping command is available only in T01 mode\r\n");
        return -1;
 #endif
    }

    if (strcmp(argv[1], "status") == 0) {
        wifi_state_t state = wifi_driver_get_state();
        const char *state_str = "UNKNOWN";
        switch (state) {
            case WIFI_STATE_IDLE:         state_str = "IDLE"; break;
            case WIFI_STATE_INITIALIZING: state_str = "INITIALIZING"; break;
            case WIFI_STATE_INIT:         state_str = "INIT"; break;
            case WIFI_STATE_CONNECTING:   state_str = "CONNECTING"; break;
            case WIFI_STATE_CONNECTED:    state_str = "CONNECTED"; break;
            case WIFI_STATE_GOT_IP:       state_str = "GOT_IP"; break;
            case WIFI_STATE_DISCONNECTED: state_str = "DISCONNECTED"; break;
            case WIFI_STATE_ERROR:        state_str = "ERROR"; break;
        }
        network_interface_config_t cfg;
        network_interface_get_config(NET_IF_WIFI, &cfg);
        cli_shell_printf(shell, "WiFi status: %s (connected=%d link=%d ip_mode=%s)\r\n",
                         state_str, wifi_driver_status(),
                         (wifi_driver_get_netif() != NULL &&
                          netif_is_link_up(wifi_driver_get_netif())) ? 1 : 0,
                         cfg.dhcp ? "DHCP" : "STATIC");
        {
            int st67_state = -1;
            if (wifi_driver_get_st67_state(&st67_state) == 0) {
                cli_shell_printf(shell, "ST67 hardware state: %d\r\n", st67_state);
            }
        }
#if (ST67_ARCH == 2)
        if (wifi_driver_get_netif() != NULL) {
            struct netif *nif = wifi_driver_get_netif();
            char ip_buf[IP4ADDR_STRLEN_MAX];
            char gw_buf[IP4ADDR_STRLEN_MAX];
            char mask_buf[IP4ADDR_STRLEN_MAX];
            ip4addr_ntoa_r(netif_ip4_addr(nif), ip_buf, sizeof(ip_buf));
            ip4addr_ntoa_r(netif_ip4_gw(nif), gw_buf, sizeof(gw_buf));
            ip4addr_ntoa_r(netif_ip4_netmask(nif), mask_buf, sizeof(mask_buf));
            cli_shell_printf(shell, "WiFi IP: %s gateway: %s netmask: %s\r\n",
                             ip_buf, gw_buf, mask_buf);
        }
#endif
        return 0;
    }

    cli_shell_put_line(shell, "Usage: wifi <on|off|init|scan|scan_results|connect|disconnect|dhcp|static|status|ip|ping>");
    return -1;
}

static int shell_diag_handler(ShellContext_t *shell, int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    shell_serial_diag_t serial;
    st67w611_diagnostics_t st67;
    shell_serial_get_diagnostics(&serial);
    (void)st67w611_get_diagnostics(&st67);
    cli_shell_printf(shell,
                     "Shell RX: irq=%lu bytes=%lu drops=%lu\r\n"
                     "ST67: rdy_rise=%lu rdy_fall=%lu xfer=%lu timeout=%lu dma_err=%lu\r\n",
                     (unsigned long)serial.rx_irq_count,
                     (unsigned long)serial.rx_byte_count,
                     (unsigned long)serial.rx_queue_drop_count,
                     (unsigned long)st67.ready_rising_count,
                     (unsigned long)st67.ready_falling_count,
                     (unsigned long)st67.spi_transfer_count,
                     (unsigned long)st67.spi_timeout_count,
                     (unsigned long)st67.dma_error_count);
    return 0;
}

static int shell_spi_handler(ShellContext_t *shell, int argc, char *argv[])
{
    if (argc >= 2 && strcmp(argv[1], "pins") == 0) {
        st67w611_port_print_pin_states();
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "loopback") == 0) {
        uint32_t mismatches = 0U;
        int32_t ret = st67w611_port_loopback_test(&mismatches);
        if (ret == 0) {
            cli_shell_put_line(shell, "SPI loopback PASS");
        } else if (ret == 1) {
            cli_shell_printf(shell, "SPI loopback FAIL: mismatches=%lu\r\n",
                             (unsigned long)mismatches);
        } else if (ret == -2) {
            cli_shell_put_line(shell, "SPI loopback requires ST67_EN LOW");
        } else if (ret == -3) {
            cli_shell_put_line(shell, "SPI loopback build is disabled");
        } else {
            cli_shell_put_line(shell, "SPI loopback HAL transfer failed");
        }
        return ret;
    }
    if (argc >= 2 && strcmp(argv[1], "miso") == 0) {
        /*
         * Reconfigure PB4 as plain GPIO INPUT with PULLUP (no SPI AF).
         * Sample it 20 times at 100ms intervals.
         * Purpose: determine if MISO is floating (HIGH = ST67 not driving)
         *          or driven LOW (short-to-GND or ST67 outputting 0).
         */
        GPIO_InitTypeDef g = {0};
        g.Pin   = GPIO_PIN_4;
        g.Mode  = GPIO_MODE_INPUT;
        g.Pull  = GPIO_PULLUP;
        g.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(GPIOB, &g);

        cli_shell_put_line(shell, "MISO(PB4) GPIO test - 20 samples @ 100ms (pull-up active):");
        int high_count = 0, low_count = 0;
        for (int i = 0; i < 20; i++) {
            int lvl = (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_4) == GPIO_PIN_SET) ? 1 : 0;
            if (lvl) high_count++; else low_count++;
            cli_shell_printf(shell, "  [%02d] PB4=%d\r\n", i, lvl);
            osDelay(100);
        }
        cli_shell_printf(shell, "Result: HIGH=%d LOW=%d -> %s\r\n", high_count, low_count,
            (low_count == 0)  ? "FLOATING or ST67 not driving (pull-up wins)" :
            (high_count == 0) ? "MISO STUCK LOW - short-to-GND or ST67 driving 0" :
                                "MISO TOGGLING - intermittent connection");

        /* Restore PB4 as SPI1_MISO (AF5) */
        g.Mode      = GPIO_MODE_AF_PP;
        g.Pull      = GPIO_PULLUP;
        g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
        g.Alternate = GPIO_AF5_SPI1;
        HAL_GPIO_Init(GPIOB, &g);
        cli_shell_put_line(shell, "PB4 restored to SPI1_MISO AF5");
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "miso2") == 0) {
        /*
         * Disable ST67 EN pin and wait up to 5s, sampling PB4 and PB5 every 500ms.
         * This determines if ST67's MISO driver releases after full power-down.
         * If PB4/PB5 go HIGH at some point: ST67 was driving them LOW (firmware/state issue).
         * If they stay LOW for 5s: true PCB/chip short-to-GND.
         */
        GPIO_InitTypeDef g = {0};
        /* Configure PB4 as GPIO input with PULLUP */
        g.Pin   = GPIO_PIN_4;
        g.Mode  = GPIO_MODE_INPUT;
        g.Pull  = GPIO_PULLUP;
        g.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(GPIOB, &g);
        /* Configure PB5 as GPIO input with PULLUP */
        g.Pin   = GPIO_PIN_5;
        HAL_GPIO_Init(GPIOB, &g);

        cli_shell_put_line(shell, "=== spi miso2: Extended EN-OFF test (5s) ===");
        cli_shell_printf(shell, "Before EN off -> PB4(MISO)=%d  PB5(MOSI)=%d\r\n",
            (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_4) == GPIO_PIN_SET) ? 1 : 0,
            (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_5) == GPIO_PIN_SET) ? 1 : 0);

        /* Disable ST67 */
        HAL_GPIO_WritePin(ST67_EN_GPIO_Port, ST67_EN_Pin, GPIO_PIN_RESET);
        cli_shell_put_line(shell, "ST67 EN=LOW. Sampling every 500ms for 5s...");

        int pb4_went_high = 0, pb5_went_high = 0;
        for (int i = 0; i < 10; i++) {
            osDelay(500);
            int pb4 = (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_4) == GPIO_PIN_SET) ? 1 : 0;
            int pb5 = (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_5) == GPIO_PIN_SET) ? 1 : 0;
            cli_shell_printf(shell, "  t=%ds -> PB4=%d  PB5=%d\r\n", (i+1)/2, pb4, pb5);
            if (pb4) pb4_went_high = 1;
            if (pb5) pb5_went_high = 1;
        }

        cli_shell_put_line(shell, "--- DIAGNOSIS ---");
        if (pb4_went_high) {
            cli_shell_put_line(shell, "PB4 released HIGH: ST67 was driving MISO=LOW (SPI slave stuck)");
            cli_shell_put_line(shell, "  -> Hardware trace OK. ST67 init sequence or firmware issue.");
        } else {
            cli_shell_put_line(shell, "PB4 stayed LOW 5s: PCB short-to-GND on MISO net (hardware defect)");
        }
        if (pb5_went_high) {
            cli_shell_put_line(shell, "PB5 released HIGH: ST67 was driving MOSI input LOW");
            cli_shell_put_line(shell, "  -> Check for MOSI/MISO swap on ST67 module pins");
        } else {
            cli_shell_put_line(shell, "PB5 stayed LOW 5s: PCB short-to-GND on MOSI net OR strong pull-down");
        }

        /* Re-enable ST67 */
        HAL_GPIO_WritePin(ST67_EN_GPIO_Port, ST67_EN_Pin, GPIO_PIN_SET);
        cli_shell_put_line(shell, "ST67 EN=HIGH restored");

        /* Restore PB4+PB5 as SPI1 AF5 */
        g.Pin       = GPIO_PIN_4;
        g.Mode      = GPIO_MODE_AF_PP;
        g.Pull      = GPIO_PULLUP;
        g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
        g.Alternate = GPIO_AF5_SPI1;
        HAL_GPIO_Init(GPIOB, &g);
        g.Pin       = GPIO_PIN_5;
        g.Pull      = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOB, &g);
        cli_shell_put_line(shell, "PB4+PB5 restored to SPI1_MISO/MOSI AF5");
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "miso3") == 0) {
        /*
         * Drive PB4 as GPIO OUTPUT HIGH, then LOW, and read back.
         * Also read PB5 (MOSI) in input mode for comparison.
         * Purpose: measure how strongly PB4 is being pulled, and check
         *          if MISO/MOSI might be physically swapped on the PCB.
         *
         * If PB4 can be driven HIGH and reads back 1: pull is weaker than GPIO output
         *   (could be leakage, weak pull-down, or ESD-damaged diode)
         * If PB4 output HIGH reads back 0: hard short to GND
         */
        GPIO_InitTypeDef g = {0};

        /* First: Read PB5 (MOSI) as plain input to compare */
        g.Pin   = GPIO_PIN_5;
        g.Mode  = GPIO_MODE_INPUT;
        g.Pull  = GPIO_PULLUP;
        g.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(GPIOB, &g);
        int pb5_input = (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_5) == GPIO_PIN_SET) ? 1 : 0;
        cli_shell_printf(shell, "PB5 (MOSI) as input+pullup: %d\r\n", pb5_input);

        /* Now drive PB4 HIGH as GPIO output */
        g.Pin   = GPIO_PIN_4;
        g.Mode  = GPIO_MODE_OUTPUT_PP;
        g.Pull  = GPIO_NOPULL;
        g.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(GPIOB, &g);

        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_SET);
        osDelay(10);
        int pb4_driveH = (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_4) == GPIO_PIN_SET) ? 1 : 0;
        cli_shell_printf(shell, "PB4 driven HIGH -> readback: %d\r\n", pb4_driveH);

        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET);
        osDelay(10);
        int pb4_driveL = (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_4) == GPIO_PIN_SET) ? 1 : 0;
        cli_shell_printf(shell, "PB4 driven LOW  -> readback: %d\r\n", pb4_driveL);

        if (pb4_driveH == 1 && pb4_driveL == 0) {
            cli_shell_put_line(shell, "PB4 GPIO OK. External pull-down exists (weaker than output driver).");
            cli_shell_put_line(shell, "MISO line has external pull-down resistor or weak short.");
        } else if (pb4_driveH == 0) {
            cli_shell_put_line(shell, "PB4 HARD SHORT: driven HIGH but reads LOW -> strong GND short.");
        } else {
            cli_shell_put_line(shell, "Unexpected result - check hardware.");
        }

        if (pb5_input == 0) {
            cli_shell_put_line(shell, "WARNING: PB5 (MOSI) also reads LOW in input mode!");
            cli_shell_put_line(shell, "  -> Possible MISO/MOSI swap on PCB (PB5 connected to ST67 MISO output=LOW)");
        }

        /* Restore PB5 as SPI1 MOSI (AF5) */
        g.Pin       = GPIO_PIN_5;
        g.Mode      = GPIO_MODE_AF_PP;
        g.Pull      = GPIO_NOPULL;
        g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
        g.Alternate = GPIO_AF5_SPI1;
        HAL_GPIO_Init(GPIOB, &g);

        /* Restore PB4 as SPI1_MISO (AF5) */
        g.Pin       = GPIO_PIN_4;
        g.Mode      = GPIO_MODE_AF_PP;
        g.Pull      = GPIO_PULLUP;
        g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
        g.Alternate = GPIO_AF5_SPI1;
        HAL_GPIO_Init(GPIOB, &g);
        cli_shell_put_line(shell, "PB4+PB5 restored to SPI1 AF5");
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "bitbang") == 0) {
        /*
         * Bit-bang SPI header exchange using plain GPIO.
         * Bypasses SPI peripheral entirely (no KEEP_IO_STATE, no AF).
         * Mode 0: SCK idle LOW, sample on rising edge.
         * CS = PD7 (active HIGH for ST67W611M1)
         * SCK = PB3, MOSI = PB5, MISO = PB4
         *
         * Sends: AA 55 00 00 00 00 00 00 (header magic)
         * Reads: 8 bytes from MISO
         *
         * If MISO reads all 0x00: PB4 is truly stuck LOW (hardware defect)
         * If MISO reads AA 55 ...: SPI peripheral was the problem, bitbang works!
         */
        GPIO_InitTypeDef g = {0};

        /* Configure SCK (PB3) as output, idle LOW */
        g.Pin = GPIO_PIN_3; g.Mode = GPIO_MODE_OUTPUT_PP;
        g.Pull = GPIO_NOPULL; g.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOB, &g);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_RESET);  /* SCK=0 */

        /* Configure MOSI (PB5) as output */
        g.Pin = GPIO_PIN_5;
        HAL_GPIO_Init(GPIOB, &g);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET);

        /* Configure MISO (PB4) as input with pull-up */
        g.Pin = GPIO_PIN_4; g.Mode = GPIO_MODE_INPUT;
        g.Pull = GPIO_PULLUP;
        HAL_GPIO_Init(GPIOB, &g);

        /* Assert CS = HIGH (active-high) */
        HAL_GPIO_WritePin(ST67_NCS_GPIO_Port, ST67_NCS_Pin, GPIO_PIN_SET);
        osDelay(5);  /* Setup time */

        uint8_t tx[8] = {0xAA, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        uint8_t rx[8] = {0};

        cli_shell_put_line(shell, "=== SPI bit-bang test (Mode 0, MSB first) ===");

        for (int byte_i = 0; byte_i < 8; byte_i++) {
            uint8_t rx_byte = 0;
            for (int bit = 7; bit >= 0; bit--) {
                /* Set MOSI bit */
                if (tx[byte_i] & (1u << bit)) {
                    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_SET);
                } else {
                    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET);
                }
                __NOP(); __NOP(); __NOP(); __NOP();
                /* Rising edge - sample MISO */
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_SET);   /* SCK=1 */
                __NOP(); __NOP(); __NOP(); __NOP();
                if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_4) == GPIO_PIN_SET) {
                    rx_byte |= (1u << bit);
                }
                /* Falling edge */
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_RESET); /* SCK=0 */
                __NOP(); __NOP(); __NOP(); __NOP();
            }
            rx[byte_i] = rx_byte;
        }

        /* Deassert CS = LOW */
        HAL_GPIO_WritePin(ST67_NCS_GPIO_Port, ST67_NCS_Pin, GPIO_PIN_RESET);

        cli_shell_printf(shell, "TX: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
            tx[0],tx[1],tx[2],tx[3],tx[4],tx[5],tx[6],tx[7]);
        cli_shell_printf(shell, "RX: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
            rx[0],rx[1],rx[2],rx[3],rx[4],rx[5],rx[6],rx[7]);

        int all_zero = 1;
        for (int i = 0; i < 8; i++) { if (rx[i] != 0) { all_zero = 0; break; } }
        if (all_zero) {
            cli_shell_put_line(shell, "RX all zeros: PB4(MISO) stuck LOW - hardware defect confirmed");
        } else if (rx[0] == 0xAA && rx[1] == 0x55) {
            cli_shell_put_line(shell, "RX magic match! ST67 responding - SPI peripheral was the issue!");
        } else {
            cli_shell_put_line(shell, "RX non-zero but wrong magic - partial communication");
        }

        /* Restore PB3 PB4 PB5 to SPI1 AF5 */
        g.Mode = GPIO_MODE_AF_PP; g.Pull = GPIO_NOPULL;
        g.Speed = GPIO_SPEED_FREQ_VERY_HIGH; g.Alternate = GPIO_AF5_SPI1;
        g.Pin = GPIO_PIN_3; HAL_GPIO_Init(GPIOB, &g);
        g.Pin = GPIO_PIN_5; HAL_GPIO_Init(GPIOB, &g);
        g.Pull = GPIO_PULLUP;
        g.Pin = GPIO_PIN_4; HAL_GPIO_Init(GPIOB, &g);
        cli_shell_put_line(shell, "PB3/PB4/PB5 restored to SPI1 AF5");
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "bitbang2") == 0) {
        /*
         * Bit-bang SPI with MISO/MOSI SWAPPED: PB5=MISO(input), PB4=MOSI(output).
         * Tests hypothesis: PCB routes PB5→ST67_MISO, PB4→ST67_MOSI.
         * If PB5 reads AA 55 from ST67: PCB has MISO/MOSI swapped!
         */
        GPIO_InitTypeDef g = {0};

        /* SCK (PB3) as output, idle LOW */
        g.Pin = GPIO_PIN_3; g.Mode = GPIO_MODE_OUTPUT_PP;
        g.Pull = GPIO_NOPULL; g.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOB, &g);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_RESET);

        /* PB4 as MOSI (output) */
        g.Pin = GPIO_PIN_4;
        HAL_GPIO_Init(GPIOB, &g);

        /* PB5 as MISO (input with pullup) */
        g.Pin = GPIO_PIN_5; g.Mode = GPIO_MODE_INPUT;
        g.Pull = GPIO_PULLUP;
        HAL_GPIO_Init(GPIOB, &g);

        /* Assert CS = HIGH */
        HAL_GPIO_WritePin(ST67_NCS_GPIO_Port, ST67_NCS_Pin, GPIO_PIN_SET);
        osDelay(5);

        uint8_t tx[8] = {0xAA, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        uint8_t rx[8] = {0};

        /* Read PB5 level before first clock */
        int pb5_before = (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_5) == GPIO_PIN_SET) ? 1 : 0;
        cli_shell_printf(shell, "=== bitbang2: PB5=MISO, PB4=MOSI ===\r\n");
        cli_shell_printf(shell, "PB5 idle (before SCK): %d\r\n", pb5_before);

        for (int byte_i = 0; byte_i < 8; byte_i++) {
            uint8_t rx_byte = 0;
            for (int bit = 7; bit >= 0; bit--) {
                /* MOSI on PB4 */
                if (tx[byte_i] & (1u << bit)) {
                    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_SET);
                } else {
                    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET);
                }
                __NOP(); __NOP(); __NOP(); __NOP();
                /* Rising edge - sample MISO on PB5 */
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_SET);
                __NOP(); __NOP(); __NOP(); __NOP();
                if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_5) == GPIO_PIN_SET) {
                    rx_byte |= (1u << bit);
                }
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_RESET);
                __NOP(); __NOP(); __NOP(); __NOP();
            }
            rx[byte_i] = rx_byte;
        }

        /* Deassert CS */
        HAL_GPIO_WritePin(ST67_NCS_GPIO_Port, ST67_NCS_Pin, GPIO_PIN_RESET);

        cli_shell_printf(shell, "TX: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
            tx[0],tx[1],tx[2],tx[3],tx[4],tx[5],tx[6],tx[7]);
        cli_shell_printf(shell, "RX: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
            rx[0],rx[1],rx[2],rx[3],rx[4],rx[5],rx[6],rx[7]);

        if (rx[0] == 0xAA && rx[1] == 0x55) {
            cli_shell_put_line(shell, "*** MAGIC MATCH on PB5! PCB has MISO/MOTI swapped! ***");
            cli_shell_put_line(shell, "PB5=ST67_MISO, PB4=ST67_MOSI. Enable IOSwap to fix in firmware.");
        } else if (rx[0] == 0xFF) {
            cli_shell_put_line(shell, "PB5 = 0xFF: floating - PB5 not connected to ST67 MISO");
        } else if (rx[0] == 0x00) {
            cli_shell_put_line(shell, "PB5 = all zeros: ST67 not responding on PB5");
        } else {
            cli_shell_printf(shell, "PB5 partial response (first byte=0x%02X) - possible partial comm\r\n", rx[0]);
        }

        /* Restore PB3 PB4 PB5 to SPI1 AF5 */
        g.Mode = GPIO_MODE_AF_PP; g.Pull = GPIO_NOPULL;
        g.Speed = GPIO_SPEED_FREQ_VERY_HIGH; g.Alternate = GPIO_AF5_SPI1;
        g.Pin = GPIO_PIN_3; HAL_GPIO_Init(GPIOB, &g);
        g.Pin = GPIO_PIN_4;
        g.Pull = GPIO_PULLUP;
        HAL_GPIO_Init(GPIOB, &g);
        g.Pin = GPIO_PIN_5; g.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOB, &g);
        cli_shell_put_line(shell, "PB3/PB4/PB5 restored");
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "cstest") == 0) {
        /*
         * Test CS Active-LOW vs Active-HIGH and Mode 0 vs Mode 3 bitbang.
         * Bypasses HAL SPI.
         */
        GPIO_InitTypeDef g = {0};

        /* SCK (PB3) as GPIO output */
        g.Pin = GPIO_PIN_3; g.Mode = GPIO_MODE_OUTPUT_PP;
        g.Pull = GPIO_NOPULL; g.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOB, &g);

        /* MOSI (PB5) as GPIO output */
        g.Pin = GPIO_PIN_5; g.Mode = GPIO_MODE_OUTPUT_PP;
        HAL_GPIO_Init(GPIOB, &g);

        /* MISO (PB4) as GPIO input with pullup */
        g.Pin = GPIO_PIN_4; g.Mode = GPIO_MODE_INPUT;
        g.Pull = GPIO_PULLUP;
        HAL_GPIO_Init(GPIOB, &g);

        uint8_t tx[8] = {0xAA, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        uint8_t rx_active_low[8] = {0};
        uint8_t rx_active_high[8] = {0};

        /* TEST 1: CS Active-LOW (CS=0 during transfer, CS=1 idle) */
        cli_shell_put_line(shell, "=== TEST 1: CS Active-LOW (CS=0 active) ===");
        HAL_GPIO_WritePin(ST67_NCS_GPIO_Port, ST67_NCS_Pin, GPIO_PIN_RESET); /* CS = 0 */
        osDelay(2);
        for (int b = 0; b < 8; b++) {
            uint8_t val = 0;
            for (int bit = 7; bit >= 0; bit--) {
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, (tx[b] & (1u << bit)) ? GPIO_PIN_SET : GPIO_PIN_RESET);
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_RESET);
                for (volatile int d=0; d<50; d++);
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_SET);
                for (volatile int d=0; d<50; d++);
                if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_4) == GPIO_PIN_SET) val |= (1u << bit);
            }
            rx_active_low[b] = val;
        }
        HAL_GPIO_WritePin(ST67_NCS_GPIO_Port, ST67_NCS_Pin, GPIO_PIN_SET); /* CS = 1 idle */
        cli_shell_printf(shell, "Active-LOW  RX: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
            rx_active_low[0],rx_active_low[1],rx_active_low[2],rx_active_low[3],
            rx_active_low[4],rx_active_low[5],rx_active_low[6],rx_active_low[7]);

        osDelay(10);

        /* TEST 2: CS Active-HIGH (CS=1 during transfer, CS=0 idle) */
        cli_shell_put_line(shell, "=== TEST 2: CS Active-HIGH (CS=1 active) ===");
        HAL_GPIO_WritePin(ST67_NCS_GPIO_Port, ST67_NCS_Pin, GPIO_PIN_SET); /* CS = 1 */
        osDelay(2);
        for (int b = 0; b < 8; b++) {
            uint8_t val = 0;
            for (int bit = 7; bit >= 0; bit--) {
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, (tx[b] & (1u << bit)) ? GPIO_PIN_SET : GPIO_PIN_RESET);
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_RESET);
                for (volatile int d=0; d<50; d++);
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_SET);
                for (volatile int d=0; d<50; d++);
                if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_4) == GPIO_PIN_SET) val |= (1u << bit);
            }
            rx_active_high[b] = val;
        }
        HAL_GPIO_WritePin(ST67_NCS_GPIO_Port, ST67_NCS_Pin, GPIO_PIN_RESET); /* CS = 0 idle */
        cli_shell_printf(shell, "Active-HIGH RX: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
            rx_active_high[0],rx_active_high[1],rx_active_high[2],rx_active_high[3],
            rx_active_high[4],rx_active_high[5],rx_active_high[6],rx_active_high[7]);

        /* Restore PB3 PB4 PB5 to SPI1 AF5 */
        g.Mode = GPIO_MODE_AF_PP; g.Pull = GPIO_NOPULL;
        g.Speed = GPIO_SPEED_FREQ_VERY_HIGH; g.Alternate = GPIO_AF5_SPI1;
        g.Pin = GPIO_PIN_3; HAL_GPIO_Init(GPIOB, &g);
        g.Pin = GPIO_PIN_4; g.Pull = GPIO_PULLUP; HAL_GPIO_Init(GPIOB, &g);
        g.Pin = GPIO_PIN_5; g.Pull = GPIO_NOPULL; HAL_GPIO_Init(GPIOB, &g);
        cli_shell_put_line(shell, "PB3/PB4/PB5 restored");
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "reset_test") == 0) {
        /*
         * Test ST67 BOOT pin modes: BOOT=0 vs BOOT=1 during EN reset pulse.
         * Also monitor ST67 UART TX (PD6) and RDY (PD0).
         */
        GPIO_InitTypeDef g = {0};

        /* Configure PD6 (ST67_TX) as GPIO input with pullup */
        g.Pin = GPIO_PIN_6; g.Mode = GPIO_MODE_INPUT; g.Pull = GPIO_PULLUP;
        HAL_GPIO_Init(GPIOD, &g);

        cli_shell_put_line(shell, "=== SPI Reset Mode Test ===");
        cli_shell_printf(shell, "Initial PD6 (ST67_TX)=%d, PD0 (RDY)=%d\r\n",
            HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_6) == GPIO_PIN_SET ? 1 : 0,
            HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_0) == GPIO_PIN_SET ? 1 : 0);

        /* Configure SPI pins for bit-bang */
        g.Pin = GPIO_PIN_3; g.Mode = GPIO_MODE_OUTPUT_PP; g.Pull = GPIO_NOPULL; g.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOB, &g); HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_RESET);
        g.Pin = GPIO_PIN_5; g.Mode = GPIO_MODE_OUTPUT_PP; HAL_GPIO_Init(GPIOB, &g);
        g.Pin = GPIO_PIN_4; g.Mode = GPIO_MODE_INPUT; g.Pull = GPIO_PULLUP; HAL_GPIO_Init(GPIOB, &g);

        for (int mode = 0; mode <= 1; mode++) {
            cli_shell_printf(shell, "\r\n--- Testing BOOT=%d ---\r\n", mode);
            /* Set BOOT pin */
            HAL_GPIO_WritePin(ST67_BOOT_GPIO_Port, ST67_BOOT_Pin, mode ? GPIO_PIN_SET : GPIO_PIN_RESET);
            /* Reset EN */
            HAL_GPIO_WritePin(ST67_EN_GPIO_Port, ST67_EN_Pin, GPIO_PIN_RESET);
            osDelay(200);
            /* Release EN */
            HAL_GPIO_WritePin(ST67_EN_GPIO_Port, ST67_EN_Pin, GPIO_PIN_SET);
            cli_shell_put_line(shell, "EN pulsed LOW->HIGH, waiting 1000ms...");
            osDelay(1000);

            int rdy = HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_0) == GPIO_PIN_SET ? 1 : 0;
            int tx_pin = HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_6) == GPIO_PIN_SET ? 1 : 0;
            cli_shell_printf(shell, "After boot: RDY(PD0)=%d, ST67_TX(PD6)=%d\r\n", rdy, tx_pin);

            /* Bit-bang test with CS Active-HIGH */
            HAL_GPIO_WritePin(ST67_NCS_GPIO_Port, ST67_NCS_Pin, GPIO_PIN_SET);
            osDelay(2);
            uint8_t tx_buf[8] = {0xAA, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
            uint8_t rx_buf[8] = {0};
            for (int b = 0; b < 8; b++) {
                uint8_t val = 0;
                for (int bit = 7; bit >= 0; bit--) {
                    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, (tx_buf[b] & (1u << bit)) ? GPIO_PIN_SET : GPIO_PIN_RESET);
                    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_RESET);
                    for (volatile int d=0; d<50; d++);
                    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_SET);
                    for (volatile int d=0; d<50; d++);
                    if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_4) == GPIO_PIN_SET) val |= (1u << bit);
                }
                rx_buf[b] = val;
            }
            HAL_GPIO_WritePin(ST67_NCS_GPIO_Port, ST67_NCS_Pin, GPIO_PIN_RESET);
            cli_shell_printf(shell, "CS=1 RX: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
                rx_buf[0],rx_buf[1],rx_buf[2],rx_buf[3],rx_buf[4],rx_buf[5],rx_buf[6],rx_buf[7]);

            /* Bit-bang test with CS Active-LOW */
            HAL_GPIO_WritePin(ST67_NCS_GPIO_Port, ST67_NCS_Pin, GPIO_PIN_RESET);
            osDelay(2);
            memset(rx_buf, 0, 8);
            for (int b = 0; b < 8; b++) {
                uint8_t val = 0;
                for (int bit = 7; bit >= 0; bit--) {
                    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, (tx_buf[b] & (1u << bit)) ? GPIO_PIN_SET : GPIO_PIN_RESET);
                    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_RESET);
                    for (volatile int d=0; d<50; d++);
                    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_SET);
                    for (volatile int d=0; d<50; d++);
                    if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_4) == GPIO_PIN_SET) val |= (1u << bit);
                }
                rx_buf[b] = val;
            }
            HAL_GPIO_WritePin(ST67_NCS_GPIO_Port, ST67_NCS_Pin, GPIO_PIN_SET);
            cli_shell_printf(shell, "CS=0 RX: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
                rx_buf[0],rx_buf[1],rx_buf[2],rx_buf[3],rx_buf[4],rx_buf[5],rx_buf[6],rx_buf[7]);
        }

        /* Restore BOOT pin to 0 */
        HAL_GPIO_WritePin(ST67_BOOT_GPIO_Port, ST67_BOOT_Pin, GPIO_PIN_RESET);

        /* Restore PB3 PB4 PB5 to SPI1 AF5 */
        g.Mode = GPIO_MODE_AF_PP; g.Pull = GPIO_NOPULL;
        g.Speed = GPIO_SPEED_FREQ_VERY_HIGH; g.Alternate = GPIO_AF5_SPI1;
        g.Pin = GPIO_PIN_3; HAL_GPIO_Init(GPIOB, &g);
        g.Pin = GPIO_PIN_4; g.Pull = GPIO_PULLUP; HAL_GPIO_Init(GPIOB, &g);
        g.Pin = GPIO_PIN_5; g.Pull = GPIO_NOPULL; HAL_GPIO_Init(GPIOB, &g);
        cli_shell_put_line(shell, "Restore complete");
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "spimode_test") == 0) {
        /*
         * Test HAL SPI Mode 0, Mode 1, Mode 2, Mode 3 automatically with Active-LOW CS.
         */
        extern SPI_HandleTypeDef hspi1;
        uint8_t tx_buf[8] = {0xAA, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        uint8_t rx_buf[8] = {0};

        cli_shell_put_line(shell, "=== Testing 4 SPI Modes with HAL SPI1 (Active-LOW CS) ===");

        uint32_t polarities[4] = {SPI_POLARITY_LOW, SPI_POLARITY_LOW, SPI_POLARITY_HIGH, SPI_POLARITY_HIGH};
        uint32_t phases[4]     = {SPI_PHASE_1EDGE,  SPI_PHASE_2EDGE,  SPI_PHASE_1EDGE,   SPI_PHASE_2EDGE};
        const char *mode_names[4] = {"Mode 0 (CPOL=0, CPHA=1EDGE)", "Mode 1 (CPOL=0, CPHA=2EDGE)",
                                     "Mode 2 (CPOL=1, CPHA=1EDGE)", "Mode 3 (CPOL=1, CPHA=2EDGE)"};

        for (int m = 0; m < 4; m++) {
            hspi1.Init.CLKPolarity = polarities[m];
            hspi1.Init.CLKPhase    = phases[m];
            HAL_SPI_Init(&hspi1);

            memset(rx_buf, 0, 8);
            /* Assert CS (LOW) */
            HAL_GPIO_WritePin(ST67_NCS_GPIO_Port, ST67_NCS_Pin, GPIO_PIN_RESET);
            osDelay(1);

            HAL_StatusTypeDef st = HAL_SPI_TransmitReceive(&hspi1, tx_buf, rx_buf, 8, 200);

            /* Deassert CS (HIGH) */
            HAL_GPIO_WritePin(ST67_NCS_GPIO_Port, ST67_NCS_Pin, GPIO_PIN_SET);

            cli_shell_printf(shell, "[%s] status=%d -> RX: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
                mode_names[m], st, rx_buf[0], rx_buf[1], rx_buf[2], rx_buf[3],
                rx_buf[4], rx_buf[5], rx_buf[6], rx_buf[7]);
            osDelay(5);
        }

        /* Restore Mode 0 */
        hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
        hspi1.Init.CLKPhase    = SPI_PHASE_1EDGE;
        HAL_SPI_Init(&hspi1);
        cli_shell_put_line(shell, "Restored SPI1 to Mode 0");
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "mosi_test") == 0) {
        /*
         * Toggle PB5 (MOSI) HIGH (3.3V) and LOW (0V) at 2-second intervals.
         * Allows measuring voltage on ST67 MOSI pin to check trace continuity.
         */
        GPIO_InitTypeDef g = {0};
        g.Pin   = GPIO_PIN_5;
        g.Mode  = GPIO_MODE_OUTPUT_PP;
        g.Pull  = GPIO_NOPULL;
        g.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(GPIOB, &g);

        cli_shell_put_line(shell, "=== MOSI (PB5) Hardware Toggle Test ===");
        cli_shell_put_line(shell, "Toggling PB5: 2s HIGH (3.3V), 2s LOW (0V) for 5 cycles...");

        for (int i = 1; i <= 5; i++) {
            cli_shell_printf(shell, "Cycle %d: PB5 = HIGH (3.3V) -> Measure ST67 MOSI pin now!\r\n", i);
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_SET);
            osDelay(2000);

            cli_shell_printf(shell, "Cycle %d: PB5 = LOW  (0.0V) -> Measure ST67 MOSI pin now!\r\n", i);
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET);
            osDelay(2000);
        }

        /* Restore PB5 as SPI1 MOSI (AF5) */
        g.Pin       = GPIO_PIN_5;
        g.Mode      = GPIO_MODE_AF_PP;
        g.Pull      = GPIO_NOPULL;
        g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
        g.Alternate = GPIO_AF5_SPI1;
        HAL_GPIO_Init(GPIOB, &g);
        cli_shell_put_line(shell, "PB5 restored to SPI1_MOSI AF5");
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "matrix_scan") == 0) {
        /*
         * Exhaustive matrix scan across:
         * - CS Active Polarity: HIGH (1) vs LOW (0)
         * - SPI Modes: Mode 0, Mode 1, Mode 2, Mode 3
         * - Clock Prescalers: 16 (10.4MHz), 32 (5.2MHz), 64 (2.6MHz), 128 (1.3MHz)
         * - Header formats: AA 55 vs 55 AA
         */
        extern SPI_HandleTypeDef hspi1;
        uint8_t tx1[8] = {0xAA, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        uint8_t tx2[8] = {0x55, 0xAA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        uint8_t rx[8] = {0};

        cli_shell_put_line(shell, "=== Starting SPI Matrix Scan ===");
        cli_shell_put_line(shell, "Resetting ST67 (BOOT=0, EN pulse), waiting 2500ms...");

        /* Power on ST67 in mission mode */
        HAL_GPIO_WritePin(ST67_BOOT_GPIO_Port, ST67_BOOT_Pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(ST67_EN_GPIO_Port, ST67_EN_Pin, GPIO_PIN_RESET);
        osDelay(200);
        HAL_GPIO_WritePin(ST67_EN_GPIO_Port, ST67_EN_Pin, GPIO_PIN_SET);
        osDelay(2500);

        int rdy_state = HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_0) == GPIO_PIN_SET ? 1 : 0;
        cli_shell_printf(shell, "ST67 EN=HIGH, RDY pin state: %d (1=ready)\r\n", rdy_state);

        int match_count = 0;

        uint32_t polarities[4] = {SPI_POLARITY_LOW, SPI_POLARITY_LOW, SPI_POLARITY_HIGH, SPI_POLARITY_HIGH};
        uint32_t phases[4]     = {SPI_PHASE_1EDGE,  SPI_PHASE_2EDGE,  SPI_PHASE_1EDGE,   SPI_PHASE_2EDGE};
        uint32_t prescalers[4] = {SPI_BAUDRATEPRESCALER_16, SPI_BAUDRATEPRESCALER_32,
                                  SPI_BAUDRATEPRESCALER_64, SPI_BAUDRATEPRESCALER_128};
        uint32_t prescaler_vals[4] = {16, 32, 64, 128};

        for (int cs_active_high = 1; cs_active_high >= 0; cs_active_high--) {
            for (int p_idx = 0; p_idx < 4; p_idx++) {
                for (int m = 0; m < 4; m++) {
                    hspi1.Init.BaudRatePrescaler = prescalers[p_idx];
                    hspi1.Init.CLKPolarity       = polarities[m];
                    hspi1.Init.CLKPhase          = phases[m];
                    HAL_SPI_Init(&hspi1);

                    for (int hdr = 0; hdr < 2; hdr++) {
                        uint8_t *tx = (hdr == 0) ? tx1 : tx2;
                        memset(rx, 0, 8);

                        /* Assert CS */
                        HAL_GPIO_WritePin(ST67_NCS_GPIO_Port, ST67_NCS_Pin, cs_active_high ? GPIO_PIN_SET : GPIO_PIN_RESET);
                        for (volatile int d=0; d<100; d++);

                        HAL_SPI_TransmitReceive(&hspi1, tx, rx, 8, 100);

                        /* Deassert CS */
                        HAL_GPIO_WritePin(ST67_NCS_GPIO_Port, ST67_NCS_Pin, cs_active_high ? GPIO_PIN_RESET : GPIO_PIN_SET);

                        int non_zero = 0;
                        for (int k = 0; k < 8; k++) { if (rx[k] != 0) { non_zero = 1; break; } }

                        if (non_zero) {
                            match_count++;
                            cli_shell_printf(shell, "*** MATCH! CS_Active=%s, Prescaler=/%lu, Mode=%d, Header=%s -> RX: %02X %02X %02X %02X %02X %02X %02X %02X ***\r\n",
                                cs_active_high ? "HIGH(1)" : "LOW(0)",
                                (unsigned long)prescaler_vals[p_idx], m,
                                hdr == 0 ? "AA 55" : "55 AA",
                                rx[0], rx[1], rx[2], rx[3], rx[4], rx[5], rx[6], rx[7]);
                        }
                    }
                }
            }
        }

        if (match_count == 0) {
            cli_shell_put_line(shell, "Scan complete: All combinations returned all 0x00.");
        } else {
            cli_shell_printf(shell, "Scan complete: Found %d matching configuration(s)!\r\n", match_count);
        }

        /* Restore Mode 0, Prescaler 64 */
        hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_64;
        hspi1.Init.CLKPolarity       = SPI_POLARITY_LOW;
        hspi1.Init.CLKPhase          = SPI_PHASE_1EDGE;
        HAL_SPI_Init(&hspi1);
        return 0;
    }
    cli_shell_put_line(shell, "Usage: spi <pins|loopback|miso|miso2|miso3|bitbang|bitbang2|cstest|reset_test|spimode_test|mosi_test|matrix_scan>");
    return -1;
}

extern int cmd_st67_boot(ShellContext_t *shell, int argc, char **argv) {
    cli_shell_printf(shell, "\r\n=== ST67 Boot Strapping Scan ===\r\n");
    
    int found = 0;
    
    for (int boot = 0; boot <= 1; boot++) {
        for (int wakeup = 0; wakeup <= 1; wakeup++) {
            for (int cs = 0; cs <= 1; cs++) {
                cli_shell_printf(shell, "Testing BOOT=%d, WAKEUP=%d, CS=%d...\r\n", boot, wakeup, cs);
                
                // 1. Reset all pins
                HAL_GPIO_WritePin(GPIOD, GPIO_PIN_1, GPIO_PIN_RESET); // EN (PD1)
                HAL_GPIO_WritePin(GPIOD, GPIO_PIN_2, boot ? GPIO_PIN_SET : GPIO_PIN_RESET); // BOOT (PD2)
                HAL_GPIO_WritePin(GPIOD, GPIO_PIN_3, wakeup ? GPIO_PIN_SET : GPIO_PIN_RESET); // WAKEUP (PD3)
                HAL_GPIO_WritePin(GPIOD, GPIO_PIN_7, cs ? GPIO_PIN_SET : GPIO_PIN_RESET); // CS (PD7)
                
                osDelay(200);
                
                // 2. Enable chip
                HAL_GPIO_WritePin(GPIOD, GPIO_PIN_1, GPIO_PIN_SET); // EN
                
                // 3. Poll RDY
                int rdy_went_high = 0;
                for(int i = 0; i < 150; i++) {
                    if(HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_0) == GPIO_PIN_SET) { // RDY (PD0)
                        cli_shell_printf(shell, ">>> SUCCESS! RDY went HIGH at %d ms with BOOT=%d, WAKEUP=%d, CS=%d\r\n", 
                                       i * 10, boot, wakeup, cs);
                        rdy_went_high = 1;
                        found = 1;
                        break;
                    }
                    osDelay(10);
                }
                
                if (found) break;
            }
            if (found) break;
        }
        if (found) break;
    }
    
    if(!found) {
        cli_shell_printf(shell, "ERROR: RDY never went HIGH for ANY combination.\r\n");
    }
    
    cli_shell_printf(shell, "=== Scan Done ===\r\n");
    return 0;
}

#include "mqtt_client.h"

static int shell_mqtt_handler(ShellContext_t *shell, int argc, char *argv[]) {
    if (argc < 2) {
        cli_shell_put_line(shell, "Usage: mqtt <status|pub|config>");
        return -1;
    }

    if (strcmp(argv[1], "status") == 0) {
        bool conn = app_mqtt_is_connected();
        cli_shell_printf(shell, "MQTT Connected: %s\r\n", conn ? "YES" : "NO");
        cli_shell_printf(shell, "State: %s epoch=%lu route=%s generation=%lu\r\n",
                         app_mqtt_state_name(),
                         (unsigned long)app_mqtt_attempt_epoch(),
                         network_interface_name(network_get_active_interface()),
                         (unsigned long)network_get_route_generation());
        cli_shell_printf(shell, "Stats: dns_failures=%lu timeouts=%lu stale_callbacks=%lu last_error=%d\r\n",
                         (unsigned long)app_mqtt_dns_failures(),
                         (unsigned long)app_mqtt_connect_timeouts(),
                         (unsigned long)app_mqtt_stale_callbacks(),
                         app_mqtt_last_error());
        cli_shell_printf(shell, "UART dropped frames: %lu\r\n",
                         (unsigned long)app_mqtt_dropped_uart_frames());
        cli_shell_printf(shell, "Vendor dropped frames: %lu\r\n",
                         (unsigned long)app_mqtt_dropped_vendor_logs());
        cli_shell_printf(shell, "Broker: %s:%u\r\n", g_sys_config.mqtt.broker_url, (unsigned)g_sys_config.mqtt.port);
        cli_shell_printf(shell, "Base Topic: %s\r\n", g_sys_config.mqtt.base_topic);
        return 0;
    }

    if (strcmp(argv[1], "config") == 0) {
        if (argc < 3) {
            cli_shell_put_line(shell, "Usage: mqtt config <broker_url> [port]");
            return -1;
        }
        sys_config_lock();
        strncpy(g_sys_config.mqtt.broker_url, argv[2], sizeof(g_sys_config.mqtt.broker_url) - 1);
        if (argc >= 4) {
            g_sys_config.mqtt.port = (uint16_t)atoi(argv[3]);
        }
        sys_config_unlock();
        cli_shell_printf(shell, "MQTT broker updated to %s:%u\r\n", g_sys_config.mqtt.broker_url, g_sys_config.mqtt.port);
        return 0;
    }

    if (strcmp(argv[1], "pub") == 0) {
        if (argc < 4) {
            cli_shell_put_line(shell, "Usage: mqtt pub <topic> <payload>");
            return -1;
        }
        if (!app_mqtt_is_connected()) {
            cli_shell_put_line(shell, "MQTT is not connected yet!");
            return -1;
        }
        int res = mqtt_client_publish(argv[2], argv[3]);
        if (res == 0) {
            cli_shell_printf(shell, "Published to %s\r\n", argv[2]);
        } else {
            cli_shell_printf(shell, "Publish failed (%d)\r\n", res);
        }
        return res;
    }

    cli_shell_put_line(shell, "Usage: mqtt <status|pub|config>");
    return -1;
}

static const Cli_Shell_Cmd s_shell_commands[] = {
    {"help", cli_shell_help_handler, "List all commands"},
    {"ping", shell_ping_handler, "Ping an IP address"},
    {"reboot", shell_reboot_handler, "Reboot the gateway"},
    {"config", shell_config_handler, "Show JSON configuration"},
    {"ipconfig", shell_ipconfig_handler, "Show network IP configuration"},
    {"wifi", shell_wifi_handler, "WiFi control: on/off/connect/status"},
    {"eth", shell_eth_handler, "Ethernet status/PHY/DHCP control"},
    {"mqtt", shell_mqtt_handler, "MQTT client control: status/pub/config"},
    {"diag", shell_diag_handler, "Show shell and ST67 diagnostics"},
    {"spi", shell_spi_handler, "SPI diagnostics: loopback"},
    {"ls", shell_ls_handler, "List files in directory"},
    {"cat", shell_cat_handler, "Print file contents"},
    {"rm", shell_rm_handler, "Delete a file"},
    {"st67_boot", cmd_st67_boot, "Test ST67 hardware boot sequence"},
};

const Cli_Shell_Cmd *const g_shell_commands = s_shell_commands;
const size_t g_num_shell_commands = sizeof(s_shell_commands) / sizeof(Cli_Shell_Cmd);

void shell_commands_init(void) {
    // Dummy function to force linker to include this file
}
