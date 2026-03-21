// evalGapHandler.cpp
#include "evalGapHandler.h"

static void printBleAddr(const ble_addr_t &addr)
{
	const char *typeStr = "unknown";
	switch (addr.type)
	{
		case BLE_ADDR_PUBLIC: typeStr = "public"; break;
		case BLE_ADDR_RANDOM: typeStr = "random"; break;
		case BLE_ADDR_PUBLIC_ID: typeStr = "public_id"; break;
		case BLE_ADDR_RANDOM_ID: typeStr = "random_id"; break;
		default: typeStr = "unknown"; break;
	}

	Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X (type %u %s)\n", addr.val[5], addr.val[4], addr.val[3], addr.val[2], addr.val[1], addr.val[0], (unsigned)addr.type,
				  typeStr);
}

// Helper: print security state
static void printSecurityState(const ble_gap_sec_state &s)
{
	Serial.printf("  **encrypted**: %u  **authenticated**: %u  **bonded**: %u  **key_size**: %u  **authorize**: %u\n", (unsigned)s.encrypted,
					(unsigned)s.authenticated, (unsigned)s.bonded, (unsigned)s.key_size, (unsigned)s.authorize);
}

// Helper: print connection description
static void printConnDescDebug(const ble_gap_conn_desc &d)
{
	Serial.println("=== ble_gap_conn_desc ===");

	// Role string
	const char *role_str = "UNKNOWN";
	if (d.role == BLE_GAP_ROLE_MASTER)
		role_str = "MASTER";
	else if (d.role == BLE_GAP_ROLE_SLAVE)
		role_str = "SLAVE";

	Serial.printf("**conn_handle**: %u  **role**: %s  **master_clock_accuracy**: %u\n", (unsigned)d.conn_handle, role_str, (unsigned)d.master_clock_accuracy);
	Serial.printf("**conn_itvl**: %u  **conn_latency**: %u  **supervision_timeout**: %u\n", (unsigned)d.conn_itvl, (unsigned)d.conn_latency, (unsigned)d.supervision_timeout);
	Serial.print("**sec_state**:   ");
	printSecurityState(d.sec_state);

	Serial.print("**our_id_addr**:");
	printBleAddr(d.our_id_addr);
	Serial.println();
	Serial.print("**peer_id_addr**:");
	printBleAddr(d.peer_id_addr);
	Serial.println();
	Serial.print("**our_ota_addr**:");
	printBleAddr(d.our_ota_addr);
	Serial.println();
	Serial.print("**peer_ota_addr**:");
	printBleAddr(d.peer_ota_addr);
	Serial.println();

	Serial.println("=========================");
	Serial.println();
}

// Map advertisng event type to string (common values)
static const char *advertPduTypeToString(uint8_t eventType)
{
	switch (eventType)
	{
		case 0: return "ADV_IND";
		case 1: return "DIRECT_IND";
		case 2: return "SCAN_IND";
		case 3: return "NONCONN_IND";
		case 4: return "SCAN_RSP";
		default: return "UNKNOWN";
	}
}

// Helper: print advertisment description
static void printDiscoveryDescription(const ble_gap_disc_desc &d)
{
	Serial.println("=== ble_gap_disc_desc ===");
	Serial.printf("event_type: %u  (%s)\n", (unsigned)d.event_type, advertPduTypeToString(d.event_type));
	Serial.printf("length_data: %u  rssi: %d\n", (unsigned)d.length_data, (int)d.rssi);
	Serial.print("addr:         ");
	printBleAddr(d.addr);
	Serial.println();
	Serial.print("direct_addr:  ");
	printBleAddr(d.direct_addr);
	Serial.println();
	Serial.print("data:         ");
	NimBLEUtils::dataToHexString(d.data, d.length_data);

	Serial.println("=========================");
	Serial.println();
}

static void printBleGapUpdParams(const ble_gap_upd_params &p)
{
	Serial.printf("itvl_min: %.2f ms  itvl_max: %.2f ms  |  latency: %u  |  sup: %.0f ms  |  ce_min: %.2f ms  ce_max: %.2f ms\n",
				  static_cast<float>(p.itvl_min) * 1.25f, static_cast<float>(p.itvl_max) * 1.25f, static_cast<unsigned>(p.latency),
				  static_cast<float>(p.supervision_timeout) * 10.0f, static_cast<float>(p.min_ce_len) * 0.625f, static_cast<float>(p.max_ce_len) * 0.625f);
}

#if CONFIG_BT_NIMBLE_EXT_ADV
// Map data_status to string if constants are available, else numeric
static const char *dataStatusToString(uint8_t status)
{
#ifdef BLE_GAP_EXT_ADV_DATA_STATUS_COMPLETE
	if (status == BLE_GAP_EXT_ADV_DATA_STATUS_COMPLETE)
		return "COMPLETE";
#endif
#ifdef BLE_GAP_EXT_ADV_DATA_STATUS_INCOMPLETE
	if (status == BLE_GAP_EXT_ADV_DATA_STATUS_INCOMPLETE)
		return "INCOMPLETE";
#endif
#ifdef BLE_GAP_EXT_ADV_DATA_STATUS_TRUNCATED
	if (status == BLE_GAP_EXT_ADV_DATA_STATUS_TRUNCATED)
		return "TRUNCATED";
#endif
	return "UNKNOWN";
}

// Try to decode props bitmask into human readable list if macros exist, otherwise just print numeric value.
static void printProps(uint8_t props)
{
	Serial.printf("props: 0x%02X", (unsigned)props);

	bool printed = false;
#ifdef BLE_HCI_ADV_CONN_MASK
	if (props & BLE_HCI_ADV_CONN_MASK)
	{
		Serial.print(" [CONN]");
		printed = true;
	}
#endif
#ifdef BLE_HCI_ADV_SCAN_MASK
	if (props & BLE_HCI_ADV_SCAN_MASK)
	{
		Serial.print(" [SCAN]");
		printed = true;
	}
#endif
#ifdef BLE_HCI_ADV_DIRECT_MASK
	if (props & BLE_HCI_ADV_DIRECT_MASK)
	{
		Serial.print(" [DIRECT]");
		printed = true;
	}
#endif
#ifdef BLE_HCI_ADV_SCAN_RSP_MASK
	if (props & BLE_HCI_ADV_SCAN_RSP_MASK)
	{
		Serial.print(" [SCAN_RSP]");
		printed = true;
	}
#endif
#ifdef BLE_HCI_ADV_LEGACY_MASK
	if (props & BLE_HCI_ADV_LEGACY_MASK)
	{
		Serial.print(" [LEGACY]");
		printed = true;
	}
#endif

	if (!printed)
	{
		Serial.print(" [undecoded]");
	}

	Serial.println();
}

// Map legacy event type to string (common values)
static const char *legacyEventTypeToString(uint8_t ev)
{
	switch (ev)
	{
		case 0: return "ADV_IND";
		case 1: return "DIRECT_IND";
		case 2: return "SCAN_IND";
		case 3: return "NONCONN_IND";
		case 4: return "SCAN_RSP";
		default: return "UNKNOWN";
	}
}

// Map PHY value to string (common values)
static const char *phyToString(uint8_t phy)
{
	switch (phy)
	{
		case 0: return "PHY_1M";
		case 1: return "PHY_2M";
		case 2: return "PHY_CODED";
		default: return "UNKNOWN";
	}
}

// Main debug printer for extended discovery descriptor
static void printExtDiscDescDebug(const ble_gap_ext_disc_desc &d)
{
	Serial.println("=== ble_gap_ext_disc_desc ===");
	printProps(d.props);
	Serial.printf("data_status: %u (%s)\n", (unsigned)d.data_status, dataStatusToString(d.data_status));
	Serial.printf("legacy_event_type: %u (%s)\n", (unsigned)d.legacy_event_type, legacyEventTypeToString(d.legacy_event_type));
	Serial.printf("rssi: %d  tx_power: %d  sid: %u\n", (int)d.rssi, (int)d.tx_power, (unsigned)d.sid);
	Serial.printf("prim_phy: %u (%s)  sec_phy: %u (%s)\n", (unsigned)d.prim_phy, phyToString(d.prim_phy), (unsigned)d.sec_phy, phyToString(d.sec_phy));
	Serial.printf("periodic_adv_itvl: %u\n", (unsigned)d.periodic_adv_itvl);
	Serial.print("addr:         ");
	printBleAddr(d.addr);
	Serial.println();
	Serial.print("direct_addr:  ");
	printBleAddr(d.direct_addr);
	Serial.println();
	Serial.printf("length_data: %u\n", (unsigned)d.length_data);
	Serial.print("data:         ");
	NimBLEUtils::dataToHexString(d.data, d.length_data);

	Serial.println("=============================");
}
#endif

int evalGapHandler(ble_gap_event *event, void *arg)
{
	// Print event id first
	Serial.printf("\nGAP EVENT: id=%d %s\n", event->type, NimBLEUtils::gapEventToString(event->type));

	switch (event->type)
	{
		case BLE_GAP_EVENT_CONNECT: Serial.printf("status=%s conn=%d\n", NimBLEUtils::returnCodeToString(event->connect.status), event->connect.conn_handle); break;
		case BLE_GAP_EVENT_DISCONNECT:
		{
			Serial.printf("reason=%s\n", NimBLEUtils::returnCodeToString(event->disconnect.reason));
			printConnDescDebug(event->disconnect.conn);
			break;
		}
		case BLE_GAP_EVENT_CONN_UPDATE: Serial.printf("status=%s conn=%d\n", NimBLEUtils::returnCodeToString(event->conn_update.status), event->connect.conn_handle); break;

		case BLE_GAP_EVENT_CONN_UPDATE_REQ: // not propagated to custom handler
		case BLE_GAP_EVENT_L2CAP_UPDATE_REQ:
		{
			Serial.printf("conn: %u\n", static_cast<unsigned>(event->conn_update_req.conn_handle));
			if (event->conn_update_req.peer_params)
			{
				Serial.print("  peer:  ");
				printBleGapUpdParams(*event->conn_update_req.peer_params);
			}

			if (event->conn_update_req.self_params)
			{
				Serial.print("  self:  ");
				printBleGapUpdParams(*event->conn_update_req.self_params);
			}

			break;
		}

		case BLE_GAP_EVENT_TERM_FAILURE:  Serial.printf("status=%s conn=%d\n", NimBLEUtils::returnCodeToString(event->term_failure.status), event->term_failure.conn_handle); break;

		case BLE_GAP_EVENT_DISC:
		{
			printDiscoveryDescription(event->disc);
			break;
		}

		case BLE_GAP_EVENT_DISC_COMPLETE: Serial.printf("%s\n", NimBLEUtils::returnCodeToString(event->disc_complete.reason)); break;

		case BLE_GAP_EVENT_ADV_COMPLETE: Serial.printf("%s\n", NimBLEUtils::returnCodeToString(event->adv_complete.reason)); break;

		case BLE_GAP_EVENT_PASSKEY_ACTION: // not propagated to custom handler
		{
			int action = event->passkey.params.action;
			const char *act_str;
			switch (action)
			{
				case BLE_SM_IOACT_NONE: act_str = "NONE"; break;
				case BLE_SM_IOACT_OOB: act_str = "OOB"; break;
				case BLE_SM_IOACT_INPUT: act_str = "INPUT"; break;
				case BLE_SM_IOACT_DISP: act_str = "DISPLAY"; break;
				case BLE_SM_IOACT_NUMCMP: act_str = "NUMERIC_COMPARISON"; break;
				case BLE_SM_IOACT_OOB_SC: act_str = "OOB_SECURE_CONNECTION"; break;
				case BLE_SM_IOACT_MAX_PLUS_ONE: act_str = "MAX_PLUS_ONE"; break;
				default: act_str = "UNKNOWN"; break;
			}

			if (action == BLE_SM_IOACT_NUMCMP)
				Serial.printf(" numcmp=%u", event->passkey.params.numcmp);

			Serial.printf(" action=%s (%d) conn_handle=%d\n", act_str, action, event->passkey.conn_handle);
			break;
		}

		case BLE_GAP_EVENT_NOTIFY_TX:
		{
			Serial.printf("%s\n", NimBLEUtils::returnCodeToString(event->notify_tx.status));

			// notify_tx: status, conn_handle, attr_handle, indication
			int status = event->notify_tx.status;
			uint16_t conn = event->notify_tx.conn_handle;
			uint16_t attr = event->notify_tx.attr_handle;
			const char *ind_str = event->notify_tx.indication ? "Indication" : "Notification";

			Serial.printf(" conn_handle=%d attr_handle=0x%04X (%u) type=%s\n", conn, attr, (unsigned)attr, ind_str);
			break;
		}

		case BLE_GAP_EVENT_NOTIFY_RX:
		{
			struct os_mbuf *om = event->notify_rx.om;
			uint16_t attr = event->notify_rx.attr_handle;
			uint16_t conn = event->notify_rx.conn_handle;
			const char *ind_str = event->notify_rx.indication ? "Indication" : "Notification";

			Serial.printf("  NOTIFY_RX: conn_handle=%d attr_handle=0x%04X (%u) type=%s\n", conn, attr, (unsigned)attr, ind_str);

			// Safe mbuf length check
			size_t len = 0;
			if (om != nullptr)
				len = om->om_len;

			Serial.printf("    payload_len=%zu\n", len);

			// Inspect payload safely (no ownership change)
			if (om != nullptr && len > 0)
			{
				const unsigned MAX_PRINT = 64;
				unsigned to_print = (len > MAX_PRINT) ? MAX_PRINT : (unsigned)len;
				uint8_t buf[MAX_PRINT];
				int rc = os_mbuf_copydata(om, 0, to_print, buf);
				if (rc == 0)
				{
					Serial.print("    payload=");
					for (unsigned i = 0; i < to_print; ++i)
					{
						Serial.printf("%02X", buf[i]);
						if (i + 1 < to_print)
							Serial.print(" ");
					}
					if (len > to_print)
						Serial.print(" ...");
					Serial.println();
				}
				else
				{
					Serial.printf("    (failed to copy payload rc=%d)\n", rc);
				}
			}

			// If you want to retain the mbuf beyond this handler:
			// event->notify_rx.om = NULL; // take ownership, then os_mbuf_free(om) later
			break;
		}

		case BLE_GAP_EVENT_SUBSCRIBE:
		{
			const char *reason_str;
			switch (event->subscribe.reason)
			{
				case BLE_GAP_SUBSCRIBE_REASON_WRITE: reason_str = "CCCD Write"; break;
				case BLE_GAP_SUBSCRIBE_REASON_TERM: reason_str = "Connection Terminated"; break;
				case BLE_GAP_SUBSCRIBE_REASON_RESTORE: reason_str = "Restored from Persistence"; break;
				default: reason_str = "Unknown Reason"; break;
			}

			/* Convert bit flags to readable text */
			const char *prev_notify_str = event->subscribe.prev_notify ? "subscribed" : "not subscribed";
			const char *cur_notify_str = event->subscribe.cur_notify ? "subscribed" : "not subscribed";
			const char *prev_indicate_str = event->subscribe.prev_indicate ? "subscribed" : "not subscribed";
			const char *cur_indicate_str = event->subscribe.cur_indicate ? "subscribed" : "not subscribed";

			/* Single, tidy log line plus optional detail line */
			Serial.printf("  conn=%u attr=0x%04X reason=%s (%u)\n", (unsigned)event->subscribe.conn_handle, (unsigned)event->subscribe.attr_handle, reason_str,
						  (unsigned)event->subscribe.reason);
			Serial.printf("    notify: prev=%s cur=%s; indicate: prev=%s cur=%s\n", prev_notify_str, cur_notify_str, prev_indicate_str, cur_indicate_str);

			break;
		}

		case BLE_GAP_EVENT_MTU:
		{
			Serial.print("  MTU: ");
			Serial.printf("conn_handle=%d value=%d\n", event->mtu.conn_handle, event->mtu.value);
			break;
		}

		case BLE_GAP_EVENT_IDENTITY_RESOLVED:
		{
			Serial.print("  IDENTITY_RESOLVED: ");
			Serial.printf("conn_handle=%d\n", event->identity_resolved.conn_handle);
			break;
		}

		case BLE_GAP_EVENT_REPEAT_PAIRING:
		{
			Serial.print("  REPEAT_PAIRING\n");
			break;
		}

		case BLE_GAP_EVENT_PHY_UPDATE_COMPLETE:
		{
			Serial.print("  PHY_UPDATE_COMPLETE\n");
			break;
		}

		case BLE_GAP_EVENT_EXT_DISC:
#if MYNEWT_VAL(CONFIG_BT_NIMBLE_EXT_ADV)
			printExtDiscDescDebug(event->ext_disc);
#endif
			break;

		case BLE_GAP_EVENT_PERIODIC_SYNC:
		{
			Serial.print("  PERIODIC_SYNC\n");
			break;
		}

		case BLE_GAP_EVENT_PERIODIC_REPORT:
		{
			Serial.print("  PERIODIC_REPORT\n");
			break;
		}

		case BLE_GAP_EVENT_PERIODIC_SYNC_LOST:
		{
			Serial.print("  PERIODIC_SYNC_LOST\n");
			break;
		}

		case BLE_GAP_EVENT_SCAN_REQ_RCVD:
		{
			Serial.print("  SCAN_REQ_RCVD\n");
			break;
		}

		case BLE_GAP_EVENT_PERIODIC_TRANSFER:
		{
			Serial.print("  PERIODIC_TRANSFER\n");
			break;
		}

		case BLE_GAP_EVENT_PATHLOSS_THRESHOLD:
		{
			Serial.print("  PATHLOSS_THRESHOLD\n");
			break;
		}

		case BLE_GAP_EVENT_TRANSMIT_POWER:
		{
			Serial.print("  TRANSMIT_POWER\n");
			break;
		}

		case BLE_GAP_EVENT_PARING_COMPLETE: // not propagated to custom handler
		{
			int status = event->pairing_complete.status;
			uint16_t conn = event->pairing_complete.conn_handle;
			const char *desc = rcDescription(status); // or nimble_status_description
			int base = status & ~0xFF;
			int code = status & 0xFF;

			if (base == 0x0)
				Serial.printf("PAIRING_COMPLETE: HOST code=0x%02X (%s) conn=%d\n", code, desc, conn);
			else
				Serial.printf("PAIRING_COMPLETE: base=0x%X code=0x%02X (%s) conn=%d\n", base, code, desc, conn);
			break;
		}

		case BLE_GAP_EVENT_SUBRATE_CHANGE:
		{
			Serial.print("  SUBRATE_CHANGE\n");
			break;
		}

		case BLE_GAP_EVENT_VS_HCI:
		{
			Serial.print("  VS_HCI (vendor specific HCI)\n");
			break;
		}

		case BLE_GAP_EVENT_BIGINFO_REPORT:
		{
			Serial.print("  BIGINFO_REPORT\n");
			break;
		}

		case BLE_GAP_EVENT_REATTEMPT_COUNT:
		{
			Serial.print("  REATTEMPT_COUNT\n");
			break;
		}

		case BLE_GAP_EVENT_AUTHORIZE:
		{
			Serial.print("  AUTHORIZE: ");
			Serial.printf("conn_handle=%d attr_handle=%d is_read=%d out_response=%d\n", event->authorize.conn_handle, event->authorize.attr_handle,
						  event->authorize.is_read, event->authorize.out_response);
			break;
		}

		case BLE_GAP_EVENT_TEST_UPDATE:
		{
			Serial.print("  TEST_UPDATE\n");
			break;
		}

		case BLE_GAP_EVENT_DATA_LEN_CHG:
		{
			Serial.print("  DATA_LEN_CHG\n");
			break;
		}

		case BLE_GAP_EVENT_CONNLESS_IQ_REPORT:
		{
			Serial.print("  CONNLESS_IQ_REPORT\n");
			break;
		}

		case BLE_GAP_EVENT_CONN_IQ_REPORT:
		{
			Serial.print("  CONN_IQ_REPORT\n");
			break;
		}

		case BLE_GAP_EVENT_CTE_REQ_FAILED:
		{
			Serial.print("  CTE_REQ_FAILED\n");
			break;
		}

		case BLE_GAP_EVENT_LINK_ESTAB:
			Serial.printf("conn=%d status=%s\n", event->connect.conn_handle, NimBLEUtils::returnCodeToString(event->connect.status));
			break;

		case BLE_GAP_EVENT_EATT:
		{
			Serial.print("  EATT\n");
			break;
		}

		case BLE_GAP_EVENT_PER_SUBEV_DATA_REQ:
		{
			Serial.print("  PER_SUBEV_DATA_REQ\n");
			break;
		}

		case BLE_GAP_EVENT_PER_SUBEV_RESP:
		{
			Serial.print("  PER_SUBEV_RESP\n");
			break;
		}

		case BLE_GAP_EVENT_PERIODIC_TRANSFER_V2:
		{
			Serial.print("  PERIODIC_TRANSFER_V2\n");
			break;
		}

		default:
		{
			Serial.printf("  UNKNOWN/UNHANDLED EVENT: id=%d\n", event->type);
			break;
		}
	}

	// Return 0 (unused by most NimBLE code)
	return 0;
}
