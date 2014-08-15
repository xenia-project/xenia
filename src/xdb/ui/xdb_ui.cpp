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
	file = new wxMenu();
	menu_bar_->Append( file, wxT("File") ); 
	
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
