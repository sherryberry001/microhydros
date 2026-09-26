-- MicroHydros - lagringsschema for gatewayen
--
-- Kunden skrev att historiska matdata kommer vara viktiga i framtiden, och att
-- systemet pa sikt ska kunna analysera hur klimatet utvecklas. Schemat ar darfor
-- byggt for tidsserier fran borjan, inte for att visa ett aktuellt varde.
--
-- Tva val som ar vard att forklara:
--
-- 1. En rad per matvarde, inte en rad per ram. Det kallas long format och ar det
--    som gor att en ny sensor kan laggas till utan att tabellen andras. Hade
--    varje ram blivit en rad med kolumnerna air_in, air_out, water och rh_in
--    hade vi behovt en migrering den dagen kunden vill mata pH eller ledningstal,
--    vilket ar en ganska sannolik utveckling for ett hydroponiskt system.
--
-- 2. Kvalitetsflaggan lagras tillsammans med vardet. Underkanda matvarden kastas
--    inte, de markeras. Da kan en graf rita hal dar data saknas istallet for att
--    dra en rak linje over ett fel, och man kan i efterhand rakna ut hur ofta en
--    viss givare krånglar. Det var precis det kunden saknade hos sin forra
--    leverantor: systemet fungerade vid demonstration men gick inte att lita pa
--    over tid, och ingen kunde visa varfor.

PRAGMA journal_mode = WAL;

CREATE TABLE IF NOT EXISTS frames (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id      TEXT    NOT NULL,
    seq            INTEGER NOT NULL,
    schema_version INTEGER NOT NULL,
    uptime_ms      INTEGER NOT NULL,
    -- Mottagningstid satt av gatewayen. Enheten har ingen batteribackad
    -- realtidsklocka och ska inte gissa vad klockan ar, den rapporterar bara
    -- monoton drifttid. Gatewayen ar den forsta i kedjan som vet vad klockan ar.
    received_utc   TEXT    NOT NULL,
    trend_direction   TEXT,
    trend_slope_per_h REAL,
    trend_r2          REAL,
    trend_slope_ratio REAL,
    projected_equilibrium_c REAL,
    tau_minutes       REAL,
    limit_exceeded    INTEGER,
    minutes_to_limit  REAL,
    queued_frames  INTEGER,
    dropped_frames INTEGER,
    bus_errors     INTEGER,
    raw_json       TEXT    NOT NULL,
    UNIQUE (device_id, seq)
);

CREATE TABLE IF NOT EXISTS measurements (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    frame_id     INTEGER NOT NULL REFERENCES frames(id) ON DELETE CASCADE,
    device_id    TEXT    NOT NULL,
    sensor_id    TEXT    NOT NULL,
    quantity     TEXT    NOT NULL,
    location     TEXT    NOT NULL,
    unit         TEXT    NOT NULL,
    value        REAL,              -- NULL nar quality = 'bad'
    quality      TEXT    NOT NULL,
    reason       TEXT    NOT NULL,
    device_t_ms  INTEGER NOT NULL,
    received_utc TEXT    NOT NULL
);

-- Den vanligaste fragan ar "visa den har sensorn over den har tidsperioden".
CREATE INDEX IF NOT EXISTS idx_measurements_series
    ON measurements (device_id, sensor_id, received_utc);

-- Den nast vanligaste ar "vilka matvarden underkandes och varfor".
CREATE INDEX IF NOT EXISTS idx_measurements_quality
    ON measurements (device_id, quality, received_utc);

CREATE INDEX IF NOT EXISTS idx_frames_received
    ON frames (device_id, received_utc);

-- Hal i lopnummerserien betyder att ramar gick forlorade. Vyn gor det synligt
-- utan att nagon behover skriva fragan sjalv.
CREATE VIEW IF NOT EXISTS v_sequence_gaps AS
SELECT
    device_id,
    seq   AS previous_seq,
    next_seq,
    next_seq - seq - 1 AS missing_frames
FROM (
    SELECT device_id, seq,
           LEAD(seq) OVER (PARTITION BY device_id ORDER BY seq) AS next_seq
    FROM frames
)
WHERE next_seq IS NOT NULL AND next_seq - seq > 1;

-- Sammanfattning per sensor: hur manga matvarden, hur manga underkanda, och
-- spannet. Det ar den har vyn man tittar pa for att svara pa fragan "gar det att
-- lita pa den har givaren".
CREATE VIEW IF NOT EXISTS v_sensor_health AS
SELECT
    device_id,
    sensor_id,
    quantity,
    COUNT(*)                                            AS n_total,
    SUM(CASE WHEN quality = 'good' THEN 1 ELSE 0 END)    AS n_good,
    SUM(CASE WHEN quality = 'suspect' THEN 1 ELSE 0 END) AS n_suspect,
    SUM(CASE WHEN quality = 'bad' THEN 1 ELSE 0 END)     AS n_bad,
    ROUND(MIN(value), 3)                                AS min_value,
    ROUND(MAX(value), 3)                                AS max_value,
    ROUND(AVG(value), 3)                                AS avg_value
FROM measurements
GROUP BY device_id, sensor_id, quantity;
