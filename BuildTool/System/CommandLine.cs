using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Beast.BuildTool
{
    public class CommandLine
    {

        public CommandLine() { }
        public CommandLine(string[] args)
        {
            Parse(args);
        }

        private readonly Dictionary<string, string> _arguments = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        private readonly HashSet<string> _flags = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        private readonly List<string> _positionals = new List<string>();

        /// <summary>
        /// Parses command line arguments into key-value pairs and flags
        /// </summary>
        /// <param name="args">Command line arguments</param>
        public void Parse(string[] args)
        {
            for (int i = 0; i < args.Length; i++)
            {
                string arg = args[i];

                if (arg.StartsWith("--"))
                {
                    // Handle long form arguments (--key=value or --key value)
                    string key = arg.Substring(2);

                    if (key.Contains('='))
                    {
                        // --key=value format
                        var parts = key.Split(new[] { '=' }, 2);
                        _arguments[parts[0]] = parts[1];
                    }
                    else if (i + 1 < args.Length && !args[i + 1].StartsWith("-"))
                    {
                        // --key value format
                        _arguments[key] = args[i + 1];
                        i++;
                    }
                    else
                    {
                        // Flag (--flag)
                        _flags.Add(key);
                    }
                }
                else if (arg.StartsWith("-"))
                {
                    // Handle short form arguments (-k value or -k=value or -f)
                    string key = arg.Substring(1);

                    if (key.Contains('='))
                    {
                        // -k=value format
                        var parts = key.Split(new[] { '=' }, 2);
                        _arguments[parts[0]] = parts[1];
                    }
                    else if (i + 1 < args.Length && !args[i + 1].StartsWith("-"))
                    {
                        // -k value format
                        _arguments[key] = args[i + 1];
                        i++;
                    }
                    else
                    {
                        // Flag (-f)
                        _flags.Add(key);
                    }
                }
                else
                {
                    _positionals.Add(arg);
                }
            }
        }

        /// <summary>
        /// Gets the value of a named argument
        /// </summary>
        /// <param name="name">Argument name</param>
        /// <returns>Argument value or null if not found</returns>
        public string GetArgument(string name)
        {
            if (_arguments.TryGetValue(name, out string? value))
                return value;

            return "";
        }
        public bool HasArgument(string name)
        {
            return _arguments.ContainsKey(name);
        }

        /// <summary>
        /// Checks if a flag is present
        /// </summary>
        /// <param name="name">Flag name</param>
        /// <returns>True if flag exists</returns>
        public bool HasFlag(string name)
        {
            return _flags.Contains(name);
        }

        /// <summary>
        /// Gets all parsed arguments
        /// </summary>
        public IReadOnlyDictionary<string, string> Arguments => _arguments;

        /// <summary>
        /// Gets all parsed flags
        /// </summary>
        public IReadOnlyCollection<string> Flags => _flags;
        public IReadOnlyList<string> Positionals => _positionals;
    }
}
