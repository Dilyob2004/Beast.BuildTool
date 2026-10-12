using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

namespace Beast.BuildTool
{
    public class CppIncludeParser
    {
        private static string GetFullPathSource(HashSet<string> AllIncludeFiles, string Header)
        {
            foreach (var IncludeHeader in AllIncludeFiles)
            {
                string FullPath = Path.Combine(IncludeHeader, Header);
                if (File.Exists(FullPath))
                    return FullPath;
            }
            return "";
        }
        public static HashSet<string> GetAllHeaderDependencies(HashSet<string> AllIncludeFiles, string sourceFile)
        {
            var visitedFiles = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            if (File.Exists(sourceFile))
            {
                var directDependencies = GetDirectHeaderDependencies(sourceFile);

                foreach (var dependency in directDependencies)
                {
                    string FullPathHeader = GetFullPathSource(AllIncludeFiles, dependency);
                    if (!string.IsNullOrEmpty(FullPathHeader))
                    {
                        if (!visitedFiles.Add(FullPathHeader))
                            continue;
                        ProcessFile(AllIncludeFiles, FullPathHeader, visitedFiles);
                    }

                }
            }
            return visitedFiles;
        }

        private static void ProcessFile(HashSet<string> AllIncludeFiles, string filePath, HashSet<string> visitedFiles)
        {
            var directDependencies = GetDirectHeaderDependencies(filePath);
            foreach (var dependency in directDependencies)
            {
                string FullPathHeader = GetFullPathSource(AllIncludeFiles, dependency);
                if (!string.IsNullOrEmpty(FullPathHeader))
                {
                    if (!visitedFiles.Add(FullPathHeader))
                        continue;
                    ProcessFile(AllIncludeFiles, FullPathHeader, visitedFiles);
                }
            }
        }
        private static List<string> GetDirectHeaderDependencies(string filePath)
        {
            if (!File.Exists(filePath))
            {
                return new List<string>();
            }
            
            var headers = new List<string>();
            
            // Improved regex that handles:
            // - Optional whitespace before #
            // - Whitespace between #include and path
            // - Paths with forward or back slashes
            // - Lines that may or may not end with newline
            // - Ignores lines that are commented out (// or /* */)
            var includePattern = new Regex(
                @"^\s*#\s*include\s*""([^""]+)""", 
                RegexOptions.Multiline | RegexOptions.Compiled);
            
            var fileContent = File.ReadAllText(filePath);

            // Remove single-line comments to avoid false positives
            var noCommentsContent = Regex.Replace(fileContent, @"//.*$", "", RegexOptions.Multiline);
            // Remove multi-line comments
            noCommentsContent = Regex.Replace(noCommentsContent, @"/\*[\s\S]*?\*/", "");

            var matches = includePattern.Matches(noCommentsContent);

            foreach (Match match in matches)
            {
                if (match.Groups.Count > 1)
                {
                    var relativeHeader = match.Groups[1].Value.Trim();
                    if (!string.IsNullOrEmpty(relativeHeader))
                    {
                        // Normalize path separators
                        relativeHeader = relativeHeader.Replace('/', '\\');
                        headers.Add(relativeHeader);
                    }
                }
            }

            return headers;
        }


        public static List<string> GetAllSourceFiles(HashSet<string> Paths)
        {
            List<string> SourcesPath = new List<string>();
            foreach (var path in Paths)
            {
                if (string.IsNullOrEmpty(path) || !Directory.Exists(path))
                    continue;

                SourcesPath.AddRange(Directory.GetFiles(path, "*.cc", SearchOption.AllDirectories));
                SourcesPath.AddRange(Directory.GetFiles(path, "*.c", SearchOption.AllDirectories));
                SourcesPath.AddRange(Directory.GetFiles(path, "*.cpp", SearchOption.AllDirectories));
                SourcesPath.AddRange(Directory.GetFiles(path, "*.inl", SearchOption.AllDirectories));
                SourcesPath.AddRange(Directory.GetFiles(path, "*.cxx", SearchOption.AllDirectories));
            }
            return SourcesPath;
        }
    }

}
