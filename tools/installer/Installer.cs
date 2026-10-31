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
		// /silent: install into the detected folder without dialogs (scripted installs and tests).
		if (args.Length > 0 && args[0].Equals("/silent", StringComparison.OrdinalIgnoreCase))
		{
			try
			{
				var found = FindGame();
				if (found == null || Running("h1_sp64_ship") || Running("overlord")) return 2;
				Extract(found);
				return 0;
			}
			catch { return 1; }
		}
		try
		{
			string game = FindGame();
			if (game == null || MessageBox.Show(T("다음 폴더에 설치합니다:\n\n", "Install into:\n\n") + game +
				T("\n\n다른 폴더를 고르려면 '아니요'를 누르세요.", "\n\nChoose 'No' to pick another folder."),
				Title, MessageBoxButtons.YesNo, MessageBoxIcon.Question) == DialogResult.No)
				game = PickGame();
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

	static bool IsGame(string folder) { return folder != null && File.Exists(Path.Combine(folder, GameExe)); }

	// Steam install path from the registry, then every library in libraryfolders.vdf.
	static string FindGame()
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
		foreach (var library in libraries)
		{
			var folder = Path.Combine(library, @"steamapps\common", GameFolder);
			if (IsGame(folder)) return folder;
		}
		return null;
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
