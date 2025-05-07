"""
SilkTex - A lightweight LaTeX editor with live preview
"""

# Import all required classes for easy access
try:
    # Try to import from the silktex module
    from .silktex import SilkTexApplication
    from .window import SilkTexWindow
    from .config import ConfigManager
except ImportError:
    # If that fails, try direct imports
    try:
        from silktex import SilkTexApplication
        from window import SilkTexWindow
        from config import ConfigManager
    except ImportError:
        # Provide direct imports as a fallback
        from .silktex import SilkTexApplication

# Define what's available when importing from this package
__all__ = [
    'SilkTexApplication', 
    'SilkTexWindow', 
    'ConfigManager'
]