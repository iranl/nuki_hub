/**
 * @file BleScanner.cpp
 *
 * Created: 2022
 * License: GNU GENERAL PUBLIC LICENSE (see LICENSE)
 *
 * This library provides a BLE scanner to be used by other libraries to
 * receive advertisements from BLE devices
 *
 */

#include "BleScanner.h"
#include <NimBLEUtils.h>
#include <NimBLEScan.h>
#include <NimBLEAdvertisedDevice.h>

namespace BleScanner {

Scanner::Scanner(int reservedSubscribers) {
  subscribers.reserve(reservedSubscribers);
}

Scanner::~Scanner() {
  Serial.println("Destroying scanner");
  bleScan->stop();
  Serial.println("bleScan stopped");
  bleScan->clearResults();
  Serial.println("bleScan results cleared");
  bleScan = nullptr;
  Serial.println("bleScan nulled");
}

void Scanner::initialize(const std::string& deviceName, const bool wantDuplicates, const uint16_t interval, const uint16_t window) {
  if (!NimBLEDevice::isInitialized()) {
    if (wantDuplicates) {
      #ifdef CONFIG_BTDM_BLE_SCAN_DUPL
      NimBLEDevice::setScanDuplicateCacheSize(10);
      #endif
    }
    NimBLEDevice::init(deviceName);
  }

  bleScan = NimBLEDevice::getScan();

  #ifndef BLESCANNER_USE_LATEST_NIMBLE
  bleScan->setAdvertisedDeviceCallbacks(this, wantDuplicates);
  #else
  bleScan->setScanCallbacks(this, wantDuplicates);
  #endif
  bleScan->setInterval(interval);
  bleScan->setWindow(window);
  bleScan->setActiveScan(false);
}

void Scanner::update() {
  if (!scanningEnabled || bleScan->isScanning()) {
    return;
  }

  if (scanDuration == 0) {
    bleScan->setMaxResults(0);
  } else {
    log_w("Ble scanner max results not 0. Be aware of memory issue due to unbridled growth of results vector");
  }

  #ifndef BLESCANNER_USE_LATEST_NIMBLE
  bool result = bleScan->start(scanDuration, nullptr, false);
  #else
  bool result = bleScan->start(scanDuration * 1000, false);
  #endif
}

void Scanner::enableScanning(bool enable) {
  scanningEnabled = enable;
  if (!enable) {
    bleScan->stop();
  }
}

void Scanner::setScanDuration(const uint32_t value) {
  scanDuration = value;
}

void Scanner::subscribe(Subscriber* subscriber) {
  if (std::find(subscribers.begin(), subscribers.end(), subscriber) != subscribers.end()) {
    return;
  }
  subscribers.push_back(subscriber);
}

void Scanner::unsubscribe(Subscriber* subscriber) {
  auto it = std::find(subscribers.begin(), subscribers.end(), subscriber);
  if (it != subscribers.end()) {
    subscribers.erase(it);
  }
}

void Scanner::onResult(const NimBLEAdvertisedDevice* advertisedDevice) {
  for (const auto& subscriber : subscribers) {
    subscriber->onResult(advertisedDevice);
  }
}

void Scanner::whitelist(BLEAddress bleAddress) {
  NimBLEDevice::whiteListAdd(bleAddress);   
  bleScan->setFilterPolicy(BLE_HCI_SCAN_FILT_USE_WL);
}

} // namespace BleScanner