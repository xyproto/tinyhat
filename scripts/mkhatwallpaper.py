#!/usr/bin/env python3
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkgrubtheme

SCALE = 8
out = sys.argv[1]
mkgrubtheme.png(out, mkgrubtheme.LOW * SCALE, mkgrubtheme.LOW * SCALE, mkgrubtheme.hat(0.45, SCALE))
