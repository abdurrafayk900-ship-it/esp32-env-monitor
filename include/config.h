#pragma once

// ---------- Device ----------
#define DEVICE_ID          "envmon-01"
#define DEVICE_NAME        "Environmental Monitor"
#define PUBLISH_INTERVAL_MS 30000UL   // how often readings are published
#define SAMPLE_COUNT        5         // readings averaged per publish

// ---------- Pins (ESP32 DevKit v1) ----------
#define PIN_I2C_SDA   21
#define PIN_I2C_SCL   22
#define PIN_MQ135     34   // ADC1 channel, safe to use with Wi-Fi on
#define PIN_LED_OK    25
#define PIN_LED_ALERT 26
#define PIN_BUZZER    27

// ---------- Sensor addresses ----------
#define BME280_ADDR   0x76
#define BH1750_ADDR   0x23

// ---------- Alert thresholds ----------
#define TEMP_HIGH_C       30.0f
#define TEMP_LOW_C        15.0f
#define HUMIDITY_HIGH     70.0f
#define AIR_QUALITY_POOR  2000   // raw MQ-135 ADC value (0-4095), calibrate for your sensor

// ---------- MQTT topics ----------
#define TOPIC_BASE        "home/" DEVICE_ID
#define TOPIC_STATE       TOPIC_BASE "/state"
#define TOPIC_STATUS      TOPIC_BASE "/status"   // online/offline (LWT)
#define TOPIC_COMMAND     TOPIC_BASE "/cmd"
#define HA_DISCOVERY_PREFIX "homeassistant"
