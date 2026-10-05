#!/usr/bin/env python3
"""Absolute-path entrypoint; preserves the minimal native runner environment."""
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent/'src'))
if sys.argv[1]=='sandbox':
    from lab_interface.linux_sandbox import main
    sys.argv.pop(1)
    main()
else:
    from lab_interface.remote_worker import main
    main(sys.argv[1])
