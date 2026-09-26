// MicroHydros - JSON utan heap
//
// Prototypen skickar JSON for att det gar att lasa med blotta ogat under
// felsokning och for att varje tankbar mottagare forstar det. Men ArduinoJson och
// liknande bibliotek allokerar, och pa en enhet som ska sta i manader ar en
// fragmenterad heap en av de vanligaste orsakerna till att den till slut hanger
// sig.
//
// Den har skrivaren tar en buffert som anroparen ager och fyller den. Blir det
// fullt satts ett overflow-flagga och ingenting skrivs utanfor bufferten.
// truncated() maste kollas innan man skickar. Det ar medvetet att den inte
// kastar undantag: undantag ar avstangda i de flesta embedded-byggen.
//
// Storleken pa telemetriramen ar darfor kand vid kompilering, vilket i sin tur
// ar det som gor att store-and-forward-kön kan vara statiskt allokerad.

#pragma once

#include <cstddef>
#include <cstdint>

namespace mh {

class JsonWriter {
public:
    JsonWriter(char* buffer, size_t capacity);

    void beginObject();
    void endObject();
    void beginArray(const char* key = nullptr);
    void endArray();

    void key(const char* k);
    void valueString(const char* v);
    void valueInt(int32_t v);
    void valueUInt(uint32_t v);
    void valueFloat(float v, uint8_t decimals = 2);
    void valueBool(bool v);
    void valueNull();

    void kvString(const char* k, const char* v);
    void kvInt(const char* k, int32_t v);
    void kvUInt(const char* k, uint32_t v);
    void kvFloat(const char* k, float v, uint8_t decimals = 2);
    void kvBool(const char* k, bool v);

    /// Avslutar och nollterminerar. Returnerar antal tecken, eller 0 om ramen
    /// inte fick plats.
    size_t finish();

    bool   truncated() const { return truncated_; }
    size_t length() const { return len_; }
    const char* c_str() const { return buffer_; }

private:
    void put(char c);
    void puts(const char* s);
    void putEscaped(const char* s);
    void separatorIfNeeded();

    char*  buffer_;
    size_t capacity_;
    size_t len_;
    bool   truncated_;
    bool   need_comma_;
    bool   after_key_;
};

} // namespace mh
