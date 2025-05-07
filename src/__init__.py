# __init__.py - Package initialization
"""SilkTex - A lightweight LaTeX editor"""
# SilkTex package initialization
# This makes importing modules from the package easier
"""
SilkTex - LaTeX editor with live preview
"""

# Import main classes for easy access
from .main import main
from .silktex import SilkTexApp
from .window import SilkTexWindow
from .config import ConfigManager
from .template_manager import TemplateManager
from .snippets_manager import SnippetsManager
from .document_structure import DocumentStructure
from .preferences_dialog import PreferencesDialog
from .spell_checker import LatexSpellChecker
"""
SilkTex - LaTeX editor with live preview
"""

# Import main functions and classes for easy access
from src.main import main
__all__ = [
    'SilkTexApp', 
    'SilkTexWindow', 
    'ConfigManager',
    'TemplateManager',
    'SnippetsManager',
    'DocumentStructure',
    'PreferencesDialog',
    'LatexSpellChecker'
]
# Import modules for package level access
from silktex.main import main
"""
SilkTex - LaTeX editor with live preview
"""

# Make main.py importable directly
from .main import main, SilkTexApplication, SilkTexWindow