import sys
from pathlib import Path

# Make cad/ importable (smart_register package + export.py registry) without
# requiring `pip install -e`.
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
