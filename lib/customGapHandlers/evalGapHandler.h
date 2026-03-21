//evalGapHandler.h
#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>

#include "rcStatus.h"

int evalGapHandler(ble_gap_event *event, void *arg);