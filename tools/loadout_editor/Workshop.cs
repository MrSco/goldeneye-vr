using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace TacticalEditor
{
    internal static class Workshop
    {
        private const string Native = "TacticalWorkshopNative.dll";
        private const CallingConvention Cdecl = CallingConvention.Cdecl;
        [StructLayout(LayoutKind.Sequential)]
        internal struct WeaponStats
        {
            public int Modes, Rpm, SemiRpm, Burst;
            public float Mass, Ergo, Recoil, Spread, Damage, Flash;
        }
        [DllImport(Native, CallingConvention = Cdecl)] public static extern int EditorWeaponStats(int key, out WeaponStats stats);
        [DllImport(Native, CallingConvention = Cdecl)] public static extern int EditorGunCount();
        [DllImport(Native, CallingConvention = Cdecl)] public static extern int EditorIsSecondary(int key);
        [DllImport(Native, CallingConvention = Cdecl)] public static extern int EditorPreviewLateral(int key);
        [DllImport(Native, CallingConvention = Cdecl)] public static extern int EditorCapacity();
        [DllImport(Native, CallingConvention = Cdecl)] public static extern int EditorMountCount(int key);
        [DllImport(Native, CallingConvention = Cdecl)] public static extern int EditorMountedPart(int key, int index);
        [DllImport(Native, CallingConvention = Cdecl)] public static extern int EditorOptions(int key, int index, [Out] int[] options);
        [DllImport(Native, CallingConvention = Cdecl)] public static extern int EditorReset(int key);
        [DllImport(Native, CallingConvention = Cdecl)] public static extern int EditorFit(int key, int index, int part, out int conflict);
        [DllImport(Native, CallingConvention = Cdecl)] public static extern int EditorInstall(int key, int index, int part, out int conflict);
        [DllImport(Native, CallingConvention = Cdecl)] public static extern int EditorIsSuppressed(int key);
        [DllImport(Native, CallingConvention = Cdecl)] public static extern int EditorPartCapacity(int part);
        [DllImport(Native, CallingConvention = Cdecl)] public static extern void EditorRefresh();
        [DllImport(Native, CallingConvention = Cdecl)] public static extern int EditorPresetCount(int key);
        [DllImport(Native, CallingConvention = Cdecl)] private static extern IntPtr EditorPresetName(int key, int index);
        [DllImport(Native, CallingConvention = Cdecl)] private static extern IntPtr EditorGunName(int key);
        [DllImport(Native, CallingConvention = Cdecl)] private static extern IntPtr EditorPartName(int part);
        [DllImport(Native, CallingConvention = Cdecl)] private static extern IntPtr EditorMountLabel(int key, int index);
        [DllImport(Native, CallingConvention = Cdecl)] private static extern IntPtr EditorMountPath(int key, int index);
        [DllImport(Native, CallingConvention = Cdecl)] private static extern IntPtr EditorPartId(int part);
        [DllImport(Native, CallingConvention = Cdecl)] private static extern IntPtr EditorArmourMesh(int id, int body);
        [DllImport(Native, CallingConvention = Cdecl)] public static extern int EditorGuardVest(int id);
        [DllImport(Native, CallingConvention = Cdecl)] public static extern int EditorMissionAllowsStartingKit(int stage);
        [DllImport(Native, CallingConvention = Cdecl)] private static extern IntPtr EditorHelmetMesh(int id);
        [DllImport(Native, CallingConvention = Cdecl, CharSet = CharSet.Ansi, ExactSpelling = true)]
        public static extern int EditorPreviewCount(int key, string preset);
        [DllImport(Native, CallingConvention = Cdecl, CharSet = CharSet.Ansi, ExactSpelling = true)]
        public static extern int EditorPreviewPiece(int key, string preset, int index, [Out] float[] transform);
        [DllImport(Native, CallingConvention = Cdecl, CharSet = CharSet.Ansi, ExactSpelling = true)]
        public static extern int EditorSave(int key, string name);
        [DllImport(Native, CallingConvention = Cdecl, CharSet = CharSet.Ansi, ExactSpelling = true)]
        public static extern int EditorUpdate(int key, string name, ulong revision);
        [DllImport(Native, CallingConvention = Cdecl, CharSet = CharSet.Ansi, ExactSpelling = true)]
        public static extern int EditorDelete(int key, string name, ulong revision);
        [DllImport(Native, CallingConvention = Cdecl, CharSet = CharSet.Ansi, ExactSpelling = true)]
        public static extern ulong EditorPresetRevision(int key, string name);
        [DllImport(Native, CallingConvention = Cdecl, CharSet = CharSet.Ansi, ExactSpelling = true)]
        public static extern int EditorLoad(int key, string name);
        [DllImport(Native, CallingConvention = Cdecl, ExactSpelling = true)]
        public static extern int EditorImport(int key, [MarshalAs(UnmanagedType.LPWStr)] string path);
        [DllImport(Native, CallingConvention = Cdecl, ExactSpelling = true)]
        public static extern int EditorExport(int key, [MarshalAs(UnmanagedType.LPStr)] string name,
            [MarshalAs(UnmanagedType.LPWStr)] string path);

        public static string GunName(int key) { return Marshal.PtrToStringAnsi(EditorGunName(key)); }
        public static string PartName(int part) { return Marshal.PtrToStringAnsi(EditorPartName(part)) ?? "Unknown part"; }
        public static string MountName(int key, int index) { return Marshal.PtrToStringAnsi(EditorMountLabel(key, index)); }
        public static string MountPath(int key, int index) { return Marshal.PtrToStringAnsi(EditorMountPath(key, index)); }
        public static string PartId(int part) { return Marshal.PtrToStringAnsi(EditorPartId(part)); }
        public static string ArmourMesh(int id, int body = -1) { return Marshal.PtrToStringAnsi(EditorArmourMesh(id, body)); }
        public static string HelmetMesh(int id) { return Marshal.PtrToStringAnsi(EditorHelmetMesh(id)); }

        public static List<string> Presets(int key)
        {
            var names = new List<string>();
            if (key < 0) return names;
            for (int i = 0, count = EditorPresetCount(key); i < count; ++i)
                names.Add(Marshal.PtrToStringAnsi(EditorPresetName(key, i)));
            return names;
        }
    }
}
