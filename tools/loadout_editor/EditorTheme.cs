using System;
using System.Collections.Generic;
using System.Drawing;
using System.Windows.Forms;

namespace TacticalEditor
{
    internal static class Theme
    {
        public static readonly Color Base = Color.FromArgb(19, 23, 20);
        public static readonly Color Panel = Color.FromArgb(29, 36, 31);
        public static readonly Color Inset = Color.FromArgb(23, 29, 25);
        public static readonly Color Edge = Color.FromArgb(63, 77, 66);
        public static readonly Color Text = Color.FromArgb(235, 238, 222);
        public static readonly Color Muted = Color.FromArgb(168, 181, 168);
        public static readonly Color Accent = Color.FromArgb(196, 215, 154);
        public static readonly Color Selected = Color.FromArgb(59, 79, 60);
        public static readonly Color Error = Color.FromArgb(236, 167, 139);
        public static readonly Font Normal = new Font("Segoe UI", 10);
        public static readonly Font Small = new Font("Segoe UI", 9);
        private static readonly Font Heading = new Font("Segoe UI Semibold", 14);
        private static readonly Font Hero = new Font("Segoe UI Semibold", 24);

        public static Label Caption(string value, int size)
        {
            return new Label { Text = value, AutoSize = true, ForeColor = size == 0 ? Muted : Text,
                Font = size == 2 ? Hero : size == 1 ? Heading : Normal, Margin = new Padding(0, 0, 0, 8) };
        }

        public static Label Note(string value)
        {
            return new Label { Text = value, ForeColor = Muted, AutoSize = true, Font = Small,
                Margin = new Padding(0, 4, 0, 8) };
        }

        public static TextBox Input(int maxLength)
        {
            return new TextBox { BackColor = Inset, ForeColor = Text, BorderStyle = BorderStyle.FixedSingle,
                Font = Normal, MaxLength = maxLength, Margin = new Padding(0, 0, 0, 10), Dock = DockStyle.Top };
        }

        public static ComboBox Combo(bool search = false)
        {
            var box = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Font = Normal, BackColor = Inset,
                ForeColor = Text, FlatStyle = FlatStyle.Flat, Dock = DockStyle.Top, DropDownHeight = 330,
                Margin = new Padding(0, 0, 0, 10), IntegralHeight = false, DrawMode = DrawMode.OwnerDrawFixed,
                ItemHeight = 25 };
            box.DrawItem += delegate(object sender, DrawItemEventArgs e) {
                var selected = (e.State & DrawItemState.Selected) != 0;
                using (var brush = new SolidBrush(selected ? Selected : Inset)) e.Graphics.FillRectangle(brush, e.Bounds);
                if (e.Index < 0 || e.Index >= box.Items.Count) return;
                var rect = new Rectangle(e.Bounds.X + 4, e.Bounds.Y, e.Bounds.Width - 7, e.Bounds.Height);
                TextRenderer.DrawText(e.Graphics, box.Items[e.Index].ToString(), box.Font, rect, box.Enabled ? Text : Muted,
                    TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis);
            };
            if (search)
            {
                box.AutoCompleteSource = AutoCompleteSource.ListItems;
                box.AutoCompleteMode = AutoCompleteMode.SuggestAppend;
            }
            return box;
        }

        public static ListBox List()
        {
            var list = new ListBox { BackColor = Inset, ForeColor = Text, Font = Normal, BorderStyle = BorderStyle.None,
                Dock = DockStyle.Fill, DrawMode = DrawMode.OwnerDrawFixed, ItemHeight = 34, IntegralHeight = false,
                Margin = new Padding(0, 4, 0, 8) };
            list.DrawItem += delegate(object sender, DrawItemEventArgs e) {
                if (e.Index < 0 || e.Index >= list.Items.Count) return;
                var selected = (e.State & DrawItemState.Selected) != 0;
                using (var brush = new SolidBrush(selected ? Selected : Inset))
                    e.Graphics.FillRectangle(brush, e.Bounds);
                var rect = new Rectangle(e.Bounds.X + 9, e.Bounds.Y, e.Bounds.Width - 14, e.Bounds.Height);
                TextRenderer.DrawText(e.Graphics, list.Items[e.Index].ToString(), list.Font, rect,
                    selected ? Accent : Text, TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis);
                e.DrawFocusRectangle();
            };
            return list;
        }

        public static Button Button(string label, bool main, Action clicked)
        {
            var button = new Button { Text = label, AutoSize = true, Height = 34, MinimumSize = new Size(0, 34),
                Padding = new Padding(10, 0, 10, 0), Margin = new Padding(0, 0, 7, 7),
                BackColor = main ? Accent : Selected, ForeColor = main ? Base : Text,
                FlatStyle = FlatStyle.Flat, Font = Normal, UseVisualStyleBackColor = false,
                Cursor = Cursors.Hand };
            button.FlatAppearance.BorderSize = 0;
            button.Click += delegate { clicked(); };
            return button;
        }

        public static CheckBox Check(string label)
        {
            return new CheckBox { Text = label, Font = Normal, AutoSize = true, ForeColor = Text,
                Margin = new Padding(0, 4, 0, 10) };
        }

        public static TableLayoutPanel Stack(int padding)
        {
            return new TableLayoutPanel { BackColor = Panel, Dock = DockStyle.Fill, ColumnCount = 1,
                Padding = new Padding(padding), Margin = new Padding(0, 0, 10, 0) };
        }

        public static void Row(TableLayoutPanel table, Control control, bool fill = false)
        {
            var index = table.RowCount++;
            table.RowStyles.Add(fill ? new RowStyle(SizeType.Percent, 100) : new RowStyle(SizeType.AutoSize));
            table.Controls.Add(control, 0, index);
        }

        public static void Field(TableLayoutPanel table, string caption, Control field)
        {
            var title = Note(caption);
            title.Margin = new Padding(0, 3, 0, 4);
            Row(table, title);
            Row(table, field);
        }

        public static FlowLayoutPanel Actions(params Control[] buttons)
        {
            var row = new FlowLayoutPanel { AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink,
                Dock = DockStyle.Top, Margin = new Padding(0, 3, 0, 2) };
            row.Controls.AddRange(buttons);
            return row;
        }

        public static TableLayoutPanel Columns(int rail, int second, Control left, Control middle, Control right)
        {
            var grid = new TableLayoutPanel { ColumnCount = 3, RowCount = 1, Dock = DockStyle.Fill,
                Margin = new Padding(0), BackColor = Base };
            grid.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, rail));
            grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, second));
            grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100 - second));
            grid.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            right.Margin = new Padding(0);
            grid.Controls.Add(left, 0, 0);
            grid.Controls.Add(middle, 1, 0);
            grid.Controls.Add(right, 2, 0);
            return grid;
        }

        public static DataGridView Grid(params string[] labels)
        {
            var grid = new DataGridView { Dock = DockStyle.Fill, BackgroundColor = Inset, GridColor = Edge,
                AllowUserToAddRows = false, AllowUserToDeleteRows = false, AllowUserToResizeRows = false,
                AllowUserToResizeColumns = false, BorderStyle = BorderStyle.None, EnableHeadersVisualStyles = false,
                MultiSelect = false, ReadOnly = true, RowHeadersVisible = false, SelectionMode = DataGridViewSelectionMode.FullRowSelect,
                AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.Fill, ColumnHeadersHeight = 34,
                ColumnHeadersHeightSizeMode = DataGridViewColumnHeadersHeightSizeMode.DisableResizing,
                Margin = new Padding(0, 4, 0, 8) };
            grid.RowTemplate.Height = 34;
            grid.ColumnHeadersDefaultCellStyle.BackColor = Selected;
            grid.ColumnHeadersDefaultCellStyle.SelectionBackColor = Selected;
            grid.ColumnHeadersDefaultCellStyle.ForeColor = Accent;
            grid.ColumnHeadersDefaultCellStyle.Font = Small;
            grid.DefaultCellStyle.BackColor = Inset;
            grid.DefaultCellStyle.SelectionBackColor = Selected;
            grid.DefaultCellStyle.ForeColor = Text;
            grid.DefaultCellStyle.SelectionForeColor = Accent;
            grid.DefaultCellStyle.Font = Small;
            grid.DefaultCellStyle.Padding = new Padding(5, 0, 2, 0);
            foreach (var label in labels)
                grid.Columns.Add(new DataGridViewTextBoxColumn { HeaderText = label, SortMode = DataGridViewColumnSortMode.NotSortable });
            return grid;
        }

        public static void Fill(ComboBox box, IEnumerable<Choice> values, int id)
        {
            box.BeginUpdate();
            box.Items.Clear();
            foreach (var value in values) box.Items.Add(value);
            Select(box, id);
            box.EndUpdate();
        }

        public static void Select(ComboBox box, int id)
        {
            for (var i = 0; i < box.Items.Count; ++i)
                if (((Choice)box.Items[i]).Id == id)
                {
                    box.SelectedIndex = i;
                    return;
                }
            box.SelectedIndex = box.Items.Count > 0 ? 0 : -1;
        }

        public static int Id(ComboBox box)
        {
            var choice = box.SelectedItem as Choice;
            return choice == null ? -1 : choice.Id;
        }
    }

    internal sealed class GunPicker : TableLayoutPanel
    {
        private readonly ComboBox gun = Theme.Combo(true);
        private readonly ComboBox preset = Theme.Combo();
        private bool updating;
        public event EventHandler SelectionChanged;

        public GunPicker(IEnumerable<Choice> choices)
        {
            Dock = DockStyle.Top;
            AutoSize = true;
            AutoSizeMode = AutoSizeMode.GrowAndShrink;
            Margin = new Padding(0);
            ColumnCount = 1;
            Theme.Row(this, gun);
            Theme.Row(this, preset);
            Theme.Fill(gun, choices, -1);
            gun.SelectedIndexChanged += delegate {
                if (updating) return;
                LoadPresets("-");
                Changed();
            };
            preset.SelectedIndexChanged += delegate { if (!updating) Changed(); };
            LoadPresets("-");
        }

        private void Changed()
        {
            if (SelectionChanged != null) SelectionChanged(this, EventArgs.Empty);
        }

        public GunRef Value
        {
            get {
                return new GunRef(Theme.Id(gun), preset.SelectedIndex <= 0 ? "-" : (string)preset.SelectedItem);
            }
            set {
                updating = true;
                Theme.Select(gun, value.Key);
                LoadPresets(value.Preset);
                updating = false;
            }
        }

        public void RefreshPresets() { Value = Value; }

        private void LoadPresets(string name)
        {
            var previous = updating;
            updating = true;
            preset.BeginUpdate();
            preset.Items.Clear();
            var key = Theme.Id(gun);
            preset.Items.Add(key < 0 ? "Mission weapon" : "Factory build");
            foreach (var saved in Workshop.Presets(key)) preset.Items.Add(saved);
            if (name != "-" && !preset.Items.Contains(name)) preset.Items.Add(name);
            preset.SelectedItem = name == "-" ? preset.Items[0] : name;
            preset.Enabled = key >= 0;
            preset.EndUpdate();
            updating = previous;
        }
    }
}
