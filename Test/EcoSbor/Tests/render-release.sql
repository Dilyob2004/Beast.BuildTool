-- Last 100 ms of input + first 120 ms after SDK TouchUp, with native frame slices.
WITH release AS (SELECT MAX(ts+dur) AS ts FROM slice WHERE name='Beast.Input.Release')
SELECT ROUND((s.ts-r.ts)/1e6,3) AS relative_ms, s.name, ROUND(s.dur/1e6,3) AS duration_ms
FROM slice s, release r
WHERE s.ts BETWEEN r.ts-100000000 AND r.ts+120000000 AND s.dur>=0
  AND (s.name IN ('Beast.Android.Vsync','Beast.Android.RequestVsync','Beast.UI.Frame',
    'Beast.UI.Build','Beast.VK.Submit','Beast.Input.Event','Beast.Input.Release')
    OR s.name LIKE 'latchBuffer com.beast.ecosbor%')
ORDER BY s.ts;

-- UI frame scheduling cadence and whether we actually submitted a new buffer.
WITH frames AS (
 SELECT *, (ts-LAG(ts) OVER (ORDER BY ts))/1e6 AS interval_ms
 FROM slice WHERE name='Beast.UI.Frame' AND dur>=0
), release AS (SELECT MAX(ts+dur) AS ts FROM slice WHERE name='Beast.Input.Release')
SELECT ROUND((f.ts-r.ts)/1e6,2) AS relative_ms, ROUND(interval_ms,3) AS interval_ms,
 ROUND(f.dur/1e6,3) AS duration_ms,
 (SELECT COUNT(*) FROM slice s WHERE s.name='Beast.VK.Submit' AND s.track_id=f.track_id
   AND s.ts>=f.ts AND s.ts<f.ts+f.dur) AS submitted
FROM frames f,release r WHERE f.ts BETWEEN r.ts-850000000 AND r.ts+750000000;
