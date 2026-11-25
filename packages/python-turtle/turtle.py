import ctypes
import math
import os
import sys
import time
from ctypes import (CFUNCTYPE, POINTER, Structure, Union, byref, c_bool, c_char_p, c_float, c_int, c_int32,
                    c_uint8, c_uint16, c_uint32, c_uint64, c_void_p)

__all__ = ['ScrolledCanvas', 'TurtleScreen', 'Screen', 'RawTurtle', 'Turtle', 'RawPen', 'Pen', 'Shape', 'Vec2D',
           'addshape', 'bgcolor', 'bgpic', 'bye', 'clearscreen', 'colormode', 'delay', 'exitonclick', 'getcanvas',
           'getshapes', 'listen', 'mainloop', 'mode', 'numinput', 'onkey', 'onkeypress', 'onkeyrelease',
           'onscreenclick', 'ontimer', 'register_shape', 'resetscreen', 'screensize', 'setup', 'setworldcoordinates',
           'textinput', 'title', 'tracer', 'turtles', 'update', 'window_height', 'window_width',
           'back', 'backward', 'begin_fill', 'begin_poly', 'bk', 'circle', 'clear', 'clearstamp', 'clearstamps',
           'clone', 'color', 'degrees', 'distance', 'dot', 'down', 'end_fill', 'end_poly', 'fd', 'fillcolor',
           'filling', 'forward', 'get_poly', 'getpen', 'getscreen', 'get_shapepoly', 'getturtle', 'goto', 'heading',
           'hideturtle', 'home', 'ht', 'isdown', 'isvisible', 'left', 'lt', 'onclick', 'ondrag', 'onrelease', 'pd',
           'pen', 'pencolor', 'pendown', 'pensize', 'penup', 'pos', 'position', 'pu', 'radians', 'right', 'reset',
           'resizemode', 'rt', 'seth', 'setheading', 'setpos', 'setposition', 'settiltangle', 'setundobuffer', 'setx',
           'sety', 'shape', 'shapesize', 'shapetransform', 'shearfactor', 'showturtle', 'speed', 'st', 'stamp',
           'teleport', 'tilt', 'tiltangle', 'towards', 'turtlesize', 'undo', 'undobufferentries', 'up', 'width',
           'write', 'xcor', 'ycor', 'done', 'Terminator']


class Terminator(Exception):
    pass


class TurtleGraphicsError(Exception):
    pass


class Vec2D(tuple):
    def __new__(cls, x, y):
        return tuple.__new__(cls, (x, y))

    def __add__(self, other):
        return Vec2D(self[0] + other[0], self[1] + other[1])

    def __mul__(self, other):
        if isinstance(other, Vec2D):
            return self[0] * other[0] + self[1] * other[1]
        return Vec2D(self[0] * other, self[1] * other)

    def __rmul__(self, other):
        if isinstance(other, (int, float)):
            return Vec2D(self[0] * other, self[1] * other)
        return NotImplemented

    def __sub__(self, other):
        return Vec2D(self[0] - other[0], self[1] - other[1])

    def __neg__(self):
        return Vec2D(-self[0], -self[1])

    def __abs__(self):
        return math.hypot(*self)

    def rotate(self, angle):
        perp = Vec2D(-self[1], self[0])
        angle = math.radians(angle)
        c, s = math.cos(angle), math.sin(angle)
        return Vec2D(self[0] * c + perp[0] * s, self[1] * c + perp[1] * s)

    def __getnewargs__(self):
        return (self[0], self[1])

    def __repr__(self):
        return "(%.2f,%.2f)" % self


_COLORS = {
    'aliceblue': (240, 248, 255), 'antiquewhite': (250, 235, 215), 'aqua': (0, 255, 255),
    'aquamarine': (127, 255, 212), 'azure': (240, 255, 255), 'beige': (245, 245, 220), 'bisque': (255, 228, 196),
    'black': (0, 0, 0), 'blanchedalmond': (255, 235, 205), 'blue': (0, 0, 255), 'blueviolet': (138, 43, 226),
    'brown': (165, 42, 42), 'burlywood': (222, 184, 135), 'cadetblue': (95, 158, 160),
    'chartreuse': (127, 255, 0), 'chocolate': (210, 105, 30), 'coral': (255, 127, 80),
    'cornflowerblue': (100, 149, 237), 'cornsilk': (255, 248, 220), 'crimson': (220, 20, 60),
    'cyan': (0, 255, 255), 'darkblue': (0, 0, 139), 'darkcyan': (0, 139, 139), 'darkgoldenrod': (184, 134, 11),
    'darkgray': (169, 169, 169), 'darkgreen': (0, 100, 0), 'darkgrey': (169, 169, 169),
    'darkkhaki': (189, 183, 107), 'darkmagenta': (139, 0, 139), 'darkolivegreen': (85, 107, 47),
    'darkorange': (255, 140, 0), 'darkorchid': (153, 50, 204), 'darkred': (139, 0, 0),
    'darksalmon': (233, 150, 122), 'darkseagreen': (143, 188, 143), 'darkslateblue': (72, 61, 139),
    'darkslategray': (47, 79, 79), 'darkslategrey': (47, 79, 79), 'darkturquoise': (0, 206, 209),
    'darkviolet': (148, 0, 211), 'deeppink': (255, 20, 147), 'deepskyblue': (0, 191, 255),
    'dimgray': (105, 105, 105), 'dimgrey': (105, 105, 105), 'dodgerblue': (30, 144, 255),
    'firebrick': (178, 34, 34), 'floralwhite': (255, 250, 240), 'forestgreen': (34, 139, 34),
    'fuchsia': (255, 0, 255), 'gainsboro': (220, 220, 220), 'ghostwhite': (248, 248, 255), 'gold': (255, 215, 0),
    'goldenrod': (218, 165, 32), 'gray': (190, 190, 190), 'green': (0, 255, 0), 'greenyellow': (173, 255, 47),
    'grey': (190, 190, 190), 'honeydew': (240, 255, 240), 'hotpink': (255, 105, 180),
    'indianred': (205, 92, 92), 'indigo': (75, 0, 130), 'ivory': (255, 255, 240), 'khaki': (240, 230, 140),
    'lavender': (230, 230, 250), 'lavenderblush': (255, 240, 245), 'lawngreen': (124, 252, 0),
    'lemonchiffon': (255, 250, 205), 'lightblue': (173, 216, 230), 'lightcoral': (240, 128, 128),
    'lightcyan': (224, 255, 255), 'lightgoldenrodyellow': (250, 250, 210), 'lightgray': (211, 211, 211),
    'lightgreen': (144, 238, 144), 'lightgrey': (211, 211, 211), 'lightpink': (255, 182, 193),
    'lightsalmon': (255, 160, 122), 'lightseagreen': (32, 178, 170), 'lightskyblue': (135, 206, 250),
    'lightslategray': (119, 136, 153), 'lightslategrey': (119, 136, 153), 'lightsteelblue': (176, 196, 222),
    'lightyellow': (255, 255, 224), 'lime': (0, 255, 0), 'limegreen': (50, 205, 50), 'linen': (250, 240, 230),
    'magenta': (255, 0, 255), 'maroon': (176, 48, 96), 'mediumaquamarine': (102, 205, 170),
    'mediumblue': (0, 0, 205), 'mediumorchid': (186, 85, 211), 'mediumpurple': (147, 112, 219),
    'mediumseagreen': (60, 179, 113), 'mediumslateblue': (123, 104, 238), 'mediumspringgreen': (0, 250, 154),
    'mediumturquoise': (72, 209, 204), 'mediumvioletred': (199, 21, 133), 'midnightblue': (25, 25, 112),
    'mintcream': (245, 255, 250), 'mistyrose': (255, 228, 225), 'moccasin': (255, 228, 181),
    'navajowhite': (255, 222, 173), 'navy': (0, 0, 128), 'navyblue': (0, 0, 128), 'oldlace': (253, 245, 230),
    'olive': (128, 128, 0), 'olivedrab': (107, 142, 35), 'orange': (255, 165, 0), 'orangered': (255, 69, 0),
    'orchid': (218, 112, 214), 'palegoldenrod': (238, 232, 170), 'palegreen': (152, 251, 152),
    'paleturquoise': (175, 238, 238), 'palevioletred': (219, 112, 147), 'papayawhip': (255, 239, 213),
    'peachpuff': (255, 218, 185), 'peru': (205, 133, 63), 'pink': (255, 192, 203), 'plum': (221, 160, 221),
    'powderblue': (176, 224, 230), 'purple': (160, 32, 240), 'rebeccapurple': (102, 51, 153),
    'red': (255, 0, 0), 'rosybrown': (188, 143, 143), 'royalblue': (65, 105, 225),
    'saddlebrown': (139, 69, 19), 'salmon': (250, 128, 114), 'sandybrown': (244, 164, 96),
    'seagreen': (46, 139, 87), 'seashell': (255, 245, 238), 'sienna': (160, 82, 45), 'silver': (192, 192, 192),
    'skyblue': (135, 206, 235), 'slateblue': (106, 90, 205), 'slategray': (112, 128, 144),
    'slategrey': (112, 128, 144), 'snow': (255, 250, 250), 'springgreen': (0, 255, 127),
    'steelblue': (70, 130, 180), 'tan': (210, 180, 140), 'teal': (0, 128, 128), 'thistle': (216, 191, 216),
    'tomato': (255, 99, 71), 'turquoise': (64, 224, 208), 'violet': (238, 130, 238),
    'violetred': (208, 32, 144), 'wheat': (245, 222, 179), 'white': (255, 255, 255),
    'whitesmoke': (245, 245, 245), 'yellow': (255, 255, 0), 'yellowgreen': (154, 205, 50),
}

_SHAPES = {
    'arrow': ((-10, 0), (10, 0), (0, 10)),
    'turtle': ((0, 16), (-2, 14), (-1, 10), (-4, 7), (-7, 9), (-9, 8), (-6, 5), (-7, 1), (-5, -3), (-8, -6),
               (-6, -8), (-4, -5), (0, -7), (4, -5), (6, -8), (8, -6), (5, -3), (7, 1), (6, 5), (9, 8), (7, 9),
               (4, 7), (1, 10), (2, 14)),
    'circle': tuple((10 * math.cos(math.radians(a)), 10 * math.sin(math.radians(a))) for a in range(0, 360, 12)),
    'square': ((10, -10), (10, 10), (-10, 10), (-10, -10)),
    'triangle': ((10, -5.77), (0, 11.55), (-10, -5.77)),
    'classic': ((0, 0), (-5, -9), (0, -7), (5, -9)),
    'blank': (),
}

_SPEEDS = {'fastest': 0, 'fast': 10, 'normal': 6, 'slow': 3, 'slowest': 1}
_FONTS = '/usr/share/fonts/TTF'


class _FPoint(Structure):
    _fields_ = [('x', c_float), ('y', c_float)]


class _FRect(Structure):
    _fields_ = [('x', c_float), ('y', c_float), ('w', c_float), ('h', c_float)]


class _FColor(Structure):
    _fields_ = [('r', c_float), ('g', c_float), ('b', c_float), ('a', c_float)]


class _Color(Structure):
    _fields_ = [('r', c_uint8), ('g', c_uint8), ('b', c_uint8), ('a', c_uint8)]


class _Vertex(Structure):
    _fields_ = [('position', _FPoint), ('color', _FColor), ('tex_coord', _FPoint)]


class _KeyboardEvent(Structure):
    _fields_ = [('type', c_uint32), ('reserved', c_uint32), ('timestamp', c_uint64), ('windowID', c_uint32),
                ('which', c_uint32), ('scancode', c_int), ('key', c_uint32), ('mod', c_uint16), ('raw', c_uint16),
                ('down', c_bool), ('repeat', c_bool)]


class _MouseButtonEvent(Structure):
    _fields_ = [('type', c_uint32), ('reserved', c_uint32), ('timestamp', c_uint64), ('windowID', c_uint32),
                ('which', c_uint32), ('button', c_uint8), ('down', c_bool), ('clicks', c_uint8),
                ('padding', c_uint8), ('x', c_float), ('y', c_float)]


class _MouseMotionEvent(Structure):
    _fields_ = [('type', c_uint32), ('reserved', c_uint32), ('timestamp', c_uint64), ('windowID', c_uint32),
                ('which', c_uint32), ('state', c_uint32), ('x', c_float), ('y', c_float), ('xrel', c_float),
                ('yrel', c_float)]


class _TextInputEvent(Structure):
    _fields_ = [('type', c_uint32), ('reserved', c_uint32), ('timestamp', c_uint64), ('windowID', c_uint32),
                ('text', c_char_p)]


class _Event(Union):
    _fields_ = [('type', c_uint32), ('key', _KeyboardEvent), ('button', _MouseButtonEvent),
                ('motion', _MouseMotionEvent), ('text', _TextInputEvent), ('padding', c_uint8 * 128)]


class _Surface(Structure):
    _fields_ = [('flags', c_uint32), ('format', c_uint32), ('w', c_int), ('h', c_int)]


class _DisplayMode(Structure):
    _fields_ = [('displayID', c_uint32), ('format', c_uint32), ('w', c_int), ('h', c_int)]


_QUIT, _WINDOW_EXPOSED, _WINDOW_PIXEL_SIZE_CHANGED, _WINDOW_CLOSE = 0x100, 0x204, 0x207, 0x210
_KEY_DOWN, _KEY_UP, _TEXT_INPUT = 0x300, 0x301, 0x303
_MOUSE_MOTION, _MOUSE_DOWN, _MOUSE_UP = 0x400, 0x401, 0x402
_RENDER_TARGETS_RESET, _RENDER_DEVICE_RESET = 0x2000, 0x2001

_sdl = _ttf = _img = None


def _load():
    global _sdl, _ttf, _img
    if _sdl:
        return
    try:
        sdl = ctypes.CDLL('libSDL3.so.0')
    except OSError as e:
        raise TurtleGraphicsError('turtle needs SDL3: %s' % e)
    sig = {
        'SDL_Init': (c_bool, [c_uint32]), 'SDL_GetError': (c_char_p, []), 'SDL_Quit': (None, []),
        'SDL_CreateWindow': (c_void_p, [c_char_p, c_int, c_int, c_uint64]),
        'SDL_DestroyWindow': (None, [c_void_p]), 'SDL_SetWindowTitle': (c_bool, [c_void_p, c_char_p]),
        'SDL_SetWindowSize': (c_bool, [c_void_p, c_int, c_int]),
        'SDL_GetWindowSizeInPixels': (c_bool, [c_void_p, POINTER(c_int), POINTER(c_int)]),
        'SDL_GetPrimaryDisplay': (c_uint32, []),
        'SDL_GetCurrentDisplayMode': (POINTER(_DisplayMode), [c_uint32]),
        'SDL_CreateRenderer': (c_void_p, [c_void_p, c_char_p]), 'SDL_DestroyRenderer': (None, [c_void_p]),
        'SDL_CreateTexture': (c_void_p, [c_void_p, c_uint32, c_int, c_int, c_int]),
        'SDL_CreateTextureFromSurface': (c_void_p, [c_void_p, c_void_p]),
        'SDL_DestroyTexture': (None, [c_void_p]), 'SDL_DestroySurface': (None, [c_void_p]),
        'SDL_SetRenderTarget': (c_bool, [c_void_p, c_void_p]),
        'SDL_SetRenderViewport': (c_bool, [c_void_p, c_void_p]),
        'SDL_SetRenderClipRect': (c_bool, [c_void_p, c_void_p]),
        'SDL_SetRenderDrawColor': (c_bool, [c_void_p, c_uint8, c_uint8, c_uint8, c_uint8]),
        'SDL_SetRenderDrawBlendMode': (c_bool, [c_void_p, c_int]),
        'SDL_RenderClear': (c_bool, [c_void_p]),
        'SDL_RenderLine': (c_bool, [c_void_p, c_float, c_float, c_float, c_float]),
        'SDL_RenderFillRects': (c_bool, [c_void_p, POINTER(_FRect), c_int]),
        'SDL_RenderGeometry': (c_bool, [c_void_p, c_void_p, POINTER(_Vertex), c_int, c_void_p, c_int]),
        'SDL_RenderTexture': (c_bool, [c_void_p, c_void_p, POINTER(_FRect), POINTER(_FRect)]),
        'SDL_RenderPresent': (c_bool, [c_void_p]),
        'SDL_RenderReadPixels': (c_void_p, [c_void_p, c_void_p]),
        'SDL_SaveBMP': (c_bool, [c_void_p, c_char_p]),
        'SDL_PollEvent': (c_bool, [POINTER(_Event)]), 'SDL_GetKeyName': (c_char_p, [c_uint32]),
        'SDL_StartTextInput': (c_bool, [c_void_p]), 'SDL_StopTextInput': (c_bool, [c_void_p]),
        'SDL_Delay': (None, [c_uint32]), 'SDL_GetTicks': (c_uint64, []),
        'SDL_SetHint': (c_bool, [c_char_p, c_char_p]),
        'SDL_GetTextureSize': (c_bool, [c_void_p, POINTER(c_float), POINTER(c_float)]),
    }
    for name, (res, args) in sig.items():
        f = getattr(sdl, name)
        f.restype, f.argtypes = res, args
    try:
        ttf = ctypes.CDLL('libSDL3_ttf.so.0')
        ttf.TTF_Init.restype, ttf.TTF_Init.argtypes = c_bool, []
        ttf.TTF_OpenFont.restype, ttf.TTF_OpenFont.argtypes = c_void_p, [c_char_p, c_float]
        ttf.TTF_RenderText_Blended.restype = c_void_p
        ttf.TTF_RenderText_Blended.argtypes = [c_void_p, c_char_p, ctypes.c_size_t, _Color]
        ttf.TTF_Init()
    except (OSError, AttributeError):
        ttf = None
    try:
        img = ctypes.CDLL('libSDL3_image.so.0')
        img.IMG_LoadTexture.restype, img.IMG_LoadTexture.argtypes = c_void_p, [c_void_p, c_char_p]
    except (OSError, AttributeError):
        img = None
    _sdl, _ttf, _img = sdl, ttf, img


def _keyname(key):
    name = (_sdl.SDL_GetKeyName(key) or b'').decode('utf-8', 'replace')
    if len(name) == 1:
        return name.lower()
    return {'Space': 'space', 'Backspace': 'BackSpace', 'Keypad Enter': 'KP_Enter'}.get(name, name)


class Shape:
    def __init__(self, type_, data=None):
        self._type = type_
        if type_ == 'polygon' and isinstance(data, list):
            data = tuple(data)
        elif type_ == 'compound' and data is None:
            data = []
        self._data = data

    def addcomponent(self, poly, fill, outline=None):
        if self._type != 'compound':
            raise TurtleGraphicsError('cannot add component to %s Shape' % self._type)
        self._data.append([poly, fill, outline if outline is not None else fill])


class _Item:
    __slots__ = ('kind', 'owner', 'data')

    def __init__(self, kind, owner, data):
        self.kind, self.owner, self.data = kind, owner, data


class TurtleScreen:
    _instance = None

    def __init__(self, width=None, height=None, startx=None, starty=None):
        _load()
        _sdl.SDL_SetHint(b'SDL_VIDEO_WAYLAND_ALLOW_LIBDECOR', b'1')
        _sdl.SDL_SetHint(b'SDL_APP_ID', b'turtle-repl' if hasattr(sys, 'ps1') or sys.flags.interactive else b'turtle')
        if not _sdl.SDL_Init(0x20):
            raise TurtleGraphicsError('turtle could not open a window: %s' % _sdl.SDL_GetError().decode())
        w, h = self._size(width, height)
        self._win = _sdl.SDL_CreateWindow(b'Python Turtle Graphics', w, h, 0x20)
        if not self._win:
            raise TurtleGraphicsError('turtle could not open a window: %s' % _sdl.SDL_GetError().decode())
        self._ren = _sdl.SDL_CreateRenderer(self._win, None)
        if not self._ren:
            _sdl.SDL_DestroyWindow(self._win)
            raise TurtleGraphicsError('turtle could not draw: %s' % _sdl.SDL_GetError().decode())
        _sdl.SDL_SetRenderDrawBlendMode(self._ren, 1)
        self._canvas = None
        self._w = self._h = 0
        self._items = []
        self._flushed = 0
        self._bg = (255, 255, 255)
        self._bgpic = None
        self._bgpicname = 'nopic'
        self._world = None
        self._turtles = []
        self._shapes = {name: Shape('polygon', poly) for name, poly in _SHAPES.items()}
        self._colormode = 1.0
        self._mode = 'standard'
        self._delay = 10
        self._tracer = 1
        self._tracecount = 0
        self._keys_down = {}
        self._keys_up = {}
        self._clicks = {}
        self._timers = []
        self._dragging = None
        self._textbox = None
        self._running = True
        self._dirty = True
        self._fonts = {}
        self._hook = None
        self._resize()
        self._install_hook()
        self._render()

    def _size(self, width, height):
        sw, sh = 1024, 768
        mode = _sdl.SDL_GetCurrentDisplayMode(_sdl.SDL_GetPrimaryDisplay())
        if mode:
            sw, sh = mode.contents.w, mode.contents.h
        width = 0.5 if width is None else width
        height = 0.75 if height is None else height
        w = int(sw * width) if isinstance(width, float) and width <= 1 else int(width)
        h = int(sh * height) if isinstance(height, float) and height <= 1 else int(height)
        return max(w, 100), max(h, 100)

    def _install_hook(self):
        if not hasattr(sys, 'ps1') and not sys.flags.interactive:
            return
        try:
            hook = c_void_p.in_dll(ctypes.pythonapi, 'PyOS_InputHook')
        except (ValueError, AttributeError):
            return

        @CFUNCTYPE(c_int)
        def inputhook():
            try:
                if self._running:
                    self._pump()
                    if self._dirty:
                        self._render()
                    _sdl.SDL_Delay(10)
            except Exception:
                pass
            return 0
        self._hook = inputhook
        hook.value = ctypes.cast(inputhook, c_void_p).value

    def _remove_hook(self):
        if self._hook:
            try:
                c_void_p.in_dll(ctypes.pythonapi, 'PyOS_InputHook').value = None
            except (ValueError, AttributeError):
                pass

    def _resize(self):
        w, h = c_int(), c_int()
        _sdl.SDL_GetWindowSizeInPixels(self._win, byref(w), byref(h))
        if (w.value, h.value) == (self._w, self._h) and self._canvas:
            return
        self._w, self._h = max(w.value, 1), max(h.value, 1)
        if self._canvas:
            _sdl.SDL_DestroyTexture(self._canvas)
        self._canvas = _sdl.SDL_CreateTexture(self._ren, 0x16362004, 2, self._w, self._h)
        _sdl.SDL_SetRenderDrawBlendMode(self._ren, 1)
        self._redraw()

    def _target(self, tex):
        _sdl.SDL_SetRenderTarget(self._ren, tex)
        _sdl.SDL_SetRenderViewport(self._ren, None)
        _sdl.SDL_SetRenderClipRect(self._ren, None)

    def _redraw(self):
        self._flushed = 0
        self._target(self._canvas)
        _sdl.SDL_SetRenderDrawColor(self._ren, *self._bg, 255)
        _sdl.SDL_RenderFillRects(self._ren, (_FRect * 1)(_FRect(0, 0, self._w, self._h)), 1)
        if self._bgpic:
            tex, tw, th = self._bgpic
            _sdl.SDL_RenderTexture(self._ren, tex, None, byref(_FRect((self._w - tw) / 2, (self._h - th) / 2, tw, th)))
        self._target(None)
        self._dirty = True

    def _flush(self):
        if self._flushed >= len(self._items):
            return
        self._target(self._canvas)
        for item in self._items[self._flushed:]:
            self._draw_item(item)
        self._target(None)
        self._flushed = len(self._items)

    def _add(self, item):
        self._items.append(item)
        self._dirty = True
        return item

    def _remove(self, items):
        items = set(map(id, items))
        if not items:
            return
        self._items = [i for i in self._items if id(i) not in items]
        self._redraw()

    def _to_screen(self, x, y):
        if self._world:
            llx, lly, urx, ury = self._world
            return (x - llx) * self._w / (urx - llx), (ury - y) * self._h / (ury - lly)
        return self._w / 2 + x, self._h / 2 - y

    def _to_world(self, sx, sy):
        if self._world:
            llx, lly, urx, ury = self._world
            return llx + sx * (urx - llx) / self._w, ury - sy * (ury - lly) / self._h
        return sx - self._w / 2, self._h / 2 - sy

    def _scale(self):
        if self._world:
            llx, lly, urx, ury = self._world
            return (self._w / (urx - llx) + self._h / (ury - lly)) / 2
        return 1.0

    def _color(self, *args):
        if len(args) == 1:
            args = args[0]
        if isinstance(args, str):
            name = args.strip()
            if name.startswith('#'):
                h = name[1:]
                if len(h) in (3, 6, 9, 12):
                    n = len(h) // 3
                    try:
                        v = [int(h[i * n:(i + 1) * n], 16) for i in range(3)]
                    except ValueError:
                        raise TurtleGraphicsError('bad color string: %s' % args)
                    return tuple(c * 255 // (16 ** n - 1) for c in v)
            key = name.lower().replace(' ', '')
            if key in _COLORS:
                return _COLORS[key]
            for p in ('gray', 'grey'):
                if key.startswith(p) and key[len(p):].isdigit() and int(key[len(p):]) <= 100:
                    v = round(int(key[len(p):]) * 255 / 100)
                    return (v, v, v)
            raise TurtleGraphicsError('bad color string: %s' % args)
        try:
            r, g, b = args
        except (TypeError, ValueError):
            raise TurtleGraphicsError('bad color arguments: %s' % str(args))
        if self._colormode == 1.0:
            if not all(0 <= c <= 1 for c in (r, g, b)):
                raise TurtleGraphicsError('bad color sequence: %s' % str(args))
            return (round(r * 255), round(g * 255), round(b * 255))
        if not all(0 <= c <= 255 for c in (r, g, b)):
            raise TurtleGraphicsError('bad color sequence: %s' % str(args))
        return (int(r), int(g), int(b))

    def _colorout(self, rgb):
        if isinstance(rgb, str):
            return rgb
        if self._colormode == 1.0:
            return tuple(c / 255 for c in rgb)
        return tuple(rgb)

    def _line(self, pts, rgb, width):
        if len(pts) < 2:
            return
        r, g, b = rgb
        if width <= 1.5:
            _sdl.SDL_SetRenderDrawColor(self._ren, r, g, b, 255)
            for (x1, y1), (x2, y2) in zip(pts, pts[1:]):
                _sdl.SDL_RenderLine(self._ren, x1, y1, x2, y2)
            return
        hw = width / 2
        verts = []
        col = _FColor(r / 255, g / 255, b / 255, 1)
        for (x1, y1), (x2, y2) in zip(pts, pts[1:]):
            dx, dy = x2 - x1, y2 - y1
            d = math.hypot(dx, dy)
            if d > 0:
                nx, ny = -dy / d * hw, dx / d * hw
                a, b_, c, e = (x1 + nx, y1 + ny), (x2 + nx, y2 + ny), (x2 - nx, y2 - ny), (x1 - nx, y1 - ny)
                verts += [a, b_, c, a, c, e]
        n = max(8, int(width * 1.5))
        for (cx, cy) in pts:
            prev = (cx + hw, cy)
            for k in range(1, n + 1):
                t = 2 * math.pi * k / n
                cur = (cx + hw * math.cos(t), cy + hw * math.sin(t))
                verts += [(cx, cy), prev, cur]
                prev = cur
        self._geometry(verts, col)

    def _geometry(self, verts, col):
        if not verts:
            return
        arr = (_Vertex * len(verts))()
        for i, (x, y) in enumerate(verts):
            arr[i].position.x, arr[i].position.y = x, y
            arr[i].color = col
        _sdl.SDL_RenderGeometry(self._ren, None, arr, len(verts), None, 0)

    def _fill(self, pts, rgb):
        if len(pts) < 3:
            return
        ys = [p[1] for p in pts]
        y0, y1 = max(int(math.floor(min(ys))), 0), min(int(math.ceil(max(ys))), self._h)
        edges = list(zip(pts, pts[1:] + pts[:1]))
        rects = []
        for y in range(y0, y1):
            yc = y + 0.5
            xs = []
            for (ax, ay), (bx, by) in edges:
                if (ay <= yc < by) or (by <= yc < ay):
                    xs.append((ax + (yc - ay) * (bx - ax) / (by - ay), 1 if by > ay else -1))
            xs.sort()
            wind = 0
            for i in range(len(xs) - 1):
                wind += xs[i][1]
                if wind:
                    xa, xb = round(xs[i][0]), round(xs[i + 1][0])
                    if xb > xa:
                        rects.append((xa, y, xb - xa, 1))
        if rects:
            arr = (_FRect * len(rects))(*[_FRect(*r) for r in rects])
            _sdl.SDL_SetRenderDrawColor(self._ren, *rgb, 255)
            _sdl.SDL_RenderFillRects(self._ren, arr, len(rects))

    def _disc(self, cx, cy, radius, rgb):
        n = max(12, int(radius * 2))
        verts = []
        prev = (cx + radius, cy)
        for k in range(1, n + 1):
            t = 2 * math.pi * k / n
            cur = (cx + radius * math.cos(t), cy + radius * math.sin(t))
            verts += [(cx, cy), prev, cur]
            prev = cur
        self._geometry(verts, _FColor(rgb[0] / 255, rgb[1] / 255, rgb[2] / 255, 1))

    def _font(self, font):
        family, size, style = (list(font) + ['Arial', 8, 'normal'][len(font):])[:3]
        style = str(style).lower()
        bold, italic = 'bold' in style, 'italic' in style
        mono = str(family).lower() in ('courier', 'courier new', 'monospace', 'mono', 'fixed', 'terminal')
        base = 'JetBrainsMonoNL' if mono else 'DejaVuSans'
        suffix = ('-Bold' if bold else '') + ('Italic' if italic and mono else '-Oblique' if italic else '')
        if mono and not suffix:
            suffix = '-Regular'
        if mono and suffix == 'Italic':
            suffix = '-Italic'
        if suffix.startswith('-Bold-'):
            suffix = '-BoldOblique'
        path = os.path.join(_FONTS, base + suffix + '.ttf')
        if not os.path.exists(path):
            path = os.path.join(_FONTS, base + ('-Regular' if mono else '') + '.ttf')
        key = (path, int(size))
        if key not in self._fonts and _ttf:
            self._fonts[key] = _ttf.TTF_OpenFont(path.encode(), float(abs(int(size)) * 4 / 3))
        return self._fonts.get(key)

    def _text_texture(self, text, font, rgb):
        f = self._font(font)
        if not f or not text:
            return None
        data = text.encode('utf-8')
        surf = _ttf.TTF_RenderText_Blended(f, data, len(data), _Color(*rgb, 255))
        if not surf:
            return None
        s = ctypes.cast(surf, POINTER(_Surface)).contents
        w, h = s.w, s.h
        tex = _sdl.SDL_CreateTextureFromSurface(self._ren, surf)
        _sdl.SDL_DestroySurface(surf)
        return (tex, w, h) if tex else None

    def _draw_item(self, item):
        k, d = item.kind, item.data
        if k == 'line':
            pts, rgb, width = d
            self._line([self._to_screen(*p) for p in pts], rgb, width * self._scale())
        elif k == 'fill':
            pts, rgb = d
            if pts:
                self._fill([self._to_screen(*p) for p in pts], rgb)
        elif k == 'dot':
            (x, y), size, rgb = d
            sx, sy = self._to_screen(x, y)
            self._disc(sx, sy, size / 2, rgb)
        elif k == 'text':
            (x, y), tex, align = d
            if tex:
                t, w, h = tex
                sx, sy = self._to_screen(x, y)
                sx -= {'left': 0, 'center': w / 2, 'right': w}.get(align, 0)
                _sdl.SDL_RenderTexture(self._ren, t, None, byref(_FRect(sx, sy - h, w, h)))
        elif k == 'stamp':
            self._draw_shape(*d)

    def _draw_shape(self, shape, pos, heading, stretch, rgb_pen, rgb_fill, outline):
        if shape._type == 'image':
            if shape._data:
                t, w, h = shape._data
                sx, sy = self._to_screen(*pos)
                _sdl.SDL_RenderTexture(self._ren, t, None, byref(_FRect(sx - w / 2, sy - h / 2, w, h)))
            return
        parts = [(shape._data, rgb_fill, rgb_pen)] if shape._type == 'polygon' else \
            [(p, self._color(f) if f else None, self._color(o) if o else None) for p, f, o in shape._data]
        sw, sl = stretch
        a = math.radians(heading - 90)
        c, s = math.cos(a), math.sin(a)
        sx0, sy0 = self._to_screen(*pos)
        for poly, fill, line in parts:
            if not poly:
                continue
            pts = []
            for x, y in poly:
                x, y = x * sw, y * sl
                rx, ry = x * c - y * s, x * s + y * c
                pts.append((sx0 + rx, sy0 - ry))
            if fill:
                self._fill(pts, fill)
            if line and outline:
                self._line(pts + pts[:1], line, outline)

    def _render(self, overlay=None):
        if not self._running:
            return
        self._flush()
        self._target(None)
        _sdl.SDL_RenderTexture(self._ren, self._canvas, None, None)
        if overlay:
            overlay()
        for t in self._turtles:
            if t._shown:
                t._draw_turtle()
        if self._textbox:
            self._draw_textbox()
        _sdl.SDL_RenderPresent(self._ren)
        self._dirty = False

    def _close(self):
        if not self._running:
            return
        self._running = False
        self._remove_hook()
        for tex in [i.data[1][0] for i in self._items if i.kind == 'text' and i.data[1]]:
            _sdl.SDL_DestroyTexture(tex)
        if self._canvas:
            _sdl.SDL_DestroyTexture(self._canvas)
        _sdl.SDL_DestroyRenderer(self._ren)
        _sdl.SDL_DestroyWindow(self._win)
        _sdl.SDL_Quit()
        global _screen, _pen
        if TurtleScreen._instance is self:
            TurtleScreen._instance = None
            _screen = _pen = None
        for t in self._turtles:
            t._dead = True

    def _pump(self):
        if not self._running:
            raise Terminator
        ev = _Event()
        while self._running and _sdl.SDL_PollEvent(byref(ev)):
            et = ev.type
            if et in (_QUIT, _WINDOW_CLOSE):
                self._close()
                raise Terminator
            if et in (_WINDOW_EXPOSED, _RENDER_TARGETS_RESET, _RENDER_DEVICE_RESET):
                if et != _WINDOW_EXPOSED:
                    self._redraw()
                self._dirty = True
            elif et == _WINDOW_PIXEL_SIZE_CHANGED:
                self._resize()
            elif et in (_KEY_DOWN, _KEY_UP):
                name = _keyname(ev.key.key)
                if self._textbox and et == _KEY_DOWN:
                    self._textbox_key(name)
                    if len(self._textbox) > 3:
                        break
                    continue
                if et == _KEY_DOWN:
                    for k in (name, None):
                        if k in self._keys_down:
                            self._keys_down[k]()
                            break
                elif name in self._keys_up:
                    self._keys_up[name]()
            elif et == _TEXT_INPUT and self._textbox:
                self._textbox[2] += (ev.text.text or b'').decode('utf-8', 'replace')
                self._dirty = True
            elif et == _MOUSE_DOWN:
                x, y = self._to_world(ev.button.x, ev.button.y)
                if self._textbox:
                    continue
                hit = None
                for t in reversed(self._turtles):
                    if t._shown and (ev.button.button in t._clicks or ev.button.button in t._drags):
                        if math.hypot(x - t._x, y - t._y) <= 10 * max(t._stretch) / self._scale() + 2:
                            hit = t
                            break
                if hit:
                    self._dragging = (hit, ev.button.button)
                    if ev.button.button in hit._clicks:
                        hit._clicks[ev.button.button](x, y)
                elif ev.button.button in self._clicks:
                    self._clicks[ev.button.button](x, y)
            elif et == _MOUSE_UP:
                if self._dragging:
                    t, btn = self._dragging
                    self._dragging = None
                    if btn in t._releases:
                        t._releases[btn](*self._to_world(ev.button.x, ev.button.y))
            elif et == _MOUSE_MOTION and self._dragging:
                t, btn = self._dragging
                if btn in t._drags:
                    t._drags[btn](*self._to_world(ev.motion.x, ev.motion.y))
        now = _sdl.SDL_GetTicks()
        due = [tm for tm in self._timers if tm[0] <= now]
        if due:
            self._timers = [tm for tm in self._timers if tm[0] > now]
            for _, fun in sorted(due, key=lambda tm: tm[0]):
                fun()

    def _check(self):
        if not self._running:
            raise Terminator

    def _update(self, force=False):
        self._check()
        self._pump()
        if self._tracer or force:
            self._render()

    def _animate(self, steps, draw):
        for i in range(1, steps + 1):
            self._pump()
            self._render(lambda: draw(i / steps))
            if self._delay:
                _sdl.SDL_Delay(self._delay)

    def _draw_textbox(self):
        title, prompt, text = self._textbox[:3]
        w, h = min(self._w - 40, 480), 150
        x, y = (self._w - w) / 2, (self._h - h) / 2
        rects = (_FRect * 1)(_FRect(x, y, w, h))
        _sdl.SDL_SetRenderDrawColor(self._ren, 240, 240, 240, 255)
        _sdl.SDL_RenderFillRects(self._ren, rects, 1)
        self._line([(x, y), (x + w, y), (x + w, y + h), (x, y + h), (x, y)], (60, 60, 60), 2)
        field = (_FRect * 1)(_FRect(x + 16, y + 90, w - 32, 34))
        _sdl.SDL_SetRenderDrawColor(self._ren, 255, 255, 255, 255)
        _sdl.SDL_RenderFillRects(self._ren, field, 1)
        for txt, ty, size, style in ((title, y + 12, 12, 'bold'), (prompt, y + 50, 11, 'normal'),
                                     (text + '_', y + 96, 12, 'normal')):
            tex = self._text_texture(txt, ('Arial', size, style), (20, 20, 20))
            if tex:
                t, tw, th = tex
                _sdl.SDL_RenderTexture(self._ren, t, None, byref(_FRect(x + 20, ty, tw, th)))
                _sdl.SDL_DestroyTexture(t)

    def _textbox_key(self, name):
        if name in ('Return', 'KP_Enter'):
            self._textbox.append(True)
        elif name == 'Escape':
            self._textbox.append(False)
        elif name == 'BackSpace':
            self._textbox[2] = self._textbox[2][:-1]
        self._dirty = True

    def _ask(self, title, prompt, default=''):
        self._textbox = [str(title), str(prompt), str(default)]
        _sdl.SDL_StartTextInput(self._win)
        try:
            while len(self._textbox) == 3:
                self._pump()
                if self._dirty:
                    self._render()
                _sdl.SDL_Delay(10)
            ok, text = self._textbox[3], self._textbox[2]
        finally:
            if self._running:
                _sdl.SDL_StopTextInput(self._win)
            self._textbox = None
            if self._running:
                self._render()
        return text if ok else None

    def textinput(self, title, prompt):
        return self._ask(title, prompt)

    def numinput(self, title, prompt, default=None, minval=None, maxval=None):
        msg = prompt
        while True:
            s = self._ask(title, msg, '' if default is None else default)
            if s is None:
                return None
            try:
                v = float(s)
            except ValueError:
                msg = 'Please type a number. ' + prompt
                continue
            if (minval is not None and v < minval) or (maxval is not None and v > maxval):
                msg = 'Please type a number from %s to %s. %s' % (minval, maxval, prompt)
                continue
            return v

    def clear(self):
        for t in list(self._turtles):
            t._dead = True
        self._turtles = []
        self._items = []
        self._bg = (255, 255, 255)
        self._bgpic = None
        self._bgpicname = 'nopic'
        self._keys_down, self._keys_up, self._clicks, self._timers = {}, {}, {}, []
        self._world = None
        self._mode = 'standard'
        self._tracer = 1
        self._redraw()
        self._render()

    clearscreen = clear

    def reset(self):
        for t in self._turtles:
            t._setmode(self._mode)
            t.reset()

    resetscreen = reset

    def bgcolor(self, *args):
        if not args:
            return self._colorout(self._bg)
        self._bg = self._color(*args)
        self._redraw()
        self._update()

    def bgpic(self, picname=None):
        if picname is None:
            return self._bgpicname
        if self._bgpic:
            _sdl.SDL_DestroyTexture(self._bgpic[0])
            self._bgpic = None
        if picname != 'nopic':
            self._bgpic = self._image(picname)
        self._bgpicname = picname
        self._redraw()
        self._update()

    def _image(self, path):
        if not _img:
            raise TurtleGraphicsError('turtle needs SDL3_image to load %s' % path)
        tex = _img.IMG_LoadTexture(self._ren, os.fsencode(path))
        if not tex:
            raise TurtleGraphicsError('could not load image %s: %s' % (path, _sdl.SDL_GetError().decode()))
        w, h = c_float(), c_float()
        _sdl.SDL_GetTextureSize(tex, byref(w), byref(h))
        return (tex, w.value, h.value)

    def mode(self, mode=None):
        if mode is None:
            return self._mode
        mode = mode.lower()
        if mode not in ('standard', 'logo', 'world'):
            raise TurtleGraphicsError('No turtle-graphics-mode %s' % mode)
        self._mode = mode
        if mode in ('standard', 'logo'):
            self._world = None
        for t in self._turtles:
            t._setmode(mode)
        self.reset()

    def setworldcoordinates(self, llx, lly, urx, ury):
        if self._mode != 'world':
            self._mode = 'world'
        self._world = (llx, lly, urx, ury)
        self._redraw()
        self._update()

    def colormode(self, cmode=None):
        if cmode is None:
            return self._colormode
        if cmode == 1.0:
            self._colormode = 1.0
        elif cmode == 255:
            self._colormode = 255
        else:
            raise TurtleGraphicsError('colormode must be 1.0 or 255')

    def tracer(self, n=None, delay=None):
        if n is None:
            return self._tracer
        self._tracer = int(n)
        self._tracecount = 0
        if delay is not None:
            self._delay = int(delay)
        if self._tracer:
            self.update()

    def delay(self, delay=None):
        if delay is None:
            return self._delay
        self._delay = int(delay)

    def update(self):
        self._check()
        self._pump()
        self._render()

    def window_width(self):
        return self._w

    def window_height(self):
        return self._h

    def getcanvas(self):
        return None

    def getshapes(self):
        return sorted(self._shapes)

    def register_shape(self, name, shape=None):
        if shape is None:
            if not str(name).lower().endswith(('.gif', '.png', '.bmp', '.jpg', '.jpeg')):
                raise TurtleGraphicsError('register_shape needs a shape or an image file')
            shape = Shape('image', self._image(name))
        elif isinstance(shape, tuple):
            shape = Shape('polygon', shape)
        self._shapes[name] = shape

    addshape = register_shape

    def turtles(self):
        return list(self._turtles)

    def onclick(self, fun, btn=1, add=None):
        if fun is None:
            self._clicks.pop(btn, None)
        else:
            self._clicks[btn] = fun

    onscreenclick = onclick

    def onkey(self, fun, key):
        if fun is None:
            self._keys_up.pop(key, None)
        else:
            self._keys_up[key] = fun

    onkeyrelease = onkey

    def onkeypress(self, fun, key=None):
        if fun is None:
            self._keys_down.pop(key, None)
        else:
            self._keys_down[key] = fun

    def listen(self, xdummy=None, ydummy=None):
        pass

    def ontimer(self, fun, t=0):
        self._timers.append((_sdl.SDL_GetTicks() + int(t), fun))

    def mainloop(self):
        try:
            while self._running:
                self._pump()
                if self._dirty:
                    self._render()
                wait = 10
                if self._timers:
                    wait = max(0, min(wait, min(tm[0] for tm in self._timers) - _sdl.SDL_GetTicks()))
                _sdl.SDL_Delay(wait)
        except Terminator:
            pass

    done = mainloop

    def bye(self):
        self._close()

    def exitonclick(self):
        def leave(x, y):
            self.bye()
        self.onclick(leave)
        self.mainloop()

    def title(self, titlestring):
        _sdl.SDL_SetWindowTitle(self._win, str(titlestring).encode())

    def setup(self, width=0.5, height=0.75, startx=None, starty=None):
        w, h = self._size(width, height)
        _sdl.SDL_SetWindowSize(self._win, w, h)
        self._pump()
        self._resize()
        self._update()

    def screensize(self, canvwidth=None, canvheight=None, bg=None):
        if bg is not None:
            self.bgcolor(bg)
        if canvwidth is None and canvheight is None:
            return self._w, self._h

    def _screenshot(self, path):
        self._render()
        surf = _sdl.SDL_RenderReadPixels(self._ren, None)
        if surf:
            _sdl.SDL_SaveBMP(surf, os.fsencode(path))
            _sdl.SDL_DestroySurface(surf)


class _Screen(TurtleScreen):
    pass


def Screen():
    if TurtleScreen._instance is None or not TurtleScreen._instance._running:
        TurtleScreen._instance = _Screen()
    return TurtleScreen._instance


ScrolledCanvas = None


class RawTurtle:
    _pen = None

    def __init__(self, canvas=None, shape='classic', undobuffersize=1000, visible=True):
        self.screen = canvas if isinstance(canvas, TurtleScreen) else Screen()
        self.screen._turtles.append(self)
        self._dead = False
        self._undo = []
        self._undosize = undobuffersize
        self._setmode(self.screen._mode)
        self._init_state()
        self._shown = visible
        self._shape = shape
        self.screen._update()

    def _init_state(self):
        self._x = self._y = 0.0
        self._heading = 90.0 if self._logo else 0.0
        self._down = True
        self._width = 1
        self._pen = (0, 0, 0)
        self._fillc = (0, 0, 0)
        self._speed = 3
        self._stretch = (1.0, 1.0)
        self._outline = 1
        self._tilt = 0.0
        self._filling = None
        self._poly = None
        self._mine = []
        self._clicks, self._releases, self._drags = {}, {}, {}
        self._stamps = {}
        self._stampid = 0

    def _setmode(self, mode):
        self._logo = mode == 'logo'
        self._fullcircle = getattr(self, '_fullcircle', 360.0)

    def _s(self):
        if self._dead:
            raise Terminator
        return self.screen

    def _angle(self, a):
        return a * 360.0 / self._fullcircle

    def _push(self):
        if self._undosize:
            self._undo.append((self._x, self._y, self._heading, self._down, len(self._mine),
                               list(self._filling) if self._filling else None))
            if len(self._undo) > self._undosize:
                self._undo.pop(0)

    def _item(self, kind, data):
        item = self._s()._add(_Item(kind, self, data))
        self._mine.append(item)
        return item

    def _hops(self, dist):
        sp = self._speed
        if not sp or not self.screen._tracer:
            return 0
        return 1 + int(dist / (3 * (1.1 ** sp) * sp))

    def _goto(self, x, y, draw=True):
        scr = self._s()
        self._push()
        x0, y0 = self._x, self._y
        if self._filling is not None:
            self._filling.append((x, y))
        if self._poly is not None:
            self._poly.append((x, y))
        hops = self._hops(math.hypot(x - x0, y - y0) * scr._scale())
        if hops and (self._shown or (self._down and draw)):
            rgb, width = self._pen, self._width
            down = self._down and draw

            def overlay(f):
                self._x, self._y = x0 + (x - x0) * f, y0 + (y - y0) * f
                if down:
                    scr._line([scr._to_screen(x0, y0), scr._to_screen(self._x, self._y)], rgb, width * scr._scale())
            try:
                scr._animate(hops, overlay)
            finally:
                self._x, self._y = x, y
        self._x, self._y = x, y
        if self._down and draw and (x, y) != (x0, y0):
            self._item('line', ([(x0, y0), (x, y)], self._pen, self._width))
        self._after()

    def _after(self):
        scr = self.screen
        if scr._tracer:
            scr._tracecount += 1
            if scr._tracecount >= scr._tracer:
                scr._tracecount = 0
                scr._update()
        else:
            scr._dirty = True
            scr._pump()

    def _turn(self, deg):
        scr = self._s()
        self._push()
        h0 = self._heading
        if self._speed and scr._tracer and self._shown:
            steps = 1 + int(abs(deg) / (3.0 * self._speed))

            def overlay(f):
                self._heading = h0 + deg * f
            try:
                scr._animate(steps, overlay)
            finally:
                self._heading = h0
        self._heading = (h0 + deg) % 360.0
        self._after()

    def forward(self, distance):
        a = math.radians(self._heading)
        self._goto(self._x + distance * math.cos(a), self._y + distance * math.sin(a))

    fd = forward

    def back(self, distance):
        self.forward(-distance)

    bk = backward = back

    def left(self, angle):
        self._turn(self._angle(angle))

    lt = left

    def right(self, angle):
        self._turn(-self._angle(angle))

    rt = right

    def goto(self, x, y=None):
        if y is None:
            x, y = x
        self._goto(float(x), float(y))

    setpos = setposition = goto

    def teleport(self, x=None, y=None, *, fill_gap=False):
        nx = self._x if x is None else float(x)
        ny = self._y if y is None else float(y)
        down = self._down
        self._down = False
        try:
            self._goto(nx, ny, draw=False)
        finally:
            self._down = down

    def setx(self, x):
        self._goto(float(x), self._y)

    def sety(self, y):
        self._goto(self._x, float(y))

    def _user_heading(self, h):
        return (90 - h) % 360 if self._logo else h

    def heading(self):
        return self._user_heading(self._heading) * self._fullcircle / 360.0

    def setheading(self, to_angle):
        target = self._angle(to_angle)
        if self._logo:
            target = 90 - target
        self._turn((target - self._heading + 180) % 360 - 180)
        self._heading = target % 360.0

    seth = setheading

    def home(self):
        self.goto(0, 0)
        self.setheading(0)

    def circle(self, radius, extent=None, steps=None):
        speed = self._speed
        fullcircle = self._fullcircle
        if extent is None:
            extent = fullcircle
        extent = self._angle(extent)
        if steps is None:
            frac = abs(extent) / 360.0
            steps = 1 + int(min(11 + abs(radius) / 6.0, 59.0) * frac)
        w = extent / steps
        w2 = 0.5 * w
        length = 2.0 * radius * math.sin(math.radians(w2))
        if radius < 0:
            length, w, w2 = -length, -w, -w2
        try:
            if speed:
                self._speed = min(10, max(speed, 6) + 4)
            self._turn(w2)
            for _ in range(steps):
                self.forward(length)
                self._turn(w)
            self._turn(-w2)
        finally:
            self._speed = speed

    def dot(self, size=None, *color):
        if not color:
            if isinstance(size, (str, tuple)):
                color, size = (size,), None
        rgb = self.screen._color(*color) if color else self._pen
        if size is None:
            size = max(self._width + 4, 2 * self._width)
        self._push()
        self._item('dot', ((self._x, self._y), size, rgb))
        self._after()

    def stamp(self):
        self._stampid += 1
        item = self._item('stamp', (self._shapeobj(), (self._x, self._y), self._heading + self._tilt, self._stretch,
                                    self._pen, self._fillc, self._outline))
        self._stamps[self._stampid] = item
        self._after()
        return self._stampid

    def clearstamp(self, stampid):
        item = self._stamps.pop(stampid, None)
        if item:
            self._mine = [i for i in self._mine if i is not item]
            self.screen._remove([item])
            self.screen._update()

    def clearstamps(self, n=None):
        ids = sorted(self._stamps)
        if n is not None:
            ids = ids[:n] if n >= 0 else ids[n:]
        for i in ids:
            self.clearstamp(i)

    def undo(self):
        if not self._undo:
            return
        x, y, h, down, n, filling = self._undo.pop()
        gone = self._mine[n:]
        self._mine = self._mine[:n]
        self._x, self._y, self._heading, self._down = x, y, h, down
        if self._filling is not None:
            self._filling = filling
        self.screen._remove(gone)
        self.screen._update()

    def setundobuffer(self, size):
        self._undosize = size or 0
        if not size:
            self._undo = []

    def undobufferentries(self):
        return len(self._undo)

    def speed(self, speed=None):
        if speed is None:
            return self._speed
        if isinstance(speed, str):
            speed = _SPEEDS.get(speed, self._speed)
        speed = round(speed) if 0.5 < speed < 10.5 else 0
        self._speed = speed

    def position(self):
        return Vec2D(self._x, self._y)

    pos = position

    def towards(self, x, y=None):
        if y is None:
            x, y = x
        a = math.degrees(math.atan2(y - self._y, x - self._x)) % 360
        return self._user_heading(a) * self._fullcircle / 360.0

    def xcor(self):
        return self._x

    def ycor(self):
        return self._y

    def distance(self, x, y=None):
        if y is None:
            x, y = x
        return math.hypot(x - self._x, y - self._y)

    def degrees(self, fullcircle=360.0):
        self._fullcircle = float(fullcircle)

    def radians(self):
        self._fullcircle = 2 * math.pi

    def pendown(self):
        self._down = True

    pd = down = pendown

    def penup(self):
        self._down = False

    pu = up = penup

    def isdown(self):
        return self._down

    def pensize(self, width=None):
        if width is None:
            return self._width
        self._width = width

    width = pensize

    def pen(self, pen=None, **pendict):
        state = {'shown': self._shown, 'pendown': self._down, 'pencolor': self.pencolor(),
                 'fillcolor': self.fillcolor(), 'pensize': self._width, 'speed': self._speed,
                 'resizemode': 'noresize', 'stretchfactor': self._stretch, 'outline': self._outline,
                 'tilt': self._tilt}
        if pen is None and not pendict:
            return state
        d = dict(pen or {}, **pendict)
        if 'shown' in d:
            self._shown = bool(d['shown'])
        if 'pendown' in d:
            self._down = bool(d['pendown'])
        if 'pencolor' in d:
            self.pencolor(d['pencolor'])
        if 'fillcolor' in d:
            self.fillcolor(d['fillcolor'])
        if 'pensize' in d:
            self._width = d['pensize']
        if 'speed' in d:
            self.speed(d['speed'])
        if 'stretchfactor' in d:
            self._stretch = tuple(d['stretchfactor'])
        if 'outline' in d:
            self._outline = d['outline']
        if 'tilt' in d:
            self._tilt = d['tilt']
        self.screen._update()

    def color(self, *args):
        if not args:
            return self.pencolor(), self.fillcolor()
        if len(args) == 1:
            if isinstance(args[0], str) or len(args[0]) == 3 and not isinstance(args[0][0], (tuple, list, str)):
                self._pen = self._fillc = self.screen._color(args[0])
            else:
                self._pen, self._fillc = self.screen._color(args[0][0]), self.screen._color(args[0][1])
        elif len(args) == 2:
            self._pen, self._fillc = self.screen._color(args[0]), self.screen._color(args[1])
        elif len(args) == 3:
            self._pen = self._fillc = self.screen._color(args)
        else:
            raise TurtleGraphicsError('bad color arguments')
        self.screen._update()

    def pencolor(self, *args):
        if not args:
            return self.screen._colorout(self._pen)
        self._pen = self.screen._color(*args)
        self.screen._update()

    def fillcolor(self, *args):
        if not args:
            return self.screen._colorout(self._fillc)
        self._fillc = self.screen._color(*args)
        self.screen._update()

    def filling(self):
        return self._filling is not None

    def begin_fill(self):
        if self._filling is not None:
            self.end_fill()
        self._filling = [(self._x, self._y)]
        self._fillitem = self._item('fill', ([], self._fillc))
        self.screen._flushed = min(self.screen._flushed, len(self.screen._items) - 1)

    def end_fill(self):
        if self._filling is None:
            return
        pts = self._filling
        self._filling = None
        item = self._fillitem
        if len(pts) > 2:
            item.data = (pts, self._fillc)
            scr = self.screen
            idx = next((k for k, i in enumerate(scr._items) if i is item), None)
            if idx is not None and idx < scr._flushed:
                scr._target(scr._canvas)
                for it in scr._items[idx:scr._flushed]:
                    scr._draw_item(it)
                scr._target(None)
        self.screen._update()

    def begin_poly(self):
        self._poly = [(self._x, self._y)]

    def end_poly(self):
        self._lastpoly = tuple(self._poly or ())
        self._poly = None

    def get_poly(self):
        return getattr(self, '_lastpoly', None)

    def reset(self):
        self.screen._remove(self._mine)
        self._init_state()
        self._undo = []
        self._shown = True
        self.screen._update()

    def clear(self):
        self.screen._remove(self._mine)
        self._mine = []
        self._stamps = {}
        self._undo = []
        self.screen._update()

    def write(self, arg, move=False, align='left', font=('Arial', 8, 'normal')):
        scr = self._s()
        text = str(arg)
        tex = scr._text_texture(text, font, self._pen)
        self._push()
        self._item('text', ((self._x, self._y), tex, align))
        if move and tex:
            self._x += tex[1] / scr._scale()
        self._after()

    def showturtle(self):
        self._shown = True
        self.screen._update()

    st = showturtle

    def hideturtle(self):
        self._shown = False
        self.screen._update()

    ht = hideturtle

    def isvisible(self):
        return self._shown

    def _shapeobj(self):
        return self.screen._shapes.get(self._shape, self.screen._shapes['classic'])

    def shape(self, name=None):
        if name is None:
            return self._shape
        if name not in self.screen._shapes:
            raise TurtleGraphicsError('There is no shape named %s' % name)
        self._shape = name
        self.screen._update()

    def shapesize(self, stretch_wid=None, stretch_len=None, outline=None):
        if stretch_wid is None and stretch_len is None and outline is None:
            return self._stretch[0], self._stretch[1], self._outline
        if stretch_wid is not None and stretch_len is None:
            stretch_len = stretch_wid
        sw = self._stretch[0] if stretch_wid is None else stretch_wid
        sl = self._stretch[1] if stretch_len is None else stretch_len
        self._stretch = (sw, sl)
        if outline is not None:
            self._outline = outline
        self.screen._update()

    turtlesize = shapesize

    def resizemode(self, rmode=None):
        return 'noresize' if rmode is None else None

    def shearfactor(self, shear=None):
        return 0.0 if shear is None else None

    def shapetransform(self, t11=None, t12=None, t21=None, t22=None):
        return (self._stretch[0], 0.0, 0.0, self._stretch[1])

    def get_shapepoly(self):
        return self._shapeobj()._data if self._shapeobj()._type == 'polygon' else None

    def settiltangle(self, angle):
        self._tilt = self._angle(angle)
        self.screen._update()

    def tiltangle(self, angle=None):
        if angle is None:
            return self._tilt * self._fullcircle / 360.0
        self.settiltangle(angle)

    def tilt(self, angle):
        self.settiltangle((self._tilt * self._fullcircle / 360.0) + angle)

    def onclick(self, fun, btn=1, add=None):
        if fun is None:
            self._clicks.pop(btn, None)
        else:
            self._clicks[btn] = fun

    def onrelease(self, fun, btn=1, add=None):
        if fun is None:
            self._releases.pop(btn, None)
        else:
            self._releases[btn] = fun

    def ondrag(self, fun, btn=1, add=None):
        if fun is None:
            self._drags.pop(btn, None)
        else:
            self._drags[btn] = fun

    def getscreen(self):
        return self.screen

    def getturtle(self):
        return self

    getpen = getturtle

    def clone(self):
        t = self.__class__.__new__(self.__class__)
        t.__dict__.update(self.__dict__)
        t._mine, t._undo, t._stamps = [], [], {}
        t._clicks, t._releases, t._drags = {}, {}, {}
        t._filling = None
        self.screen._turtles.append(t)
        self.screen._update()
        return t

    def _draw_turtle(self):
        self.screen._draw_shape(self._shapeobj(), (self._x, self._y), self._heading + self._tilt, self._stretch,
                                self._pen, self._fillc, self._outline)


RawPen = RawTurtle


class Turtle(RawTurtle):
    def __init__(self, shape='classic', undobuffersize=1000, visible=True):
        RawTurtle.__init__(self, Screen(), shape=shape, undobuffersize=undobuffersize, visible=visible)


Pen = Turtle
TurtleScreenBase = TurtleScreen

_screen = None
_pen = None


def _getscreen():
    global _screen
    if _screen is None or not _screen._running:
        _screen = Screen()
    return _screen


def _getpen():
    global _pen
    if _pen is None or _pen._dead:
        _pen = Turtle()
    return _pen


def _screenfunc(name):
    def f(*args, **kwargs):
        return getattr(_getscreen(), name)(*args, **kwargs)
    f.__name__ = name
    f.__doc__ = getattr(TurtleScreen, name).__doc__
    return f


def _turtlefunc(name):
    def f(*args, **kwargs):
        return getattr(_getpen(), name)(*args, **kwargs)
    f.__name__ = name
    f.__doc__ = getattr(RawTurtle, name).__doc__
    return f


for _name in ('addshape', 'bgcolor', 'bgpic', 'clearscreen', 'colormode', 'delay', 'getcanvas', 'getshapes',
              'listen', 'mode', 'numinput', 'onkey', 'onkeypress', 'onkeyrelease', 'onscreenclick', 'ontimer',
              'register_shape', 'resetscreen', 'screensize', 'setup', 'setworldcoordinates', 'textinput', 'title',
              'tracer', 'turtles', 'update', 'window_height', 'window_width'):
    globals()[_name] = _screenfunc(_name)

for _name in ('back', 'backward', 'begin_fill', 'begin_poly', 'bk', 'circle', 'clear', 'clearstamp', 'clearstamps',
              'clone', 'color', 'degrees', 'distance', 'dot', 'down', 'end_fill', 'end_poly', 'fd', 'fillcolor',
              'filling', 'forward', 'get_poly', 'getpen', 'getscreen', 'get_shapepoly', 'getturtle', 'goto',
              'heading', 'hideturtle', 'home', 'ht', 'isdown', 'isvisible', 'left', 'lt', 'onclick', 'ondrag',
              'onrelease', 'pd', 'pen', 'pencolor', 'pendown', 'pensize', 'penup', 'pos', 'position', 'pu',
              'radians', 'right', 'reset', 'resizemode', 'rt', 'seth', 'setheading', 'setpos', 'setposition',
              'settiltangle', 'setundobuffer', 'setx', 'sety', 'shape', 'shapesize', 'shapetransform',
              'shearfactor', 'showturtle', 'speed', 'st', 'stamp', 'teleport', 'tilt', 'tiltangle', 'towards',
              'turtlesize', 'undo', 'undobufferentries', 'up', 'width', 'write', 'xcor', 'ycor'):
    globals()[_name] = _turtlefunc(_name)


def mainloop():
    if _screen is not None and _screen._running:
        _screen.mainloop()


done = mainloop


def bye():
    if _screen is not None:
        _screen.bye()


def exitonclick():
    _getscreen().exitonclick()


if __name__ == '__main__':
    speed(0)
    for i in range(36):
        color('red' if i % 2 else 'orange')
        circle(100)
        left(10)
    done()
