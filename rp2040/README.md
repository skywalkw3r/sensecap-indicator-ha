# RP2040 Firmware

This directory is an Arduino sketch for the SenseCAP Indicator RP2040 coprocessor.
It is intentionally not a PlatformIO project yet.

## Build

Use the installed Arduino RP2040 core:

```sh
arduino-cli compile --fqbn rp2040:rp2040:generic rp2040
```

The current local toolchain uses:

- Core: `rp2040:rp2040` 5.4.2
- FQBN: `rp2040:rp2040:generic`
- Libraries: `PacketSerial`, `AHT20`, `Sensirion I2C SGP40`,
  `Sensirion I2C SCD4x`, `Sensirion Gas Index Algorithm`

## Direction

Keep this as Arduino sketch code while the ESP32/Home Assistant path is being
stabilized. A future PlatformIO migration is useful when Grove hot-plug driver
work starts, because it will let us split the firmware into `src/` and
`include/` modules with pinned dependencies.

Do not mix that migration with protocol or sensor behavior changes.
