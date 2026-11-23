using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Windows.Forms;

namespace TacticalEditor
{
    internal sealed partial class EditorForm
    {
        private readonly ListBox guardList = Theme.List();
        private readonly TextBox guardFilter = Theme.Input(80);
        private readonly TextBox guardName = Theme.Input(48);
        private readonly ComboBox guardBody = Theme.Combo(true);
        private readonly ComboBox guardTier = Theme.Combo();
        private readonly ComboBox guardVest = Theme.Combo(true);
        private readonly ComboBox guardHelmet = Theme.Combo(true);
        private readonly Label guardTitle = Theme.Caption("", 1);
        private readonly Label guardDetails = Theme.Note("");
        private readonly Label guardMissions = Theme.Note("");
        private readonly PreviewCanvas gearPreview = new PreviewCanvas();
        private readonly ComboBox guardPreviewMode = Theme.Combo();
        private GunPicker guardGun;
        private string guardId;

        private Control BuildGuardScreen()
        {
            var library = Theme.Stack(14);
            Theme.Row(library, Theme.Caption("Guard library", 1));
            Theme.Field(library, "Find a preset", guardFilter);
            Theme.Row(library, guardList, true);
            Theme.Row(library, Theme.Button("New guard preset", true, delegate { LoadGuard(null); }));

            var form = Theme.Stack(18);
            Theme.Row(form, Theme.Caption("Guard preset", 1));
            Theme.Field(form, "Preset name", guardName);
            Theme.Field(form, "NPC type", guardBody);
            Theme.Field(form, "Weight class", guardTier);
            Theme.Field(form, "Armour", guardVest);
            Theme.Field(form, "Helmet", guardHelmet);
            guardGun = new GunPicker(new[] { new Choice(-1, "Keep mission weapon") }.Concat(guns));
            Theme.Field(form, "Weapon and saved build", guardGun);
            Theme.Row(form, Theme.Actions(
                Theme.Button("Save guard preset", true, delegate { Perform(SaveGuard); }),
                Theme.Button("Duplicate", false, DuplicateGuard),
                Theme.Button("Delete", false, delegate { Perform(DeleteGuard); })));

            var detail = Theme.Stack(16);
            guardDetails.MaximumSize = new Size(305, 0);
            guardMissions.MaximumSize = new Size(305, 0);
            Theme.Row(detail, guardTitle);
            Theme.Row(detail, guardDetails);
            Theme.Row(detail, guardMissions);
            Theme.Fill(guardPreviewMode, new[] { new Choice(0, "Fitted outfit"), new Choice(1, "NPC"),
                new Choice(2, "Armour"), new Choice(3, "Helmet"), new Choice(4, "Gun") }, 0);
            Theme.Field(detail, "3D preview", guardPreviewMode);
            Theme.Row(detail, gearPreview, true);
            Theme.Row(detail, Theme.Note("Drag to orbit · Scroll to zoom · Double-click to reset"));

            Theme.Fill(guardBody, catalogue.Bodies, 2);
            Theme.Fill(guardTier, GuardPreset.Tiers.Select((tier, index) => new Choice(index, tier)), 0);
            Theme.Fill(guardHelmet, catalogue.Helmets.OrderBy(h => h.Id >= 0 ? 1 : 0).ThenBy(h => h.Id >= 0 ? h.Name : ""), -2);
            guardFilter.TextChanged += delegate { RefreshGuardList(guardId); };
            guardList.SelectedIndexChanged += delegate {
                if (syncing == 0 && guardList.SelectedItem != null) LoadGuard((GuardPreset)guardList.SelectedItem);
            };
            guardName.TextChanged += delegate { UpdateGuardDetails(); };
            guardBody.SelectedIndexChanged += delegate { UpdateGuardDetails(); };
            guardTier.SelectedIndexChanged += delegate { UpdateGuardDetails(); };
            guardVest.SelectedIndexChanged += delegate { UpdateGuardDetails(); };
            guardHelmet.SelectedIndexChanged += delegate { UpdateGuardDetails(); };
            guardGun.SelectionChanged += delegate { UpdateGuardDetails(); };
            guardPreviewMode.SelectedIndexChanged += delegate { UpdateGuardPreview(); };

            RefreshGuardList(null);
            LoadGuard(store.Guards.FirstOrDefault());
            return Theme.Columns(235, 56, library, form, detail);
        }

        private void RefreshGuardList(string selected)
        {
            Sync(delegate {
                guardList.Items.Clear();
                foreach (var g in store.Guards.OrderBy(g => catalogue.BodyName(g.Body)).ThenBy(g => g.Tier).ThenBy(g => g.Name))
                {
                    if (!Filter(g.Name + " " + catalogue.BodyName(g.Body) + " " + GuardPreset.Tiers[g.Tier], guardFilter.Text))
                        continue;
                    var index = guardList.Items.Add(g);
                    if (g.Id == selected) guardList.SelectedIndex = index;
                }
            });
        }

        private void LoadGuard(GuardPreset g)
        {
            Sync(delegate {
                guardId = g == null ? null : g.Id;
                guardName.Text = g == null ? "" : g.Name;
                Theme.Select(guardBody, g == null ? 2 : g.Body);
                Theme.Select(guardTier, g == null ? 0 : g.Tier);
                var vest = g == null ? -1 : g.Vest;
                var activeVest = Workshop.EditorGuardVest(vest);
                var choices = catalogue.Vests.Where(v => Workshop.EditorGuardVest(v.Id) == v.Id).ToList();
                if (activeVest != vest)
                    choices.Add(new Choice(vest, catalogue.VestName(vest) + " → " + catalogue.VestName(activeVest)));
                Theme.Fill(guardVest, choices.OrderBy(v => v.Id > 0 ? 1 : 0)
                    .ThenBy(v => v.Id > 0 ? v.Name : ""), vest);
                Theme.Select(guardHelmet, g == null ? -2 : g.Helmet);
                guardGun.Value = g == null ? GunRef.Default : g.Gun;
                RefreshGuardList(guardId);
            });
            UpdateGuardDetails();
            if (g == null) guardName.Focus();
        }

        private void UpdateGuardDetails()
        {
            if (syncing != 0 || guardGun == null) return;
            var name = guardName.Text.Trim();
            guardTitle.Text = name.Length > 0 ? name : "New guard";
            var tier = Math.Max(0, Theme.Id(guardTier));
            guardDetails.Text = catalogue.BodyName(Theme.Id(guardBody)) + "   /   " + GuardPreset.Tiers[tier] + "\n\n"
                + catalogue.VestName(Workshop.EditorGuardVest(Theme.Id(guardVest))) + "\n"
                + catalogue.HelmetName(Theme.Id(guardHelmet)) + "\n"
                + GunName(guardGun.Value);
            var missions = guardId == null ? 0 : store.Assignments.Where(a => a.Guard == guardId).Select(a => a.Stage).Distinct().Count();
            guardMissions.Text = missions == 0 ? "Assign this preset in Missions." : "Assigned to " + missions + (missions == 1 ? " mission." : " missions.");
            UpdateGuardPreview();
        }

        private void UpdateGuardPreview()
        {
            if (syncing != 0 || guardGun == null) return;
            var body = Theme.Id(guardBody);
            var vest = Theme.Id(guardVest);
            var helmet = Theme.Id(guardHelmet);
            List<PreviewPiece> scene;
            switch (Theme.Id(guardPreviewMode))
            {
                case 1: scene = preview.Guard(body, 0, -1); break;
                case 2: scene = preview.Wearable(Workshop.EditorGuardVest(vest), false); break;
                case 3: scene = preview.Wearable(helmet, true); break;
                case 4: var gun = guardGun.Value; scene = preview.Gun(gun.Key, gun.Preset); break;
                default: scene = preview.Guard(body, vest, helmet); break;
            }
            gearPreview.ShowScene(scene, "Choose a specific item for this preview.");
        }

        private void SaveGuard()
        {
            var name = guardName.Text.Trim();
            var body = Theme.Id(guardBody);
            var tier = Theme.Id(guardTier);
            if (name.Length == 0) throw new InvalidDataException("Enter a name for the guard preset.");
            if (store.Guards.Any(g => g.Id != guardId && g.Body == body && g.Tier == tier
                && String.Equals(g.Name, name, StringComparison.OrdinalIgnoreCase)))
                throw new InvalidDataException("This NPC and weight class already have a preset with that name.");
            var gnew = new GuardPreset {
                Id = guardId ?? Guid.NewGuid().ToString("N"), Name = name, Body = body, Tier = tier,
                Vest = Theme.Id(guardVest), Helmet = Theme.Id(guardHelmet), Gun = guardGun.Value
            };
            if (store.Assignments.Any(a => a.Guard == gnew.Id && a.Body != gnew.Body))
                throw new InvalidDataException("Duplicate this preset to use a different NPC type in other assignments.");
            store = store.Update(draft => {
                Workshop.EditorRefresh();
                ValidatePreset(gnew.Gun);
                draft.Guards.RemoveAll(g => g.Id == gnew.Id);
                draft.Guards.Add(gnew);
            });
            guardId = gnew.Id;
            RefreshGuardList(guardId);
            UpdateGuardDetails();
            Status("Saved " + gnew.Name + ". It is available in Missions.");
        }

        private void DuplicateGuard()
        {
            guardId = null;
            var original = guardName.Text.Trim();
            guardName.Text = original.Length > 43 ? original.Substring(0, 43) + " copy" : original + " copy";
            RefreshGuardList(null);
            UpdateGuardDetails();
            guardName.Focus();
            guardName.SelectAll();
            Status("Choose a name and save the duplicate as another style.");
        }

        private void DeleteGuard()
        {
            if (guardId == null) return;
            if (store.Assignments.Any(a => a.Guard == guardId))
                throw new InvalidDataException("Remove this preset's mission assignments before deleting it.");
            if (MessageBox.Show(this, "Delete this guard preset?", "Delete guard preset",
                MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes) return;
            store = store.Update(draft => draft.Guards.RemoveAll(g => g.Id == guardId));
            LoadGuard(null);
            Status("Deleted guard preset.");
        }

        private static void ValidatePreset(GunRef gun)
        {
            if (gun.Key >= 0 && gun.Preset != "-" && !Workshop.Presets(gun.Key).Contains(gun.Preset))
                throw new InvalidDataException("The selected gun preset is missing or incompatible. Choose an available build.");
        }
    }
}
