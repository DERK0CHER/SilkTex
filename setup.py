#!/usr/bin/env python3

from setuptools import setup, find_packages

setup(
    name="silktex",
    version="0.1.0",
    description="A lightweight LaTeX editor with live preview",
    author="SilkTex Team",
    author_email="example@example.com",
    url="https://github.com/example/silktex",
    packages=find_packages(),
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
