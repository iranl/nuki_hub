#pragma once

#if !defined(CONFIG_IDF_TARGET_ESP32) && !defined(CONFIG_IDF_TARGET_ESP32P4)
typedef enum {
    ETH_CLOCK_GPIO0_IN = 0,
    ETH_CLOCK_GPIO0_OUT = 1,    
    ETH_CLOCK_GPIO16_OUT = 2,
    ETH_CLOCK_GPIO17_OUT = 3
} eth_clock_mode_t;

#define ETH_PHY_TYPE_LAN8720       ETH_PHY_MAX
#else
#define ETH_PHY_TYPE_LAN8720       ETH_PHY_LAN8720
#if defined(CONFIG_IDF_TARGET_ESP32P4)
#define ETH_CLOCK_GPIO0_IN         EMAC_CLK_EXT_IN
#define ETH_CLOCK_GPIO0_OUT        EMAC_CLK_OUT
#define ETH_CLOCK_GPIO16_OUT       EMAC_CLK_OUT
#define ETH_CLOCK_GPIO17_OUT       EMAC_CLK_OUT
#endif
#endif

#if defined(CONFIG_IDF_TARGET_ESP32P4)
#define ETH_CLK_MODE_LAN8720       EMAC_CLK_EXT_IN
#define ETH_PHY_ADDR_LAN8720       0
#define ETH_PHY_MDC_LAN8720        31
#define ETH_PHY_MDIO_LAN8720       52
#define ETH_PHY_POWER_LAN8720      51
#define ETH_RESET_PIN_LAN8720      1
#else
#define ETH_CLK_MODE_LAN8720       ETH_CLOCK_GPIO0_IN
#define ETH_PHY_ADDR_LAN8720       0
#define ETH_PHY_MDC_LAN8720        23
#define ETH_PHY_MDIO_LAN8720       18
#define ETH_PHY_POWER_LAN8720      -1
#define ETH_RESET_PIN_LAN8720      1
#endif
