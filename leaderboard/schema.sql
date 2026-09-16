-- DRIFTFALL leaderboard. One row per accepted run.
-- A board is (seed, mode, tag): tag is flightTag(flight, pilot), so a custom
-- flight ranks against its own course and never against the standard one.
CREATE TABLE IF NOT EXISTS runs (
  id       TEXT PRIMARY KEY,
  seed     TEXT    NOT NULL,
  mode     TEXT    NOT NULL,
  tag      TEXT    NOT NULL,
  pilot    TEXT    NOT NULL,
  name     TEXT    NOT NULL,
  elapsed  REAL    NOT NULL,
  score    INTEGER NOT NULL,
  ghost    TEXT    NOT NULL,
  created  INTEGER NOT NULL
);
-- The board query: fastest first within one course build.
CREATE INDEX IF NOT EXISTS idx_board ON runs (seed, mode, tag, elapsed);
CREATE INDEX IF NOT EXISTS idx_created ON runs (created);

-- Rate limiting. Stores a salted, truncated hash of the connecting address for
-- 60 seconds, never an address itself, and is swept on every submission.
CREATE TABLE IF NOT EXISTS throttle (
  who     TEXT    NOT NULL,
  created INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_throttle ON throttle (who, created);
