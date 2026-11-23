using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;

namespace TacticalEditor
{
    internal sealed class PreviewLibrary
    {
        private readonly string folder;
        private readonly Dictionary<int, Dictionary<string, float[]>> fit = new Dictionary<int, Dictionary<string, float[]>>();
        private static readonly string[] Shoulders = { "left", "right" };

        public PreviewLibrary(string directory) { folder = directory; }

        public List<PreviewPiece> MissionLayout(Mission mission, int body, int actor)
        {
            var result = new List<PreviewPiece>();
            Add(result, "loadout-models/mission-" + mission.Id + ".bin", PreviewPiece.Identity());
            var markerSize = Math.Max(1f, mission.MapSpan * .032f);
            foreach (var placed in mission.Actors.Where(a => a.Body == body && (actor < 0 || a.Id == actor)))
            {
                if (placed.Position.Length != 3) continue;
                var pose = PreviewPiece.Identity();
                pose[0] = pose[5] = pose[10] = markerSize;
                pose[12] = placed.Position[0];
                pose[13] = placed.Position[1] + markerSize * .14f;
                pose[14] = placed.Position[2];
                Add(result, "loadout-models/mission-marker.bin", pose);
            }
            return result;
        }

        public List<PreviewPiece> Gun(int key, string preset)
        {
            var result = new List<PreviewPiece>();
            var count = key < 0 ? 0 : Workshop.EditorPreviewCount(key, preset);
            var weaponPose = WeaponPose(key);
            for (int i = 0; i < count; ++i)
            {
                var pose = PreviewPiece.Identity();
                var part = Workshop.EditorPreviewPiece(key, preset, i, pose);
                Add(result, PartFile(part), PreviewPiece.Compose(pose, weaponPose));
            }
            return result;
        }

        public List<PreviewPiece> Attachment(int gun, int part)
        {
            var result = new List<PreviewPiece>();
            Add(result, PartFile(part), WeaponPose(gun));
            return result;
        }

        private static float[] WeaponPose(int key)
        {
            return new float[] { 0, 0, -Workshop.EditorPreviewLateral(key), 0,
                1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 0, 1 };
        }

        private string PartFile(int part)
        {
            var key = Workshop.PartId(part);
            if (key == null) return null;
            var sight = "mods/tactical/gear/optics/" + key + ".bin";
            return File.Exists(Path.Combine(folder, sight)) ? sight : "mods/tactical/gear/meshes/" + key + ".bin";
        }

        public List<PreviewPiece> Wearable(int id, bool helmet)
        {
            var result = new List<PreviewPiece>();
            var source = helmet ? Workshop.HelmetMesh(id) : Workshop.ArmourMesh(id);
            Add(result, source, PreviewPiece.Identity(), helmet);
            if (!helmet && source != null)
                foreach (var side in Shoulders)
                    Add(result, ArmFile(source, side), FallbackShoulder(side));
            return result;
        }

        public List<PreviewPiece> Guard(int body, int vest, int helmet)
        {
            var result = new List<PreviewPiece>();
            Add(result, "loadout-models/npc-" + body + ".bin", PreviewPiece.Identity());
            var prepared = BodyFit(body);
            vest = Workshop.EditorGuardVest(vest);
            var armour = Workshop.ArmourMesh(vest, body);
            if (armour != null && !File.Exists(Path.Combine(folder, armour)))
                armour = Workshop.ArmourMesh(vest);
            var headgear = Workshop.HelmetMesh(helmet);
            AddFitted(result, armour, "vest", prepared);
            AddFitted(result, headgear, "helmet", prepared, true);
            if (armour != null)
                foreach (var side in Shoulders)
                    AddFitted(result, ArmFile(armour, side), side, prepared);
            return result;
        }

        private Dictionary<string, float[]> BodyFit(int body)
        {
            Dictionary<string, float[]> cached;
            if (fit.TryGetValue(body, out cached)) return cached;
            cached = new Dictionary<string, float[]>();
            var file = Path.Combine(folder, "loadout-models", "npc-" + body + ".txt");
            if (File.Exists(file))
                foreach (var line in File.ReadLines(file))
                {
                    var tokens = line.Split(' ');
                    if (tokens.Length != 17) continue;
                    cached[tokens[0]] = tokens.Skip(1).Select(value => Single.Parse(value, CultureInfo.InvariantCulture)).ToArray();
                }
            fit.Add(body, cached);
            return cached;
        }

        private void AddFitted(List<PreviewPiece> target, string asset, string slot,
            Dictionary<string, float[]> fitData, bool blendMaterials = false)
        {
            float[] pose;
            if (asset != null && fitData.TryGetValue(slot, out pose)) Add(target, asset, pose, blendMaterials);
        }

        private static string ArmFile(string armour, string side)
        {
            return armour.Substring(0, armour.Length - 4) + "-" + side + "-arm.bin";
        }

        private static float[] FallbackShoulder(string side)
        {
            var sign = side == "left" ? 1f : -1f;
            return new float[] { .02f, -sign, 0, 0, sign, .02f, 0, 0, 0, 0, 1, 0, sign * .45f, .38f, -.06f, 1 };
        }

        private void Add(List<PreviewPiece> target, string asset, float[] pose, bool blendMaterials = false)
        {
            if (asset == null) return;
            var path = Path.Combine(folder, asset);
            if (File.Exists(path)) target.Add(new PreviewPiece(path, pose, blendMaterials));
        }
    }
}
