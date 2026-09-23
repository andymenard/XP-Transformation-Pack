"""Compatibility entry point for the non-destructive preview."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from startmenu_gen import MsStyle, color, export, font, numbers
