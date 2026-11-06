#!/usr/bin/env python3
import os
import re
import signal
import subprocess
import sys
import time

error = re.compile(rb"^make(\[\d+\])?: \*\*\* .*Error \d+")
p = subprocess.Popen(["make", *sys.argv[1:]], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, start_new_session=True)
failed = False
for line in p.stdout:
    sys.stdout.buffer.write(line)
    sys.stdout.buffer.flush()
    if not failed and error.match(line):
        failed = True
        sys.stdout.buffer.write(b"failfast.py: stopping the build at the first error\n")
        sys.stdout.buffer.flush()
        os.killpg(p.pid, signal.SIGTERM)
        time.sleep(5)
        try:
            os.killpg(p.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        break
rc = p.wait()
sys.exit(2 if failed else rc)
