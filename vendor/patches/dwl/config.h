/* Taken from https://github.com/djpohly/dwl/issues/466 */
#define COLOR(hex)    { ((hex >> 24) & 0xFF) / 255.0f, \
                        ((hex >> 16) & 0xFF) / 255.0f, \
                        ((hex >> 8) & 0xFF) / 255.0f, \
                        (hex & 0xFF) / 255.0f }
/* appearance */
static const int sloppyfocus               = 0;  /* focus follows mouse */
static const int bypass_surface_visibility = 0;  /* 1 means idle inhibitors will disable idle tracking even if it's surface isn't visible  */
static const unsigned int borderpx         = 2;  /* border pixel of windows */
static const unsigned int gappx            = 6;  /* gap between windows */
static const int barvpad                   = 4;  /* vertical padding in the bar */
static const int showbar                   = 1; /* 0 means no bar */
static const int topbar                    = 1; /* 0 means bottom bar */
static const char *fonts[]                 = {"DejaVu Sans:size=10"};
static const char *cursor_theme            = "Vanilla-DMZ";
static const float rootcolor[]             = COLOR(0x1a1b26ff);
/* This conforms to the xdg-protocol. Set the alpha to zero to restore the old behavior */
static const float fullscreen_bg[]         = {0.0f, 0.0f, 0.0f, 1.0f}; /* You can also use glsl colors */
static uint32_t colors[][3]                = {
	/*               fg          bg          border    */
	[SchemeNorm] = { 0xc0caf5ff, 0x292e42ff, 0x414868ff },
	[SchemeSel]  = { 0x1a1b26ff, 0x1a9e96ff, 0x1a9e96ff },
	[SchemeUrg]  = { 0x1a1b26ff, 0xf7768eff, 0xf7768eff },
};

/* tagging */
static char *tags[] = { "1", "2", "3", "4" };

/* logging */
static int log_level = WLR_ERROR;

static const Rule rules[] = {
	/* app_id             title       tags mask     isfloating   monitor */
	{ NULL,               "DOSBox-X", 0,            1,           -1 },
	{ NULL,               "ScummVM",  0,            1,           -1 },
	{ NULL,               "ZSNES",    0,            1,           -1 },
	{ NULL,               "Tiny Hat", 0,            1,           -1 },
	{ NULL,               "Fasttracker II", 0,      1,           -1 },
};

/* layout(s) */
static const Layout layouts[] = {
	/* symbol     arrange function */
	{ "[]=",      tile },
	{ "><>",      NULL },    /* no layout function means floating behavior */
	{ "[M]",      monocle },
};

/* monitors */
/* (x=-1, y=-1) is reserved as an "autoconfigure" monitor position indicator
 * WARNING: negative values other than (-1, -1) cause problems with Xwayland clients due to
 * https://gitlab.freedesktop.org/xorg/xserver/-/issues/899 */
static const MonitorRule monrules[] = {
   /* name        mfact  nmaster scale layout       rotate/reflect                x    y
    * example of a HiDPI laptop monitor:
    { "eDP-1",    0.5f,  1,      2,    &layouts[0], WL_OUTPUT_TRANSFORM_NORMAL,   -1,  -1 }, */
	{ NULL,       0.55f, 1,      1,    &layouts[0], WL_OUTPUT_TRANSFORM_NORMAL,   -1,  -1 },
	/* default monitor rule: can be changed but cannot be eliminated; at least one monitor rule must exist */
};

/* keyboard */
static const struct xkb_rule_names xkb_rules = {
	/* can specify fields: rules, model, layout, variant, options */
	/* example:
	.options = "ctrl:nocaps",
	*/
	.options = NULL,
};

static const int repeat_rate = 25;
static const int repeat_delay = 600;

/* Trackpad */
static const int tap_to_click = 1;
static const int tap_and_drag = 1;
static const int drag_lock = 1;
static const int natural_scrolling = 0;
static const int disable_while_typing = 1;
static const int left_handed = 0;
static const int middle_button_emulation = 0;
/* You can choose between:
LIBINPUT_CONFIG_SCROLL_NO_SCROLL
LIBINPUT_CONFIG_SCROLL_2FG
LIBINPUT_CONFIG_SCROLL_EDGE
LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN
*/
static const enum libinput_config_scroll_method scroll_method = LIBINPUT_CONFIG_SCROLL_2FG;

/* You can choose between:
LIBINPUT_CONFIG_CLICK_METHOD_NONE
LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS
LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER
*/
static const enum libinput_config_click_method click_method = LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS;

/* You can choose between:
LIBINPUT_CONFIG_SEND_EVENTS_ENABLED
LIBINPUT_CONFIG_SEND_EVENTS_DISABLED
LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE
*/
static const uint32_t send_events_mode = LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;

/* You can choose between:
LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT
LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE
*/
static const enum libinput_config_accel_profile accel_profile = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
static const double accel_speed = 0.0;

/* You can choose between:
LIBINPUT_CONFIG_TAP_MAP_LRM -- 1/2/3 finger tap maps to left/right/middle
LIBINPUT_CONFIG_TAP_MAP_LMR -- 1/2/3 finger tap maps to left/middle/right
*/
static const enum libinput_config_tap_button_map button_map = LIBINPUT_CONFIG_TAP_MAP_LRM;

/* If you want to use the windows key for MODKEY, use WLR_MODIFIER_LOGO */
#define MODKEY WLR_MODIFIER_LOGO

#define TAGKEYS(KEY,SKEY,TAG) \
	{ MODKEY,                    KEY,            view,            {.ui = 1 << TAG} }, \
	{ MODKEY|WLR_MODIFIER_CTRL,  KEY,            toggleview,      {.ui = 1 << TAG} }, \
	{ MODKEY|WLR_MODIFIER_SHIFT, SKEY,           tag,             {.ui = 1 << TAG} }, \
	{ MODKEY|WLR_MODIFIER_CTRL|WLR_MODIFIER_SHIFT,SKEY,toggletag, {.ui = 1 << TAG} }

/* helper for spawning shell commands in the pre dwm-5.0 fashion */
#define SHCMD(cmd) { .v = (const char*[]){ "/bin/sh", "-c", cmd, NULL } }

/* commands */
static const char *termcmd[] = { "tinyhat-term", NULL };
static const char *codecmd[] = { "tinyhat-code", NULL };
static const char *fennelcmd[] = { "tinyhat-fennel", NULL };
static const char *cyclecmd[] = { "tinyhat-wallpaper", "--cycle", NULL };
static const char *scummvmcmd[] = { "tinyhat-launch", "tinyhat-scummvm", NULL };
static const char *scangamescmd[] = { "tinyhat-scan-games", NULL };
static const char *dosboxcmd[] = { "tinyhat-launch", "tinyhat-dosbox", NULL };
static const char *zsnescmd[] = { "tinyhat-launch", "zsnes", NULL };
static const char *screenshotcmd[] = { "tinyhat-screenshot", NULL };
static const char *browsercmd[] = { "netsurf-gtk3", NULL };
static const char *wificmd[] = { "tinyhat-wifi", NULL };
static const char *watcomcmd[] = { "tinyhat-watcom", NULL };
static const char *mtpaintcmd[] = { "mtpaint", NULL };
static const char *librepaintcmd[] = { "librepaint", NULL };
static const char *keyboardcmd[] = { "tinyhat-keyboard", NULL };
static const char *sharecmd[] = { "tinyhat-share", NULL };
static const char *lockcmd[] = { "swaylock", "-f", "-c", "1a1b26", "--inside-color", "1a1b26", "--ring-color", "1a9e96", "--key-hl-color", "73daca", "--line-color", "1a1b26", "--text-color", "a9b1d6", "--inside-ver-color", "1a1b26", "--ring-ver-color", "7aa2f7", "--inside-wrong-color", "1a1b26", "--ring-wrong-color", "f7768e", NULL };
static const char *menucmd[] = { "tinyhat-menu", NULL };
static const char *menulabel = "☰ Applications";
static const char *volup[] = { "tinyhat-volume", "up", NULL };
static const char *voldown[] = { "tinyhat-volume", "down", NULL };
static const char *volmute[] = { "tinyhat-volume", "mute", NULL };
static const char *brightup[] = { "tinyhat-brightness", "up", NULL };
static const char *brightdown[] = { "tinyhat-brightness", "down", NULL };

static const Key keys[] = {
	/* Note that Shift changes certain key codes: 2 -> at, etc. */
	/* modifier                  key                  function          argument */
	{ MODKEY,                    XKB_KEY_p,           spawn,            {.v = menucmd} },
	{ MODKEY,                    XKB_KEY_Return,      spawn,            {.v = termcmd} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_Return,      zoom,             {0} },
	{ WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT, XKB_KEY_t,  spawn,            {.v = termcmd} },
	{ WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT, XKB_KEY_r,  spawn,            {.v = termcmd} },
	{ WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT, XKB_KEY_p,  spawn,            {.v = codecmd} },
	{ WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT, XKB_KEY_i,  spawn,            {.v = fennelcmd} },
	{ WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT, XKB_KEY_m,  spawn,            {.v = mtpaintcmd} },
	{ WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT, XKB_KEY_b,  spawn,            {.v = librepaintcmd} },
	{ WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT, XKB_KEY_s,  spawn,            {.v = scummvmcmd} },
	{ WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT, XKB_KEY_h,  spawn,            {.v = sharecmd} },
	{ WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT, XKB_KEY_d,  spawn,            {.v = dosboxcmd} },
	{ WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT, XKB_KEY_z,  spawn,            {.v = zsnescmd} },
	{ WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT, XKB_KEY_f,  spawn,            {.v = browsercmd} },
	{ WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT, XKB_KEY_n,  spawn,            {.v = wificmd} },
	{ WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT, XKB_KEY_a,  spawn,            {.v = scangamescmd} },
	{ WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT, XKB_KEY_w,  spawn,            {.v = watcomcmd} },
	{ WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT, XKB_KEY_k,  spawn,            {.v = keyboardcmd} },
	{ WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT, XKB_KEY_l,  spawn,            {.v = lockcmd} },
	{ MODKEY,                    XKB_KEY_l,           spawn,            {.v = lockcmd} },
	{ WLR_MODIFIER_ALT,          XKB_KEY_space,       spawn,            {.v = menucmd} },
	{ WLR_MODIFIER_ALT|WLR_MODIFIER_SHIFT, XKB_KEY_W, killclient,       {0} },
	{ WLR_MODIFIER_ALT,          XKB_KEY_F4,          killclient,       {0} },
	{ MODKEY,                    XKB_KEY_q,           killclient,       {0} },
	{ MODKEY,                    XKB_KEY_r,           spawn,            {.v = menucmd} },
	{ WLR_MODIFIER_ALT|WLR_MODIFIER_SHIFT, XKB_KEY_F, togglefullscreen, {0} },
	{ 0,                         XKB_KEY_XF86AudioRaiseVolume, spawn,   {.v = volup} },
	{ 0,                         XKB_KEY_XF86AudioLowerVolume, spawn,   {.v = voldown} },
	{ 0,                         XKB_KEY_XF86AudioMute, spawn,          {.v = volmute} },
	{ 0,                         XKB_KEY_XF86MonBrightnessUp, spawn,    {.v = brightup} },
	{ 0,                         XKB_KEY_XF86MonBrightnessDown, spawn,  {.v = brightdown} },
	{ WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT, XKB_KEY_Up, spawn,            {.v = volup} },
	{ WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT, XKB_KEY_Down, spawn,          {.v = voldown} },
	{ MODKEY,                    XKB_KEY_b,           togglebar,        {0} },
	{ WLR_MODIFIER_ALT,          XKB_KEY_Tab,         focusstack,       {.i = +1} },
	{ WLR_MODIFIER_ALT|WLR_MODIFIER_SHIFT, XKB_KEY_ISO_Left_Tab, focusstack, {.i = -1} },
	{ MODKEY,                    XKB_KEY_j,           focusstack,       {.i = +1} },
	{ MODKEY,                    XKB_KEY_k,           focusstack,       {.i = -1} },
	{ MODKEY,                    XKB_KEY_i,           incnmaster,       {.i = +1} },
	{ MODKEY,                    XKB_KEY_d,           incnmaster,       {.i = -1} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_H,           setmfact,         {.f = -0.05f} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_L,           setmfact,         {.f = +0.05f} },
	{ MODKEY,                    XKB_KEY_Tab,         view,             {0} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_c,           killclient,       {0} },
	{ MODKEY,                    XKB_KEY_t,           setlayout,        {.v = &layouts[0]} },
	{ MODKEY,                    XKB_KEY_f,           setlayout,        {.v = &layouts[1]} },
	{ MODKEY,                    XKB_KEY_m,           setlayout,        {.v = &layouts[2]} },
	{ MODKEY,                    XKB_KEY_space,       spawn,            {.v = menucmd} },
	{ 0,                         XKB_KEY_Menu,        spawn,            {.v = menucmd} },
	{ 0,                         XKB_KEY_Print,       spawn,            {.v = screenshotcmd} },
	{ MODKEY|WLR_MODIFIER_CTRL,  XKB_KEY_space,       setlayout,        {0} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_space,       togglefloating,   {0} },
	{ MODKEY,                    XKB_KEY_e,           togglefullscreen, {0} },
	{ MODKEY,                    XKB_KEY_0,           view,             {.ui = ~0} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_parenright,  tag,              {.ui = ~0} },
	{ MODKEY,                    XKB_KEY_comma,       focusmon,         {.i = WLR_DIRECTION_LEFT} },
	{ MODKEY,                    XKB_KEY_period,      focusmon,         {.i = WLR_DIRECTION_RIGHT} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_less,        tagmon,           {.i = WLR_DIRECTION_LEFT} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_greater,     tagmon,           {.i = WLR_DIRECTION_RIGHT} },
	TAGKEYS(          XKB_KEY_1, XKB_KEY_exclam,                        0),
	TAGKEYS(          XKB_KEY_2, XKB_KEY_at,                            1),
	TAGKEYS(          XKB_KEY_3, XKB_KEY_numbersign,                    2),
	TAGKEYS(          XKB_KEY_4, XKB_KEY_dollar,                        3),
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_q,           quit,             {0} },

	/* Ctrl-Alt-Backspace and Ctrl-Alt-Fx used to be handled by X server */
	{ WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT,XKB_KEY_Terminate_Server, quit, {0} },
	/* Ctrl-Alt-Fx is used to switch to another VT, if you don't know what a VT is
	 * do not remove them.
	 */
#define CHVT(n) { WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT,XKB_KEY_XF86Switch_VT_##n, chvt, {.ui = (n)} }
	CHVT(1), CHVT(2), CHVT(3), CHVT(4), CHVT(5), CHVT(6),
	CHVT(7), CHVT(8), CHVT(9), CHVT(10), CHVT(11), CHVT(12),
};

static const Button buttons[] = {
	{ ClkLtSymbol, 0,      BTN_LEFT,   setlayout,      {.v = &layouts[0]} },
	{ ClkLtSymbol, 0,      BTN_RIGHT,  setlayout,      {.v = &layouts[2]} },
	{ ClkTitle,    0,      BTN_MIDDLE, zoom,           {0} },
	{ ClkStatus,   0,      BTN_LEFT,   spawn,          {.v = menucmd} },
	{ ClkRoot,     0,      BTN_RIGHT,  spawn,          {.v = cyclecmd} },
	{ ClkMenu,     0,      BTN_LEFT,   spawn,          {.v = menucmd} },
	{ ClkStatus,   0,      BTN_MIDDLE, spawn,          {.v = termcmd} },
	{ ClkClient,   MODKEY, BTN_LEFT,   moveresize,     {.ui = CurMove} },
	{ ClkClient,   MODKEY, BTN_MIDDLE, togglefloating, {0} },
	{ ClkClient,   MODKEY, BTN_RIGHT,  moveresize,     {.ui = CurResize} },
	{ ClkTagBar,   0,      BTN_LEFT,   view,           {0} },
	{ ClkTagBar,   0,      BTN_RIGHT,  toggleview,     {0} },
	{ ClkTagBar,   MODKEY, BTN_LEFT,   tag,            {0} },
	{ ClkTagBar,   MODKEY, BTN_RIGHT,  toggletag,      {0} },
};
