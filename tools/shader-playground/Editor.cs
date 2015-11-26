using Microsoft.Xna.Framework.Graphics;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Drawing;
using System.IO;
using System.Text;
using System.Windows.Forms;

namespace shader_playground {
  public partial class Editor : Form {
    public Editor() {
      InitializeComponent();

      wordsTextBox.Click += WordsTextBox_Click;

      sourceCodeTextBox.TextChanged += SourceCodeTextBox_TextChanged;
      sourceCodeTextBox.Text = string.Join(
        "\r\n", new string[] {
            "xps_3_0",
            "dcl_texcoord1 r0",
            "dcl_color r1.xy",
            "exec",
            "alloc colors",
            "exece",
            "mad oC0, r0, r1.y, c0",
            "mul r4.xyz, r1.xyz, c0.xyz",
            "+ adds r5.w, r0.xy",
            "cnop",
            });
    }

    private void WordsTextBox_Click(object sender, EventArgs e) {
      wordsTextBox.SelectAll();
      wordsTextBox.Copy();
    }

    void SourceCodeTextBox_TextChanged(object sender, EventArgs e) {
      Assemble(sourceCodeTextBox.Text);
    }

    class NopIncludeHandler : CompilerIncludeHandler {
      public override Stream Open(CompilerIncludeHandlerType includeType,
                             string filename) {
        throw new NotImplementedException();
      }
    }

    void Assemble(string shaderSourceCode) {
      shaderSourceCode += "\ncnop";
      shaderSourceCode += "\ncnop";
      var preprocessorDefines = new CompilerMacro[2];
      preprocessorDefines[0].Name = "XBOX";
      preprocessorDefines[0].Name = "XBOX360";
      var includeHandler = new NopIncludeHandler();
      var options = CompilerOptions.None;
      var compiledShader = ShaderCompiler.AssembleFromSource(
          shaderSourceCode, preprocessorDefines, includeHandler, options,
          Microsoft.Xna.Framework.TargetPlatform.Xbox360);

      DumpWords(compiledShader.GetShaderCode());

      var disassembledSourceCode = compiledShader.ErrorsAndWarnings;
      disassembledSourceCode = disassembledSourceCode.Replace("\n", "\r\n");
      if (disassembledSourceCode.IndexOf("// PDB hint 00000000-00000000-00000000") == -1) {
        outputTextBox.Text = disassembledSourceCode;
        return;
      }
      var prefix = disassembledSourceCode.Substring(
          0, disassembledSourceCode.IndexOf(':'));
      disassembledSourceCode =
          disassembledSourceCode.Replace(prefix + ": warning X7102: ", "");
      disassembledSourceCode = disassembledSourceCode.Replace(
          "// PDB hint 00000000-00000000-00000000\r\n", "");
      var firstLine = disassembledSourceCode.IndexOf("//");
      disassembledSourceCode = disassembledSourceCode.Substring(firstLine);
      disassembledSourceCode = disassembledSourceCode.Trim();
      outputTextBox.Text = disassembledSourceCode;
    }

    void DumpWords(byte[] shaderCode) {
      if (shaderCode == null || shaderCode.Length == 0) {
        wordsTextBox.Text = "";
        return;
      }

      uint[] swappedCode = new uint[shaderCode.Length / sizeof(uint)];
      Buffer.BlockCopy(shaderCode, 0, swappedCode, 0, shaderCode.Length);
      for (int i = 0; i < swappedCode.Length; ++i) {
        swappedCode[i] = SwapBytes(swappedCode[i]);
      }
      var sb = new StringBuilder();
      sb.Append("const uint32_t shader_words[] = {");
      for (int i = 0; i < swappedCode.Length; ++i) {
        sb.AppendFormat("0x{0:X8}, ", swappedCode[i]);
      }
      sb.Append("};");
      wordsTextBox.Text = sb.ToString();
      wordsTextBox.SelectAll();
    }

    uint SwapBytes(uint x) {
      return ((x & 0x000000ff) << 24) +
             ((x & 0x0000ff00) << 8) +
             ((x & 0x00ff0000) >> 8) +
             ((x & 0xff000000) >> 24);
    }

  }
}
