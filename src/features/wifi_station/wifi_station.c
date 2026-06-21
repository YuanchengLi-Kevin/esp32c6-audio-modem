/*
 * Copyright (c) 2026 Yuancheng Li
 * SPDX-License-Identifier: Apache-2.0
 */

#include "wifi_station.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>

LOG_MODULE_REGISTER(wifi_station, LOG_LEVEL_INF);

#define WIFI_EVENTS (NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT)

static struct net_mgmt_event_callback wifi_callback;
static struct net_mgmt_event_callback ipv4_callback;
static struct net_if *station_iface;

static void wifi_event_handler(struct net_mgmt_event_callback *callback,
			       uint64_t event, struct net_if *iface)
{
	const struct wifi_status *status = callback->info;

	if (iface != station_iface) {
		return;
	}

	if (event == NET_EVENT_WIFI_CONNECT_RESULT) {
		if ((status != NULL) && (status->status != 0)) {
			LOG_ERR("Wi-Fi connection failed: %d", status->status);
		} else {
			LOG_INF("Wi-Fi associated; waiting for IPv4 address");
		}
	} else if (event == NET_EVENT_WIFI_DISCONNECT_RESULT) {
		LOG_WRN("Wi-Fi disconnected; driver reconnect is enabled");
	}
}

static void ipv4_event_handler(struct net_mgmt_event_callback *callback,
			       uint64_t event, struct net_if *iface)
{
	ARG_UNUSED(callback);
	ARG_UNUSED(event);

	if (iface == station_iface) {
		LOG_INF("Wi-Fi station has an IPv4 address");
	}
}

int wifi_station_init(void)
{
	station_iface = net_if_get_wifi_sta();
	if (station_iface == NULL) {
		return -ENODEV;
	}

	net_mgmt_init_event_callback(&wifi_callback, wifi_event_handler, WIFI_EVENTS);
	net_mgmt_add_event_callback(&wifi_callback);
	net_mgmt_init_event_callback(&ipv4_callback, ipv4_event_handler,
				     NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&ipv4_callback);

	return 0;
}

int wifi_station_connect(void)
{
	static struct wifi_connect_req_params params;
	size_t ssid_length = strlen(CONFIG_AUDIO_MODEM_WIFI_SSID);
	size_t psk_length = strlen(CONFIG_AUDIO_MODEM_WIFI_PSK);

	if ((station_iface == NULL) || (ssid_length == 0U) || (ssid_length > 32U) ||
	    ((psk_length != 0U) && ((psk_length < 8U) || (psk_length > 64U)))) {
		return -EINVAL;
	}

	params.ssid = (const uint8_t *)CONFIG_AUDIO_MODEM_WIFI_SSID;
	params.ssid_length = (uint8_t)ssid_length;
	params.psk = (const uint8_t *)CONFIG_AUDIO_MODEM_WIFI_PSK;
	params.psk_length = (uint8_t)psk_length;
	params.security = (psk_length == 0U) ? WIFI_SECURITY_TYPE_NONE : WIFI_SECURITY_TYPE_PSK;
	params.channel = WIFI_CHANNEL_ANY;
	params.band = WIFI_FREQ_BAND_2_4_GHZ;
	params.mfp = WIFI_MFP_OPTIONAL;
	params.timeout = SYS_FOREVER_MS;

	LOG_INF("Connecting to Wi-Fi SSID %s", CONFIG_AUDIO_MODEM_WIFI_SSID);
	return net_mgmt(NET_REQUEST_WIFI_CONNECT, station_iface, &params, sizeof(params));
}
