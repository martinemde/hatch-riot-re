"""Filter: collapse the triplicated stores emitted by -mfix-esp32-psram-cache-issue.

usage: ... | dd.py
"""
import sys
prev=None
for l in sys.stdin:
    k=l.split(': ',1)[-1].strip()
    if k==prev and k.startswith('s'): continue
    prev=k; sys.stdout.write(l)
