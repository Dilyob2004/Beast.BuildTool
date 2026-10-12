-- Native app FrameTimeline rows are absent for both engines in this device trace.
-- Inspect SurfaceFlinger latchBuffer timestamps instead: these are buffer latches,
-- not CPU/GPU durations and not proof of missed rendering deadlines.
WITH frames AS (
  SELECT ts,
    CASE WHEN name LIKE 'latchBuffer com.beast.ecosbor%' THEN 'Beast'
         ELSE 'Flutter' END AS engine
  FROM slice
  WHERE name LIKE 'latchBuffer com.beast.ecosbor%'
     OR name LIKE 'latchBuffer %SurfaceView%flutter_application_1%'
), intervals AS (
  SELECT engine, ts,
    (ts - LAG(ts) OVER (PARTITION BY engine ORDER BY ts)) / 1e6 AS interval_ms,
    (ts - MIN(ts) OVER (PARTITION BY engine)) / 1e6 AS relative_ms
  FROM frames
)
SELECT engine, COUNT(*) AS intervals,
  ROUND(AVG(interval_ms), 2) AS mean_ms,
  ROUND(MAX(interval_ms), 2) AS max_ms,
  SUM(interval_ms > 25) AS gaps_over_25ms
FROM intervals WHERE interval_ms IS NOT NULL GROUP BY engine;

-- End of the injected touch sequence is approximated by the final delivery
-- timestamp on the app thread. No other input should occur during the recording.
WITH last_input AS (
  SELECT MAX(s.ts + s.dur) AS release
  FROM slice s
  JOIN thread_track tt ON tt.id = s.track_id
  JOIN thread t USING (utid)
  JOIN process p USING (upid)
  WHERE s.name LIKE 'deliverInputEvent src=0x1002%'
    AND p.name IN ('com.beast.ecosbor', 'com.example.flutter_application_1')
), frames AS (
  SELECT ts, LAG(ts) OVER (ORDER BY ts) AS previous
  FROM slice
  WHERE name LIKE 'latchBuffer com.beast.ecosbor%'
     OR name LIKE 'latchBuffer %SurfaceView%flutter_application_1%'
)
SELECT ROUND((ts - release) / 1e6, 2) AS after_last_input_ms,
       ROUND((ts - previous) / 1e6, 2) AS latch_interval_ms
FROM frames, last_input WHERE ts > release;
