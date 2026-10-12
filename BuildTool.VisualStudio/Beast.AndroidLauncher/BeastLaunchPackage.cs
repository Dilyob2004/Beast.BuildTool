using System;
using System.ComponentModel.Design;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using System.Xml;
using EnvDTE;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Shell.Interop;

namespace Beast.AndroidLauncher;

[PackageRegistration(UseManagedResourcesOnly = true, AllowsBackgroundLoading = true)]
[ProvideAutoLoad(UIContextGuids80.SolutionExists, PackageAutoLoadFlags.BackgroundLoad)]
[ProvideMenuResource("Menus.ctmenu", 1)]
[Guid(PackageGuid)]
public sealed class BeastLaunchPackage : AsyncPackage
{
    public const string PackageGuid = "eb4376b4-bb07-46fd-b8d8-597e39135427";
    private static readonly Guid Commands = new Guid("a541cd71-494c-4fd3-abfb-5b2f6bbd1668");
    private DTE? dte;

    protected override async Task InitializeAsync(CancellationToken cancellationToken, IProgress<ServiceProgressData> progress)
    {
        await JoinableTaskFactory.SwitchToMainThreadAsync(cancellationToken);
        var automation = await GetServiceAsync(typeof(DTE)) as DTE;
        if (automation == null) return;
        dte = automation;
        var menu = await GetServiceAsync(typeof(IMenuCommandService)) as OleMenuCommandService;
        if (menu == null) return;
        var command = new OleMenuCommand(Execute, new CommandID(Commands, 0x0100));
        command.BeforeQueryStatus += (_, __) =>
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            try {
                var launch = SelectedLaunch();
                command.Visible = launch != null;
                command.Enabled = launch != null && CanRun() && !launch.IsWatching();
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException || e is COMException)
            { command.Visible = command.Enabled = false; }
        };
        menu.AddCommand(command);
    }
    private bool CanRun()
    {
        ThreadHelper.ThrowIfNotOnUIThread();
        return dte != null && dte.Debugger.CurrentMode == dbgDebugMode.dbgDesignMode &&
            dte.Solution.SolutionBuild.BuildState != vsBuildState.vsBuildStateInProgress;
    }
    private HotReloadLaunch? SelectedLaunch()
    {
        ThreadHelper.ThrowIfNotOnUIThread();
        try
        {
            // The right-clicked project, never the startup project or active document.
            if (dte == null || dte.SelectedItems.Count != 1) return null;
            var item = dte.SelectedItems.Item(1);
            if (item.ProjectItem != null || item.Project == null) return null;
            return HotReloadLaunch.Read(item.Project.FullName);
        }
        catch (Exception e) when (e is IOException || e is UnauthorizedAccessException || e is XmlException || e is ArgumentException || e is COMException)
        { return null; }
    }
    private void Execute(object sender, EventArgs args)
    {
        ThreadHelper.ThrowIfNotOnUIThread();
        try
        {
            var launch = SelectedLaunch();
            if (launch == null || !CanRun() || launch.IsWatching()) return;
            // Keep output visible after failed compilation; do not redirect into
            // the IDE or run a long-lived watcher on Visual Studio's UI thread.
            using var process = System.Diagnostics.Process.Start(launch.StartInfo());
            if (process == null) throw new InvalidOperationException("Could not start Beast Hot Reload.");
        }
        catch (Exception e)
        {
            VsShellUtilities.ShowMessageBox(this, e.Message, "Beast Hot Reload",
                OLEMSGICON.OLEMSGICON_CRITICAL, OLEMSGBUTTON.OLEMSGBUTTON_OK, OLEMSGDEFBUTTON.OLEMSGDEFBUTTON_FIRST);
        }
    }
}
