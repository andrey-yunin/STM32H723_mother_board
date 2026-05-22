#!/usr/bin/env python3
"""
Compatibility wrapper for the old combined-command test entry point.

The canonical Host protocol scenarios now live in test_main_processes.py so
that ACK/DATA/DONE parsing, command constants and refactoring assumptions stay
in one place.
"""

from __future__ import annotations

import sys

from test_main_processes import main


if __name__ == "__main__":
    sys.exit(main())
