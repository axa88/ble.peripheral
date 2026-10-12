#include <nimconfig.h>

#if defined(CONFIG_IDF_TARGET_ESP32C3) && defined(CONFIG_IDF_TARGET_ESP32)
#error "Multiple ESP32 targets are selected"
#elif defined(CONFIG_IDF_TARGET_ESP32C3)

#if !defined(CONFIG_BT_NIMBLE_ROLE_CENTRAL) || !defined(CONFIG_BT_NIMBLE_ROLE_OBSERVER)
#error "C3 central and observer role settings must be defined"
#endif
static_assert(CONFIG_BT_NIMBLE_ROLE_CENTRAL == 0, "C3 central role must be disabled");
static_assert(CONFIG_BT_NIMBLE_ROLE_OBSERVER == 0, "C3 observer role must be disabled");
static_assert(CONFIG_BT_NIMBLE_ROLE_PERIPHERAL == 1, "C3 peripheral role must be enabled");
static_assert(CONFIG_BT_NIMBLE_ROLE_BROADCASTER == 1, "C3 broadcaster role must be enabled");

#if defined(CONFIG_NIMBLE_ROLE_CENTRAL) || defined(CONFIG_NIMBLE_ROLE_OBSERVER)
#error "C3 legacy central and observer aliases must be undefined"
#endif

#if !defined(CONFIG_NIMBLE_CPP_ADDR_FMT_UPPERCASE) || CONFIG_NIMBLE_CPP_ADDR_FMT_UPPERCASE != 1
#error "C3 uppercase address formatting must be enabled"
#endif
#ifndef CONFIG_NIMBLE_CPP_ENABLE_RETURN_CODE_TEXT
#error "C3 return-code diagnostic text must be enabled"
#endif

#if !defined(CONFIG_BT_NIMBLE_EXT_ADV) || CONFIG_BT_NIMBLE_EXT_ADV != 1
#error "C3 extended advertising must be enabled"
#endif
#if !defined(CONFIG_BT_NIMBLE_MAX_EXT_ADV_INSTANCES) || CONFIG_BT_NIMBLE_MAX_EXT_ADV_INSTANCES != 4
#error "C3 must support four extended advertising instances"
#endif
#if CONFIG_BT_NIMBLE_TRANSPORT_EVT_SIZE != 257
#error "C3 transport event size must be 257"
#endif
#ifndef CONFIG_NIMBLE_CPP_ENABLE_GAP_EVENT_CODE_TEXT
#error "C3 GAP-event diagnostic text must be enabled"
#endif
#if CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU != 517
#error "C3 preferred ATT MTU must be 517"
#endif
#ifdef CONFIG_BT_NIMBLE_ENABLE_PERIODIC_ADV
#error "C3 periodic advertising must remain undefined as in the baseline"
#endif

#elif defined(CONFIG_IDF_TARGET_ESP32)

#if !defined(CONFIG_BT_NIMBLE_ROLE_CENTRAL) || !defined(CONFIG_BT_NIMBLE_ROLE_OBSERVER)
#error "WROVER central and observer role settings must be defined"
#endif
static_assert(CONFIG_BT_NIMBLE_ROLE_CENTRAL == 0, "WROVER central role must be disabled");
static_assert(CONFIG_BT_NIMBLE_ROLE_OBSERVER == 0, "WROVER observer role must be disabled");
static_assert(CONFIG_BT_NIMBLE_ROLE_PERIPHERAL == 1, "WROVER peripheral role must be enabled");
static_assert(CONFIG_BT_NIMBLE_ROLE_BROADCASTER == 1, "WROVER broadcaster role must be enabled");

#if defined(CONFIG_NIMBLE_ROLE_CENTRAL) || defined(CONFIG_NIMBLE_ROLE_OBSERVER)
#error "WROVER legacy central and observer aliases must be undefined"
#endif

#if !defined(CONFIG_NIMBLE_CPP_ADDR_FMT_UPPERCASE) || CONFIG_NIMBLE_CPP_ADDR_FMT_UPPERCASE != 1
#error "WROVER uppercase address formatting must be enabled"
#endif
#ifndef CONFIG_NIMBLE_CPP_ENABLE_RETURN_CODE_TEXT
#error "WROVER return-code diagnostic text must be enabled"
#endif

#if defined(CONFIG_BT_NIMBLE_EXT_ADV)
#error "WROVER extended advertising must be disabled and undefined"
#endif
#if defined(CONFIG_BT_NIMBLE_MAX_EXT_ADV_INSTANCES)
#error "WROVER extended advertising instances must be undefined"
#endif
#if CONFIG_BT_NIMBLE_TRANSPORT_EVT_SIZE != 70
#error "WROVER transport event size must be 70"
#endif
#ifdef CONFIG_NIMBLE_CPP_ENABLE_GAP_EVENT_CODE_TEXT
#error "WROVER GAP-event diagnostic text must remain undefined"
#endif
#if CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU != 517
#error "WROVER preferred ATT MTU must be 517"
#endif
#ifdef CONFIG_BT_NIMBLE_ENABLE_PERIODIC_ADV
#error "WROVER periodic advertising must be disabled and undefined"
#endif

#else
#error "NimBLE configuration checks support only ESP32-C3 and original ESP32 WROVER"
#endif

#if !defined(CONFIG_BT_NIMBLE_NVS_PERSIST) || CONFIG_BT_NIMBLE_NVS_PERSIST != 1
#error "NimBLE NVS persistence must be enabled"
#endif
#if !defined(CONFIG_BT_NIMBLE_SM_LEGACY) || CONFIG_BT_NIMBLE_SM_LEGACY != 1
#error "NimBLE legacy pairing must be enabled"
#endif
#if !defined(CONFIG_BT_NIMBLE_SM_SC) || CONFIG_BT_NIMBLE_SM_SC != 1
#error "NimBLE secure connections must be enabled"
#endif
