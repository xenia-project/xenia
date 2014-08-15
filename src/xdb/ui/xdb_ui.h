///////////////////////////////////////////////////////////////////////////
// C++ code generated with wxFormBuilder (version Jun  5 2014)
// http://www.wxformbuilder.org/
//
// PLEASE DO "NOT" EDIT THIS FILE!
///////////////////////////////////////////////////////////////////////////

#ifndef __XDB_UI_H__
#define __XDB_UI_H__

#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/string.h>
#include <wx/menu.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/icon.h>
#include <wx/aui/aui.h>
#include <wx/aui/auibar.h>
#include <wx/statusbr.h>
#include <wx/panel.h>
#include <wx/aui/auibook.h>
#include <wx/frame.h>
#include <wx/stattext.h>
#include <wx/filepicker.h>
#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/dialog.h>

///////////////////////////////////////////////////////////////////////////

namespace xdb
{
	namespace ui
	{
		///////////////////////////////////////////////////////////////////////////////
		/// Class MainFrameBase
		///////////////////////////////////////////////////////////////////////////////
		class MainFrameBase : public wxFrame 
		{
			private:
			
			protected:
				wxMenuBar* menu_bar_;
				wxMenu* file_menu_;
				wxAuiToolBar* tool_bar_;
				wxAuiToolBarItem* tool_test_; 
				wxStatusBar* status_bar_;
				wxAuiNotebook* notebook_;
				wxPanel* m_panel3;
				wxPanel* m_panel4;
				
				// Virtual event handlers, overide them in your derived class
				virtual void OnIdle( wxIdleEvent& event ) { event.Skip(); }
				
			
			public:
				
				MainFrameBase( wxWindow* parent, wxWindowID id = wxID_ANY, const wxString& title = wxT("xenia debugger"), const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize( 1024,768 ), long style = wxDEFAULT_FRAME_STYLE|wxTAB_TRAVERSAL );
				wxAuiManager m_mgr;
				
				~MainFrameBase();
			
		};
		
		///////////////////////////////////////////////////////////////////////////////
		/// Class OpenPostmortemTraceDialogBase
		///////////////////////////////////////////////////////////////////////////////
		class OpenPostmortemTraceDialogBase : public wxDialog 
		{
			private:
			
			protected:
				wxFilePickerCtrl* trace_file_picker_;
				wxFilePickerCtrl* content_file_picker_;
				wxStdDialogButtonSizer* dialog_buttons_;
				wxButton* dialog_buttons_OK;
				wxButton* dialog_buttons_Cancel;
				
				// Virtual event handlers, overide them in your derived class
				virtual void OnCancelButtonClick( wxCommandEvent& event ) { event.Skip(); }
				virtual void OnOKButtonClick( wxCommandEvent& event ) { event.Skip(); }
				
			
			public:
				
				OpenPostmortemTraceDialogBase( wxWindow* parent, wxWindowID id = wxID_ANY, const wxString& title = wxT("Open Postmortem Trace"), const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize( -1,-1 ), long style = wxDEFAULT_DIALOG_STYLE ); 
				~OpenPostmortemTraceDialogBase();
			
		};
		
	} // namespace ui
} // namespace xdb

#endif //__XDB_UI_H__
