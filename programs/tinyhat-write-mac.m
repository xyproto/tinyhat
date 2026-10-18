#include "tinyhat-write-core.h"

#import <Cocoa/Cocoa.h>
#include <signal.h>

#ifndef TW_VERSION
#define TW_VERSION "0.0.0"
#endif

#define TW_MAX_DRIVES 32

typedef struct {
	char dev[64];
	char name[160];
	uint64_t size;
} tw_drive;

static tw_drive gdrives[TW_MAX_DRIVES];
static int gndrives;

static int tw_scan(void)
{
	io_iterator_t it;
	io_object_t obj;
	int n = 0;
	if (IOServiceGetMatchingServices(MACH_PORT_NULL, IOServiceMatching("IOMedia"), &it) != KERN_SUCCESS)
		return 0;
	while ((obj = IOIteratorNext(it))) {
		CFTypeRef bsd = IORegistryEntryCreateCFProperty(obj, CFSTR("BSD Name"), kCFAllocatorDefault, 0);
		char name[64];
		if (bsd && n < TW_MAX_DRIVES && CFGetTypeID(bsd) == CFStringGetTypeID() &&
		    CFStringGetCString((CFStringRef)bsd, name, sizeof(name), kCFStringEncodingASCII) &&
		    tw_mac_external(name, gdrives[n].name, sizeof(gdrives[n].name), &gdrives[n].size)) {
			snprintf(gdrives[n].dev, sizeof(gdrives[n].dev), "/dev/r%s", name);
			n++;
		}
		if (bsd)
			CFRelease(bsd);
		IOObjectRelease(obj);
	}
	IOObjectRelease(it);
	return n;
}

static NSString *tw_fmt_size(uint64_t n)
{
	if (n >= 1000000000ull)
		return [NSString stringWithFormat:@"%.1f GB", (double)n / 1000000000.0];
	if (n >= 1000000ull)
		return [NSString stringWithFormat:@"%.1f MB", (double)n / 1000000.0];
	return [NSString stringWithFormat:@"%.1f kB", (double)n / 1000.0];
}

static NSString *tw_quote(NSString *s)
{
	return [NSString stringWithFormat:@"'%@'", [s stringByReplacingOccurrencesOfString:@"'"
									      withString:@"'\\''"]];
}

static NSString *tw_find_image(void)
{
	char exe[4096], real[PATH_MAX];
	NSString *p = [[NSBundle mainBundle] pathForResource:@"tinyhat" ofType:@"img.gz"];
	NSFileManager *fm = [NSFileManager defaultManager];
	if (p && [fm fileExistsAtPath:p])
		return p;
	if (tw_exe_path(exe, sizeof(exe)) == 0 && realpath(exe, real)) {
		NSString *dir = [[NSString stringWithUTF8String:real] stringByDeletingLastPathComponent];
		for (NSString *c in @[ @"tinyhat.img.gz", @"../Resources/tinyhat.img.gz" ]) {
			p = [[dir stringByAppendingPathComponent:c] stringByStandardizingPath];
			if ([fm fileExistsAtPath:p])
				return p;
		}
	}
	return nil;
}

@interface TWApp : NSObject <NSApplicationDelegate, NSTableViewDataSource, NSTableViewDelegate>
@end

@implementation TWApp {
	NSWindow *win;
	NSTableView *table;
	NSButton *writeButton, *refreshButton;
	NSProgressIndicator *prog;
	NSTextField *status;
	NSTask *task;
	NSString *tmpdir;
	int infd;
	uint64_t total;
	BOOL running, finished;
	volatile BOOL exited;
}

- (NSTextField *)label:(NSString *)text frame:(NSRect)r
{
	NSTextField *t = [[NSTextField alloc] initWithFrame:r];
	t.stringValue = text;
	t.bezeled = NO;
	t.drawsBackground = NO;
	t.editable = NO;
	t.selectable = NO;
	return t;
}

- (NSButton *)button:(NSString *)title frame:(NSRect)r action:(SEL)action
{
	NSButton *b = [[NSButton alloc] initWithFrame:r];
	b.title = title;
	b.bezelStyle = NSBezelStyleRounded;
	b.target = self;
	b.action = action;
	return b;
}

- (void)column:(NSString *)ident title:(NSString *)title width:(CGFloat)w
{
	NSTableColumn *c = [[NSTableColumn alloc] initWithIdentifier:ident];
	c.title = title;
	c.width = w;
	c.editable = NO;
	[table addTableColumn:c];
}

- (void)applicationDidFinishLaunching:(NSNotification *)note
{
	NSView *v;
	NSScrollView *scroll;
	NSTextField *intro;
	NSMenu *bar = [NSMenu new], *appMenu = [NSMenu new];
	NSMenuItem *appItem = [NSMenuItem new];
	(void)note;
	infd = -1;
	[appMenu addItemWithTitle:@"Quit Tiny Hat Writer" action:@selector(terminate:) keyEquivalent:@"q"];
	appItem.submenu = appMenu;
	[bar addItem:appItem];
	NSApp.mainMenu = bar;
	win = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 560, 430)
					  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
						    NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
					    backing:NSBackingStoreBuffered
					      defer:NO];
	win.title = @"Tiny Hat USB writer";
	win.minSize = NSMakeSize(480, 380);
	v = win.contentView;
	intro = [self label:[NSString stringWithFormat:@"Select the USB drive to write Tiny Hat %s to. Everything on that drive gets erased.", TW_VERSION]
		      frame:NSMakeRect(12, 388, 536, 34)];
	intro.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin;
	[v addSubview:intro];
	scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(12, 112, 536, 270)];
	scroll.hasVerticalScroller = YES;
	scroll.borderType = NSBezelBorder;
	scroll.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
	table = [[NSTableView alloc] initWithFrame:scroll.bounds];
	[self column:@"dev" title:@"Device" width:110];
	[self column:@"name" title:@"Drive" width:300];
	[self column:@"size" title:@"Size" width:100];
	table.columnAutoresizingStyle = NSTableViewLastColumnOnlyAutoresizingStyle;
	table.dataSource = self;
	table.delegate = self;
	table.target = self;
	table.doubleAction = @selector(onWrite:);
	scroll.documentView = table;
	[v addSubview:scroll];
	writeButton = [self button:@"Write Tiny Hat to this drive" frame:NSMakeRect(8, 70, 440, 32) action:@selector(onWrite:)];
	writeButton.autoresizingMask = NSViewWidthSizable | NSViewMaxYMargin;
	writeButton.keyEquivalent = @"\r";
	[v addSubview:writeButton];
	refreshButton = [self button:@"Refresh" frame:NSMakeRect(452, 70, 100, 32) action:@selector(onRefresh:)];
	refreshButton.autoresizingMask = NSViewMinXMargin | NSViewMaxYMargin;
	[v addSubview:refreshButton];
	prog = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(12, 44, 536, 20)];
	prog.indeterminate = NO;
	prog.minValue = 0;
	prog.maxValue = 1;
	prog.autoresizingMask = NSViewWidthSizable | NSViewMaxYMargin;
	[v addSubview:prog];
	status = [self label:@"" frame:NSMakeRect(12, 12, 536, 24)];
	status.autoresizingMask = NSViewWidthSizable | NSViewMaxYMargin;
	[v addSubview:status];
	[self refresh];
	[win center];
	[win makeKeyAndOrderFront:nil];
	[NSApp activateIgnoringOtherApps:YES];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)app
{
	(void)app;
	return YES;
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)app
{
	(void)app;
	return running ? NSTerminateCancel : NSTerminateNow;
}

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tv
{
	(void)tv;
	return gndrives;
}

- (id)tableView:(NSTableView *)tv objectValueForTableColumn:(NSTableColumn *)col row:(NSInteger)row
{
	(void)tv;
	if (row < 0 || row >= gndrives)
		return nil;
	if ([col.identifier isEqual:@"dev"])
		return [NSString stringWithUTF8String:gdrives[row].dev + 6];
	if ([col.identifier isEqual:@"name"])
		return [NSString stringWithUTF8String:gdrives[row].name];
	return tw_fmt_size(gdrives[row].size);
}

- (void)refresh
{
	gndrives = tw_scan();
	[table reloadData];
	if (gndrives)
		[table selectRowIndexes:[NSIndexSet indexSetWithIndex:0] byExtendingSelection:NO];
	writeButton.enabled = gndrives > 0;
	status.stringValue = gndrives ? @"Select a USB drive" : @"No USB drives found";
}

- (void)onRefresh:(id)sender
{
	(void)sender;
	if (!running)
		[self refresh];
}

- (void)alert:(NSString *)text
{
	NSAlert *a = [NSAlert new];
	a.alertStyle = NSAlertStyleCritical;
	a.messageText = @"Tiny Hat USB writer";
	a.informativeText = text;
	[a runModal];
}

- (BOOL)ask:(NSString *)text
{
	NSAlert *a = [NSAlert new];
	a.alertStyle = NSAlertStyleWarning;
	a.messageText = @"Overwrite?";
	a.informativeText = text;
	[a addButtonWithTitle:@"Cancel"];
	[a addButtonWithTitle:@"Erase and write"];
	return [a runModal] == NSAlertSecondButtonReturn;
}

- (void)busy:(BOOL)b
{
	running = b;
	writeButton.enabled = !b && gndrives > 0;
	refreshButton.enabled = !b;
	table.enabled = !b;
}

- (void)finish:(NSString *)msg
{
	[self busy:NO];
	status.stringValue = msg;
	if (infd >= 0) {
		close(infd);
		infd = -1;
	}
	if (tmpdir) {
		unlink([tmpdir stringByAppendingPathComponent:@"in"].fileSystemRepresentation);
		unlink([tmpdir stringByAppendingPathComponent:@"out"].fileSystemRepresentation);
		rmdir(tmpdir.fileSystemRepresentation);
		tmpdir = nil;
	}
	task = nil;
}

- (void)onLine:(NSString *)line
{
	const char *s = line.UTF8String;
	if (finished)
		return;
	if (!strncmp(s, "SIZE ", 5)) {
		total = strtoull(s + 5, NULL, 10);
		status.stringValue = @"Writing, this takes a few minutes";
	} else if (!strncmp(s, "PROGRESS ", 9)) {
		uint64_t done = strtoull(s + 9, NULL, 10);
		if (total) {
			prog.doubleValue = (double)done / (double)total;
			status.stringValue = [NSString stringWithFormat:@"Writing, this takes a few minutes (%d%%)",
						       (int)(done * 100 / total)];
		}
	} else if (!strncmp(s, "CONFIRM ", 8)) {
		const char *a = [self ask:[NSString stringWithUTF8String:s + 8]] ? "YES\n" : "NO\n";
		if (infd >= 0)
			write(infd, a, strlen(a));
	} else if (!strcmp(s, "DONE")) {
		finished = YES;
		prog.doubleValue = 1;
		[self finish:@"Finished. The drive is ready."];
	} else if (!strncmp(s, "ERROR ", 6)) {
		finished = YES;
		[self alert:[NSString stringWithUTF8String:s + 6]];
		[self finish:[NSString stringWithUTF8String:s + 6]];
	}
}

- (void)ended
{
	if (finished)
		return;
	finished = YES;
	[self finish:@"Canceled"];
}

- (void)readFrom:(NSString *)path
{
	int fd = open(path.fileSystemRepresentation, O_RDONLY | O_NONBLOCK);
	char buf[512], line[1024];
	size_t len = 0;
	for (;;) {
		ssize_t got = fd < 0 ? 0 : read(fd, buf, sizeof(buf));
		ssize_t i;
		if (got > 0) {
			for (i = 0; i < got; i++) {
				if (buf[i] == '\n' || len == sizeof(line) - 1) {
					NSString *l;
					line[len] = 0;
					l = [NSString stringWithUTF8String:line];
					len = 0;
					if (l.length)
						dispatch_async(dispatch_get_main_queue(), ^{
							[self onLine:l];
						});
				} else {
					line[len++] = buf[i];
				}
			}
			continue;
		}
		if (got < 0 && errno != EAGAIN && errno != EINTR)
			break;
		if (got == 0 && exited)
			break;
		usleep(50000);
	}
	if (fd >= 0)
		close(fd);
	dispatch_async(dispatch_get_main_queue(), ^{
		[self ended];
	});
}

- (void)onWrite:(id)sender
{
	NSInteger idx = table.selectedRow;
	NSString *img, *dev, *self_, *cmd, *in, *out;
	char exe[4096], tmpl[] = "/tmp/tinyhat-write.XXXXXXX";
	(void)sender;
	if (running || idx < 0 || idx >= gndrives)
		return;
	img = tw_find_image();
	if (!img) {
		NSOpenPanel *op = [NSOpenPanel openPanel];
		op.message = @"Choose a Tiny Hat image (tinyhat32-*.img.gz)";
		if ([op runModal] != NSModalResponseOK || !op.URL)
			return;
		img = op.URL.path;
	}
	dev = [NSString stringWithUTF8String:gdrives[idx].dev];
	if (![self ask:[NSString stringWithFormat:@"Erase everything on %s (%@, %@) and write Tiny Hat to it?",
				     gdrives[idx].name, dev, tw_fmt_size(gdrives[idx].size)]])
		return;
	if (tw_exe_path(exe, sizeof(exe)) < 0 || !mkdtemp(tmpl)) {
		[self alert:@"Could not start the writer"];
		return;
	}
	tmpdir = [NSString stringWithUTF8String:tmpl];
	in = [tmpdir stringByAppendingPathComponent:@"in"];
	out = [tmpdir stringByAppendingPathComponent:@"out"];
	if (mkfifo(in.fileSystemRepresentation, 0600) < 0 || mkfifo(out.fileSystemRepresentation, 0600) < 0 ||
	    (infd = open(in.fileSystemRepresentation, O_RDWR | O_NONBLOCK)) < 0) {
		[self alert:@"Could not start the writer"];
		[self finish:@""];
		return;
	}
	self_ = [NSString stringWithUTF8String:exe];
	cmd = [NSString stringWithFormat:@"%@ --flow %@ %@ <%@ >%@ 2>&1", tw_quote(self_), tw_quote(dev),
					 tw_quote(img), tw_quote(in), tw_quote(out)];
	task = [NSTask new];
	if (geteuid() == 0) {
		task.launchPath = @"/bin/sh";
		task.arguments = @[ @"-c", cmd ];
	} else {
		task.launchPath = @"/usr/bin/osascript";
		task.arguments = @[ @"-e", @"on run argv\ndo shell script (item 1 of argv) with administrator privileges\nend run",
				    @"--", cmd ];
	}
	task.standardOutput = [NSFileHandle fileHandleWithNullDevice];
	task.standardError = [NSFileHandle fileHandleWithNullDevice];
	exited = NO;
	finished = NO;
	total = 0;
	task.terminationHandler = ^(NSTask *t) {
		(void)t;
		self->exited = YES;
	};
	prog.doubleValue = 0;
	status.stringValue = @"Waiting for the administrator password";
	[self busy:YES];
	@try {
		[task launch];
	} @catch (NSException *e) {
		(void)e;
		[self alert:@"Could not start the writer"];
		[self finish:@""];
		return;
	}
	[NSThread detachNewThreadWithBlock:^{
		[self readFrom:out];
	}];
}
@end

static int tw_flow_ask(void *ud, const char *text)
{
	char buf[64];
	(void)ud;
	if (isatty(0))
		printf("%s [y/N] ", text);
	else
		printf("CONFIRM %s\n", text);
	fflush(stdout);
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
	if (argc >= 2 && !strcmp(argv[1], "--version")) {
		printf("tinyhat-write %s\n", TW_VERSION);
		return 0;
	}
	if (argc >= 3 && !strcmp(argv[1], "--flow")) {
		tw_image img;
		tw_hooks hk;
		hk.ask = tw_flow_ask;
		hk.note = tw_flow_note;
		hk.ud = NULL;
		signal(SIGPIPE, SIG_IGN);
		if (tw_load_image(argc >= 4 ? argv[3] : NULL, &img) < 0) {
			printf("ERROR no tinyhat.img.gz found\n");
			return 1;
		}
		return tw_do_write(&img, argv[2], &hk);
	}
	@autoreleasepool {
		TWApp *d = [TWApp new];
		[NSApplication sharedApplication];
		[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
		NSApp.delegate = d;
		[NSApp run];
	}
	return 0;
}
