// MicroHydros - fuktberakningar
//
// Relativ fuktighet ensam sager forvanansvart lite om en odling, eftersom den
// andrar sig sa fort temperaturen andrar sig aven om mangden vatten i luften ar
// konstant. Tva storheter som faktiskt beskriver vaxtens situation raknas darfor
// fram i firmwaren istallet for i molnet:
//
//   Daggpunkt  - nar den narmar sig en yttemperatur bildas kondens, och kondens
//                pa blad ar startskottet for mogel och bladmogel.
//   VPD        - angtrycksdeficit, drivkraften for vaxtens transpiration. Det ar
//                den storhet odlare faktiskt reglerar efter, typiskt 0.8 till
//                1.2 kPa for sallad och ortar.
//
// Att rakna dem har nere och inte i gatewayen ar ett medvetet val: enheten kan da
// larma pa kondensrisk aven nar natet ligger nere.
//
// Formlerna ar Magnus-approximationen med koefficienter fran Alduchov och
// Eskridge (1996), som ger under 0.4 procents fel i intervallet -40 till +50 C.

#pragma once

namespace mh {
namespace psychro {

/// Magnuskoefficienter over vatten.
static constexpr float kMagnusA = 17.625f;
static constexpr float kMagnusB = 243.04f; ///< grader Celsius

/// Mattnadsangtryck i kPa vid temperaturen t_c.
float saturationVapourPressureKPa(float t_c);

/// Daggpunkt i grader Celsius ur lufttemperatur och relativ fuktighet.
/// Returnerar NaN om rh ligger utanfor (0, 100].
float dewPointC(float t_c, float rh_percent);

/// Angtrycksdeficit i kPa. Hogt varde = torr luft = hog transpiration.
/// Returnerar NaN vid ogiltig rh.
float vapourPressureDeficitKPa(float t_c, float rh_percent);

/// Absolut fuktighet i gram per kubikmeter. Anvands inte i prototypens larm men
/// ligger har for att gatewayen ska slippa rakna om det.
float absoluteHumidityGm3(float t_c, float rh_percent);

} // namespace psychro
} // namespace mh
