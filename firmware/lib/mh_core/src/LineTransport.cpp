#include "mh/LineTransport.h"

namespace mh {

bool LineTransport::publish(const char* topic, const char* payload, size_t len) {
    if (!isConnected()) return false;

    // Ramen skrivs i bitar istallet for att byggas ihop i en lokal buffert.
    // En hel ram ar 2 kB, och 2 kB pa stacken i Arduino-loopens task, som har
    // 8 kB totalt, ar precis den sortens marginal man inte upptacker forran
    // enheten borjar starta om av sig sjalv i falt. Chunkbufferten nedan ar 64 B.
    if (!sink_.write("MH1 ", 4)) return false;
    if (topic) {
        size_t tlen = 0;
        while (topic[tlen]) ++tlen;
        if (!sink_.write(topic, tlen)) return false;
    }
    if (!sink_.write(" ", 1)) return false;

    char chunk[64];
    size_t n = 0;
    for (size_t i = 0; i < len; ++i) {
        // Radslut i nyttolasten skulle bryta ramningen. JsonWriter escapar dem
        // redan, men transporten litar inte pa det, den ar sista forsvaret.
        const char c = payload[i];
        chunk[n++] = (c == '\n' || c == '\r') ? ' ' : c;
        if (n == sizeof(chunk)) {
            if (!sink_.write(chunk, n)) return false;
            n = 0;
        }
    }
    if (n > 0 && !sink_.write(chunk, n)) return false;

    return sink_.endLine();
}

} // namespace mh
