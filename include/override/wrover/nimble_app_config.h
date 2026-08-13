#pragma once

/*
 * Application-specific NimBLE settings for the original ESP32 WROVER.
 *
 * This fragment is inserted after sdkconfig.h and immediately before
 * nimconfig_rename.h by scripts/configure_nimble.py. NimBLE-Arduino remains
 * responsible for all defaults and derived values after this point.
 */

/* Clear framework role values and compatibility aliases before normalization. */
#undef CONFIG_BT_NIMBLE_ROLE_CENTRAL
#undef CONFIG_BT_NIMBLE_ROLE_OBSERVER
#undef CONFIG_NIMBLE_ROLE_CENTRAL
#undef CONFIG_NIMBLE_ROLE_OBSERVER

/* Keep this target on NimBLE's legacy advertising path. */
#undef CONFIG_BT_NIMBLE_EXT_ADV
#undef CONFIG_BT_NIMBLE_MAX_EXT_ADV_INSTANCES
#undef CONFIG_BT_NIMBLE_ENABLE_PERIODIC_ADV

/* Let NimBLE derive the legacy event buffer size. */
#undef CONFIG_BT_NIMBLE_TRANSPORT_EVT_SIZE

/* Keep diagnostic text used by the application logs. */
#define CONFIG_NIMBLE_CPP_ENABLE_RETURN_CODE_TEXT

/* This firmware is a peripheral/broadcaster, not a central/observer. */
#define CONFIG_BT_NIMBLE_ROLE_CENTRAL 0
#define CONFIG_BT_NIMBLE_ROLE_OBSERVER 0

/* Use uppercase Bluetooth addresses in diagnostic output. */
#define CONFIG_NIMBLE_CPP_ADDR_FMT_UPPERCASE 1
