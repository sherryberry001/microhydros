// MicroHydros - kooperativ schemalaggare
//
// Firmwaren har tre saker som ska handa med olika takt: lasa sensorer var tionde
// sekund, skicka telemetri varje minut, och skicka livstecken var femte minut.
// Den naiva losningen ar delay() i loop(). Den ar ocksa anledningen till att
// manga forsta prototyper blir ostabila, for under en delay() svarar enheten inte
// pa natverket, matar inte watchdogen och kan inte reagera pa nagot.
//
// Har finns istallet en liten tidtabell. loop() anropar tick(), fragar vilka
// uppgifter som ar dags, och gor dem. Ingenting blockerar.
//
// Millis() pa en ESP32 ar en uint32 och slar runt efter ungefar 49.7 dygn. Ett
// system som ska sta i ett odlingsskap i manader maste tala det. All
// tidsjamforelse har ar darfor skriven som osignerad subtraktion,
// (now - next_due) istallet for (now >= next_due), vilket ar korrekt aven over
// varvet. Det finns ett test som stegar klockan forbi overflow och kontrollerar
// just detta.

#pragma once

#include <cstdint>
#include <cstddef>

namespace mh {

static constexpr size_t kMaxTasks = 6;

/// En periodisk uppgift. Identifieras med ett id som applikationen sjalv tolkar.
struct PeriodicTask {
    uint8_t  id;
    uint32_t period_ms;
    uint32_t next_due_ms;
    bool     enabled;

    PeriodicTask() : id(0), period_ms(0), next_due_ms(0), enabled(false) {}
};

class Scheduler {
public:
    Scheduler() : count_(0) {}

    /// Registrerar en uppgift. first_delay_ms gor att man kan sprida ut starten
    /// sa att alla uppgifter inte forfaller i samma varv direkt efter boot.
    bool addTask(uint8_t id, uint32_t period_ms, uint32_t now_ms, uint32_t first_delay_ms = 0);

    /// Returnerar antal forfallna uppgifter och fyller out_ids. Uppgifter
    /// schemalaggs om direkt.
    ///
    /// Om enheten har varit upptagen sa lange att en uppgift har missat flera
    /// perioder kors den inte ikapp flera ganger. Den hoppar fram till nasta
    /// framtida tidpunkt. Att skicka fyra minuters telemetri pa en gang efter ett
    /// natavbrott hjalper ingen, och det riskerar att kasta ivag enheten i en
    /// spiral dar den aldrig hinner ikapp.
    size_t tick(uint32_t now_ms, uint8_t* out_ids, size_t max_out);

    void setEnabled(uint8_t id, bool enabled);
    void setPeriod(uint8_t id, uint32_t period_ms, uint32_t now_ms);

    /// Millisekunder tills nasta uppgift forfaller. Anvands for att kunna lagga
    /// CPU:n i lattsomn mellan varven istallet for att snurra.
    uint32_t msUntilNext(uint32_t now_ms) const;

    size_t taskCount() const { return count_; }

private:
    /// Osignerad jamforelse som overlever att millis() slar runt.
    /// Sant om now har passerat deadline, sa lange de ligger narmare an ett halvt
    /// varv fran varandra, vilket alltid galler for vara perioder.
    static bool isDue(uint32_t now, uint32_t deadline) {
        return static_cast<int32_t>(now - deadline) >= 0;
    }

    PeriodicTask tasks_[kMaxTasks];
    size_t       count_;
};

} // namespace mh
