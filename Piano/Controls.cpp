#include "stdafx.h"
#include "Controls.h"
#include "MainWindow.h"
#include "Piano.h"
#include "Cursor.h"
#include "Fingering\TrellisGraph_Facade.h"
#include "Keyboard\IKeyboard.h"

using namespace std;
using namespace boost;

HWND
Controls::hDlgControls	= nullptr,

Controls::midiLog		= nullptr,
Controls::leftHand		= nullptr,
Controls::rightHand		= nullptr,
Controls::progressLeft	= nullptr,
Controls::progressRight	= nullptr,

Controls::trackList		= nullptr,
Controls::checkAll		= nullptr,

Controls::scrollBar		= nullptr,
Controls::playButton	= nullptr;

bool Controls::isPercussionTrack_			= false;
const HBRUSH Controls::trackListBoxBrush_	= CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));

HWND Controls::time_		= nullptr;
bool Controls::isPlaying_	= false;
DWORD Controls::start_		= 0;

void Controls::Reset()
{
	if (isPlaying_) FORWARD_WM_COMMAND(hDlgControls, IDB_PLAY, playButton, 0, SendMessage);
	ComboBox_ResetContent(leftHand);
	ComboBox_ResetContent(rightHand);
	ListBox_ResetContent(trackList);
	CheckDlgButton(hDlgControls, IDC_CHECK_ALL, BST_UNCHECKED);
	InitDialog();
}
void Controls::InitDialog()
{
	Edit_SetText(midiLog, TEXT("MIDI info and errors if any"));

	Edit_SetText(time_, TEXT("Time 0:00:00"));

	ComboBox_AddString(leftHand, TEXT("None"));
	ComboBox_AddString(rightHand, TEXT("None"));
	ComboBox_SetItemData(leftHand, 0, -1);
	ComboBox_SetItemData(rightHand, 0, -1);
	ComboBox_SetCurSel(leftHand, 0);
	ComboBox_SetCurSel(rightHand, 0);
}
inline BOOL Controls::OnInitDialog(const HWND hDlg, HWND, LPARAM)
{
	hDlgControls	= hDlg;

	scrollBar		= GetDlgItem(hDlg, IDC_SCROLLBAR);

	playButton		= GetDlgItem(hDlg, IDB_PLAY);

	midiLog			= GetDlgItem(hDlg, IDC_MIDI_LOG);
	time_			= GetDlgItem(hDlg, IDC_TIME);

	leftHand		= GetDlgItem(hDlg, IDC_LEFT_HAND);
	rightHand		= GetDlgItem(hDlg, IDC_RIGHT_HAND);
	progressLeft	= GetDlgItem(hDlg, IDC_PROGRESS_LEFT);
	progressRight	= GetDlgItem(hDlg, IDC_PROGRESS_RIGHT);
	
	trackList		= GetDlgItem(hDlg, IDC_TRACKS);
	checkAll		= GetDlgItem(hDlg, IDC_CHECK_ALL);

	InitDialog();

	return true;
}
inline void Controls::OnDestroyDialog(const HWND)
{
	DeleteBrush(trackListBoxBrush_);
}

inline void Controls::StopPlaying()
{
	if (isPlaying_) FORWARD_WM_COMMAND(hDlgControls, IDB_PLAY, playButton, 0, SendMessage);
}
void Controls::UpdateTime(const DWORD dwTime)
{
	if (dwTime < start_) start_ = dwTime;
	const auto currTime(dwTime - start_);
	ScrollBar_SetPos(scrollBar, static_cast<int>(currTime), true);

	const auto seconds(currTime / 1'000), milliSec(currTime % 1'000);
	Edit_SetText(time_, (wformat{ TEXT("Time %u:%02u:%02u") } %
		(seconds / 60) % (seconds % 60) % (milliSec / 10)).str().c_str());
}
#pragma warning(push)
#pragma warning(disable:5045) // Compiler will insert Spectre mitigation for memory load
void AssignFinger(const vector<vector<vector<string>>>& fingers, size_t trackNo, bool leftHand = false)
{
	for (size_t i(0); i < fingers.at(trackNo).at(Piano::indexes.at(trackNo)).size(); ++i)
	{
		auto note(Piano::notes.at(trackNo).at(Piano::indexes.at(trackNo)).cbegin());
		std::advance(note, i);
		Piano::keyboard->AssignFinger(note->first, fingers.at(trackNo)
			.at(Piano::indexes.at(trackNo)).at(i).c_str(), leftHand);
	}
}
int Controls::PlayTrack(const size_t trackNo, const DWORD dwTime)
{
	auto result(0);

	for (; Piano::indexes.at(trackNo) < Piano::notes.at(trackNo).size()
		&& static_cast<time_t>(dwTime) - (static_cast<time_t>(start_)
			+ Piano::milliSeconds.at(trackNo).at(Piano::indexes.at(trackNo)).first) >= 0;
		++Piano::indexes.at(trackNo))
	{
		for (const auto& note : Piano::notes.at(trackNo).at(Piano::indexes.at(trackNo)))
		{
			Piano::keyboard->PressKey(note);
			if (Piano::leftTrack && *Piano::leftTrack == trackNo)
				AssignFinger(Piano::fingersLeft, trackNo, true);
			if (Piano::rightTrack && *Piano::rightTrack == trackNo)
				AssignFinger(Piano::fingersRight, trackNo);
		}
		result = 1;
	}

	// If next note starts very soon (almost immediately), then do not play current chord yet,
	// but wait for the next note and append it to the current chord and play them simalteniously
	// (this way it would probably be more convenient to watch chords with finger numbers while on "Pause".
	// However, this works only in 2D-mode:
	if (Piano::indexes.at(trackNo) < Piano::notes.at(trackNo).size() && (result &&
		static_cast<time_t>(dwTime) - (static_cast<time_t>(start_)
			+ Piano::milliSeconds.at(trackNo).at(Piano::indexes.at(trackNo)).first) >= 0))
		result = INT16_MIN;

	return result;
}
#pragma warning(pop)
bool Controls::OnTimer(const HWND hWndMain, const DWORD dwTime)
{
	auto result(false);

	UpdateTime(dwTime);

	if (typeid(*Piano::keyboard) == typeid(Keyboard3D)) Piano::keyboard->ReleaseKeys();
	else assert("Wrong keyboard class" && typeid(*Piano::keyboard) == typeid(Keyboard2D));
	
	if (accumulate(Piano::tracks.cbegin(), Piano::tracks.cend(), 0, [dwTime](int val, size_t track)
		{
			return val + PlayTrack(track, dwTime);
		}
		) > 0)	// std::any_of() and std::all_of() return as soon as result is found,
				// but we need to play all tracks anyway
	{
		InvalidateRect(hWndMain, nullptr, false);
		result = true;
	}

	if (isPlaying_ && all_of(Piano::tracks.cbegin(), Piano::tracks.cend(), [](size_t track)
		{
			return Piano::indexes.at(track) >= Piano::notes.at(track).size();
		}))
	{
		StopPlaying();
		ScrollBar_SetPos(scrollBar, 0, true);
		fill(Piano::indexes.begin(), Piano::indexes.end(), 0);
	}

	return result;
}
void CALLBACK Controls::OnTimer(const HWND hWndMain, UINT, UINT_PTR, const DWORD dwTime)
{
	OnTimer(hWndMain, dwTime);
}


void Controls::RewindTracks(const int pos)
{
	for (const auto& track : Piano::tracks) Piano::indexes.at(track) = static_cast<size_t>(
		lower_bound(Piano::milliSeconds.at(track).cbegin(), Piano::milliSeconds.at(track).cend(),
			make_pair(static_cast<unsigned>(pos), static_cast<unsigned>(pos)),
			[](const pair<unsigned, unsigned>& lhs, const pair<unsigned, unsigned>& rhs)
			{
				return lhs.first < rhs.first;
			})
		- Piano::milliSeconds.at(track).cbegin());
}
void Controls::UpdateScrollBar(int pos)
{
	pos += ScrollBar_GetPos(scrollBar);

	if (pos < 0) pos = 0;
	auto minPos_unused(0),	// otherwise PreFast warning C6387:
							// '_Param_(3)' could be '0' for the function 'GetScrollRange'
		maxPos(0);
	ScrollBar_GetRange(scrollBar, &minPos_unused, &maxPos);
	if (pos > maxPos) pos = maxPos;

	RewindTracks(pos);
	if (isPlaying_) start_ += ScrollBar_GetPos(scrollBar) - pos;
	else UpdateTime(static_cast<DWORD>(pos));
}
#pragma warning(push)
#pragma warning(disable:5045) // Compiler will insert Spectre mitigation for memory load
void Controls::NextChord()
{
	if (Piano::tracks.empty())
		MessageBox(hDlgControls, TEXT("No tracks are chosen, nothing to play yet"),
			TEXT("Choose tracks"), MB_ICONASTERISK);
	else for (;;)
	{
		const auto track(*min_element(Piano::tracks.cbegin(), Piano::tracks.cend(),
			[](size_t left, size_t right)
			{
				return Piano::indexes.at(left) >= Piano::milliSeconds.at(left).size()	? false
					: Piano::indexes.at(right) >= Piano::milliSeconds.at(right).size()	? true
					: Piano::milliSeconds.at(left).at(Piano::indexes.at(left)).first
						< Piano::milliSeconds.at(right).at(Piano::indexes.at(right)).first;
			}));
		if (Piano::indexes.at(track) >= Piano::milliSeconds.at(track).size()
				|| OnTimer(GetParent(hDlgControls), start_ + Piano::milliSeconds
					.at(track).at(Piano::indexes.at(track)).second))
			break;
	}
}
void Controls::PrevChord()
{
	if (!Piano::tracks.empty())
	{
		auto track(*max_element(Piano::tracks.cbegin(), Piano::tracks.cend(),
			[](size_t left, size_t right)
			{
				return !Piano::indexes.at(right) ? false : !Piano::indexes.at(left) ? true
					: Piano::milliSeconds.at(left).at(Piano::indexes.at(left) - 1).second
						< Piano::milliSeconds.at(right).at(Piano::indexes.at(right) - 1).second;
			}));
		for (auto finish(false); !finish;)
		{
			if (Piano::indexes.at(track)) --Piano::indexes.at(track);
			auto prevTime(Piano::milliSeconds.at(track).at(Piano::indexes.at(track)).first);
			for (; Piano::indexes.at(track) && prevTime
						- Piano::milliSeconds.at(track).at(Piano::indexes.at(track) - 1).second
					< Piano::timerTick;
				prevTime = Piano::milliSeconds.at(track).at(--Piano::indexes.at(track)).second)
				;
			finish = true;
			for (const auto& anotherTrack : Piano::tracks)
				if (Piano::indexes.at(anotherTrack) &&
					Piano::milliSeconds.at(anotherTrack).at(Piano::indexes.at(anotherTrack) - 1).second
						+ Piano::timerTick > prevTime)
				{
					track = anotherTrack;
					finish = false;
					break;
				}
		}
		auto prevIndexes(Piano::indexes);
		NextChord();
		Piano::indexes = prevIndexes;
	}
}
#pragma warning(pop)
inline void Controls::OnHScroll(const HWND, const HWND hCtl, const UINT code, const int)
	// "int pos" parameter is 16 bit, therefore, 32 bit GetScrollInfo() is used instead
{
	switch (code)
	{
	case SB_LEFT:		UpdateScrollBar(INT_MIN);	if (!isPlaying_) NextChord();	break;
	case SB_RIGHT:		UpdateScrollBar(INT_MAX);	if (!isPlaying_) PrevChord();	break;

	case SB_LINELEFT:								if (!isPlaying_) PrevChord();	break;
	case SB_LINERIGHT:								if (!isPlaying_) NextChord();	break;

	case SB_PAGELEFT:	UpdateScrollBar(-10'000);	if (!isPlaying_) PrevChord();	break;
	case SB_PAGERIGHT:	UpdateScrollBar(10'000);	if (!isPlaying_) NextChord();	break;

	case SB_THUMBTRACK: case SB_THUMBPOSITION:
	{
		SCROLLINFO scrollInfo{ sizeof scrollInfo, SIF_POS | SIF_TRACKPOS };
		GetScrollInfo(hCtl, SB_CTL, &scrollInfo);
		UpdateScrollBar(scrollInfo.nTrackPos - scrollInfo.nPos);
		if (scrollInfo.nTrackPos) NextChord();
	}
	}
}

void Controls::OnPlay()
{
	if (isPlaying_)
	{
		KillTimer(MainWindow::hWndMain, 0);
		start_ = 0;
		Button_SetText(playButton, TEXT("Play"));
		ComboBox_Enable(leftHand, true);
		ComboBox_Enable(rightHand, true);
		isPlaying_ = false;
	}
	else
	{
		if (Piano::tracks.empty())
			MessageBox(hDlgControls, TEXT("No tracks are chosen, nothing to play yet"),
				TEXT("Choose tracks"), MB_ICONASTERISK);
		else
		{
// Consider using 'GetTickCount64' : GetTickCount overflows every 49 days, and code can loop indefinitely
#pragma warning(suppress:28159)
			start_ = GetTickCount() - ScrollBar_GetPos(scrollBar);
			// Potentially throwing function passed to extern C function
			// Undefined behavior may occur if this function throws
#pragma warning(suppress:5039)
			SetTimer(MainWindow::hWndMain, 0, Piano::timerTick, OnTimer);
			Button_SetText(playButton, TEXT("Pause"));
			ComboBox_Enable(leftHand, false);
			ComboBox_Enable(rightHand, false);
			isPlaying_ = true;
		}
	}
}
void Controls::OnBadHandAlloc(const HWND hand, const HWND progressBar, const char* errMsg)
{
	const auto listIndex(ComboBox_GetCurSel(hand));
	const wstring trackName(static_cast<size_t>(
		ComboBox_GetLBTextLen(hand, listIndex)), '\0');
	ComboBox_GetLBText(hand, listIndex, trackName.data());
	if (MessageBoxA(hand, ("Cannot finish fingering calculation: insufficient memory.\n"
		"Probably, track \"" + string(trackName.cbegin(), trackName.cend()) +
		"\" consists of too many notes.\n"
		"The program may behave inadequately"
		" and bullshit may be played until you restart it."
		" Do you want to close the program now?"
		).c_str(), errMsg, MB_ICONHAND | MB_YESNO
	) == IDYES) FORWARD_WM_DESTROY(MainWindow::hWndMain, SendMessage);

	ComboBox_SetCurSel(hand, 0);
	SendMessage(progressBar, PBM_SETPOS, 0, 0);
}
bool Controls::CalcFingers(const HWND hand, const HWND progressBar, const size_t trackNo, TrellisGraph& graph)
{
// Consider using 'GetTickCount64' : GetTickCount overflows every 49 days, and code can loop indefinitely
#pragma warning(suppress:28159)
	auto timeStart(GetTickCount());
	for (size_t i(1); i; i = graph.NextStep())
	{
		SendMessage(progressBar, PBM_SETPOS, i * 95 / Piano::notes.at(trackNo).size(), 0);
// Consider using 'GetTickCount64' : GetTickCount overflows every 49 days, and code can loop indefinitely
#pragma warning(suppress:28159)
		if (static_cast<int>(GetTickCount()) - static_cast<int>(timeStart) > 10'000)
			if (MessageBox(hDlgControls,
				TEXT("It seems that fingering calculation might take a while.\n")
				TEXT("Press OK if you want to continue waiting."),
				TEXT("Lots of fingering combinations"),
				MB_ICONQUESTION | MB_OKCANCEL | MB_DEFBUTTON2) == IDCANCEL)
			{
				const auto prevSelection(hand == leftHand ? Piano::leftTrack : Piano::rightTrack);
				if (!prevSelection) ComboBox_SetCurSel(hand, 0);
				else for (auto j(0); j < ComboBox_GetCount(hand); ++j)
					if (*prevSelection == static_cast<size_t>(ComboBox_GetItemData(hand, j)))
					{
						ComboBox_SetCurSel(hand, j);
						break;
					}
				SendMessage(progressBar, PBM_SETPOS, 0, 0);
				return false;
			}
			else timeStart = USER_TIMER_MAXIMUM;
	}
	return true;
}
void Controls::UpdateFingers(const HWND hand, const HWND progressBar, const size_t trackNo)
{
	vector<set<int16_t>> notes(Piano::notes.at(trackNo).size(), set<int16_t>());
	transform(Piano::notes.at(trackNo).cbegin(), Piano::notes.at(trackNo).cend(),
		notes.begin(), [](const map<int16_t, float> input)
	{
		set<int16_t> result;
		for (const auto& note_volume : input) result.insert(note_volume.first);
		return result;
	});

	TrellisGraph graph(notes, hand == leftHand);
	Cursor cursorWait;
	if (CalcFingers(hand, progressBar, trackNo, graph))
	{
		graph.Finish();
		(hand == leftHand ? Piano::fingersLeft.at(trackNo) : Piano::fingersRight.at(trackNo))
			= graph.GetResult();
		(hand == leftHand ? Piano::leftTrack : Piano::rightTrack)
			= make_unique<size_t>(trackNo);
		SendMessage(progressBar, PBM_SETPOS, 100, 0);
	}
}
void Controls::OnLeftRightHand(const HWND hand)
{
	const auto progressBar(hand == leftHand ? progressLeft : progressRight);
	const auto trackNo(ComboBox_GetItemData(hand, ComboBox_GetCurSel(hand)));
	if (trackNo > -1)
	{
		const auto track(static_cast<size_t>(trackNo));
		if (hand == leftHand ? Piano::fingersLeft.at(track).empty() : Piano::fingersRight.at(track).empty())
			try
		{
			UpdateFingers(hand, progressBar, track);
		}
		catch (const bad_alloc& e)
		{
			OnBadHandAlloc(hand, progressBar, e.what());
		}
		else
		{
			(hand == leftHand ? Piano::leftTrack : Piano::rightTrack) = make_unique<size_t>(track);
			SendMessage(progressBar, PBM_SETPOS, 100, 0);
		}
	}
	else
	{
		(hand == leftHand ? Piano::leftTrack : Piano::rightTrack) = nullptr;
		SendMessage(progressBar, PBM_SETPOS, 0, 0);
	}

	FORWARD_WM_COMMAND(hDlgControls, IDC_TRACKS, trackList, LBN_SELCHANGE, SendMessage);
}
void Controls::OnTrackList()
{
	for (auto i(0); i < ListBox_GetCount(trackList); ++i) if (ListBox_GetSel(trackList, i)
		&& Piano::percussions.at(static_cast<size_t>(ListBox_GetItemData(trackList, i))))
	{
		isPercussionTrack_ = true;
		ListBox_SetSel(trackList, false, i);
	}

	vector<int> items(static_cast<size_t>(ListBox_GetSelCount(trackList)), 0);
	ListBox_GetSelItems(trackList, items.size(), items.data());

	Piano::tracks.clear();
	Piano::tracks.reserve(items.size());
	for (const auto& item : items)
		Piano::tracks.push_back(static_cast<size_t>(ListBox_GetItemData(trackList, item)));

	if (Piano::leftTrack
		&& find(Piano::tracks.cbegin(), Piano::tracks.cend(), *Piano::leftTrack)
		== Piano::tracks.cend()) Piano::tracks.push_back(*Piano::leftTrack);
	if (Piano::rightTrack
		&& find(Piano::tracks.cbegin(), Piano::tracks.cend(), *Piano::rightTrack)
		== Piano::tracks.cend()) Piano::tracks.push_back(*Piano::rightTrack);

	RewindTracks(ScrollBar_GetPos(scrollBar));
}
void Controls::OnCheckAll()
{
	if (IsDlgButtonChecked(hDlgControls, IDC_CHECK_ALL) == BST_CHECKED)
	{
		for (auto i(0); i < ListBox_GetCount(trackList); ++i)
			if (!Piano::percussions.at(static_cast<size_t>(ListBox_GetItemData(trackList, i))))
				ListBox_SetSel(trackList, true, i);
	}
	else ListBox_SelItemRange(trackList, false, 0, ListBox_GetCount(trackList) - 1);

	FORWARD_WM_COMMAND(hDlgControls, IDC_TRACKS, trackList, LBN_SELCHANGE, SendMessage);
}
void Controls::OnCommand(const HWND hDlg, const int id, const HWND hCtrl, const UINT notifyCode)
{
	switch (id)
	{
	case IDB_PLAY: OnPlay();
		break;
	case IDC_LEFT_HAND: case IDC_RIGHT_HAND:
		if (notifyCode == CBN_SELCHANGE) OnLeftRightHand(hCtrl);
		break;
	case IDC_TRACKS:
		if (notifyCode == LBN_SELCHANGE) OnTrackList();
		break;
	case IDC_CHECK_ALL:
		OnCheckAll();
		break;
	case IDC_NORM_VOL:
		Piano::keyboard->NormalizeVolume(IsDlgButtonChecked(hDlg, id) == BST_CHECKED);
	}
}

inline HBRUSH Controls::OnCtlColorListBox(const HWND, const HDC hDC, const HWND, const int type)
{
	assert("Only ListBox should be colored" && type == CTLCOLOR_LISTBOX);
#ifdef NDEBUG
	UNREFERENCED_PARAMETER(type);
#endif
	SetTextColor(hDC, isPercussionTrack_ ? RGB(0xAF, 0xAF, 0xAF) : RGB(0, 0, 0));
	isPercussionTrack_ = false;
	return trackListBoxBrush_;
}

void Controls::OnMouseMove(const HWND, const int, const int, const UINT)
{
	// Do not freeze 3D-Piano while moving the mouse
	if (typeid(*Piano::keyboard) == typeid(Keyboard3D))
	{
// Consider using 'GetTickCount64' : GetTickCount overflows every 49 days, and code can loop indefinitely
#pragma warning(suppress:28159)
		if (isPlaying_) OnTimer(MainWindow::hWndMain, GetTickCount());
	}
	else assert("Wrong keyboard class" && typeid(*Piano::keyboard) == typeid(Keyboard2D));
}

INT_PTR CALLBACK Controls::Main(const HWND hDlg, const UINT message,
	const WPARAM wParam, const LPARAM lParam)
{
	switch (message)
	{
		HANDLE_MSG(hDlg, WM_INITDIALOG,			OnInitDialog);
		HANDLE_MSG(hDlg, WM_DESTROY,			OnDestroyDialog);
		HANDLE_MSG(hDlg, WM_HSCROLL,			OnHScroll);
		HANDLE_MSG(hDlg, WM_COMMAND,			OnCommand);
		HANDLE_MSG(hDlg, WM_CTLCOLORLISTBOX,	OnCtlColorListBox);
		HANDLE_MSG(hDlg, WM_MOUSEMOVE,			OnMouseMove);
	default: return false;
	}
}