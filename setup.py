#!/usr/bin/env python3
# setup.py - Installation script for SilkTex

from setuptools import setup, find_packages
import os
import sys

# Check if Python version is sufficient
if sys.version_info < (3, 8):
    print("SilkTex requires Python 3.8 or later.")
    sys.exit(1)

setup(
    name="silktex",
    version="0.1.0",
    description="A modern LaTeX editor with live preview",
    author="SilkTex Team",
    author_email="info@silktex.org",
    url="https://github.com/silktex/silktex",
    packages=find_packages(where="src"),
    package_dir={"": "src"},
    include_package_data=True,
    python_requires=">=3.8",
    install_requires=[
        "PyGObject>=3.42.0",  # GTK4 requires PyGObject 3.42+
        "pycairo>=1.20.0",    # Required for GTK drawing
        "pypdf>=3.0.0",       # For PDF handling
        "pyenchant>=3.2.0",   # For spell checking
        "watchdog>=2.1.0",    # For file monitoring
    ],
    entry_points={
        "console_scripts": [
            "silktex=silktex.main:main",
        ],
    },
    classifiers=[
        "Development Status :: 3 - Alpha",
        "Environment :: X11 Applications :: GTK",
        "Intended Audience :: Education",
        "Intended Audience :: Science/Research",
        "License :: OSI Approved :: MIT License",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
        "Topic :: Text Editors",
        "Topic :: Text Processing :: Markup :: LaTeX",
    ],
)
    sys.exit('SilkTex requires Python 3.6 or later')

# Get the version from the package
version = '0.1.0'
try:
    with open('src/silktex/__init__.py', 'r') as f:
        for line in f:
            if line.startswith('__version__'):
                version = line.split('=')[1].strip().strip('"\'')
                break
except (FileNotFoundError, IndexError):
    pass

# Get long description from README.md
long_description = "SilkTex - A modern LaTeX editor with live preview"
if os.path.exists('README.md'):
    with open('README.md', 'r') as f:
        long_description = f.read()

# Dependencies
install_requires = [
    'PyGObject>=3.40.0',  # GTK4 requires PyGObject 3.40+
]

# Optional dependencies
extras_require = {
    'dev': [
        'pytest',
        'flake8',
        'black',
    ],
}

# Setup configuration
setup(
    name='silktex',
    version=version,
    description='A modern LaTeX editor with live preview',
    long_description=long_description,
    long_description_content_type='text/markdown',
    author='SilkTex Team',
    author_email='info@silktex.org',
    url='https://github.com/silktex/silktex',
    packages=find_packages(where="src"),
    package_dir={"": "src"},
    include_package_data=True,
    entry_points={
        'console_scripts': [
            'silktex=silktex:main',
        ],
        'gui_scripts': [
            'silktex-gui=silktex:main',
        ],
    },
    install_requires=install_requires,
    extras_require=extras_require,
    python_requires='>=3.6',
    classifiers=[
        'Development Status :: 3 - Alpha',
        'Environment :: X11 Applications :: GTK',
        'Intended Audience :: Education',
        'Intended Audience :: Science/Research',
        'License :: OSI Approved :: GNU General Public License v3 (GPLv3)',
        'Operating System :: POSIX :: Linux',
        'Operating System :: MacOS :: MacOS X',
        'Programming Language :: Python :: 3',
        'Programming Language :: Python :: 3.6',
        'Programming Language :: Python :: 3.7',
        'Programming Language :: Python :: 3.8',
        'Programming Language :: Python :: 3.9',
        'Programming Language :: Python :: 3.10',
        'Programming Language :: Python :: 3.11',
        'Topic :: Text Editors',
        'Topic :: Text Processing :: Markup :: LaTeX',
    ],
    keywords='latex editor gtk gtk4 preview',
    project_urls={
        'Bug Reports': 'https://github.com/silktex/silktex/issues',
        'Source': 'https://github.com/silktex/silktex',
    },
    data_files=[
        ('share/applications', ['data/org.example.silktex.desktop']),
        ('share/icons/hicolor/scalable/apps', ['data/org.example.silktex.svg']),
        ('share/metainfo', ['data/org.example.silktex.metainfo.xml']),
    ],
)
from setuptools import setup, find_packages

setup(
    name="silktex",
    version="0.1.0",
    description="A lightweight LaTeX editor with live preview",
    author="SilkTex Team",
    author_email="example@example.com",
    url="https://github.com/example/silktex",
    package_dir={"": "src"},
    packages=find_packages(where="src"),
    include_package_data=True,
    entry_points={
        "console_scripts": [
            "silktex=silktex:main",
        ],
    },
    install_requires=[
        "pygobject>=3.38.0",
    ],
    python_requires=">=3.6",
    classifiers=[
        "Development Status :: 3 - Alpha",
        "Environment :: X11 Applications :: GTK",
        "Intended Audience :: Education",
        "Intended Audience :: Science/Research",
        "License :: OSI Approved :: MIT License",
        "Programming Language :: Python :: 3",
        "Topic :: Scientific/Engineering",
        "Topic :: Text Editors",
        "Topic :: Text Processing :: Markup :: LaTeX",
    ],
)
