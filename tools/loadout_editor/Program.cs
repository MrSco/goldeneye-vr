using System;
using System.IO;
using System.Windows.Forms;

namespace TacticalEditor
{
    internal static class Program
    {
        [STAThread]
        private static void Main()
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            try
            {
                var editorFolder = AppDomain.CurrentDomain.BaseDirectory;
                var folder = editorFolder;
                var gameLink = Path.Combine(editorFolder, "tactical-game-dir.txt");
                if (File.Exists(gameLink))
                    folder = Path.GetFullPath(Path.Combine(editorFolder, File.ReadAllText(gameLink).Trim()));
                Directory.SetCurrentDirectory(folder);
                using (var form = new EditorForm(folder))
                    Application.Run(form);
            }
            catch (Exception error)
            {
                MessageBox.Show(error.Message, "GoldenEye Tactical Loadout Editor",
                    MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }
    }
}
