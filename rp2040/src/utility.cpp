#include "indicator_rp2040.hpp"

/************************ beep ****************************/
const int Buzzer = 19;

#define BEEP_FREQ_HZ    2700  /* near the passive buzzer's resonant frequency */
#define BEEP_MAX_MS     5000

void beep_init(void)
{
    pinMode(Buzzer, OUTPUT);
    digitalWrite(Buzzer, LOW);
}
void beep_off(void)
{
    noTone(Buzzer);
    digitalWrite(Buzzer, LOW);
}
/* Non-blocking (tone() runs off a timer): the packet handler stays responsive,
 * so a BEEP_OFF can cut a beep short and rapid beep sequences keep cadence. */
void beep_on(uint32_t ms)
{
    if (ms == 0) {
        ms = 50;
    } else if (ms > BEEP_MAX_MS) {
        ms = BEEP_MAX_MS;
    }
    tone(Buzzer, BEEP_FREQ_HZ, ms);
}

/************************ Format printer ****************************/
void printUint16Hex(uint16_t value)
{
    Serial.print(value < 4096 ? "0" : "");
    Serial.print(value < 256 ? "0" : "");
    Serial.print(value < 16 ? "0" : "");
    Serial.print(value, HEX);
}

void printSerialNumber(uint16_t serial0, uint16_t serial1, uint16_t serial2)
{
    Serial.print("Serial: 0x");
    printUint16Hex(serial0);
    printUint16Hex(serial1);
    printUint16Hex(serial2);
    Serial.println();
}