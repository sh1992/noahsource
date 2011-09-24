#!/usr/bin/perl
#
# distclientwx.pl - wxWidgets GUI for ga-spectroscopy distributor client.
#
use threads;
use threads::shared;
use FindBin;
BEGIN { chdir $FindBin::Bin; push @INC, "$FindBin::Bin/lib" }
use Wx qw(wxOK wxICON_ERROR);
use warnings;
use strict;

package MyApp;
use base 'Wx::App';

sub OnInit {
    my ($self) = @_;
    Wx::InitAllImageHandlers();
    my $frame = MyFrame->new();
    $frame->Show(1);
    $main::FRAME = $frame;
    return 1;
}

sub OnExit {
    main::OnExit();
    exit;
}

package MyFrame;
use base 'Wx::Frame';
use Wx::Event qw(EVT_BUTTON EVT_CLOSE EVT_COMMAND EVT_MENU);
use Wx qw(wxVERTICAL wxHORIZONTAL wxLEFT wxRIGHT wxTOP wxBOTTOM wxEXPAND wxALL wxALIGN_CENTER wxSYS_SYSTEM_FONT wxFONTWEIGHT_BOLD wxGA_SMOOTH wxGA_HORIZONTAL wxID_ABOUT wxID_EXIT wxID_CLOSE wxYES_NO wxICON_QUESTION wxYES wxBITMAP_TYPE_ICO wxDEFAULT_FRAME_STYLE wxRESIZE_BORDER wxMAXIMIZE_BOX);

sub new {
    my ($ref) = @_;
    # Should also turn off wxRESIZE_BOX, but it doesn't seem to exist
    my $style = (wxDEFAULT_FRAME_STYLE) &
        ~((wxRESIZE_BORDER) | (wxMAXIMIZE_BOX));
    my $self = $ref->SUPER::new(undef,          # Parent window
                                -1,             # ID
                                $main::NAME,    # Title
                                [-1, -1],       # Position
                                [-1, -1],       # Size
                                $style
                               );
    my $icons = Wx::IconBundle->new($main::ICONFILE, wxBITMAP_TYPE_ICO);
    $self->SetIcons($icons);
    my $panel = Wx::Panel->new($self, -1);

    #my $button = Wx::Button->new($panel, -1, 'Button');#, [30, 20], [-1, -1]);
    #EVT_BUTTON($self, $button, \&OnButton);

    #my $details = Wx::StaticText->new($panel, -1, "This application is performing thesis-related computations for noah.anderson\@ncf.edu.\nPlease do not quit this application or restart the computer.\n\nI apologize for any inconvenience.");
    # FIXME: Load from server configuration
    my $title = Wx::StaticText->new($panel, -1, $main::SERVERNAME);
    my $details = Wx::StaticText->new($panel, -1, (' 'x8).$main::SERVERDETAIL);
    Embolden($title);
    my $detailsep = Wx::StaticLine->new($panel, -1);
    my $filler = Wx::StaticText->new($panel, -1, '');

    my $vbox = Wx::BoxSizer->new(wxVERTICAL);
    $vbox->Add($title, 0, wxEXPAND|wxTOP|wxLEFT|wxRIGHT, 12);
    $vbox->Add($details, 0, wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM, 12);
    $vbox->Add($detailsep, 0, wxEXPAND|wxBOTTOM, 12);
    $vbox->Add(400, 0, 1, wxEXPAND, 0);

    my @threads = ();
    for ( my $i = 0; $i <= $main::THREADCOUNT; $i++ ) {
        my $threadtitle = ($i > 0) ? "Thread $i" : 'Server';
        my $threadframe = Wx::StaticBox->new($panel, -1, $threadtitle);
        my $threadbox = Wx::StaticBoxSizer->new($threadframe, wxVERTICAL);
        Embolden($threadframe);

        my $status = Wx::StaticText->new($panel, -1, 'Idle');
        my $progress = undef;
        $threadbox->Add($status, 0, wxEXPAND|wxLEFT|wxRIGHT|wxTOP|wxBOTTOM, 6);
        if ( $i > 0 ) {
            $progress = Wx::Gauge->new($panel, -1, 100, [-1,-1], [-1,-1], wxGA_HORIZONTAL|wxGA_SMOOTH);
            $threadbox->Add($progress, 0, wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM, 6);
        }
        $vbox->Add($threadbox, 0, wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM, 12);
        push @threads, [$status, $progress];
    }

    # Menubar
    my $menuFile = Wx::Menu->new();
    my $menuHelp = Wx::Menu->new();
    $menuFile->Append(wxID_CLOSE, "&Close");
    $menuFile->Append(wxID_EXIT, "E&xit...");
    $menuHelp->Append(wxID_ABOUT, "&About $main::NAME...");
    my $menuBar = Wx::MenuBar->new();
    $menuBar->Append($menuFile, "&File");
    $menuBar->Append($menuHelp, "&Help");
    # TODO: Preferences
    $self->SetMenuBar($menuBar);

    # Set up window
    $panel->SetSizer($vbox);
    $vbox->SetSizeHints($self);

    # Tray icon
    my $trayicon = MyTrayIcon->new($self);
    # Events
    EVT_CLOSE($self, sub { my ($self, $event) = @_; $self->OnClose($event, $trayicon) });
    EVT_COMMAND($self, -1, $main::THREAD_EVENT, sub { $_[0]->OnThreadCallback($_[1], \@threads) });
    EVT_MENU($self, wxID_ABOUT, \&OnAbout);
    EVT_MENU($self, wxID_CLOSE, sub { $self->OnToggleWindow(0) });
    EVT_MENU($self, wxID_EXIT, \&OnExit);
    return $self;
}

sub Embolden {
    my ($widget) = @_;
    my $boldfont = $widget->GetFont();
    $boldfont->SetWeight(wxFONTWEIGHT_BOLD);
    $widget->SetFont($boldfont);
}

sub OnButton {
    my ($self, $button) = @_;
    print "Button!\n";
}

sub OnThreadCallback {
    my ($self, $event, $threads) = @_;
    @_ = (); # Avoid "Scalars leaked" error, see Wx::Thread.
    # my $status = $event->GetData();
    my $status = {};
    { lock @main::events; $status = pop @main::events }
    my $thread = $status->{thread} || 0;

    if ( ($status->{mode}||'') eq 'STOPPING' )
        { $self->Close(1); return }
    my $str = main::RenderStatus($status);

    my ($label, $progress) = @{$threads->[$thread]};
    $label->SetLabel($str);
    if ( !$progress ) {}
    elsif ( defined($status->{progress}) ) {
        $progress->SetRange($status->{range}) if defined($status->{range});
        $progress->SetValue($status->{progress});
    } else { $progress->SetValue(0) }

    # Debugging
    $str = "t".($status->{thread}||0)." is $status->{mode}";
    $str .= sprintf(" (%03d)",$status->{progress}) if exists($status->{progress});
    print length($str).":'$str'\n";
}

sub OnAbout {
    my ($self) = @_;
    my $adi = Wx::AboutDialogInfo->new();
    $adi->SetName($main::NAME);
    $adi->SetVersion($main::VERSION);
    # Show author name instead of copyright statement
    $adi->SetCopyright('Noah Anderson <noah.anderson@ncf.edu>');
    #$adi->SetIcon($self->GetIcon());
    Wx::AboutBox($adi);
}

sub OnExit {
    my ($self) = @_;
    my $msg = "Are you sure you want to exit $main::NAME?\n\nThis will interrupt any computations in progress.";#`ipconfig`||
    return unless Wx::MessageBox($msg, $main::NAME, wxYES_NO|wxICON_QUESTION) == wxYES; #, $self
    main::DoExit();
}

sub OnToggleWindow {
    my ($self, $arg) = @_;
    $self->Show(defined($arg) ? $arg : !$self->IsShown());
}

sub OnClose {
    my ($self, $event, $trayicon) = @_;
    if ( $event->CanVeto() ) { $self->OnToggleWindow(); $event->Veto() }
    else {
        $trayicon->RemoveIcon();
        $trayicon->Destroy();
        $self->Destroy();
    }
}

package MyTrayIcon;
use base 'Wx::TaskBarIcon';
use Wx qw(wxID_ABOUT wxID_EXIT wxBITMAP_TYPE_ICO);
use Wx::Event qw(EVT_TASKBAR_LEFT_UP EVT_TASKBAR_CLICK EVT_MENU EVT_TASKBAR_LEFT_DCLICK);

sub new {
    my ($ref, $window) = @_;
    my $self = $ref->SUPER::new();
    #my $icon = Wx::GetWxPerlIcon;#$main::ICON->GetIcon([-1,-1]);
    my $icon = Wx::Icon->new($main::ICONFILE, wxBITMAP_TYPE_ICO, 16, 16);
    $self->SetIcon($icon, $main::NAME);

    # Menu
    my $menu = Wx::Menu->new();
    my $restoreid = $menu->Append(-1, "&Show")->GetId();
    $menu->Append(wxID_ABOUT, "&About...");
    $menu->Append(wxID_EXIT, "E&xit...");

    # Events
    EVT_TASKBAR_LEFT_UP($self, sub { $self->OnClick($window) });
    # 0 because the second firing of LEFT_UP is after LEFT_DCLICK
    EVT_TASKBAR_LEFT_DCLICK($self, sub { $window->OnToggleWindow(0) });
    EVT_TASKBAR_CLICK($self, sub { $self->OnRightClick($menu) });
    EVT_MENU($self, wxID_ABOUT, sub { $window->OnAbout() });
    EVT_MENU($self, $restoreid, sub { $window->OnToggleWindow(1) });
    EVT_MENU($self, wxID_EXIT, sub { $window->OnExit() });
    bless $self, $ref;
    return $self;
}

sub OnClick {
    my ($self, $window) = @_;
    $window->OnToggleWindow();
}

sub OnRightClick {
    my ($self, $menu) = @_;
    $self->PopupMenu($menu);
}

package main;

our $NAME = 'Distributed Computing Client';
our $ICONFILE = $FindBin::Bin . (#($^O eq 'MSWin32') ? '/distclient.exe;1' :
                                 '/box.ico');

our $THREADCOUNT; # FIXME
if ( ! do 'distclient.pl' ) {
    Wx::MessageBox("An error occurred while starting up.\nPlease reinstall the application.\n\n$@\n$!", $main::NAME, wxOK|wxICON_ERROR);
    exit 0;
    # Avoid only-used-once warning (never run)
    our ($SERVERNAME,$SERVERDETAIL) = ('','');
}

our $FRAME = undef;
our $THREAD_EVENT : shared = Wx::NewEventType;
my $app = MyApp->new;

our @events :shared = ();
our $statusposter = sub {
    my ($result) = @_;
    { lock(@events); push @events, $result }
    my $event = new Wx::PlThreadEvent(-1, $THREAD_EVENT, 0); # $result);
    Wx::PostEvent($FRAME, $event) if $FRAME;
};
StartClient();

# Process GUI events from the application this function will not
# return until the last frame is closed
$app->MainLoop;

