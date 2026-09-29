# Wiring

| Component | Pin | ESP32 pin | Notes |
|---|---|---|---|
| BME280 | VCC | 3V3 | 3.3 V module only |
| BME280 | GND | GND | |
| BME280 | SDA | GPIO 21 | shared I²C bus |
| BME280 | SCL | GPIO 22 | shared I²C bus |
| BH1750 | VCC | 3V3 | |
| BH1750 | GND | GND | |
| BH1750 | SDA | GPIO 21 | |
| BH1750 | SCL | GPIO 22 | |
| BH1750 | ADDR | GND | selects address 0x23 |
| MQ-135 | VCC | 5V (VIN) | heater needs 5 V |
| MQ-135 | GND | GND | |
| MQ-135 | AOUT | GPIO 34 via divider | 10 kΩ / 20 kΩ divider scales 5 V → 3.3 V |
| Green LED | anode | GPIO 25 via 220 Ω | system OK |
| Red LED | anode | GPIO 26 via 220 Ω | threshold alert |
| Buzzer | + | GPIO 27 | active or passive piezo |

```
         ESP32 DevKit
        ┌───────────┐
 3V3 ───┤3V3     VIN├─── 5V → MQ-135 VCC
 GND ───┤GND     GND├─── common ground
        │        D21├─── SDA ── BME280, BH1750
        │        D22├─── SCL ── BME280, BH1750
        │        D34├─── MQ-135 AOUT (through divider)
        │        D25├─── 220Ω ── Green LED
        │        D26├─── 220Ω ── Red LED
        │        D27├─── Buzzer
        └───────────┘
```

## Notes

- **Why GPIO 34?** ADC2 pins stop working while Wi-Fi is on, so the analog sensor must use an ADC1 pin (32–39).
- **MQ-135 burn-in**: let the sensor warm up for 24–48 h before first use and ~2 min after each power-on. Raw values are relative. Calibrate `AIR_QUALITY_POOR` against clean outdoor air.
- The same sensor footprint is used on my [KiCad PCB Sensor Board](https://github.com/abdurrafayk900-ship-it/kicad-sensor-board), which replaces this breadboard wiring.
