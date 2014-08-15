///////////////////////////////////////////////////////////////////////////
// C++ code generated with wxFormBuilder (version Jun  5 2014)
// http://www.wxformbuilder.org/
//
// PLEASE DO "NOT" EDIT THIS FILE!
///////////////////////////////////////////////////////////////////////////

#include "xdb_ui.h"

///////////////////////////////////////////////////////////////////////////
using namespace xdb::ui;

MainFrameBase::MainFrameBase( wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style ) : wxFrame( parent, id, title, pos, size, style )
{
	this->SetSizeHints( wxDefaultSize, wxDefaultSize );
	m_mgr.SetManagedWindow(this);
	m_mgr.SetFlags(wxAUI_MGR_DEFAULT|wxAUI_MGR_LIVE_RESIZE);
	
	menu_bar_ = new wxMenuBar( 0 );
	file_menu_ = new wxMenu();
	menu_bar_->Append( file_menu_, wxT("File") ); 
	
	this->SetMenuBar( menu_bar_ );
	
	tool_bar_ = new wxAuiToolBar( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_TB_HORZ_LAYOUT ); 
	tool_test_ = tool_bar_->AddTool( wxID_ANY, wxT("tool"), wxNullBitmap, wxNullBitmap, wxITEM_NORMAL, wxEmptyString, wxEmptyString, NULL ); 
	
	tool_bar_->Realize();
	m_mgr.AddPane( tool_bar_, wxAuiPaneInfo().Top().CaptionVisible( false ).CloseButton( false ).Dock().Resizable().FloatingSize( wxDefaultSize ).DockFixed( false ).BottomDockable( false ).LeftDockable( false ).RightDockable( false ).Layer( 10 ).ToolbarPane() );
	
	status_bar_ = this->CreateStatusBar( 1, wxST_SIZEGRIP, wxID_ANY );
	notebook_ = new wxAuiNotebook( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_NB_TAB_EXTERNAL_MOVE|wxAUI_NB_TAB_MOVE|wxAUI_NB_TAB_SPLIT|wxAUI_NB_TOP|wxAUI_NB_WINDOWLIST_BUTTON );
	m_mgr.AddPane( notebook_, wxAuiPaneInfo() .Center() .CaptionVisible( false ).CloseButton( false ).PaneBorder( false ).Movable( false ).Dock().Resizable().FloatingSize( wxDefaultSize ).DockFixed( false ).BottomDockable( false ).TopDockable( false ).LeftDockable( false ).RightDockable( false ).Floatable( false ) );
	
	m_panel3 = new wxPanel( notebook_, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL );
	notebook_->AddPage( m_panel3, wxT("a page"), false, wxNullBitmap );
	m_panel4 = new wxPanel( notebook_, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL );
	notebook_->AddPage( m_panel4, wxT("a page"), true, wxNullBitmap );
	
	
	m_mgr.Update();
	this->Centre( wxBOTH );
}

MainFrameBase::~MainFrameBase()
{
	m_mgr.UnInit();
	
}

OpenPostmortemTraceDialogBase::OpenPostmortemTraceDialogBase( wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style ) : wxDialog( parent, id, title, pos, size, style )
{
	this->SetSizeHints( wxDefaultSize, wxDefaultSize );
	
	wxBoxSizer* root_panel_outer;
	root_panel_outer = new wxBoxSizer( wxVERTICAL );
	
	wxPanel* root_panel;
	root_panel = new wxPanel( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL );
	wxFlexGridSizer* root_sizer;
	root_sizer = new wxFlexGridSizer( 0, 1, 0, 0 );
	root_sizer->AddGrowableCol( 0 );
	root_sizer->SetFlexibleDirection( wxVERTICAL );
	root_sizer->SetNonFlexibleGrowMode( wxFLEX_GROWMODE_SPECIFIED );
	
	wxStaticText* info_label;
	info_label = new wxStaticText( root_panel, wxID_ANY, wxT("Use --trace_file= to specify the output file path when running xenia-run."), wxDefaultPosition, wxDefaultSize, 0 );
	info_label->Wrap( -1 );
	root_sizer->Add( info_label, 0, wxALL, 5 );
	
	wxFlexGridSizer* content_sizer;
	content_sizer = new wxFlexGridSizer( 0, 2, 0, 0 );
	content_sizer->AddGrowableCol( 1 );
	content_sizer->SetFlexibleDirection( wxBOTH );
	content_sizer->SetNonFlexibleGrowMode( wxFLEX_GROWMODE_SPECIFIED );
	
	wxStaticText* trace_file_label;
	trace_file_label = new wxStaticText( root_panel, wxID_ANY, wxT("Trace File:"), wxDefaultPosition, wxDefaultSize, 0 );
	trace_file_label->Wrap( -1 );
	content_sizer->Add( trace_file_label, 0, wxALIGN_CENTER_VERTICAL|wxALIGN_RIGHT|wxALL, 5 );
	
	trace_file_picker_ = new wxFilePickerCtrl( root_panel, wxID_ANY, wxEmptyString, wxT("Select a .trace file"), wxT("*.trace"), wxDefaultPosition, wxDefaultSize, wxFLP_DEFAULT_STYLE|wxFLP_FILE_MUST_EXIST|wxFLP_OPEN|wxFLP_USE_TEXTCTRL );
	content_sizer->Add( trace_file_picker_, 1, wxALIGN_CENTER|wxALL|wxEXPAND, 5 );
	
	wxStaticText* content_file_label;
	content_file_label = new wxStaticText( root_panel, wxID_ANY, wxT("Content Path:"), wxDefaultPosition, wxDefaultSize, 0 );
	content_file_label->Wrap( -1 );
	content_sizer->Add( content_file_label, 0, wxALIGN_CENTER_VERTICAL|wxALIGN_RIGHT|wxALL, 5 );
	
	content_file_picker_ = new wxFilePickerCtrl( root_panel, wxID_ANY, wxEmptyString, wxT("Select the source content path"), wxT("*.xex;*.iso;*.*"), wxDefaultPosition, wxDefaultSize, wxFLP_FILE_MUST_EXIST|wxFLP_OPEN|wxFLP_USE_TEXTCTRL );
	content_sizer->Add( content_file_picker_, 1, wxALIGN_CENTER|wxALL|wxEXPAND, 5 );
	
	
	root_sizer->Add( content_sizer, 0, wxEXPAND, 5 );
	
	dialog_buttons_ = new wxStdDialogButtonSizer();
	dialog_buttons_OK = new wxButton( root_panel, wxID_OK );
	dialog_buttons_->AddButton( dialog_buttons_OK );
	dialog_buttons_Cancel = new wxButton( root_panel, wxID_CANCEL );
	dialog_buttons_->AddButton( dialog_buttons_Cancel );
	dialog_buttons_->Realize();
	
	root_sizer->Add( dialog_buttons_, 1, wxEXPAND, 0 );
	
	
	root_panel->SetSizer( root_sizer );
	root_panel->Layout();
	root_sizer->Fit( root_panel );
	root_panel_outer->Add( root_panel, 1, wxEXPAND | wxALL, 10 );
	
	
	this->SetSizer( root_panel_outer );
	this->Layout();
	root_panel_outer->Fit( this );
	
	this->Centre( wxBOTH );
	
	// Connect Events
	dialog_buttons_Cancel->Connect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( OpenPostmortemTraceDialogBase::OnCancelButtonClick ), NULL, this );
	dialog_buttons_OK->Connect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( OpenPostmortemTraceDialogBase::OnOKButtonClick ), NULL, this );
}

OpenPostmortemTraceDialogBase::~OpenPostmortemTraceDialogBase()
{
	// Disconnect Events
	dialog_buttons_Cancel->Disconnect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( OpenPostmortemTraceDialogBase::OnCancelButtonClick ), NULL, this );
	dialog_buttons_OK->Disconnect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( OpenPostmortemTraceDialogBase::OnOKButtonClick ), NULL, this );
	
}
