#ifdef ARDUINO

#include "mh/hw/Platform.h"

namespace mh {
namespace hw {

void recoverI2cBus(uint8_t sda_pin, uint8_t scl_pin) {
    pinMode(sda_pin, INPUT_PULLUP);
    pinMode(scl_pin, INPUT_PULLUP);
    delayMicroseconds(10);

    // Om SDA redan ar hog ar bussen fri och det finns inget att aterstalla.
    if (digitalRead(sda_pin) == HIGH) {
        return;
    }

    // Klocka fram slaven. Varje puls far den att lagga ut nasta bit, och efter
    // hogst nio pulser har den lamnat ifran sig hela byten och slapper SDA.
    pinMode(scl_pin, OUTPUT);
    for (uint8_t i = 0; i < 9; ++i) {
        digitalWrite(scl_pin, LOW);
        delayMicroseconds(5);
        digitalWrite(scl_pin, HIGH);
        delayMicroseconds(5);
        if (digitalRead(sda_pin) == HIGH) break;
    }

    // Avsluta med ett riktigt stoppvillkor: SDA gar fran lag till hog medan SCL
    // ar hog. Utan det tror slaven att en transaktion fortfarande pagar.
    pinMode(sda_pin, OUTPUT);
    digitalWrite(sda_pin, LOW);
    delayMicroseconds(5);
    digitalWrite(scl_pin, HIGH);
    delayMicroseconds(5);
    digitalWrite(sda_pin, HIGH);
    delayMicroseconds(5);

    pinMode(sda_pin, INPUT_PULLUP);
    pinMode(scl_pin, INPUT_PULLUP);
}

} // namespace hw
} // namespace mh

#endif // ARDUINO
