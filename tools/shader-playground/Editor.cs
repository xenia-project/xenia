using Microsoft.Xna.Framework.Graphics;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows.Forms;

namespace shader_playground {
  public partial class Editor : Form {
    string compilerPath_ = @"..\..\..\..\..\build\bin\Windows\Debug\xenia-gpu-shader-compiler.exe";

    FileSystemWatcher compilerWatcher_;
    bool pendingTimer_ = false;

    public Editor() {
      InitializeComponent();

      var scrollUpdateTimer = new Timer();
      scrollUpdateTimer.Interval = 200;
      scrollUpdateTimer.Tick += (object sender, EventArgs e) => {
        UpdateScrollStates();
      };
      scrollUpdateTimer.Start();

      var compilerBinPath = Path.Combine(Directory.GetCurrentDirectory(),
                                         Path.GetDirectoryName(compilerPath_));
      compilerWatcher_ = new FileSystemWatcher(compilerBinPath, "*.exe");
      compilerWatcher_.NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.Size;
      compilerWatcher_.Changed += (object sender, FileSystemEventArgs e) => {
        if (e.Name == Path.GetFileName(compilerPath_)) {
          Invoke((MethodInvoker)delegate {
            if (pendingTimer_) {
              return;
            }
            pendingTimer_ = true;
            var timer = new Timer();
            timer.Interval = 1000;
            timer.Tick += (object sender1, EventArgs e1) => {
              pendingTimer_ = false;
              timer.Dispose();
              Process(sourceCodeTextBox.Text);
            };
            timer.Start();
          });
        }
      };
      compilerWatcher_.EnableRaisingEvents = true;

      wordsTextBox.Click += (object sender, EventArgs e) => {
        wordsTextBox.SelectAll();
        wordsTextBox.Copy();
      };

      sourceCodeTextBox.Click += (object sender, EventArgs e) => {
        Process(sourceCodeTextBox.Text);
      };
      sourceCodeTextBox.TextChanged += (object sender, EventArgs e) => {
        Process(sourceCodeTextBox.Text);
      };

      translationComboBox.SelectedIndex = 0;
      translationComboBox.SelectedIndexChanged += (object sender, EventArgs e) => {
        Process(sourceCodeTextBox.Text);
      };
      vertexShaderComboBox.SelectedIndex = 0;
      vertexShaderComboBox.SelectedIndexChanged += (object sender, EventArgs e) => {
        Process(sourceCodeTextBox.Text);
      };

    sourceCodeTextBox.Text = string.Join(
        Environment.NewLine, new string[] {
"xps_3_0",
"dcl_texcoord1 r0",
"dcl_color r1.xy",
"exec",
"alloc colors",
"exec",
"tfetch1D r2, r0.y, tf0, FetchValidOnly=false",
"tfetch1D r2, r0.x, tf2",
"tfetch2D r3, r3.wx, tf13",
"tfetch2D r[aL+3], r[aL+5].wx, tf13, FetchValidOnly=false, UnnormalizedTextureCoords=true, MagFilter=linear, MinFilter=linear, MipFilter=point, AnisoFilter=max1to1, UseRegisterGradients=true, UseComputedLOD=false, UseRegisterLOD=true, OffsetX=-1.5, OffsetY=1.0",
"tfetch3D r31.w_01, r0.xyw, tf15",
"tfetchCube r5, r1.xyw, tf31",
"        setTexLOD r1.z",
"        setGradientH r1.zyx",
"(!p0)        setGradientV r1.zyx",
"        getGradients r5, r1.xy, tf3",
"        mad oC0, r0, r1.yyyy, c0",
"        mad oC0._, r0, r1.yyyy, c0",
"        mad oC0.x1_, r0, r1.yyyy, c0",
"        mad oC0.x10w, r0, r1.yyyy, c0",
"        mul r4.xyz, r1.xyzz, c5.xyzz",
"        mul r4.xyz, r1.xyzz, c[0 + aL].xyzz",
"        mul r4.xyz, r1.xyzz, c[6 + aL].xyzz",
"        mul r4.xyz, r1.xyzz, c[0 + a0].xyzz",
"        mul r4.xyz, r1.xyzz, c[8 + a0].xyzz",
"      + adds r5.w, r0.xz",
"        cos r6.w, r0.x",
"        adds r5.w, r0.zx",
"        mul r4.xyz, r[aL+1].xyzz, c[8 + a0].xyzz",
"        adds r5.w, r[aL+0].zx",
"        jmp l5",
"ccall b1, l5",
"nop",
"        label l5",
"(!p0)        exec",
"cexec b5, Yield=true",
"cexec !b6",
"        mulsc r3.w, c1.z, r1.w",
"loop i7, L4",
"   label L3",
"   exec",
"   setp_eq r15, c[aL].w",
"   (!p0) add r0, r0, c[aL]",
"(p0) endloop i7, L3",
"label L4",
"exece",
"        mulsc r3.w, c3.z, r6.x",
"        mulsc r3.w, c200.z, r31.x",
"        mov oDepth.x, c3.w",
"        cnop",
        });
    }

    class NopIncludeHandler : CompilerIncludeHandler {
      public override Stream Open(CompilerIncludeHandlerType includeType,
                             string filename) {
        throw new NotImplementedException();
      }
    }

    void Process(string shaderSourceCode) {
      if (shaderSourceCode.IndexOf("xvs_3_0") != -1 || shaderSourceCode.IndexOf("xps_3_0") != -1) {
        shaderSourceCode += "\ncnop";
        shaderSourceCode += "\ncnop";
      }
      var preprocessorDefines = new CompilerMacro[2];
      preprocessorDefines[0].Name = "XBOX";
      preprocessorDefines[1].Name = "XBOX360";
      var includeHandler = new NopIncludeHandler();
      var options = CompilerOptions.None;
      var compiledShader = ShaderCompiler.AssembleFromSource(
          shaderSourceCode, preprocessorDefines, includeHandler, options,
          Microsoft.Xna.Framework.TargetPlatform.Xbox360);

      var disassembledSourceCode = compiledShader.ErrorsAndWarnings;
      disassembledSourceCode = disassembledSourceCode.Replace("\n", Environment.NewLine);
      if (disassembledSourceCode.IndexOf("// PDB hint 00000000-00000000-00000000") == -1) {
        UpdateTextBox(outputTextBox, disassembledSourceCode, false);
        UpdateTextBox(compilerUcodeTextBox, "", false);
        UpdateTextBox(wordsTextBox, "", false);
        return;
      }
      var prefix = disassembledSourceCode.Substring(
          0, disassembledSourceCode.IndexOf(
                 ':', disassembledSourceCode.IndexOf(':') + 1));
      disassembledSourceCode =
          disassembledSourceCode.Replace(prefix + ": ", "");
      disassembledSourceCode = disassembledSourceCode.Replace(
          "// PDB hint 00000000-00000000-00000000" + Environment.NewLine, "");
      var firstLine = disassembledSourceCode.IndexOf("//");
      var warnings = "// " +
                     disassembledSourceCode.Substring(0, firstLine)
                         .Replace(Environment.NewLine, Environment.NewLine + "// ");
      disassembledSourceCode =
          warnings + disassembledSourceCode.Substring(firstLine + 3);
      disassembledSourceCode = disassembledSourceCode.Trim();
      UpdateTextBox(outputTextBox, disassembledSourceCode, true);

      string shaderType =
          shaderSourceCode.IndexOf("vs_") == -1 ? "ps" : "vs";
      var ucodeWords = ExtractAndDumpWords(shaderType, compiledShader.GetShaderCode());
      if (ucodeWords != null) {
        TryCompiler(shaderType, ucodeWords);
      } else {
        UpdateTextBox(compilerUcodeTextBox, "", false);
      }

      if (compilerUcodeTextBox.Text.Length > 0) {
        var sourcePrefix = disassembledSourceCode.Substring(0, disassembledSourceCode.IndexOf("/*"));
        TryRoundTrip(sourcePrefix, compilerUcodeTextBox.Text, compiledShader.GetShaderCode());
      }
    }

    void TryCompiler(string shaderType, uint[] ucodeWords) {
      string ucodePath = Path.Combine(Path.GetTempPath(), "shader_playground_ucode.bin." + shaderType);
      string ucodeDisasmPath = Path.Combine(Path.GetTempPath(), "shader_playground_disasm.ucode.txt");
      string translatedDisasmPath = Path.Combine(Path.GetTempPath(), "shader_playground_disasm.translated.txt");
      if (File.Exists(ucodePath)) {
        File.Delete(ucodePath);
      }
      if (File.Exists(ucodeDisasmPath)) {
        File.Delete(ucodeDisasmPath);
      }
      if (File.Exists(translatedDisasmPath)) {
        File.Delete(translatedDisasmPath);
      }

      byte[] ucodeBytes = new byte[ucodeWords.Length * 4];
      Buffer.BlockCopy(ucodeWords, 0, ucodeBytes, 0, ucodeWords.Length * 4);
      File.WriteAllBytes(ucodePath, ucodeBytes);

      if (!File.Exists(compilerPath_)) {
        UpdateTextBox(compilerUcodeTextBox, "Compiler not found: " + compilerPath_, false);
        return;
      }

      var startInfo = new ProcessStartInfo(compilerPath_);
      startInfo.Arguments = string.Join(" ", new string[]{
        "--shader_input=" + ucodePath,
        "--shader_input_type=" + shaderType,
        "--shader_output=" + ucodeDisasmPath,
        "--shader_output_type=ucode",
      });
      startInfo.WindowStyle = ProcessWindowStyle.Hidden;
      startInfo.CreateNoWindow = true;
      try {
        using (var process = System.Diagnostics.Process.Start(startInfo)) {
          process.WaitForExit();
        }
        string disasmText = File.ReadAllText(ucodeDisasmPath);
        UpdateTextBox(compilerUcodeTextBox, disasmText.Replace("\n", Environment.NewLine), true);
      } catch {
        UpdateTextBox(compilerUcodeTextBox, "COMPILER FAILURE", false);
      }

      string outputType = "ucode";
      switch (translationComboBox.SelectedIndex) {
        case 0:
        case 1:
          outputType = "dxbctext";
          break;
        case 2:
        case 3:
          outputType = "spirvtext";
          break;
      }

      string vertexShaderType = "vertex";
      switch (vertexShaderComboBox.SelectedIndex) {
        case 1:
          vertexShaderType = "linedomaincp";
          break;
        case 2:
          vertexShaderType = "linedomainpatch";
          break;
        case 3:
          vertexShaderType = "triangledomaincp";
          break;
        case 4:
          vertexShaderType = "triangledomainpatch";
          break;
        case 5:
          vertexShaderType = "quaddomaincp";
          break;
        case 6:
          vertexShaderType = "quaddomainpatch";
          break;
      }

      List<string> startArguments = new List<string>{
        "--shader_input=" + ucodePath,
        "--shader_input_type=" + shaderType,
        "--shader_output=" + translatedDisasmPath,
        "--shader_output_type=" + outputType,
        "--vertex_shader_output_type=" + vertexShaderType,
        "--dxbc_source_map=true",
      };
      if (translationComboBox.SelectedIndex == 1 ||
          translationComboBox.SelectedIndex == 3) {
        startArguments.Add("--shader_output_pixel_shader_interlock=true");
      }

      startInfo = new ProcessStartInfo(compilerPath_);
      startInfo.Arguments = string.Join(" ", startArguments.ToArray());
      startInfo.WindowStyle = ProcessWindowStyle.Hidden;
      startInfo.CreateNoWindow = true;
      try {
        using (var process = System.Diagnostics.Process.Start(startInfo)) {
          process.WaitForExit();
        }
        string disasmText = File.ReadAllText(translatedDisasmPath);
        UpdateTextBox(compilerTranslatedTextBox, disasmText.Replace("\n", Environment.NewLine), true);
      } catch {
        UpdateTextBox(compilerTranslatedTextBox, "COMPILER FAILURE", false);
      }
    }

    void TryRoundTrip(string sourcePrefix, string compilerSource, byte[] expectedBytes) {
      var shaderSourceCode = sourcePrefix + compilerSource;
      var preprocessorDefines = new CompilerMacro[2];
      preprocessorDefines[0].Name = "XBOX";
      preprocessorDefines[1].Name = "XBOX360";
      var includeHandler = new NopIncludeHandler();
      var options = CompilerOptions.None;
      var compiledShader = ShaderCompiler.AssembleFromSource(
          shaderSourceCode, preprocessorDefines, includeHandler, options,
          Microsoft.Xna.Framework.TargetPlatform.Xbox360);
      var compiledBytes = compiledShader.GetShaderCode();
      if (compiledBytes == null ||
          compiledBytes.Length != expectedBytes.Length ||
          !MemCmp(compiledBytes, expectedBytes)) {
        compilerUcodeTextBox.BackColor = System.Drawing.Color.Red;
      } else {
        compilerUcodeTextBox.BackColor = System.Drawing.SystemColors.Control;
      }
    }

    void UpdateScrollStates() {
      foreach (var handle in scrollPreserve_.Keys) {
        if (scrollPreserve_[handle]) {
          var scrollInfo = new ScrollInfo();
          scrollInfo.cbSize = Marshal.SizeOf(scrollInfo);
          scrollInfo.fMask = (uint)ScrollInfoMask.SIF_TRACKPOS;
          bool hasScrollInfo = GetScrollInfo(handle, SB_VERT, ref scrollInfo);
          scrollPositions_[handle] = scrollInfo.nTrackPos;
        }
      }
    }

    Dictionary<IntPtr, bool> scrollPreserve_ = new Dictionary<IntPtr, bool>();
    Dictionary<IntPtr, int> scrollPositions_ = new Dictionary<IntPtr, int>();
    void UpdateTextBox(TextBox textBox, string value, bool preserveScroll) {
      scrollPreserve_[textBox.Handle] = preserveScroll;

      textBox.Text = value;

      int previousScroll;
      if (!scrollPositions_.TryGetValue(textBox.Handle, out previousScroll)) {
        previousScroll = 0;
      }
      var scrollInfo = new ScrollInfo();
      scrollInfo.cbSize = Marshal.SizeOf(scrollInfo);
      scrollInfo.fMask = (uint)ScrollInfoMask.SIF_TRACKPOS;
      scrollInfo.nTrackPos = previousScroll;
      SetScrollInfo(textBox.Handle, SB_VERT, ref scrollInfo, 1);

      var ptrWparam = new IntPtr(SB_THUMBPOSITION | previousScroll << 16);
      SendMessage(textBox.Handle, WM_VSCROLL, ptrWparam, IntPtr.Zero);
    }

    private const int SB_VERT = 1;
    private const uint SB_THUMBPOSITION = 4;
    private const uint WM_VSCROLL = 0x115;
    public enum ScrollInfoMask : uint {
      SIF_RANGE = 0x1,
      SIF_PAGE = 0x2,
      SIF_POS = 0x4,
      SIF_DISABLENOSCROLL = 0x8,
      SIF_TRACKPOS = 0x10,
      SIF_ALL = (SIF_RANGE | SIF_PAGE | SIF_POS | SIF_TRACKPOS),
    }
    private struct ScrollInfo {
      public int cbSize;
      public uint fMask;
      public int nMin;
      public int nMax;
      public int nPage;
      public int nPos;
      public int nTrackPos;
    }
    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool GetScrollInfo(IntPtr hwnd, int bar, ref ScrollInfo scrollInfo);
    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool SetScrollInfo(IntPtr hwnd, int bar, ref ScrollInfo scrollInfo, int redraw);
    [DllImport("user32.dll", CharSet = CharSet.Auto)]
    static extern IntPtr SendMessage(IntPtr hWnd, UInt32 Msg, IntPtr wParam, IntPtr lParam);

    bool MemCmp(byte[] a1, byte[] b1) {
      if (a1 == null || b1 == null) {
        return false;
      }
      int length = a1.Length;
      if (b1.Length != length) {
        return false;
      }
      while (length > 0) {
        length--;
        if (a1[length] != b1[length]) {
          return false;
        }
      }
      return true;
    }

    uint[] ExtractAndDumpWords(string shaderType, byte[] shaderCode) {
      if (shaderCode == null || shaderCode.Length == 0) {
        UpdateTextBox(wordsTextBox, "", false);
        return null;
      }

      // Find shader code.
      int byteOffset = (shaderCode[4] << 24) | (shaderCode[5] << 16) |
                       (shaderCode[6] << 8) | (shaderCode[7] << 0);
      int wordOffset = byteOffset / 4;

      uint[] shaderDwords = new uint[(shaderCode.Length - wordOffset) / sizeof(uint)];
      Buffer.BlockCopy(shaderCode, wordOffset * 4, shaderDwords, 0, shaderCode.Length - wordOffset * 4);

      var sb = new StringBuilder();
      sb.Append("const uint32_t shader_dwords[] = {");
      for (int i = 0; i < shaderDwords.Length; ++i) {
        sb.AppendFormat("0x{0:X8}, ", SwapByte(shaderDwords[i]));
      }
      sb.Append("};" + Environment.NewLine);
      sb.Append("shader_type = ShaderType::" + (shaderType == "vs" ? "kVertex" : "kPixel") + ";" + Environment.NewLine);
      UpdateTextBox(wordsTextBox, sb.ToString(), true);
      wordsTextBox.SelectAll();

      return shaderDwords;
    }

    uint SwapByte(uint x) {
      return ((x & 0x000000ff) << 24) +
             ((x & 0x0000ff00) << 8) +
             ((x & 0x00ff0000) >> 8) +
             ((x & 0xff000000) >> 24);
    }
  }
}
