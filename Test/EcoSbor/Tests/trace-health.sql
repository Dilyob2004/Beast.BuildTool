-- Any nonzero loss/error requires inspecting the capture before drawing conclusions.
SELECT name, idx, severity, value FROM stats
WHERE value > 0 AND (severity IN ('error','data_loss') OR name GLOB '*overrun*' OR name GLOB '*dropped*');
SELECT COUNT(*) AS unfinished_beast_scopes FROM slice WHERE name GLOB 'Beast.*' AND dur < 0;
