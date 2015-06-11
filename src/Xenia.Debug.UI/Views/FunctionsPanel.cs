/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using WeifenLuo.WinFormsUI.Docking;
using Xenia.Debug.UI.Controls;

namespace Xenia.Debug.UI.Views {
  public partial class FunctionsPanel : BasePanel {
    private readonly Debugger debugger;

    public FunctionsPanel(Debugger debugger) {
      InitializeComponent();
      this.debugger = debugger;

      RefreshFunctionList();

      debugger.ModuleList.Changed += UpdateModulesList;
      UpdateModulesList(debugger.ModuleList);
    }

    private void UpdateModulesList(ModuleList sender) {
      modulesComboBox.BeginUpdate();
      modulesComboBox.Items.Clear();
      foreach (Module module in debugger.ModuleList) {
        modulesComboBox.Items.Add(module);
        module.Changed += Module_Changed;
      }
      modulesComboBox.EndUpdate();
    }

    private void Module_Changed(KernelObject sender) {
      if (modulesComboBox.SelectedItem != sender) {
        return;
      }

      RefreshFunctionList();
    }

    private void modulesComboBox_SelectedIndexChanged(object sender, EventArgs e) {
      if (modulesComboBox.SelectedItem == null) {
        return;
      }
      var module = (Module)modulesComboBox.SelectedItem;

      RefreshFunctionList();
    }

    private void RefreshFunctionList() {
      if (modulesComboBox.SelectedItem == null) {
        functionsListBox.Items.Clear();
        functionsListBox.Enabled = false;
        filterTextBox.Enabled = false;
        return;
      }
      functionsListBox.Enabled = true;
      filterTextBox.Enabled = true;
      var module = (Module)modulesComboBox.SelectedItem;

      functionsListBox.BeginUpdate();
      functionsListBox.Items.Clear();

      foreach (Function function in module) {
        functionsListBox.Items.Add(function);
      }

      functionsListBox.EndUpdate();
    }

    private void filterTextBox_TextChanged(object sender, EventArgs e) {
      var module = (Module)modulesComboBox.SelectedItem;

      var filter = filterTextBox.Text.ToLowerInvariant();

      functionsListBox.BeginUpdate();
      functionsListBox.Items.Clear();

      foreach (Function function in module) {
        if (filter.Length == 0 || function.LowerName.Contains(filter)) {
          functionsListBox.Items.Add(function);
        }
      }

      functionsListBox.EndUpdate();
    }

    private void functionsListBox_SelectedIndexChanged(object sender, EventArgs e) {
      var function = (Function)functionsListBox.SelectedItem;
      if (function == null) {
        return;
      }

      MainWindow.OpenFunction(function);
    }
  }
}
