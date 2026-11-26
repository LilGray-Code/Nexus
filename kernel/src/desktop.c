                1U, ac97_is_ready() ? 108U : 231U,
                ac97_is_ready() ? 216U : 183U,
                ac97_is_ready() ? 229U : 127U);
            struct nexus_net_info info;
            const bool have_network_info =
                net_get_info(&info) && info.interface_ready;
            const char *network_status = "NO NETWORK ADAPTER";
            if (have_network_info) {
                const char *medium =
                    info.interface_medium == NEXUS_NETDEV_MEDIUM_WIFI
                        ? "WI-FI" : "WIRED";
                const char *state = !info.link_up ? "LINK DOWN"
                    : (info.dhcp_state == NEXUS_DHCP_STATE_RENEWING
                        ? "RENEWING DHCP"
                        : (info.dhcp_state == NEXUS_DHCP_STATE_REBINDING
                            ? "REBINDING DHCP"
                            : (info.ipv4_configured ? "CONNECTED"
                                                    : "WAITING FOR DHCP")));
                network_status = state;
                graphics_draw_text(text_x + 300U, content_y + 184U,
                    medium, 1U, 171U, 191U, 211U);
            }
            graphics_draw_text(text_x, content_y + 184U, "NETWORK",
                               1U, 142U, 174U, 199U);
            graphics_draw_text(text_x + 84U, content_y + 184U,
                network_status, 1U, 108U, 216U, 229U);
            const char *wifi_status = "WI-FI NOT DETECTED";
            if (wifi_available()) {
                switch (wifi_get_state()) {
                    case NEXUS_WIFI_READY: wifi_status = "RADIO READY"; break;
                    case NEXUS_WIFI_SCANNING: wifi_status = "SCANNING"; break;
                    case NEXUS_WIFI_CONNECTING: wifi_status = "CONNECTING"; break;
                    case NEXUS_WIFI_CONNECTED: wifi_status = "CONNECTED"; break;
                    case NEXUS_WIFI_FAILED: wifi_status = "RADIO ERROR"; break;
                    default: wifi_status = "RADIO UNAVAILABLE"; break;
                }
            } else if (wifi_pci_candidate_count() != 0U) {
                wifi_status = "DEVICE FOUND - DRIVER NEEDED";
            }
            graphics_draw_text(text_x, content_y + 211U, "WI-FI",
                               1U, 142U, 174U, 199U);
            graphics_draw_text(text_x + 84U, content_y + 211U,
                wifi_status, 1U, 108U, 216U, 229U);
            char mem_line[64];
            if (have_network_info && info.dhcp_lease_valid) {
                if (info.dhcp_lease_seconds == UINT32_MAX) {
                    graphics_draw_text(text_x, content_y + 237U,
                        "DHCP LEASE: INFINITE", 1U, 171U, 191U, 211U);
                } else {
                    format_labeled_u64("DHCP LEASE LEFT: ",
                        info.dhcp_lease_remaining_seconds, mem_line);
                    graphics_draw_text(text_x, content_y + 237U,
                        mem_line, 1U, 171U, 191U, 211U);
                }
            } else {
                graphics_draw_text(text_x, content_y + 237U,
                    "DHCP LEASE: NOT ACQUIRED", 1U, 231U, 177U, 119U);
            }
            format_labeled_u64("FREE MEMORY: ", memory_free_bytes(), mem_line);
            graphics_draw_text(text_x, content_y + 264U,
                mem_line, 1U, 231U, 239U, 247U);
            format_labeled_u64("UPTIME: ", timer_ticks() / 100U, mem_line);
            graphics_draw_text(text_x, content_y + 288U,
                mem_line, 1U, 171U, 191U, 211U);
            graphics_draw_text(text_x, content_y + 322U,
                desktop_preferences_persistent
                    ? "PREFERENCES ARE STORED ON NEXUS DATA"
                    : "PREFERENCES ARE NOT YET STORED PERSISTENTLY",
                1U, desktop_preferences_persistent ? 108U : 231U,