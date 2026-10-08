#include "tinyhat-write-core.h"

#include <commctrl.h>
#include <fcntl.h>
#include <io.h>
#include <wchar.h>

#ifndef TW_VERSION
#define TW_VERSION "0.0.0"
#endif

#define TW_MAX_DRIVES 32
#define ID_LIST 101
#define ID_WRITE 102
#define ID_REFRESH 103
#define ID_PROG 104
#define ID_STATUS 105
#define ID_INTRO 106

typedef struct {
	int disk;
	char target[64];
	wchar_t display[160];
	uint64_t size;
} tw_drive;

static HWND gwin, glist, gwrite, grefresh, gprog, gstatus, gintro;
static tw_drive gdrives[TW_MAX_DRIVES];
static int gndrives;
static uint64_t gtotal;
static int grunning;
static char gerrmsg[512];

static void tw_pump(void)
{
	MSG m;
	while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
		TranslateMessage(&m);
		DispatchMessageW(&m);
	}
}

static wchar_t *tw_widen(const char *s, wchar_t *out, size_t cap)
{
	MultiByteToWideChar(CP_UTF8, 0, s, -1, out, (int)cap);
	out[cap - 1] = 0;
	return out;
}

static void tw_set_status(const char *s)
{
	wchar_t w[512];
	tw_widen(s, w, sizeof(w) / sizeof(w[0]));
	SetWindowTextW(gstatus, w);
}

static int tw_scan(void)
{
	wchar_t drives[512];
	wchar_t *d;
	int n = 0;
	if (!GetLogicalDriveStringsW(511, drives))
		return 0;
	for (d = drives; *d; d += wcslen(d) + 1) {
		char vpath[32];
		char label[64];
		wchar_t wlabel[64];
		DWORD got;
		unsigned char buf[sizeof(VOLUME_DISK_EXTENTS) + sizeof(DISK_EXTENT) * 8];
		VOLUME_DISK_EXTENTS *ext = (VOLUME_DISK_EXTENTS *)buf;
		HANDLE hv, hd;
		int disk, i, seen;
		uint64_t size = 0;
		if (GetDriveTypeW(d) != DRIVE_REMOVABLE)
			continue;
		snprintf(vpath, sizeof(vpath), "\\\\.\\%c:", d[0]);
		hv = CreateFileA(vpath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
				 OPEN_EXISTING, 0, NULL);
		if (hv == INVALID_HANDLE_VALUE)
			continue;
		if (!DeviceIoControl(hv, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, buf,
				     sizeof(buf), &got, NULL)) {
			CloseHandle(hv);
			continue;
		}
		CloseHandle(hv);
		if (!ext->NumberOfDiskExtents)
			continue;
		disk = (int)ext->Extents[0].DiskNumber;
		seen = 0;
		for (i = 0; i < n; i++)
			if (gdrives[i].disk == disk)
				seen = 1;
		if (seen)
			continue;
		snprintf(vpath, sizeof(vpath), "\\\\.\\PhysicalDrive%d", disk);
		hd = CreateFileA(vpath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
				 OPEN_EXISTING, 0, NULL);
		if (hd != INVALID_HANDLE_VALUE) {
			LARGE_INTEGER li;
			if (GetFileSizeEx(hd, &li))
				size = (uint64_t)li.QuadPart;
			CloseHandle(hd);
		}
		wlabel[0] = 0;
		GetVolumeInformationW(d, wlabel, 63, NULL, NULL, NULL, NULL, 0);
		WideCharToMultiByte(CP_UTF8, 0, wlabel, -1, label, sizeof(label), NULL, NULL);
		gdrives[n].disk = disk;
		gdrives[n].size = size;
		snprintf(gdrives[n].target, sizeof(gdrives[n].target), "\\\\.\\PhysicalDrive%d", disk);
		_snwprintf(gdrives[n].display, 159, L"%c:   Disk %d   %hs   %.1f GB", d[0], disk,
			   label[0] ? label : "USB drive", (double)size / 1000000000.0);
		gdrives[n].display[159] = 0;
		n++;
	}
	return n;
}

static void tw_refresh(void)
{
	int i;
	gndrives = tw_scan();
	SendMessageW(glist, LB_RESETCONTENT, 0, 0);
	for (i = 0; i < gndrives; i++)
		SendMessageW(glist, LB_ADDSTRING, 0, (LPARAM)gdrives[i].display);
	EnableWindow(gwrite, gndrives > 0);
	tw_set_status(gndrives ? "Select a USB drive" : "No USB drives found");
}

static int tw_win_ask(void *ud, const char *text)
{
	wchar_t w[600], cap[64];
	(void)ud;
	tw_widen(text, w, sizeof(w) / sizeof(w[0]));
	tw_widen("Overwrite?", cap, 32);
	return MessageBoxW(gwin, w, cap, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
}

static void tw_win_note(void *ud, const char *line)
{
	(void)ud;
	if (!strncmp(line, "SIZE ", 5)) {
		gtotal = strtoull(line + 5, NULL, 10);
	} else if (!strncmp(line, "PROGRESS ", 9)) {
		uint64_t done = strtoull(line + 9, NULL, 10);
		if (gtotal) {
			char msg[64];
			SendMessageW(gprog, PBM_SETPOS, (WPARAM)(done * 100 / gtotal), 0);
			snprintf(msg, sizeof(msg), "%d%% written", (int)(done * 100 / gtotal));
			tw_set_status(msg);
		}
		tw_pump();
	} else if (!strncmp(line, "ERROR ", 6)) {
		snprintf(gerrmsg, sizeof(gerrmsg), "%s", line + 6);
		tw_pump();
	} else if (!strcmp(line, "DONE")) {
		gerrmsg[0] = 0;
		tw_pump();
	}
}

static void tw_start_write(void)
{
	int idx = (int)SendMessageW(glist, LB_GETCURSEL, 0, 0);
	tw_image img;
	tw_hooks hk;
	wchar_t done[64];
	if (idx < 0 || idx >= gndrives || grunning)
		return;
	if (tw_load_image(NULL, &img) < 0) {
		tw_set_status("No embedded image found");
		return;
	}
	gtotal = 0;
	gerrmsg[0] = 0;
	grunning = 1;
	EnableWindow(gwrite, FALSE);
	EnableWindow(grefresh, FALSE);
	SendMessageW(gprog, PBM_SETPOS, 0, 0);
	tw_set_status("Writing, this takes a few minutes");
	hk.ask = tw_win_ask;
	hk.note = tw_win_note;
	hk.ud = NULL;
	tw_do_write(&img, gdrives[idx].target, &hk);
	EnableWindow(gwrite, TRUE);
	EnableWindow(grefresh, TRUE);
	grunning = 0;
	if (gerrmsg[0]) {
		wchar_t w[512];
		tw_widen(gerrmsg, w, sizeof(w) / sizeof(w[0]));
		MessageBoxW(gwin, w, L"Write failed", MB_OK | MB_ICONERROR);
		tw_set_status(gerrmsg);
		return;
	}
	SendMessageW(gprog, PBM_SETPOS, 100, 0);
	tw_widen("Finished. The drive is ready.", done, 32);
	tw_set_status("Finished. The drive is ready.");
	MessageBoxW(gwin, done, L"Done", MB_OK | MB_ICONINFORMATION);
}

static void tw_layout(int w, int h)
{
	int m = 12, top = m;
	HDWP p = BeginDeferWindowPos(6);
	p = DeferWindowPos(p, gintro, NULL, m, top, w - 2 * m, 40, SWP_NOZORDER);
	top += 50;
	p = DeferWindowPos(p, glist, NULL, m, top, w - 2 * m, h - top - 120, SWP_NOZORDER);
	top += h - top - 110;
	p = DeferWindowPos(p, gwrite, NULL, m, top, w - 2 * m - 110, 34, SWP_NOZORDER);
	p = DeferWindowPos(p, grefresh, NULL, w - m - 100, top, 100, 34, SWP_NOZORDER);
	top += 44;
	p = DeferWindowPos(p, gprog, NULL, m, top, w - 2 * m, 22, SWP_NOZORDER);
	top += 30;
	p = DeferWindowPos(p, gstatus, NULL, m, top, w - 2 * m, 24, SWP_NOZORDER);
	EndDeferWindowPos(p);
}

static LRESULT CALLBACK tw_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg) {
	case WM_SIZE:
		tw_layout(LOWORD(lp), HIWORD(lp));
		return 0;
	case WM_COMMAND:
		if (LOWORD(wp) == ID_WRITE)
			tw_start_write();
		else if (LOWORD(wp) == ID_REFRESH)
			tw_refresh();
		return 0;
	case WM_GETMINMAXINFO:
		((MINMAXINFO *)lp)->ptMinTrackSize.x = 480;
		((MINMAXINFO *)lp)->ptMinTrackSize.y = 380;
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}

static int tw_flow_ask(void *ud, const char *text)
{
	char buf[64];
	(void)ud;
	if (text[0])
		fprintf(stderr, "%s [y/N]\n", text);
	if (!fgets(buf, sizeof(buf), stdin))
		return 0;
	return buf[0] == 'y' || buf[0] == 'Y';
}

static void tw_flow_note(void *ud, const char *line)
{
	(void)ud;
	fputs(line, stdout);
	fputc('\n', stdout);
	fflush(stdout);
}

int main(int argc, char **argv)
{
	WNDCLASSW wc;
	RECT rc;
	INITCOMMONCONTROLSEX icc;
	MSG msg;
	tw_image img;
	tw_hooks hk;
	if (argc >= 2 && !strcmp(argv[1], "--version")) {
		printf("tinyhat-write %s\n", TW_VERSION);
		return 0;
	}
	if (argc >= 3 && !strcmp(argv[1], "--flow")) {
		_setmode(_fileno(stdout), _O_BINARY);
		_setmode(_fileno(stdin), _O_BINARY);
		hk.ask = tw_flow_ask;
		hk.note = tw_flow_note;
		hk.ud = NULL;
		if (tw_load_image(argc >= 4 ? argv[3] : NULL, &img) < 0) {
			printf("ERROR no tinyhat.img.gz found\n");
			return 1;
		}
		return tw_do_write(&img, argv[2], &hk);
	}
	icc.dwSize = sizeof(icc);
	icc.dwICC = ICC_PROGRESS_CLASS;
	InitCommonControlsEx(&icc);
	memset(&wc, 0, sizeof(wc));
	wc.lpfnWndProc = tw_proc;
	wc.hInstance = GetModuleHandleW(NULL);
	wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
	wc.lpszClassName = L"TinyHatWrite";
	RegisterClassW(&wc);
	gwin = CreateWindowExW(0, L"TinyHatWrite", L"Tiny Hat USB writer",
			       WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 560, 440,
			       NULL, NULL, wc.hInstance, NULL);
	{
		wchar_t intro[256];
		swprintf(intro, 256, L"Select the USB drive to write Tiny Hat %hs to. Everything on that drive gets erased.", TW_VERSION);
		gintro = CreateWindowExW(0, L"STATIC", intro,
			WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, gwin, (HMENU)ID_INTRO, wc.hInstance, NULL);
	}
	glist = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", NULL,
			       WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOINTEGRALHEIGHT | LBS_NOTIFY,
			       0, 0, 0, 0, gwin, (HMENU)ID_LIST, wc.hInstance, NULL);
	gwrite = CreateWindowExW(0, L"BUTTON", L"Write Tiny Hat to this drive",
				WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 0, 0, 0, 0, gwin,
				(HMENU)ID_WRITE, wc.hInstance, NULL);
	grefresh = CreateWindowExW(0, L"BUTTON", L"Refresh", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
				   gwin, (HMENU)ID_REFRESH, wc.hInstance, NULL);
	gprog = CreateWindowExW(0, PROGRESS_CLASSW, NULL, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
				gwin, (HMENU)ID_PROG, wc.hInstance, NULL);
	gstatus = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, gwin,
				  (HMENU)ID_STATUS, wc.hInstance, NULL);
	SendMessageW(gprog, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
	GetClientRect(gwin, &rc);
	tw_layout(rc.right, rc.bottom);
	tw_refresh();
	ShowWindow(gwin, SW_SHOW);
	UpdateWindow(gwin);
	while (GetMessageW(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	return 0;
}
