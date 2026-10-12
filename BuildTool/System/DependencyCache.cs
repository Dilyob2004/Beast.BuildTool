using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using System.Threading;

namespace Beast.BuildTool
{
    /// <summary>
    /// Caches #include dependencies to avoid re-parsing files on every build.
    /// Tracks timestamps of all dependencies to properly detect changes.
    /// Thread-safe implementation using ConcurrentDictionary.
    /// </summary>
    public class DependencyCache
    {
        private readonly string _cacheFilePath;
        private ConcurrentDictionary<string, DependencyInfo> _cache = new ConcurrentDictionary<string, DependencyInfo>();
        private volatile bool _isDirty = false;

        public class DependencyInfo
        {
            /// <summary>
            /// Timestamp of the source file when dependencies were parsed.
            /// </summary>
            public long SourceLastModifiedTicks { get; set; }

            /// <summary>
            /// List of header dependencies with their timestamps at parse time.
            /// </summary>
            public List<DependencyEntry> Dependencies { get; set; } = new List<DependencyEntry>();
        }

        public class DependencyEntry
        {
            public string Path { get; set; } = "";
            public long LastModifiedTicks { get; set; }
        }

        public DependencyCache(string outputPath)
        {
            if (!Directory.Exists(outputPath))
                Directory.CreateDirectory(outputPath);

            _cacheFilePath = Path.Combine(outputPath, ".deps.cache.json");
            Load();
        }

        /// <summary>
        /// Gets dependencies for a source file, using cache if available and valid.
        /// Cache is invalidated if the source file OR ANY of its dependencies changed.
        /// </summary>
        public HashSet<string> GetDependencies(string sourceFile, HashSet<string> includePaths)
        {
            if (!File.Exists(sourceFile))
                return new HashSet<string>();

            var normalizedPath = NormalizePath(sourceFile);
            var sourceFileInfo = new FileInfo(sourceFile);
            var sourceTime = sourceFileInfo.LastWriteTimeUtc.Ticks;

            // Check if cached entry exists
            if (_cache.TryGetValue(normalizedPath, out var info))
            {
                // FIX #1: Используем == вместо >=
                // Кэш валиден только если время файла точно совпадает с кэшированным.
                // >= принимало бы невозможные случаи (время в будущем).
                if (info.SourceLastModifiedTicks == sourceTime)
                {
                    // Check if ALL dependencies are still valid (exist AND not modified)
                    bool cacheValid = true;
                    foreach (var dep in info.Dependencies)
                    {
                        // FIX #6: Валидация пути из кэша перед использованием
                        if (string.IsNullOrEmpty(dep.Path))
                        {
                            cacheValid = false;
                            Log.TraceVerbose("Dependency cache invalid: empty path in cache entry");
                            break;
                        }

                        // FIX #5: Используем FileInfo для одного обращения к ФС
                        // вместо двух отдельных вызовов File.Exists + GetLastWriteTimeUtc,
                        // которые создают race condition между проверками.
                        var depInfo = new FileInfo(dep.Path);
                        if (!depInfo.Exists)
                        {
                            cacheValid = false;
                            Log.TraceVerbose($"Dependency cache invalid: {dep.Path} no longer exists");
                            break;
                        }

                        if (depInfo.LastWriteTimeUtc.Ticks > dep.LastModifiedTicks)
                        {
                            cacheValid = false;
                            Log.TraceVerbose($"Dependency cache invalid: {Path.GetFileName(dep.Path)} was modified");
                            break;
                        }
                    }

                    if (cacheValid)
                    {
                        // Return cached dependency paths
                        var result = new HashSet<string>();
                        foreach (var dep in info.Dependencies)
                        {
                            result.Add(dep.Path);
                        }
                        return result;
                    }
                }
            }

            var deps = CppIncludeParser.GetAllHeaderDependencies(includePaths, sourceFile);

            // FIX #2: Читаем время файла ПОСЛЕ парсинга, а не до.
            // Если файл изменился во время парсинга — следующий билд корректно
            // увидит несовпадение и перепарсит снова.
            var sourceTimeAfterParsing = new FileInfo(sourceFile).LastWriteTimeUtc.Ticks;

            // Store with timestamps
            var newInfo = new DependencyInfo
            {
                SourceLastModifiedTicks = sourceTimeAfterParsing,
                Dependencies = new List<DependencyEntry>()
            };

            foreach (var depPath in deps)
            {
                // FIX #5: Используем FileInfo вместо двух отдельных вызовов
                var depFileInfo = new FileInfo(depPath);
                if (depFileInfo.Exists)
                {
                    newInfo.Dependencies.Add(new DependencyEntry
                    {
                        // FIX #3: Нормализуем пути зависимостей так же, как ключи кэша.
                        // Иначе на Windows "C:\Foo\bar.h" и "c:\foo\bar.h" — разные строки,
                        // но один файл.
                        Path = NormalizePath(depPath),
                        LastModifiedTicks = depFileInfo.LastWriteTimeUtc.Ticks
                    });
                }
            }

            // FIX: ConcurrentDictionary.AddOrUpdate потокобезопасен без lock
            _cache[normalizedPath] = newInfo;
            _isDirty = true;

            return deps;
        }

        /// <summary>
        /// Invalidates cache entry for a specific file.
        /// </summary>
        public void Invalidate(string sourceFile)
        {
            var normalizedPath = NormalizePath(sourceFile);
            if (_cache.TryRemove(normalizedPath, out _))
            {
                _isDirty = true;
            }
        }

        /// <summary>
        /// Clears all cached dependencies.
        /// </summary>
        public void Clear()
        {
            _cache.Clear();
            _isDirty = true;
        }

        /// <summary>
        /// Saves cache to disk if modified.
        /// </summary>
        public void Save()
        {
            if (!_isDirty)
                return;

            try
            {
                var options = new JsonSerializerOptions
                {
                    WriteIndented = false // Compact for speed
                };

                // Снимаем snapshot для сериализации — ConcurrentDictionary
                // может меняться во время сериализации
                var snapshot = new Dictionary<string, DependencyInfo>(_cache);
                var json = JsonSerializer.Serialize(snapshot, options);
                File.WriteAllText(_cacheFilePath, json);
                _isDirty = false;

                Log.TraceVerbose($"Saved dependency cache with {snapshot.Count} entries");
            }
            catch (Exception ex)
            {
                Log.TraceWarning($"Failed to save dependency cache: {ex.Message}");
            }
        }

        private void Load()
        {
            if (!File.Exists(_cacheFilePath))
                return;

            try
            {
                var json = File.ReadAllText(_cacheFilePath);
                var loaded = JsonSerializer.Deserialize<Dictionary<string, DependencyInfo>>(json);
                if (loaded != null)
                {
                    // FIX #6: Фильтруем некорректные записи при загрузке из JSON
                    foreach (var kvp in loaded)
                    {
                        if (string.IsNullOrEmpty(kvp.Key) || kvp.Value == null)
                            continue;

                        // Удаляем зависимости с пустыми путями
                        kvp.Value.Dependencies.RemoveAll(d => string.IsNullOrEmpty(d.Path));

                        _cache[kvp.Key] = kvp.Value;
                    }

                    Log.TraceVerbose($"Loaded dependency cache with {_cache.Count} entries");
                }
            }
            catch (Exception ex)
            {
                Log.TraceWarning($"Failed to load dependency cache, starting fresh: {ex.Message}");
                _cache = new ConcurrentDictionary<string, DependencyInfo>();
            }
        }

        private static string NormalizePath(string path)
        {
            return Path.GetFullPath(path).ToLowerInvariant();
        }
    }
}