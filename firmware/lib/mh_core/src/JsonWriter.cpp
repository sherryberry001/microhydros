#include "mh/JsonWriter.h"

#include <cmath>

namespace mh {

JsonWriter::JsonWriter(char* buffer, size_t capacity)
    : buffer_(buffer), capacity_(capacity), len_(0), truncated_(false),
      need_comma_(false), after_key_(false) {
    if (capacity_ > 0) buffer_[0] = '\0';
}

void JsonWriter::put(char c) {
    // Spara alltid plats for den avslutande nollan.
    if (len_ + 1 >= capacity_) {
        truncated_ = true;
        return;
    }
    buffer_[len_++] = c;
}

void JsonWriter::puts(const char* s) {
    if (!s) return;
    while (*s) put(*s++);
}

void JsonWriter::putEscaped(const char* s) {
    if (!s) return;
    while (*s) {
        const char c = *s++;
        switch (c) {
            case '"':  puts("\\\""); break;
            case '\\': puts("\\\\"); break;
            case '\n': puts("\\n");  break;
            case '\r': puts("\\r");  break;
            case '\t': puts("\\t");  break;
            default:
                // Styrtecken maste kodas som \u00XX for att ramen ska vara giltig
                // JSON. Sensor-id och topics innehaller dem aldrig, men en
                // felstrang fran en drivrutin skulle kunna gora det.
                if (static_cast<unsigned char>(c) < 0x20) {
                    static const char* hex = "0123456789abcdef";
                    puts("\\u00");
                    put(hex[(c >> 4) & 0x0F]);
                    put(hex[c & 0x0F]);
                } else {
                    put(c);
                }
        }
    }
}

void JsonWriter::separatorIfNeeded() {
    if (after_key_) {
        after_key_ = false;
        return;
    }
    if (need_comma_) put(',');
}

void JsonWriter::beginObject() {
    separatorIfNeeded();
    put('{');
    need_comma_ = false;
}

void JsonWriter::endObject() {
    put('}');
    need_comma_ = true;
}

void JsonWriter::beginArray(const char* k) {
    if (k) key(k);
    separatorIfNeeded();
    put('[');
    need_comma_ = false;
}

void JsonWriter::endArray() {
    put(']');
    need_comma_ = true;
}

void JsonWriter::key(const char* k) {
    separatorIfNeeded();
    put('"');
    putEscaped(k);
    put('"');
    put(':');
    need_comma_ = false;
    after_key_ = true;
}

void JsonWriter::valueString(const char* v) {
    separatorIfNeeded();
    put('"');
    putEscaped(v);
    put('"');
    need_comma_ = true;
}

void JsonWriter::valueInt(int32_t v) {
    separatorIfNeeded();
    if (v < 0) {
        put('-');
        // Hantera INT32_MIN utan att negera, -v svammar over.
        uint32_t u = static_cast<uint32_t>(-(static_cast<int64_t>(v)));
        char tmp[12];
        int i = 0;
        do { tmp[i++] = static_cast<char>('0' + (u % 10)); u /= 10; } while (u);
        while (i--) put(tmp[i]);
    } else {
        uint32_t u = static_cast<uint32_t>(v);
        char tmp[12];
        int i = 0;
        do { tmp[i++] = static_cast<char>('0' + (u % 10)); u /= 10; } while (u);
        while (i--) put(tmp[i]);
    }
    need_comma_ = true;
}

void JsonWriter::valueUInt(uint32_t v) {
    separatorIfNeeded();
    char tmp[12];
    int i = 0;
    do { tmp[i++] = static_cast<char>('0' + (v % 10)); v /= 10; } while (v);
    while (i--) put(tmp[i]);
    need_comma_ = true;
}

void JsonWriter::valueFloat(float v, uint8_t decimals) {
    // JSON har varken NaN eller Infinity. Ett underkant matvarde blir darfor
    // null, vilket varje mottagare forstar och vilket bevarar informationen att
    // matningen fanns men saknar varde. Att skriva 0 istallet hade varit en
    // tyst logn som ser ut som en riktig matning.
    if (!std::isfinite(v)) {
        valueNull();
        return;
    }
    separatorIfNeeded();

    if (v < 0.0f) {
        put('-');
        v = -v;
    }

    // Egen formatering istallet for snprintf("%f"). Pa manga embedded-byggen ar
    // flyttalsstod i printf bortoptimerat for att spara flash, och da skrivs
    // matvardena ut som tomma strangar eller skrap. Det ar en klassisk och
    // svarfelsokt fallgrop. Har ar beteendet detsamma pa alla plattformar.
    float scale = 1.0f;
    for (uint8_t i = 0; i < decimals; ++i) scale *= 10.0f;

    // Avrunda till narmaste, halvvagsvarden uppat.
    const double scaled = static_cast<double>(v) * static_cast<double>(scale) + 0.5;
    if (scaled >= 4294967295.0) {
        // Utanfor vad vi kan skriva exakt. Hellre null an ett felaktigt tal.
        valueNull();
        return;
    }
    uint32_t fixed = static_cast<uint32_t>(scaled);

    const uint32_t int_part = fixed / static_cast<uint32_t>(scale);
    uint32_t frac_part = fixed % static_cast<uint32_t>(scale);

    uint32_t u = int_part;
    char tmp[12];
    int i = 0;
    do { tmp[i++] = static_cast<char>('0' + (u % 10)); u /= 10; } while (u);
    while (i--) put(tmp[i]);

    if (decimals > 0) {
        put('.');
        // Skriv decimalerna med ledande nollor.
        for (int d = decimals - 1; d >= 0; --d) {
            uint32_t div = 1;
            for (int k = 0; k < d; ++k) div *= 10;
            put(static_cast<char>('0' + ((frac_part / div) % 10)));
        }
    }
    need_comma_ = true;
}

void JsonWriter::valueBool(bool v) {
    separatorIfNeeded();
    puts(v ? "true" : "false");
    need_comma_ = true;
}

void JsonWriter::valueNull() {
    separatorIfNeeded();
    puts("null");
    need_comma_ = true;
}

void JsonWriter::kvString(const char* k, const char* v) { key(k); valueString(v); }
void JsonWriter::kvInt(const char* k, int32_t v) { key(k); valueInt(v); }
void JsonWriter::kvUInt(const char* k, uint32_t v) { key(k); valueUInt(v); }
void JsonWriter::kvFloat(const char* k, float v, uint8_t d) { key(k); valueFloat(v, d); }
void JsonWriter::kvBool(const char* k, bool v) { key(k); valueBool(v); }

size_t JsonWriter::finish() {
    if (capacity_ == 0) return 0;
    buffer_[len_] = '\0';
    // En trunkerad ram ar ogiltig JSON och far aldrig skickas. Anroparen ser det
    // pa returvardet 0 och kan rakna upp en felraknare istallet.
    return truncated_ ? 0 : len_;
}

} // namespace mh
