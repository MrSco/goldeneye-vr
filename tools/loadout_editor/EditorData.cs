using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;

namespace TacticalEditor
{
    internal sealed class Choice
    {
        public readonly int Id;
        public readonly string Name;
        public Choice(int id, string name) { Id = id; Name = name; }
        public override string ToString() { return Name; }
    }

    internal sealed class CampaignActor
    {
        public int Body, Id, Pad;
        public string Name;
        public float[] Position;
    }

    internal sealed class Mission
    {
        public int Id, Order;
        public string Name;
        public float MapSpan;
        public readonly List<CampaignActor> Actors = new List<CampaignActor>();
        public override string ToString() { return Order.ToString("00") + "   " + Name; }
    }

    internal sealed class CampaignCatalogue
    {
        public readonly List<Choice> Bodies = new List<Choice>();
        public readonly List<Choice> Vests = new List<Choice> {
            new Choice(-1, "Mission default armour"), new Choice(0, "Rig only")
        };
        public readonly List<Choice> Helmets = new List<Choice> {
            new Choice(-2, "Mission default helmet"), new Choice(-1, "No helmet")
        };
        public readonly List<Mission> Missions = new List<Mission>();

        public CampaignCatalogue(string path)
        {
            var rows = File.ReadAllLines(path);
            if (rows.Length == 0 || rows[0] != "GEVREDITOR1")
                throw new InvalidDataException("The loadout editor catalogue could not be read.");

            foreach (var row in rows.Skip(1))
            {
                var f = row.Split('\t');
                var id = Number(f[1]);
                switch (f[0])
                {
                    case "N": Bodies.Add(new Choice(id, f[2])); break;
                    case "V": Vests.Add(new Choice(id, f[2])); break;
                    case "H": Helmets.Add(new Choice(id, f[2])); break;
                    case "M":
                        Missions.Add(new Mission { Id = id, Order = Missions.Count + 1, Name = f[2],
                            MapSpan = f.Length > 3 ? Single.Parse(f[3], CultureInfo.InvariantCulture) : 1 });
                        break;
                    case "A":
                        Missions.First(m => m.Id == id).Actors.Add(new CampaignActor {
                            Body = Number(f[2]), Id = Number(f[3]), Pad = Number(f[4]), Name = f[5],
                            Position = f.Skip(6).Select(v => Single.Parse(v, CultureInfo.InvariantCulture)).ToArray()
                        });
                        break;
                }
            }
            Bodies.Sort((a, b) => StringComparer.OrdinalIgnoreCase.Compare(a.Name, b.Name));
        }

        public static int Number(string value) { return int.Parse(value, CultureInfo.InvariantCulture); }

        public string BodyName(int id) { return ChoiceName(Bodies, id); }
        public string VestName(int id) { return ChoiceName(Vests, id); }
        public string HelmetName(int id) { return ChoiceName(Helmets, id); }

        private static string ChoiceName(IEnumerable<Choice> options, int id)
        {
            var option = options.FirstOrDefault(x => x.Id == id);
            return option == null ? "Unknown (" + id + ")" : option.Name;
        }

        public string TargetName(Mission mission, int body, int actor)
        {
            if (actor < 0) return "All " + BodyName(body) + " NPCs";
            var placed = mission.Actors.FirstOrDefault(a => a.Body == body && a.Id == actor);
            return (placed == null ? BodyName(body) : placed.Name) + "  #" + actor;
        }
    }

    internal sealed class GunRef
    {
        public static readonly GunRef Default = new GunRef(-1, "-");
        public readonly int Key;
        public readonly string Preset;
        public GunRef(int key, string preset) { Key = key; Preset = preset; }
    }

    internal sealed class GuardPreset
    {
        public string Id, Name;
        public int Body, Tier, Vest, Helmet;
        public GunRef Gun;
        public static readonly string[] Tiers = { "Light", "Medium", "Heavy" };
        public override string ToString() { return Name + "   /   " + Tiers[Tier]; }
    }

    internal sealed class Assignment
    {
        public int Stage, Body, Actor;
        public string Guard;
    }

    internal sealed class StartingKit
    {
        public GunRef Primary, Secondary;
    }

    internal sealed class LoadoutStore
    {
        public readonly List<GuardPreset> Guards = new List<GuardPreset>();
        public readonly List<Assignment> Assignments = new List<Assignment>();
        public readonly Dictionary<int, StartingKit> Kits = new Dictionary<int, StartingKit>();
        private readonly string path;
        private string lastSaved;
        private static readonly Encoding Encoding = new UTF8Encoding(false, true);

        public LoadoutStore(string file)
        {
            path = file;
            if (!File.Exists(path)) return;
            lastSaved = File.ReadAllText(path, Encoding);
            using (var reader = new StringReader(lastSaved))
            {
                if (reader.ReadLine() != "GEVRLOADOUTS1")
                    throw new InvalidDataException("The existing campaign loadout file has an unknown format.");
                string row;
                while ((row = reader.ReadLine()) != null)
                {
                    var f = row.Split('\t');
                    if (f.Length == 9 && f[0] == "G")
                        Guards.Add(new GuardPreset { Id = f[1], Body = Num(f[2]), Tier = Num(f[3]), Vest = Num(f[4]),
                            Helmet = Num(f[5]), Gun = ReadGun(f, 6), Name = f[8] });
                    else if (f.Length == 5 && f[0] == "A")
                        Assignments.Add(new Assignment { Stage = Num(f[1]), Body = Num(f[2]), Actor = Num(f[3]), Guard = f[4] });
                    else if (f.Length == 6 && f[0] == "M")
                        Kits.Add(Num(f[1]), new StartingKit { Primary = ReadGun(f, 2), Secondary = ReadGun(f, 4) });
                    else if (row.Length > 0)
                        throw new InvalidDataException("A row in the campaign loadout file could not be read.");
                }
            }
            Validate();
        }

        private LoadoutStore(LoadoutStore source)
        {
            path = source.path;
            lastSaved = source.lastSaved;
            Guards.AddRange(source.Guards);
            Assignments.AddRange(source.Assignments);
            foreach (var kit in source.Kits) Kits.Add(kit.Key, kit.Value);
        }

        private FileStream LockForWrite()
        {
            return new FileStream(path + ".lock", FileMode.OpenOrCreate, FileAccess.ReadWrite,
                FileShare.None, 1, FileOptions.DeleteOnClose);
        }

        public LoadoutStore Update(Action<LoadoutStore> change)
        {
            using (LockForWrite())
            {
                var updated = new LoadoutStore(this);
                change(updated);
                updated.Write();
                return updated;
            }
        }

        public void DeleteGunPreset(int key, string name, ulong revision)
        {
            using (LockForWrite())
            {
                Workshop.EditorRefresh();
                if (Workshop.EditorPresetRevision(key, name) != revision)
                    throw new IOException("That build changed. Review the latest version before deleting it.");
                var current = new LoadoutStore(path);
                var guard = current.Guards.FirstOrDefault(g => UsesPreset(g.Gun, name, revision));
                if (guard != null)
                    throw new InvalidDataException("This build is used by guard preset “" + guard.Name
                        + "”. Select a different build for that guard first.");
                if (current.Kits.Values.Any(k => UsesPreset(k.Primary, name, revision)
                    || UsesPreset(k.Secondary, name, revision)))
                    throw new InvalidDataException("This build is used in a mission starting kit. Select a different build for that kit first.");
                var result = Workshop.EditorDelete(key, name, revision);
                if (result == -3) throw new IOException("That build changed. Review the latest version before deleting it.");
                if (result != 1) throw new IOException("Could not delete the saved build. Check the Workshop file and try again.");
            }
        }

        private static bool UsesPreset(GunRef gun, string name, ulong revision)
        {
            return gun.Preset == name && Workshop.EditorPresetRevision(gun.Key, name) == revision;
        }

        private static int Num(string value) { return CampaignCatalogue.Number(value); }
        private static GunRef ReadGun(string[] fields, int i) { return new GunRef(Num(fields[i]), fields[i + 1]); }
        private static bool ValidGun(GunRef gun, int minimum)
        {
            return gun != null && gun.Key >= minimum && gun.Key < Workshop.EditorGunCount()
                && Regex.IsMatch(gun.Preset, @"\A[A-Za-z0-9 _-]{1,24}\z") && gun.Preset.Trim() == gun.Preset;
        }

        private void Validate()
        {
            if (Guards.Count > 1024 || Assignments.GroupBy(a => a.Stage).Any(g => g.Count() > 1024))
                throw new InvalidDataException("The campaign file has reached its preset or assignment limit.");
            var library = new Dictionary<string, GuardPreset>(StringComparer.Ordinal);
            foreach (var g in Guards)
            {
                if (!Regex.IsMatch(g.Id, @"\A[0-9a-f]{32}\z") || g.Tier < 0 || g.Tier >= GuardPreset.Tiers.Length
                    || String.IsNullOrWhiteSpace(g.Name) || g.Name.Length > 48 || g.Name.Any(Char.IsControl)
                    || g.Body < 0 || g.Vest < -1 || g.Helmet < -2 || !ValidGun(g.Gun, -1))
                    throw new InvalidDataException("A guard preset has invalid fields.");
                library.Add(g.Id, g);
            }
            foreach (var a in Assignments)
            {
                GuardPreset preset;
                if (!library.TryGetValue(a.Guard, out preset) || preset.Body != a.Body)
                    throw new InvalidDataException("A mission assignment refers to a missing or mismatched guard preset.");
            }
            foreach (var kit in Kits.Values)
                if (!ValidGun(kit.Primary, 0) || !ValidGun(kit.Secondary, 0)
                    || Workshop.EditorIsSecondary(kit.Primary.Key) != 0 || Workshop.EditorIsSecondary(kit.Secondary.Key) == 0)
                    throw new InvalidDataException("A mission starting kit requires a primary and a secondary.");
        }

        private void Write()
        {
            Validate();
            var data = new StringBuilder("GEVRLOADOUTS1\n");
            foreach (var g in Guards)
                Row(data, "G", g.Id, g.Body, g.Tier, g.Vest, g.Helmet, g.Gun.Key, g.Gun.Preset, g.Name);
            foreach (var a in Assignments.OrderBy(a => a.Stage).ThenBy(a => a.Body).ThenBy(a => a.Actor))
                Row(data, "A", a.Stage, a.Body, a.Actor, a.Guard);
            foreach (var kit in Kits.OrderBy(k => k.Key))
                Row(data, "M", kit.Key, kit.Value.Primary.Key, kit.Value.Primary.Preset,
                    kit.Value.Secondary.Key, kit.Value.Secondary.Preset);
            var current = File.Exists(path) ? File.ReadAllText(path, Encoding) : null;
            if (!String.Equals(current, lastSaved, StringComparison.Ordinal))
                throw new IOException("Campaign loadouts changed on disk. Reopen the editor to use the latest copy.");
            var temp = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
            try
            {
                File.WriteAllText(temp, data.ToString(), Encoding);
                if (File.Exists(path)) File.Replace(temp, path, null);
                else File.Move(temp, path);
                lastSaved = data.ToString();
            }
            finally { if (File.Exists(temp)) File.Delete(temp); }
        }

        private static void Row(StringBuilder data, params object[] fields)
        {
            data.Append(String.Join("\t", fields.Select(x => Convert.ToString(x, CultureInfo.InvariantCulture))));
            data.Append('\n');
        }
    }
}
