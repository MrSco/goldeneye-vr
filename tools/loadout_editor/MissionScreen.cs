using System;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Windows.Forms;

namespace TacticalEditor
{
    internal sealed partial class EditorForm
    {
        private readonly ListBox missionList = Theme.List();
        private readonly Label missionTitle = Theme.Caption("", 1);
        private readonly Label missionActors = Theme.Note("");
        private readonly ComboBox missionBody = Theme.Combo(true);
        private readonly ComboBox missionTarget = Theme.Combo(true);
        private readonly ComboBox missionPreset = Theme.Combo(true);
        private readonly DataGridView assignmentGrid = Theme.Grid("Mission target", "Guard preset");
        private readonly CheckBox customKit = Theme.Check("Set a starting kit for this mission");
        private readonly Label kitHint = Theme.Note("The kit opens in Select Gear before deployment.");
        private readonly PreviewCanvas missionPreview = new PreviewCanvas();
        private readonly ComboBox missionPreviewMode = Theme.Combo();
        private GunPicker missionPrimary, missionSecondary;
        private Button assignButton, saveKitButton;

        private Mission CurrentMission { get { return missionList.SelectedItem as Mission; } }

        private Control BuildMissionScreen()
        {
            var list = Theme.Stack(14);
            Theme.Row(list, Theme.Caption("Campaign missions", 1));
            Theme.Row(list, missionList, true);

            var assignments = Theme.Stack(17);
            Theme.Row(assignments, missionTitle);
            Theme.Row(assignments, missionActors);
            Theme.Field(assignments, "NPC type", missionBody);
            Theme.Field(assignments, "Apply to", missionTarget);
            Theme.Field(assignments, "Guard preset", missionPreset);
            assignButton = Theme.Button("Apply guard preset", true, delegate { Perform(ApplyMissionGuard); });
            Theme.Row(assignments, assignButton);
            Theme.Row(assignments, assignmentGrid, true);
            Theme.Row(assignments, Theme.Button("Remove selected assignment", false, delegate { Perform(RemoveAssignment); }));

            var kit = Theme.Stack(0);
            Theme.Row(kit, Theme.Caption("Player starting kit", 1));
            Theme.Row(kit, customKit);
            missionPrimary = new GunPicker(guns.Where(g => Workshop.EditorIsSecondary(g.Id) == 0));
            missionSecondary = new GunPicker(guns.Where(g => Workshop.EditorIsSecondary(g.Id) != 0));
            Theme.Field(kit, "Primary and saved build", missionPrimary);
            Theme.Field(kit, "Secondary and saved build", missionSecondary);
            saveKitButton = Theme.Button("Save starting kit", true, delegate { Perform(SaveMissionKit); });
            Theme.Row(kit, saveKitButton);
            kitHint.MaximumSize = new Size(310, 0);
            Theme.Row(kit, kitHint);
            var rule = Theme.Note("Assignments for individual guards take precedence over their NPC type.");
            rule.MaximumSize = new Size(310, 0);
            Theme.Row(assignments, rule);

            var previewPanel = Theme.Stack(0);
            Theme.Fill(missionPreviewMode, new[] { new Choice(0, "Selected guard"), new Choice(1, "Starting primary"),
                new Choice(2, "Starting secondary"), new Choice(3, "Mission layout and guard positions") }, 0);
            Theme.Field(previewPanel, "Preview mode", missionPreviewMode);
            Theme.Row(previewPanel, missionPreview, true);
            Theme.Row(previewPanel, Theme.Note("Drag to orbit · Scroll to zoom · Double-click to reset"));

            var side = Theme.Stack(16);
            var sideContent = new Panel { Dock = DockStyle.Fill, Margin = new Padding(0) };
            var previewTab = Theme.Button("3D preview", true, delegate { });
            var kitTab = Theme.Button("Starting kit", false, delegate { });
            previewPanel.Margin = kit.Margin = new Padding(0);
            sideContent.Controls.Add(previewPanel);
            sideContent.Controls.Add(kit);
            kit.Visible = false;
            previewTab.Click += delegate {
                kit.Visible = false;
                previewPanel.Visible = true;
                previewTab.BackColor = Theme.Accent;
                previewTab.ForeColor = Theme.Base;
                kitTab.BackColor = Theme.Selected;
                kitTab.ForeColor = Theme.Text;
            };
            kitTab.Click += delegate {
                kit.Visible = true;
                previewPanel.Visible = false;
                kitTab.BackColor = Theme.Accent;
                kitTab.ForeColor = Theme.Base;
                previewTab.BackColor = Theme.Selected;
                previewTab.ForeColor = Theme.Text;
            };
            Theme.Row(side, Theme.Actions(previewTab, kitTab));
            Theme.Row(side, sideContent, true);

            foreach (var mission in catalogue.Missions) missionList.Items.Add(mission);
            missionList.SelectedIndexChanged += delegate { LoadMission(); };
            missionBody.SelectedIndexChanged += delegate { if (syncing == 0) RefreshMissionTargets(-1); };
            missionTarget.SelectedIndexChanged += delegate { if (syncing == 0) RefreshMissionGuardChoices(null); };
            missionPreset.SelectedIndexChanged += delegate {
                assignButton.Enabled = missionPreset.SelectedIndex >= 0;
                RefreshMissionPreview();
            };
            missionPreviewMode.SelectedIndexChanged += delegate { RefreshMissionPreview(); };
            missionPrimary.SelectionChanged += delegate { RefreshMissionPreview(); };
            missionSecondary.SelectionChanged += delegate { RefreshMissionPreview(); };
            customKit.CheckedChanged += delegate { RefreshKitEnabled(); };
            assignmentGrid.CellDoubleClick += delegate { EditSelectedAssignment(); };
            missionList.SelectedIndex = 0;
            return Theme.Columns(235, 52, list, assignments, side);
        }

        private void LoadMission()
        {
            var mission = CurrentMission;
            if (mission == null) return;
            missionTitle.Text = mission.Name;
            missionActors.Text = mission.Actors.Count + " placed actors in the mission setup";
            Sync(delegate {
                var placed = mission.Actors.Select(a => a.Body).ToLookup(b => b);
                var options = catalogue.Bodies.OrderBy(b => placed.Contains(b.Id) ? 0 : 1).ThenBy(b => b.Name);
                Theme.Fill(missionBody, options, options.First().Id);
                StartingKit kit;
                customKit.Checked = store.Kits.TryGetValue(mission.Id, out kit)
                    && Workshop.EditorMissionAllowsStartingKit(mission.Id) != 0;
                missionPrimary.Value = kit == null ? new GunRef(4, "-") : kit.Primary;
                missionSecondary.Value = kit == null ? new GunRef(8, "-") : kit.Secondary;
            });
            RefreshKitEnabled();
            RefreshMissionTargets(-1);
            RefreshMissionAssignments();
        }

        private void RefreshKitEnabled()
        {
            var mission = CurrentMission;
            var allowed = mission != null && Workshop.EditorMissionAllowsStartingKit(mission.Id) != 0;
            customKit.Enabled = saveKitButton.Enabled = allowed;
            missionPrimary.Enabled = missionSecondary.Enabled = allowed && customKit.Checked;
            kitHint.Text = !allowed ? "This mission starts unarmed with the watch magnet."
                : customKit.Checked ? "The kit opens in Select Gear before deployment." : "The mission uses its usual starting gear.";
            RefreshMissionPreview();
        }

        private void RefreshMissionTargets(int selected)
        {
            var mission = CurrentMission;
            if (mission == null) return;
            var body = Theme.Id(missionBody);
            var actors = mission.Actors.Where(a => a.Body == body).OrderBy(a => a.Id);
            var scope = actors.Any() ? "All of this NPC type" : "All of this NPC type (scripted spawns)";
            var available = new[] { new Choice(-1, scope) }.Concat(actors.Select(a => new Choice(a.Id, a.Name + "  #" + a.Id)));
            Sync(delegate { Theme.Fill(missionTarget, available, selected); });
            RefreshMissionGuardChoices(null);
        }

        private void RefreshMissionGuardChoices(string selected)
        {
            var mission = CurrentMission;
            if (mission == null) return;
            var body = Theme.Id(missionBody);
            var actor = Theme.Id(missionTarget);
            var rule = store.Assignments.FirstOrDefault(a => a.Stage == mission.Id && a.Body == body && a.Actor == actor);
            selected = selected ?? (rule == null ? null : rule.Guard);
            missionPreset.Items.Clear();
            foreach (var guard in store.Guards.Where(g => g.Body == body).OrderBy(g => g.Tier).ThenBy(g => g.Name))
            {
                var index = missionPreset.Items.Add(guard);
                if (guard.Id == selected) missionPreset.SelectedIndex = index;
            }
            if (missionPreset.SelectedIndex < 0 && missionPreset.Items.Count > 0) missionPreset.SelectedIndex = 0;
            assignButton.Enabled = missionPreset.SelectedIndex >= 0;
            RefreshMissionPreview();
        }

        private void RefreshMissionPreview()
        {
            if (syncing != 0 || CurrentMission == null) return;
            var mode = Theme.Id(missionPreviewMode);
            if (mode == 3)
            {
                missionPreview.ShowScene(preview.MissionLayout(CurrentMission, Theme.Id(missionBody), Theme.Id(missionTarget)),
                    "Mission layout unavailable.", true);
                return;
            }
            if (mode == 1 || mode == 2)
            {
                if (!customKit.Checked)
                {
                    missionPreview.ShowScene(new System.Collections.Generic.List<PreviewPiece>(),
                        customKit.Enabled ? "Set a starting kit for this mission to preview its guns." : kitHint.Text);
                    return;
                }
                var gun = (mode == 1 ? missionPrimary : missionSecondary).Value;
                missionPreview.ShowScene(preview.Gun(gun.Key, gun.Preset));
                return;
            }
            var guard = missionPreset.SelectedItem as GuardPreset;
            missionPreview.ShowScene(preview.Guard(Theme.Id(missionBody), guard == null ? 0 : guard.Vest,
                guard == null ? -1 : guard.Helmet));
        }

        private void RefreshMissionAssignments()
        {
            var mission = CurrentMission;
            if (mission == null) return;
            RefreshMissionGuardChoices(null);
            assignmentGrid.Rows.Clear();
            foreach (var entry in store.Assignments.Where(a => a.Stage == mission.Id)
                .OrderBy(a => catalogue.BodyName(a.Body)).ThenBy(a => a.Actor))
            {
                var preset = store.Guards.First(g => g.Id == entry.Guard);
                var row = assignmentGrid.Rows.Add(catalogue.TargetName(mission, entry.Body, entry.Actor), preset.ToString());
                assignmentGrid.Rows[row].Tag = entry;
            }
        }

        private void EditSelectedAssignment()
        {
            var a = SelectedRow<Assignment>(assignmentGrid);
            if (a == null) return;
            Sync(delegate { Theme.Select(missionBody, a.Body); });
            RefreshMissionTargets(a.Actor);
            RefreshMissionGuardChoices(a.Guard);
        }

        private void ApplyMissionGuard()
        {
            var g = missionPreset.SelectedItem as GuardPreset;
            if (g == null) throw new InvalidDataException("Save a guard preset for this NPC type first.");
            var mission = CurrentMission;
            var body = Theme.Id(missionBody);
            var actor = Theme.Id(missionTarget);
            store = store.Update(draft => {
                Workshop.EditorRefresh();
                ValidatePreset(g.Gun);
                draft.Assignments.RemoveAll(a => a.Stage == mission.Id && a.Body == body && a.Actor == actor);
                draft.Assignments.Add(new Assignment { Stage = mission.Id, Body = body, Actor = actor, Guard = g.Id });
            });
            RefreshMissionAssignments();
            UpdateGuardDetails();
            Status("Applied " + g.Name + " to " + catalogue.TargetName(mission, body, actor) + " in " + mission.Name + ".");
        }

        private void RemoveAssignment()
        {
            var a = SelectedRow<Assignment>(assignmentGrid);
            if (a == null) return;
            store = store.Update(draft => draft.Assignments.Remove(a));
            RefreshMissionAssignments();
            UpdateGuardDetails();
            Status("Removed the selected assignment for " + CurrentMission.Name + ".");
        }

        private void SaveMissionKit()
        {
            var mission = CurrentMission;
            if (mission == null || Workshop.EditorMissionAllowsStartingKit(mission.Id) == 0) return;
            var primary = missionPrimary.Value;
            var secondary = missionSecondary.Value;
            store = store.Update(draft => {
                if (customKit.Checked)
                {
                    Workshop.EditorRefresh();
                    ValidatePreset(primary);
                    ValidatePreset(secondary);
                    draft.Kits[mission.Id] = new StartingKit { Primary = primary, Secondary = secondary };
                }
                else draft.Kits.Remove(mission.Id);
            });
            Status(customKit.Checked ? "Saved the starting kit for " + mission.Name + "."
                : "Restored the default starting gear for " + mission.Name + ".");
        }
    }
}
