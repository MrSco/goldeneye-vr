using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace TacticalEditor
{
    internal sealed class PreviewPiece
    {
        public readonly string File;
        public readonly float[] Pose;
        public readonly bool BlendMaterials;
        public PreviewPiece(string file, float[] pose, bool blendMaterials)
        {
            File = file;
            Pose = pose;
            BlendMaterials = blendMaterials;
        }

        public static float[] Identity()
        {
            return new float[] { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        }

        public static float[] Compose(float[] first, float[] second)
        {
            var result = new float[16];
            for (int row = 0; row < 4; ++row)
                for (int col = 0; col < 4; ++col)
                    for (int k = 0; k < 4; ++k)
                        result[row * 4 + col] += first[row * 4 + k] * second[k * 4 + col];
            return result;
        }
    }

    internal sealed class PreviewCanvas : Control
    {
        private sealed class Mesh
        {
            public uint OpaqueList, TranslucentList;
            public uint[] Textures;
            public readonly float[] Min = { float.MaxValue, float.MaxValue, float.MaxValue };
            public readonly float[] Max = { float.MinValue, float.MinValue, float.MinValue };
        }

        private readonly Dictionary<string, Mesh> meshes = new Dictionary<string, Mesh>(StringComparer.OrdinalIgnoreCase);
        private List<PreviewPiece> scene = new List<PreviewPiece>();
        private IntPtr dc, gl;
        private bool glUnavailable;
        private bool overview;
        private float pitch = 9, yaw = -20, zoom = 1;
        private Point? drag;
        private string empty = "Choose a model to preview.";

        public PreviewCanvas()
        {
            BackColor = Theme.Inset;
            ForeColor = Theme.Muted;
            Dock = DockStyle.Fill;
            Margin = new Padding(0, 6, 0, 7);
            TabStop = true;
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.Opaque
                | ControlStyles.ResizeRedraw | ControlStyles.Selectable, true);
        }

        public void ShowScene(List<PreviewPiece> parts, string message = "No model selected.", bool overhead = false)
        {
            if (overhead != overview)
            {
                overview = overhead;
                ResetCamera();
            }
            scene = parts;
            empty = message;
            Invalidate();
        }

        protected override CreateParams CreateParams
        {
            get { var value = base.CreateParams; value.ClassStyle |= 0x20; return value; }
        }

        protected override void OnMouseDown(MouseEventArgs e)
        {
            base.OnMouseDown(e);
            if (e.Button != MouseButtons.Left) return;
            Focus();
            drag = e.Location;
            Capture = true;
        }

        protected override void OnMouseMove(MouseEventArgs e)
        {
            base.OnMouseMove(e);
            if (!drag.HasValue) return;
            yaw += (e.X - drag.Value.X) * .6f;
            pitch = Math.Min(84, Math.Max(-84, pitch + (e.Y - drag.Value.Y) * .6f));
            drag = e.Location;
            Invalidate();
        }

        protected override void OnMouseUp(MouseEventArgs e)
        {
            base.OnMouseUp(e);
            drag = null;
            Capture = false;
        }

        protected override void OnMouseCaptureChanged(EventArgs e) { drag = null; base.OnMouseCaptureChanged(e); }

        protected override void OnMouseWheel(MouseEventArgs e)
        {
            base.OnMouseWheel(e);
            zoom = Math.Min(3.2f, Math.Max(.6f, zoom * (float)Math.Pow(1.12, e.Delta / 120f)));
            Invalidate();
        }

        protected override void OnDoubleClick(EventArgs e)
        {
            base.OnDoubleClick(e);
            ResetCamera();
            Invalidate();
        }

        private void ResetCamera()
        {
            pitch = overview ? 56 : 9;
            yaw = overview ? -34 : -20;
            zoom = 1;
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            if (scene.Count == 0 || !OpenContext())
            {
                e.Graphics.Clear(BackColor);
                var value = scene.Count == 0 ? empty : "OpenGL preview is unavailable.";
                TextRenderer.DrawText(e.Graphics, value, Theme.Small, ClientRectangle, ForeColor,
                    TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.WordBreak);
                return;
            }
            try
            {
                Draw();
                GL.SwapBuffers(dc);
            }
            catch (IOException)
            {
                e.Graphics.Clear(BackColor);
                TextRenderer.DrawText(e.Graphics, "Could not read this 3D model.", Theme.Small, ClientRectangle, Theme.Error,
                    TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter);
            }
            finally { GL.wglMakeCurrent(IntPtr.Zero, IntPtr.Zero); }
        }

        protected override void OnHandleDestroyed(EventArgs e)
        {
            if (gl != IntPtr.Zero)
            {
                if (GL.wglMakeCurrent(dc, gl)) DeleteMeshes();
                GL.wglMakeCurrent(IntPtr.Zero, IntPtr.Zero);
                GL.wglDeleteContext(gl);
            }
            if (dc != IntPtr.Zero) GL.ReleaseDC(Handle, dc);
            dc = gl = IntPtr.Zero;
            base.OnHandleDestroyed(e);
        }

        private bool OpenContext()
        {
            if (glUnavailable || Width <= 0 || Height <= 0) return false;
            if (gl == IntPtr.Zero)
            {
                dc = GL.GetDC(Handle);
                var format = new GL.PixelFormat { Size = 40, Version = 1, Flags = 0x25, ColorBits = 24, Depth = 24 };
                var index = GL.ChoosePixelFormat(dc, ref format);
                if (index > 0 && GL.SetPixelFormat(dc, index, ref format)) gl = GL.wglCreateContext(dc);
                glUnavailable = gl == IntPtr.Zero;
            }
            return !glUnavailable && GL.wglMakeCurrent(dc, gl);
        }

        private void DeleteMeshes()
        {
            foreach (var mesh in meshes.Values)
            {
                GL.glDeleteLists(mesh.OpaqueList, 1);
                if (mesh.TranslucentList != 0) GL.glDeleteLists(mesh.TranslucentList, 1);
                GL.glDeleteTextures(mesh.Textures.Length, mesh.Textures);
            }
            meshes.Clear();
        }

        private void Draw()
        {
            if (meshes.Count > 64) DeleteMeshes();
            var lower = new[] { float.MaxValue, float.MaxValue, float.MaxValue };
            var upper = new[] { float.MinValue, float.MinValue, float.MinValue };
            foreach (var piece in scene)
                AddBounds(GetMesh(piece.File, piece.BlendMaterials), piece.Pose, lower, upper);
            float size = Math.Max(.005f, Math.Max(upper[0] - lower[0], Math.Max(upper[1] - lower[1], upper[2] - lower[2])));
            float aspect = (float)Width / Height;
            float distance = FrameDistance(lower, upper, aspect) / zoom;
            double near = Math.Max(.0001, size * .017f), halfHeight = near * .413;

            GL.glViewport(0, 0, Width, Height);
            GL.glClearColor(.121f, .144f, .132f, 1);
            GL.glClear(0x4100);
            GL.glEnable(0x0B71); // depth test
            GL.glDepthFunc(0x0203);
            GL.glMatrixMode(0x1701);
            GL.glLoadIdentity();
            GL.glFrustum(-halfHeight * aspect, halfHeight * aspect, -halfHeight, halfHeight, near, size * 40);
            GL.glMatrixMode(0x1700);
            GL.glLoadIdentity();
            GL.glTranslatef(0, 0, -distance);
            GL.glRotatef(pitch, 1, 0, 0);
            GL.glRotatef(yaw, 0, 1, 0);
            var x = (lower[0] + upper[0]) * .5f;
            var y = (lower[1] + upper[1]) * .5f;
            var z = (lower[2] + upper[2]) * .5f;
            GL.glTranslatef(-x, -y, -z);
            DrawFloor(x, lower[1] - size * .045f, z, size);
            GL.glEnable(0x0DE1); // texture 2D
            GL.glEnable(0x0BC0); // cutout alpha
            GL.glAlphaFunc(0x0204, .22f);
            bool translucent = false;
            foreach (var piece in scene)
            {
                var mesh = meshes[piece.File];
                DrawPiece(piece, mesh.OpaqueList);
                translucent |= mesh.TranslucentList != 0;
            }
            if (!translucent) return;

            // Let the shield reveal the already rendered face and leave its depth intact.
            GL.glDisable(0x0BC0);
            GL.glEnable(0x0BE2);
            GL.glBlendFunc(0x0302, 0x0303);
            GL.glDepthMask(false);
            foreach (var piece in scene)
                DrawPiece(piece, meshes[piece.File].TranslucentList);
            GL.glDepthMask(true);
            GL.glDisable(0x0BE2);
        }

        private static void DrawPiece(PreviewPiece piece, uint list)
        {
            if (list == 0) return;
            GL.glPushMatrix();
            GL.glMultMatrixf(piece.Pose);
            GL.glCallList(list);
            GL.glPopMatrix();
        }

        private float FrameDistance(float[] lower, float[] upper, float aspect)
        {
            double sinYaw = Math.Sin(yaw * Math.PI / 180), cosYaw = Math.Cos(yaw * Math.PI / 180);
            double sinPitch = Math.Sin(pitch * Math.PI / 180), cosPitch = Math.Cos(pitch * Math.PI / 180);
            double chosen = .005;
            for (int corner = 0; corner < 8; ++corner)
            {
                var x = (upper[0] - lower[0]) * ((corner & 1) == 0 ? .5 : -.5);
                var y = (upper[1] - lower[1]) * ((corner & 2) == 0 ? .5 : -.5);
                var z = (upper[2] - lower[2]) * ((corner & 4) == 0 ? .5 : -.5);
                var vx = cosYaw * x + sinYaw * z;
                var vz = -sinYaw * x + cosYaw * z;
                var vy = cosPitch * y - sinPitch * vz;
                vz = sinPitch * y + cosPitch * vz;
                chosen = Math.Max(chosen, vz + Math.Max(Math.Abs(vx) / aspect, Math.Abs(vy)) / .343);
            }
            return (float)chosen;
        }

        private static void DrawFloor(float x, float y, float z, float size)
        {
            GL.glDisable(0x0DE1);
            GL.glDisable(0x0BC0);
            GL.glColor4ub(39, 52, 43, 255);
            GL.glBegin(0x0001);
            float radius = size * .57f, step = radius / 6;
            for (int i = -6; i <= 6; ++i)
            {
                float p = i * step;
                GL.glVertex3f(x - radius, y, z + p);
                GL.glVertex3f(x + radius, y, z + p);
                GL.glVertex3f(x + p, y, z - radius);
                GL.glVertex3f(x + p, y, z + radius);
            }
            GL.glEnd();
        }

        private static void AddBounds(Mesh mesh, float[] transform, float[] lower, float[] upper)
        {
            for (int mask = 0; mask < 8; ++mask)
            {
                var x = (mask & 1) != 0 ? mesh.Max[0] : mesh.Min[0];
                var y = (mask & 2) != 0 ? mesh.Max[1] : mesh.Min[1];
                var z = (mask & 4) != 0 ? mesh.Max[2] : mesh.Min[2];
                for (int axis = 0; axis < 3; ++axis)
                {
                    var v = x * transform[axis] + y * transform[4 + axis] + z * transform[8 + axis] + transform[12 + axis];
                    lower[axis] = Math.Min(lower[axis], v);
                    upper[axis] = Math.Max(upper[axis], v);
                }
            }
        }

        private Mesh GetMesh(string path, bool blendMaterials)
        {
            Mesh mesh;
            if (meshes.TryGetValue(path, out mesh)) return mesh;
            mesh = new Mesh();
            using (var reader = new BinaryReader(File.OpenRead(path)))
            {
                if (new string(reader.ReadChars(8)) != "GEVRWPN1") throw new InvalidDataException("Invalid preview mesh.");
                var materialCount = reader.ReadInt32();
                if (materialCount < 1 || materialCount > 64) throw new InvalidDataException("Invalid material count.");
                mesh.Textures = new uint[materialCount];
                GL.glGenTextures(materialCount, mesh.Textures);
                for (int i = 0; i < materialCount; ++i)
                {
                    int width = reader.ReadUInt16(), height = reader.ReadUInt16();
                    if (width < 1 || height < 1 || width > 512 || height > 512) throw new InvalidDataException("Invalid texture size.");
                    var pixels = reader.ReadBytes(width * height * 4);
                    if (pixels.Length != width * height * 4) throw new EndOfStreamException();
                    // Inventory mesh packs carry material masks in the fourth channel.
                    if (!Path.GetFileName(path).StartsWith("npc-", StringComparison.OrdinalIgnoreCase))
                        for (int a = 3; a < pixels.Length; a += 4) pixels[a] = 255;
                    GL.glBindTexture(0x0DE1, mesh.Textures[i]);
                    GL.glTexParameteri(0x0DE1, 0x2800, 0x2601);
                    GL.glTexParameteri(0x0DE1, 0x2801, 0x2601);
                    var powerWidth = PowerOfTwo(width);
                    var powerHeight = PowerOfTwo(height);
                    if (width != powerWidth || height != powerHeight)
                        pixels = Resample(pixels, width, height, powerWidth, powerHeight);
                    GL.glTexImage2D(0x0DE1, 0, 0x1908, powerWidth, powerHeight, 0, 0x1908, 0x1401, pixels);
                }
                var triangleCount = reader.ReadInt32();
                if (triangleCount < 1 || triangleCount > 50000) throw new InvalidDataException("Invalid triangle count.");
                var raw = reader.ReadBytes(triangleCount * 84);
                if (raw.Length != triangleCount * 84) throw new EndOfStreamException();
                CreateLists(mesh, raw, triangleCount * 3, blendMaterials);
            }
            meshes.Add(path, mesh);
            return mesh;
        }

        private static void CreateLists(Mesh mesh, byte[] raw, int vertexCount, bool blendMaterials)
        {
            var counts = new int[mesh.Textures.Length];
            var offsets = new int[counts.Length];
            var translucent = new bool[counts.Length];
            for (int v = 0; v < raw.Length; v += 28)
            {
                int material = raw[v + 24];
                if (material >= counts.Length) throw new InvalidDataException("Invalid texture index.");
                counts[material]++;
                translucent[material] |= blendMaterials && raw[v + 23] < 255;
                for (int axis = 0; axis < 3; ++axis)
                {
                    var value = BitConverter.ToSingle(raw, v + axis * 4);
                    if (Single.IsNaN(value) || Single.IsInfinity(value)) throw new InvalidDataException("Invalid vertex.");
                    mesh.Min[axis] = Math.Min(mesh.Min[axis], value);
                    mesh.Max[axis] = Math.Max(mesh.Max[axis], value);
                }
            }
            for (int i = 1; i < counts.Length; ++i) offsets[i] = offsets[i - 1] + counts[i - 1];
            var cursor = (int[])offsets.Clone();
            var packed = new byte[vertexCount * 24];
            for (int v = 0; v < raw.Length; v += 28)
            {
                var at = cursor[raw[v + 24]]++ * 24;
                Buffer.BlockCopy(raw, v + 12, packed, at, 12);
                Buffer.BlockCopy(raw, v, packed, at + 12, 12);
            }
            var handle = GCHandle.Alloc(packed, GCHandleType.Pinned);
            try
            {
                GL.glInterleavedArrays(0x2A29, 0, handle.AddrOfPinnedObject()); // T2F C4UB V3F
                mesh.OpaqueList = CompileList(mesh, counts, offsets, translucent, false);
                if (Array.IndexOf(translucent, true) >= 0)
                    mesh.TranslucentList = CompileList(mesh, counts, offsets, translucent, true);
            }
            finally { handle.Free(); }
        }

        private static uint CompileList(Mesh mesh, int[] counts, int[] offsets, bool[] translucent, bool blend)
        {
            var id = GL.glGenLists(1);
            GL.glNewList(id, 0x1300);
            for (int i = 0; i < counts.Length; ++i)
            {
                if (counts[i] == 0 || translucent[i] != blend) continue;
                GL.glBindTexture(0x0DE1, mesh.Textures[i]);
                GL.glDrawArrays(0x0004, offsets[i], counts[i]);
            }
            GL.glEndList();
            return id;
        }

        private static int PowerOfTwo(int value)
        {
            int size = 1;
            while (size < value) size *= 2;
            return size;
        }

        private static byte[] Resample(byte[] raw, int width, int height, int targetW, int targetH)
        {
            var scaled = new byte[targetW * targetH * 4];
            for (var y = 0; y < targetH; ++y)
                for (var x = 0; x < targetW; ++x)
                    Buffer.BlockCopy(raw, (y * height / targetH * width + x * width / targetW) * 4,
                        scaled, (y * targetW + x) * 4, 4);
            return scaled;
        }

        internal Bitmap RenderBitmap()
        {
            if (!OpenContext()) throw new InvalidOperationException("The preview window could not create an OpenGL context.");
            var frame = new Bitmap(Width, Height, PixelFormat.Format32bppArgb);
            try
            {
                Draw();
                var rect = new Rectangle(0, 0, Width, Height);
                var data = frame.LockBits(rect, ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
                try { GL.glReadPixels(0, 0, Width, Height, 0x80E1, 0x1401, data.Scan0); }
                finally { frame.UnlockBits(data); }
                frame.RotateFlip(RotateFlipType.RotateNoneFlipY);
                return frame;
            }
            catch { frame.Dispose(); throw; }
            finally { GL.wglMakeCurrent(IntPtr.Zero, IntPtr.Zero); }
        }

        private static class GL
        {
            [StructLayout(LayoutKind.Sequential)]
            internal struct PixelFormat
            {
                public ushort Size, Version;
                public uint Flags;
                public byte Type, ColorBits, R, RShift, G, GShift, B, BShift, A, AShift,
                    AccBits, AccR, AccG, AccB, AccA, Depth, Stencil, Aux, Layer, Reserved;
                public uint LayerMask, VisibleMask, DamageMask;
            }
            [DllImport("user32.dll")] internal static extern IntPtr GetDC(IntPtr handle);
            [DllImport("user32.dll")] internal static extern int ReleaseDC(IntPtr handle, IntPtr dc);
            [DllImport("gdi32.dll")] internal static extern int ChoosePixelFormat(IntPtr dc, ref PixelFormat format);
            [DllImport("gdi32.dll")] internal static extern bool SetPixelFormat(IntPtr dc, int index, ref PixelFormat format);
            [DllImport("gdi32.dll")] internal static extern bool SwapBuffers(IntPtr dc);
            [DllImport("opengl32.dll")] internal static extern IntPtr wglCreateContext(IntPtr dc);
            [DllImport("opengl32.dll")] internal static extern bool wglMakeCurrent(IntPtr dc, IntPtr gl);
            [DllImport("opengl32.dll")] internal static extern bool wglDeleteContext(IntPtr gl);
            [DllImport("opengl32.dll")] internal static extern void glViewport(int x, int y, int w, int h);
            [DllImport("opengl32.dll")] internal static extern void glClearColor(float r, float g, float b, float a);
            [DllImport("opengl32.dll")] internal static extern void glClear(uint mask);
            [DllImport("opengl32.dll")] internal static extern void glEnable(uint token);
            [DllImport("opengl32.dll")] internal static extern void glDisable(uint token);
            [DllImport("opengl32.dll")] internal static extern void glDepthFunc(uint token);
            [DllImport("opengl32.dll")] internal static extern void glDepthMask([MarshalAs(UnmanagedType.U1)] bool enabled);
            [DllImport("opengl32.dll")] internal static extern void glBlendFunc(uint source, uint destination);
            [DllImport("opengl32.dll")] internal static extern void glAlphaFunc(uint token, float value);
            [DllImport("opengl32.dll")] internal static extern void glMatrixMode(uint token);
            [DllImport("opengl32.dll")] internal static extern void glLoadIdentity();
            [DllImport("opengl32.dll")] internal static extern void glFrustum(double l, double r, double b, double t, double n, double f);
            [DllImport("opengl32.dll")] internal static extern void glTranslatef(float x, float y, float z);
            [DllImport("opengl32.dll")] internal static extern void glRotatef(float angle, float x, float y, float z);
            [DllImport("opengl32.dll")] internal static extern void glMultMatrixf(float[] pose);
            [DllImport("opengl32.dll")] internal static extern void glPushMatrix();
            [DllImport("opengl32.dll")] internal static extern void glPopMatrix();
            [DllImport("opengl32.dll")] internal static extern void glBegin(uint type);
            [DllImport("opengl32.dll")] internal static extern void glEnd();
            [DllImport("opengl32.dll")] internal static extern void glColor4ub(byte r, byte g, byte b, byte a);
            [DllImport("opengl32.dll")] internal static extern void glVertex3f(float x, float y, float z);
            [DllImport("opengl32.dll")] internal static extern uint glGenLists(int count);
            [DllImport("opengl32.dll")] internal static extern void glNewList(uint list, uint mode);
            [DllImport("opengl32.dll")] internal static extern void glEndList();
            [DllImport("opengl32.dll")] internal static extern void glCallList(uint list);
            [DllImport("opengl32.dll")] internal static extern void glDeleteLists(uint list, int count);
            [DllImport("opengl32.dll")] internal static extern void glInterleavedArrays(uint format, int stride, IntPtr data);
            [DllImport("opengl32.dll")] internal static extern void glDrawArrays(uint mode, int start, int count);
            [DllImport("opengl32.dll")] internal static extern void glGenTextures(int count, [Out] uint[] ids);
            [DllImport("opengl32.dll")] internal static extern void glDeleteTextures(int count, uint[] ids);
            [DllImport("opengl32.dll")] internal static extern void glBindTexture(uint token, uint id);
            [DllImport("opengl32.dll")] internal static extern void glTexParameteri(uint target, uint name, int value);
            [DllImport("opengl32.dll")] internal static extern void glTexImage2D(uint target, int level, int format,
                int width, int height, int border, uint source, uint type, byte[] pixels);
            [DllImport("opengl32.dll")] internal static extern void glReadPixels(int x, int y, int w, int h,
                uint format, uint type, IntPtr data);
        }
    }
}
