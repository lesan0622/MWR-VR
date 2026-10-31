// One-click installer for Overlord MWR VR. Built by tools/build_installer.py with
// the .NET Framework csc (no extra runtime on Windows 10/11): the Release files
// are embedded as payload.zip and extracted into the Call of Duty: Modern Warfare
// Remastered folder, found through Steam (app 393080) or picked by the player.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Text.RegularExpressions;
using System.Windows.Forms;
using Microsoft.Win32;

static class Installer
{
	const string Title = "Overlord MWR VR";
	const string GameFolder = "Call of Duty Modern Warfare Remastered";
	const string GameExe = "h1_sp64_ship.exe";

	static bool Korean { get { return System.Globalization.CultureInfo.CurrentUICulture.TwoLetterISOLanguageName == "ko"; } }
	static string T(string ko, string en) { return Korean ? ko : en; }

	[STAThread]
	static int Main(string[] args)
	{
		Application.EnableVisualStyles();
		// /list <file>: write the detected installs (store<TAB>folder), install nothing.
		if (args.Length > 1 && args[0].Equals("/list", StringComparison.OrdinalIgnoreCase))
		{
			var lines = new List<string>();
			foreach (var i in FindGames()) lines.Add(i.Store + "\t" + i.Folder);
			File.WriteAllLines(args[1], lines.ToArray());
			return 0;
		}
		// /silent: install into the detected folder without dialogs (scripted installs and tests).
		if (args.Length > 0 && args[0].Equals("/silent", StringComparison.OrdinalIgnoreCase))
		{
			try
			{
				var found = FindGames();
				if (found.Count == 0 || Running("h1_sp64_ship") || Running("overlord")) return 2;
				Extract(found[0].Folder);
				return 0;
			}
			catch { return 1; }
		}
		try
		{
			var found = FindGames();
			string game = null;
			if (found.Count == 1)
			{
				if (MessageBox.Show(T("다음 폴더에 설치합니다 (", "Install into (") + found[0].Store + "):\n\n" + found[0].Folder +
					T("\n\n다른 폴더를 고르려면 '아니요'를 누르세요.", "\n\nChoose 'No' to pick another folder."),
					Title, MessageBoxButtons.YesNo, MessageBoxIcon.Question) == DialogResult.Yes)
					game = found[0].Folder;
				else game = PickGame();
			}
			else if (found.Count > 1) game = Choose(found);
			else game = PickGame();
			if (game == null) return 1;
			while (Running("h1_sp64_ship") || Running("overlord"))
				if (MessageBox.Show(T("게임이 실행 중입니다. 게임을 끄고 '다시 시도'를 누르세요.", "The game is running. Close it and press Retry."),
					Title, MessageBoxButtons.RetryCancel, MessageBoxIcon.Warning) == DialogResult.Cancel) return 1;
			Extract(game);
			Shortcut(game);
			MessageBox.Show(T("설치가 끝났습니다.\n\n바탕화면의 'Overlord MWR VR' 또는 게임 폴더의 overlord.exe로 실행하세요.\n(SteamVR을 먼저 켜 두세요.)",
				"Installed.\n\nStart 'Overlord MWR VR' on the desktop or overlord.exe in the game folder.\n(Start SteamVR first.)"),
				Title, MessageBoxButtons.OK, MessageBoxIcon.Information);
			return 0;
		}
		catch (Exception e)
		{
			MessageBox.Show(T("설치하지 못했습니다:\n\n", "Installation failed:\n\n") + e.Message, Title, MessageBoxButtons.OK, MessageBoxIcon.Error);
			return 1;
		}
	}

	static bool IsGame(string folder)
	{
		try { return !string.IsNullOrEmpty(folder) && File.Exists(Path.Combine(folder, GameExe)); }
		catch { return false; } // Paths scraped from product.db may be malformed.
	}

	struct Install { public string Store, Folder; }

	// Steam first, then Battle.net; one entry per distinct folder.
	static List<Install> FindGames()
	{
		var result = new List<Install>();
		Action<string, string> add = (store, folder) =>
		{
			if (!IsGame(folder)) return;
			var full = Path.GetFullPath(folder).TrimEnd('\\');
			foreach (var i in result) if (string.Equals(i.Folder, full, StringComparison.OrdinalIgnoreCase)) return;
			result.Add(new Install { Store = store, Folder = full });
		};
		foreach (var folder in SteamFolders()) add("Steam", folder);
		foreach (var folder in BattleNetFolders()) add("Battle.net", folder);
		return result;
	}

	// Battle.net: the uninstall entry Blizzard writes, the agent's product.db
	// install paths, then the default and common library folders on every drive.
	static IEnumerable<string> BattleNetFolders()
	{
		var folders = new List<string>();
		foreach (var hive in new[] { Registry.LocalMachine, Registry.CurrentUser })
			foreach (var key in new[] { @"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall", @"SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall" })
				using (var root = hive.OpenSubKey(key))
				{
					if (root == null) continue;
					foreach (var name in root.GetSubKeyNames())
						using (var app = root.OpenSubKey(name))
						{
							var display = app == null ? null : app.GetValue("DisplayName") as string;
							var location = app == null ? null : app.GetValue("InstallLocation") as string;
							if (display != null && location != null && display.IndexOf("Modern Warfare Remastered", StringComparison.OrdinalIgnoreCase) >= 0)
								folders.Add(location.Trim('"'));
						}
				}
		try
		{
			var db = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData), @"Battle.net\Agent\product.db");
			if (File.Exists(db))
			{
				var text = System.Text.Encoding.UTF8.GetString(File.ReadAllBytes(db));
				foreach (Match m in Regex.Matches(text, @"[A-Za-z]:[\\/][^\x00-\x1f""<>|*?]*?" + Regex.Escape(GameFolder).Replace("\\ ", "[ _]")))
					folders.Add(m.Value.Replace('/', '\\'));
			}
		}
		catch { } // An unreadable agent database only skips this source.
		foreach (var drive in DriveInfo.GetDrives())
		{
			if (drive.DriveType != DriveType.Fixed) continue;
			var d = drive.RootDirectory.FullName;
			foreach (var parent in new[] { @"Program Files (x86)", @"Program Files", @"Games", @"Battle.net", @"Battle.net\Games", @"Blizzard", "" })
				folders.Add(Path.Combine(d, parent, GameFolder));
		}
		return folders;
	}

	// Install folder picked from several detected copies.
	static string Choose(List<Install> found)
	{
		using (var form = new Form())
		{
			form.Text = Title;form.FormBorderStyle = FormBorderStyle.FixedDialog;form.MaximizeBox = form.MinimizeBox = false;
			form.StartPosition = FormStartPosition.CenterScreen;form.AutoSize = true;form.AutoSizeMode = AutoSizeMode.GrowAndShrink;
			form.Padding = new Padding(12);
			var layout = new FlowLayoutPanel { FlowDirection = FlowDirection.TopDown, AutoSize = true, Dock = DockStyle.Fill };
			layout.Controls.Add(new Label { Text = T("설치할 게임을 고르세요:", "Choose the game to install into:"), AutoSize = true });
			var choices = new List<RadioButton>();
			foreach (var i in found)
			{
				var r = new RadioButton { Text = i.Store + "  —  " + i.Folder, AutoSize = true, Checked = choices.Count == 0 };
				choices.Add(r);layout.Controls.Add(r);
			}
			var buttons = new FlowLayoutPanel { AutoSize = true };
			var ok = new Button { Text = T("설치", "Install"), DialogResult = DialogResult.OK, AutoSize = true };
			var other = new Button { Text = T("다른 폴더...", "Other folder..."), DialogResult = DialogResult.Retry, AutoSize = true };
			var cancel = new Button { Text = T("취소", "Cancel"), DialogResult = DialogResult.Cancel, AutoSize = true };
			buttons.Controls.AddRange(new Control[] { ok, other, cancel });
			layout.Controls.Add(buttons);
			form.Controls.Add(layout);form.AcceptButton = ok;form.CancelButton = cancel;
			var answer = form.ShowDialog();
			if (answer == DialogResult.Retry) return PickGame();
			if (answer != DialogResult.OK) return null;
			for (int n = 0; n < choices.Count; ++n) if (choices[n].Checked) return found[n].Folder;
			return null;
		}
	}

	// Steam install path from the registry, then every library in libraryfolders.vdf.
	static IEnumerable<string> SteamFolders()
	{
		var roots = new List<string>();
		foreach (var key in new[] { @"SOFTWARE\WOW6432Node\Valve\Steam", @"SOFTWARE\Valve\Steam" })
			using (var k = Registry.LocalMachine.OpenSubKey(key))
			{
				var p = k == null ? null : k.GetValue("InstallPath") as string;
				if (p != null) roots.Add(p);
			}
		using (var k = Registry.CurrentUser.OpenSubKey(@"Software\Valve\Steam"))
		{
			var p = k == null ? null : k.GetValue("SteamPath") as string;
			if (p != null) roots.Add(p.Replace('/', '\\'));
		}
		var libraries = new List<string>(roots);
		foreach (var root in roots)
		{
			var vdf = Path.Combine(root, @"steamapps\libraryfolders.vdf");
			if (!File.Exists(vdf)) continue;
			foreach (Match m in Regex.Matches(File.ReadAllText(vdf), "\"path\"\\s*\"([^\"]+)\""))
				libraries.Add(m.Groups[1].Value.Replace(@"\\", @"\"));
		}
		var folders = new List<string>();
		foreach (var library in libraries) folders.Add(Path.Combine(library, @"steamapps\common", GameFolder));
		return folders;
	}

	static string PickGame()
	{
		using (var dialog = new FolderBrowserDialog())
		{
			dialog.Description = T("Call of Duty: Modern Warfare Remastered 폴더(" + GameExe + "가 있는 곳)를 고르세요.",
				"Choose the Call of Duty: Modern Warfare Remastered folder (the one with " + GameExe + ").");
			while (dialog.ShowDialog() == DialogResult.OK)
			{
				if (IsGame(dialog.SelectedPath)) return dialog.SelectedPath;
				MessageBox.Show(T(GameExe + "가 없는 폴더입니다.", "That folder has no " + GameExe + "."), Title, MessageBoxButtons.OK, MessageBoxIcon.Warning);
			}
		}
		return null;
	}

	static bool Running(string name) { return Process.GetProcessesByName(name).Length > 0; }

	// Overwrites the mod's own files only; game files are never touched.
	static void Extract(string game)
	{
		var root = Path.GetFullPath(game) + Path.DirectorySeparatorChar;
		using (var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream("payload.zip"))
		using (var zip = new ZipArchive(stream, ZipArchiveMode.Read))
			foreach (var entry in zip.Entries)
			{
				var target = Path.GetFullPath(Path.Combine(game, entry.FullName));
				if (!target.StartsWith(root, StringComparison.OrdinalIgnoreCase)) throw new IOException("bad payload path: " + entry.FullName);
				if (entry.FullName.EndsWith("/")) { Directory.CreateDirectory(target); continue; }
				Directory.CreateDirectory(Path.GetDirectoryName(target));
				entry.ExtractToFile(target, true);
			}
	}

	static void Shortcut(string game)
	{
		try
		{
			var desktop = Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory);
			var shell = Activator.CreateInstance(Type.GetTypeFromProgID("WScript.Shell"));
			var link = shell.GetType().InvokeMember("CreateShortcut", BindingFlags.InvokeMethod, null, shell,
				new object[] { Path.Combine(desktop, Title + ".lnk") });
			Action<string, object> set = (name, value) => link.GetType().InvokeMember(name, BindingFlags.SetProperty, null, link, new[] { value });
			set("TargetPath", Path.Combine(game, "overlord.exe"));
			set("WorkingDirectory", game);
			set("IconLocation", Path.Combine(game, "overlord.exe") + ",0");
			link.GetType().InvokeMember("Save", BindingFlags.InvokeMethod, null, link, null);
		}
		catch { } // A missing shortcut never fails the install.
	}
}
