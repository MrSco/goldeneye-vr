using System;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using System.Windows.Forms;

namespace TacticalEditor
{
    internal sealed partial class EditorForm
    {
        private sealed class Mount
        {
            public int Index, Part;
            public string Name, Path;
        }

        private sealed class Part
        {
            public int Id, Conflict;
            public bool Fits;
        }

        private readonly ListBox gunList = Theme.List();
        private readonly TextBox gunFilter = Theme.Input(80);
        private readonly TextBox partFilter = Theme.Input(80);
        private readonly TextBox gunPresetName = Theme.Input(24);
        private readonly ComboBox gunPresets = Theme.Combo();
        private readonly DataGridView mounts = Theme.Grid("Slot", "Fitted part");
        private readonly DataGridView parts = Theme.Grid("Part", "Fit");
        private readonly CheckBox showIncompatible = Theme.Check("Show incompatible parts");
        private readonly Label gunTitle = Theme.Caption("", 1);
        private readonly Label gunDetails = Theme.Note("");
        private readonly Label gunStats = Theme.Note("");
        private readonly Label partTitle = Theme.Caption("Attachments", 1);
        private readonly Label partDetails = Theme.Note("");
        private readonly PreviewCanvas gunPreview = new PreviewCanvas { Height = 198, Dock = DockStyle.Top };
        private readonly PreviewCanvas partPreview = new PreviewCanvas { Height = 155, Dock = DockStyle.Top };
        private Button installPart;
        private Button saveGunPreset, deleteGunPreset;
        private string loadedGunPreset;
        private ulong loadedGunRevision;
        private int gunKey = -1;

        private Control BuildGunScreen()
        {
            var rail = Theme.Stack(14);
            Theme.Row(rail, Theme.Caption("Weapon library", 1));
            Theme.Field(rail, "Find a gun", gunFilter);
            Theme.Row(rail, gunList, true);
            Theme.Field(rail, "Saved builds", gunPresets);
            deleteGunPreset = Theme.Button("Delete", false, delegate { Perform(DeleteGunPreset); });
            deleteGunPreset.AccessibleName = "Delete saved gun build";
            gunPresets.SelectedIndexChanged += delegate { UpdateGunDeleteButton(); };
            Theme.Row(rail, Theme.Actions(Theme.Button("Load", false, LoadGunPreset), deleteGunPreset));
            Theme.Row(rail, Theme.Actions(Theme.Button("Import file…", false, delegate { Perform(ImportGunPreset); })));

            var assembly = Theme.Stack(16);
            Theme.Row(assembly, gunTitle);
            Theme.Row(assembly, gunDetails);
            Theme.Row(assembly, gunStats);
            Theme.Row(assembly, gunPreview);
            mounts.Columns[0].FillWeight = 38;
            mounts.Columns[1].FillWeight = 62;
            Theme.Row(assembly, mounts, true);
            Theme.Field(assembly, "Preset name", gunPresetName);
            saveGunPreset = Theme.Button("Save to Workshop", true, delegate { Perform(SaveGunPreset); });
            gunPresetName.TextChanged += delegate { UpdateGunSaveLabel(); };
            Theme.Row(assembly, Theme.Actions(saveGunPreset,
                Theme.Button("Export file…", false, delegate { Perform(ExportGunPreset); }),
                Theme.Button("Reset to stock", false, ResetGun)));

            var options = Theme.Stack(16);
            Theme.Row(options, partTitle);
            Theme.Row(options, partPreview);
            Theme.Field(options, "Find a part", partFilter);
            Theme.Row(options, showIncompatible);
            parts.Columns[0].FillWeight = 77;
            parts.Columns[1].FillWeight = 23;
            Theme.Row(options, parts, true);
            partDetails.MaximumSize = new System.Drawing.Size(305, 0);
            Theme.Row(options, partDetails);
            installPart = Theme.Button("Fit selected part", true, InstallGunPart);
            Theme.Row(options, installPart);

            gunFilter.TextChanged += delegate { RefreshGunList(gunKey); };
            gunList.SelectedIndexChanged += delegate {
                var selected = gunList.SelectedItem as Choice;
                if (syncing == 0 && selected != null) LoadGun(selected.Id);
            };
            mounts.SelectionChanged += delegate {
                if (syncing == 0) { partFilter.Clear(); RefreshParts(); }
            };
            partFilter.TextChanged += delegate { if (syncing == 0) RefreshParts(); };
            showIncompatible.CheckedChanged += delegate { RefreshParts(); };
            parts.SelectionChanged += delegate { if (syncing == 0) UpdatePartDetails(); };
            parts.CellDoubleClick += delegate { InstallGunPart(); };
            RefreshGunList(guns.First().Id);
            LoadGun(guns.First().Id);
            return Theme.Columns(235, 54, rail, assembly, options);
        }

        private void RefreshGunList(int selected)
        {
            Sync(delegate {
                gunList.Items.Clear();
                foreach (var g in guns.Where(g => Filter(g.Name, gunFilter.Text)))
                {
                    var index = gunList.Items.Add(g);
                    if (g.Id == selected) gunList.SelectedIndex = index;
                }
            });
        }

        private void LoadGun(int key)
        {
            gunKey = key;
            gunTitle.Text = Workshop.GunName(key);
            TrackGunPreset(null);
            partFilter.Clear();
            RefreshGunPresets(null);
            RefreshMounts(null);
        }

        private void RefreshGunPresets(string select)
        {
            if (gunKey < 0) return;
            var saved = select ?? gunPresets.SelectedItem as string;
            gunPresets.Items.Clear();
            foreach (var name in Workshop.Presets(gunKey)) gunPresets.Items.Add(name);
            if (saved != null && gunPresets.Items.Contains(saved)) gunPresets.SelectedItem = saved;
            else gunPresets.SelectedIndex = gunPresets.Items.Count > 0 ? 0 : -1;
            UpdateGunDeleteButton();
        }

        private void UpdateGunDeleteButton()
        {
            deleteGunPreset.Enabled = gunPresets.SelectedItem is string;
        }

        private void RefreshMounts(string target)
        {
            if (gunKey < 0) return;
            var buffer = new int[Workshop.EditorCapacity()];
            Sync(delegate {
                mounts.Rows.Clear();
                var line = -1;
                for (int i = 1, count = Workshop.EditorMountCount(gunKey); i < count; ++i)
                {
                    if (Workshop.EditorOptions(gunKey, i, buffer) <= 0) continue;
                    var slot = new Mount { Index = i, Name = Workshop.MountName(gunKey, i),
                        Path = Workshop.MountPath(gunKey, i), Part = Workshop.EditorMountedPart(gunKey, i) };
                    var row = mounts.Rows.Add(slot.Name, Workshop.PartName(slot.Part));
                    mounts.Rows[row].Tag = slot;
                    mounts.Rows[row].Cells[0].ToolTipText = slot.Path;
                    mounts.Rows[row].Cells[1].ToolTipText = Workshop.PartName(slot.Part);
                    if (slot.Path == target) line = row;
                }
                if (mounts.RowCount > 0) mounts.CurrentCell = mounts.Rows[Math.Max(0, line)].Cells[0];
            });
            var role = Workshop.EditorIsSecondary(gunKey) != 0 ? "Secondary" : "Primary";
            gunDetails.Text = role + "   •   " + mounts.RowCount + " configurable slots"
                + (Workshop.EditorIsSuppressed(gunKey) != 0 ? "   •   Suppressed" : "");
            Workshop.WeaponStats stats;
            if (Workshop.EditorWeaponStats(gunKey, out stats) != 0)
            {
                var modes = new[] { "Semi", "Burst", "Auto" };
                var available = modes.Where((mode, index) => (stats.Modes & (1 << index)) != 0);
                var rate = stats.Rpm > 0 ? stats.Rpm + " RPM cyclic"
                    : stats.SemiRpm + " RPM semi limit";
                var gun = Workshop.GunName(gunKey);
                var damage = gun.StartsWith("RPG") || gun.StartsWith("Milkor") ? "Projectile"
                    : string.Format("Damage {0:F2}", stats.Damage);
                gunStats.Text = string.Join(" / ", available) + "   •   " + rate + "\n"
                    + string.Format("{0:F2} kg   •   Ergo {1:F0}   •   Recoil {2:F0}   •   Spread {3:F2}°   •   {4}",
                        stats.Mass, stats.Ergo, stats.Recoil * 100, stats.Spread, damage);
            }
            else gunStats.Text = "Weapon stats unavailable";
            gunPreview.ShowScene(preview.Gun(gunKey, null));
            RefreshParts();
        }

        private void RefreshParts()
        {
            var slot = SelectedRow<Mount>(mounts);
            Sync(delegate {
                parts.Rows.Clear();
                if (slot == null) return;
                var buffer = new int[Workshop.EditorCapacity()];
                var count = Workshop.EditorOptions(gunKey, slot.Index, buffer);
                for (int i = 0; i < count; ++i)
                {
                    var id = buffer[i];
                    var name = Workshop.PartName(id);
                    if (!Filter(name, partFilter.Text)) continue;
                    int conflict;
                    var fits = Workshop.EditorFit(gunKey, slot.Index, id, out conflict) != 0;
                    if (!fits && !showIncompatible.Checked && id != slot.Part) continue;
                    var row = parts.Rows.Add(name, id == slot.Part ? "Fitted" : fits ? "Fits" : "Blocked");
                    parts.Rows[row].Tag = new Part { Id = id, Conflict = conflict, Fits = fits };
                    parts.Rows[row].Cells[0].ToolTipText = name;
                }
                if (parts.RowCount > 0) parts.CurrentCell = parts.Rows[0].Cells[0];
            });
            partTitle.Text = slot == null ? "Attachments" : slot.Name;
            UpdatePartDetails();
        }

        private void UpdatePartDetails()
        {
            var part = SelectedRow<Part>(parts);
            var slot = SelectedRow<Mount>(mounts);
            partPreview.ShowScene(preview.Attachment(gunKey, part == null ? -1 : part.Id), "Select an attachment to preview it.");
            installPart.Enabled = part != null && slot != null && part.Fits && part.Id != slot.Part;
            if (part == null || slot == null)
            {
                partDetails.Text = slot == null ? "This build has no adjustable slots." : "No parts match this search.";
                return;
            }
            var detail = Workshop.PartName(part.Id);
            var capacity = Workshop.EditorPartCapacity(part.Id);
            if (capacity > 0) detail += "\nCapacity " + capacity + " rounds";
            if (!part.Fits && part.Id != slot.Part)
                detail += part.Conflict >= 0 ? "\nConflicts with " + Workshop.PartName(part.Conflict) : "\nDoes not fit this assembly";
            partDetails.Text = detail;
        }

        private void InstallGunPart()
        {
            var slot = SelectedRow<Mount>(mounts);
            var part = SelectedRow<Part>(parts);
            if (slot == null || part == null || !part.Fits || part.Id == slot.Part) return;
            int conflict;
            if (Workshop.EditorInstall(gunKey, slot.Index, part.Id, out conflict) == 0)
            {
                Status("Could not install that part on the current assembly.", true);
                return;
            }
            RefreshMounts(slot.Path);
            Status("Updated " + slot.Name + " on " + Workshop.GunName(gunKey) + ".");
        }

        private void ResetGun()
        {
            Workshop.EditorReset(gunKey);
            partFilter.Clear();
            RefreshMounts(null);
            Status("Restored the stock " + Workshop.GunName(gunKey) + " in the editor.");
        }

        private void LoadGunPreset()
        {
            var name = gunPresets.SelectedItem as string;
            Workshop.EditorRefresh();
            if (name == null || Workshop.EditorLoad(gunKey, name) == 0)
            {
                Status("Choose a compatible saved build for this gun.", true);
                return;
            }
            TrackGunPreset(name);
            GunLibraryChanged(name);
            RefreshMounts(null);
            Status("Loaded " + name + ".");
        }

        private void TrackGunPreset(string name)
        {
            loadedGunPreset = name;
            loadedGunRevision = name == null ? 0 : Workshop.EditorPresetRevision(gunKey, name);
            gunPresetName.Text = name ?? "";
            UpdateGunSaveLabel();
        }

        private bool IsGunPresetUpdate(string name)
        {
            return loadedGunRevision != 0 && name == loadedGunPreset;
        }

        private void UpdateGunSaveLabel()
        {
            saveGunPreset.Text = IsGunPresetUpdate(gunPresetName.Text.Trim()) ? "Update in Workshop" : "Save to Workshop";
        }

        private string ReadGunPresetName()
        {
            var name = gunPresetName.Text.Trim();
            if (name == "-" || !Regex.IsMatch(name, @"\A[A-Za-z0-9 _-]{1,24}\z"))
                throw new InvalidDataException("Use 1–24 letters, numbers, spaces, hyphens or underscores for the preset name.");
            return name;
        }

        private static void CheckGunSave(int result)
        {
            if (result == 1) return;
            if (result == -1) throw new InvalidDataException("Load that preset to edit it, or enter a new name.");
            if (result == -2) throw new InvalidDataException("The in-game Workshop's 48 user preset slots are full.");
            if (result == -3) throw new InvalidDataException("That preset changed elsewhere. Reload it or save this build under a new name.");
            throw new IOException("Could not save the Workshop preset. Check that the file is valid and try again.");
        }

        private void GunLibraryChanged(string select)
        {
            RefreshGunPresets(select);
            guardGun.RefreshPresets();
            missionPrimary.RefreshPresets();
            missionSecondary.RefreshPresets();
            UpdateGuardDetails();
            RefreshMissionPreview();
        }

        private void SaveGunPreset()
        {
            var name = ReadGunPresetName();
            var updating = IsGunPresetUpdate(name);
            CheckGunSave(updating ? Workshop.EditorUpdate(gunKey, name, loadedGunRevision)
                : Workshop.EditorSave(gunKey, name));
            TrackGunPreset(name);
            GunLibraryChanged(name);
            Status((updating ? "Updated " : "Saved ") + name + " in the shared Workshop library.");
        }

        private void DeleteGunPreset()
        {
            var name = gunPresets.SelectedItem as string;
            if (name == null) return;
            var key = gunKey;
            Workshop.EditorRefresh();
            var revision = Workshop.EditorPresetRevision(key, name);
            if (revision == 0)
            {
                GunLibraryChanged(null);
                Status("That saved build is no longer available.", true);
                return;
            }
            var question = "Delete “" + name + "” for the " + Workshop.GunName(key) + " from the shared Workshop?";
            if (MessageBox.Show(this, question, "Delete saved build", MessageBoxButtons.YesNo,
                MessageBoxIcon.Question, MessageBoxDefaultButton.Button2) != DialogResult.Yes) return;

            store.DeleteGunPreset(key, name, revision);
            if (gunKey == key && loadedGunPreset == name)
            {
                loadedGunPreset = null;
                loadedGunRevision = 0;
                if (gunPresetName.Text.Trim() == name) gunPresetName.Clear();
                UpdateGunSaveLabel();
            }
            RefreshWorkshopPresets();
            Status("Deleted " + name + " from the shared Workshop.");
        }

        private void ExportGunPreset()
        {
            var name = ReadGunPresetName();
            using (var choose = new SaveFileDialog { Filter = "GoldenEye Tactical preset (*.gevrpreset)|*.gevrpreset",
                DefaultExt = "gevrpreset", FileName = name + ".gevrpreset", RestoreDirectory = true })
            {
                if (choose.ShowDialog(this) != DialogResult.OK) return;
                var destination = choose.FileName;
                var temp = destination + "." + Guid.NewGuid().ToString("N") + ".tmp";
                try
                {
                    if (Workshop.EditorExport(gunKey, name, temp) == 0)
                        throw new IOException("Could not export the gun preset.");
                    if (File.Exists(destination)) File.Replace(temp, destination, null);
                    else File.Move(temp, destination);
                }
                finally { if (File.Exists(temp)) File.Delete(temp); }
                Status("Exported " + Path.GetFileName(destination) + ".");
            }
        }

        private void ImportGunPreset()
        {
            using (var choose = new OpenFileDialog { Filter = "GoldenEye Tactical preset (*.gevrpreset)|*.gevrpreset",
                RestoreDirectory = true })
            {
                if (choose.ShowDialog(this) != DialogResult.OK) return;
                Workshop.EditorRefresh();
                var before = Workshop.Presets(gunKey);
                var result = Workshop.EditorImport(gunKey, choose.FileName);
                if (result == 0) throw new InvalidDataException("This file cannot be imported for the selected gun.");
                CheckGunSave(result);
                var name = Workshop.Presets(gunKey).Except(before).FirstOrDefault();
                TrackGunPreset(name);
                GunLibraryChanged(name);
                RefreshMounts(null);
                Status("Imported " + (name ?? Path.GetFileName(choose.FileName)) + " into the in-game Workshop library.");
            }
        }
    }
}
