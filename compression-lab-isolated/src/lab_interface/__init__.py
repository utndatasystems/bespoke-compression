"""A small, unscored interface around the retained Compression Lab runtime."""
from pathlib import Path
import sys

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / "vendor"))

__version__ = "0.1.0"
