using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Windows.Forms;

namespace TacticalEditor
{
    internal sealed partial class EditorForm : Form
    {
        private readonly string gameFolder;
        private FileSystemWatcher presetWatcher;
        private readonly PreviewLibrary preview;
        private readonly CampaignCatalogue catalogue;
        private readonly List<Choice> guns = new List<Choice>();
        private LoadoutStore store;
        private int syncing;
        private readonly Panel pages = new Panel { Dock = DockStyle.Fill, Margin = new Padding(0) };
        private readonly Label status = Theme.Note("");
        private readonly List<Control> screens = new List<Control>();
        private readonly List<Button> tabs = new List<Button>();

        public EditorForm(string path)
        {
            gameFolder = path;
            preview = new PreviewLibrary(path);
            catalogue = new CampaignCatalogue(Path.Combine(path, "tactical-editor-catalogue.tsv"));
            for (int key = 0; key < Workshop.EditorGunCount(); ++key)
            {
                var name = Workshop.GunName(key);
                if (name != null) guns.Add(new Choice(key, name));
            }
            if (guns.Count == 0)
                throw new InvalidDataException("The Tactical Workshop catalogue is missing. Keep the editor beside the game.");
            guns.Sort((a, b) => StringComparer.OrdinalIgnoreCase.Compare(a.Name, b.Name));
            store = new LoadoutStore(Path.Combine(path, "tactical-loadouts.txt"));

            Text = "GoldenEye Tactical  /  Loadout Editor";
            BackColor = Theme.Base;
            ForeColor = Theme.Text;
            Font = Theme.Normal;
            MinimumSize = new Size(1100, 720);
            ClientSize = new Size(1250, 765);
            StartPosition = FormStartPosition.CenterScreen;
            AutoScaleMode = AutoScaleMode.Dpi;

            var layout = Theme.Stack(20);
            layout.BackColor = Theme.Base;
            layout.Margin = new Padding(0);
            Theme.Row(layout, Theme.Caption("GoldenEye Tactical", 2));
            var nav = Theme.Actions();
            foreach (var name in new[] { "Guard presets", "Gun presets", "Missions" })
            {
                var page = tabs.Count;
                var button = Theme.Button(name, false, delegate { ActivatePage(page); });
                tabs.Add(button);
                nav.Controls.Add(button);
            }
            Theme.Row(layout, nav);
            Theme.Row(layout, pages, true);
            status.Margin = new Padding(0, 11, 0, 0);
            status.AutoEllipsis = true;
            status.AutoSize = false;
            status.Height = 23;
            status.Dock = DockStyle.Top;
            Theme.Row(layout, status);
            Controls.Add(layout);

            screens.Add(BuildGuardScreen());
            screens.Add(BuildGunScreen());
            screens.Add(BuildMissionScreen());
            ActivatePage(0);
            Status(catalogue.Missions.Count + " missions   •   " + guns.Count + " gun configurations   •   "
                + catalogue.Bodies.Count + " NPC types");
        }

        private void ActivatePage(int index)
        {
            pages.SuspendLayout();
            pages.Controls.Clear();
            pages.Controls.Add(screens[index]);
            for (int i = 0; i < tabs.Count; ++i)
            {
                tabs[i].BackColor = i == index ? Theme.Accent : Theme.Selected;
                tabs[i].ForeColor = i == index ? Theme.Base : Theme.Text;
            }
            pages.ResumeLayout();
            RefreshWorkshopPresets();
            if (index == 2) RefreshMissionAssignments();
        }

        protected override void OnShown(EventArgs e)
        {
            base.OnShown(e);
            presetWatcher = new FileSystemWatcher(gameFolder, "tactical-presets*.txt") {
                NotifyFilter = NotifyFilters.FileName | NotifyFilters.LastWrite,
                SynchronizingObject = this
            };
            presetWatcher.Changed += PresetFileChanged;
            presetWatcher.Created += PresetFileChanged;
            presetWatcher.Deleted += PresetFileChanged;
            presetWatcher.Renamed += PresetFileChanged;
            presetWatcher.EnableRaisingEvents = true;
            RefreshWorkshopPresets();
        }

        protected override void OnFormClosed(FormClosedEventArgs e)
        {
            if (presetWatcher != null) presetWatcher.Dispose();
            base.OnFormClosed(e);
        }

        private void PresetFileChanged(object sender, FileSystemEventArgs e)
        {
            if (!IsDisposed && !Disposing) RefreshWorkshopPresets();
        }

        private void RefreshWorkshopPresets()
        {
            Workshop.EditorRefresh();
            GunLibraryChanged(null);
        }

        private void Sync(Action action)
        {
            ++syncing;
            try { action(); }
            finally { --syncing; }
        }

        private void Perform(Action action)
        {
            try { action(); }
            catch (Exception error) { Status(error.Message, true); }
        }

        private void Status(string message, bool error = false)
        {
            status.ForeColor = error ? Theme.Error : Theme.Muted;
            status.Text = message;
        }

        private string GunName(GunRef gun)
        {
            if (gun.Key < 0) return "Mission weapon";
            var label = guns.FirstOrDefault(x => x.Id == gun.Key);
            return (label == null ? "Unavailable gun" : label.Name) + (gun.Preset == "-" ? "" : "  /  " + gun.Preset);
        }

        private static bool Filter(string name, string search)
        {
            return name.IndexOf(search.Trim(), StringComparison.OrdinalIgnoreCase) >= 0;
        }

        private static T SelectedRow<T>(DataGridView grid) where T : class
        {
            return grid.SelectedRows.Count == 0 ? null : grid.SelectedRows[0].Tag as T;
        }
    }
}
