-- Wall durations, NOT GPU execution times. Nested scopes must not be added.
SELECT name, COUNT(*) AS samples, ROUND(AVG(dur)/1e6,3) AS mean_ms,
       ROUND(MAX(dur)/1e6,3) AS max_ms
FROM slice WHERE name GLOB 'Beast.*' AND dur >= 0
GROUP BY name ORDER BY max_ms DESC;

-- Slowest submitted frames. Text/UploadWait are nested in Build/Prepare.
WITH frames AS (
  SELECT f.* FROM slice f WHERE f.name = 'Beast.UI.Frame' AND f.dur >= 0
    AND EXISTS (SELECT 1 FROM slice s WHERE s.track_id = f.track_id
      AND s.name = 'Beast.VK.Submit' AND s.ts >= f.ts AND s.ts < f.ts + f.dur)
), release AS (SELECT MAX(ts + dur) AS ts FROM slice WHERE name = 'Beast.Input.Release')
SELECT f.id AS frame_id, ROUND((f.ts-r.ts)/1e6,2) AS from_release_ms,
  ROUND(f.dur/1e6,3) AS frame_ms,
  ROUND(SUM(CASE WHEN s.name='Beast.UI.Build' THEN s.dur ELSE 0 END)/1e6,3) AS build_ms,
  ROUND(SUM(CASE WHEN s.name='Beast.Text.Resolve' THEN s.dur ELSE 0 END)/1e6,3) AS text_ms,
  ROUND(SUM(CASE WHEN s.name='Beast.VK.Prepare' THEN s.dur ELSE 0 END)/1e6,3) AS prepare_ms,
  ROUND(SUM(CASE WHEN s.name='Beast.VK.UploadWait' THEN s.dur ELSE 0 END)/1e6,3) AS upload_wait_ms,
  ROUND(SUM(CASE WHEN s.name='Beast.VK.FrameFence' THEN s.dur ELSE 0 END)/1e6,3) AS fence_ms,
  ROUND(SUM(CASE WHEN s.name='Beast.VK.Acquire' THEN s.dur ELSE 0 END)/1e6,3) AS acquire_ms,
  ROUND(SUM(CASE WHEN s.name='Beast.VK.Record' THEN s.dur ELSE 0 END)/1e6,3) AS record_ms,
  ROUND(SUM(CASE WHEN s.name='Beast.VK.Present' THEN s.dur ELSE 0 END)/1e6,3) AS present_ms
FROM frames f CROSS JOIN release r LEFT JOIN slice s ON s.track_id=f.track_id
  AND s.ts>=f.ts AND s.ts<f.ts+f.dur AND s.dur>=0
GROUP BY f.id ORDER BY f.dur DESC LIMIT 16;

-- CPU scheduling within the longest UI builds: Running vs Runnable/sleeping.
WITH builds AS (
  SELECT s.*, tt.utid FROM slice s JOIN thread_track tt ON tt.id=s.track_id
  WHERE s.name='Beast.UI.Build' AND s.dur>=0 ORDER BY s.dur DESC LIMIT 8
)
SELECT b.id AS build_id, ROUND(b.dur/1e6,3) AS build_ms, t.state,
  ROUND(SUM(MIN(b.ts+b.dur,t.ts+t.dur)-MAX(b.ts,t.ts))/1e6,3) AS state_ms
FROM builds b JOIN thread_state t ON t.utid=b.utid AND t.dur>=0
  AND t.ts<b.ts+b.dur AND t.ts+t.dur>b.ts
GROUP BY b.id,t.state ORDER BY b.dur DESC,state_ms DESC;
