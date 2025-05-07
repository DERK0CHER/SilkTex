# SilkTex
# SilkTex

SilkTex is a lightweight LaTeX editor with live preview functionality, built using the GTK 4 and libadwaita toolkit.

## Features

- Clean, modern user interface using GTK 4 and libadwaita
- Real-time PDF preview
- Syntax highlighting for LaTeX
- Project file navigation
- Multiple LaTeX engine support (pdflatex, xelatex, lualatex)
- Auto-save and auto-compile options
- Customizable editor preferences
- Light and dark theme support

## Requirements

- Python 3.6+
- GTK 4
- Libadwaita
- GtkSourceView 5
- WebKit2GTK 6.0
- LaTeX distribution (TeXLive, MiKTeX, etc.)

## Installation

### Dependencies

First, install the required system dependencies:

#### Debian/Ubuntu
A modern GTK4/libadwaita application with a beautiful and responsive user interface for working with TeX files.
# SilkTex LaTeX Editor

SilkTex is a modern LaTeX editor built with GTK4 and Libadwaita, designed to be lightweight, elegant, and easy to use. It provides an integrated environment for authoring, editing, and compiling LaTeX documents.

## Features

- Modern GTK4 and Libadwaita-based user interface
- Real-time document structure navigation
- Syntax highlighting for LaTeX
- Integrated PDF preview
- Live compilation and error reporting
- Support for various LaTeX engines (pdflatex, xelatex, lualatex)
- Customizable editor settings
- Dark and light themes with system integration

## Installation

### Dependencies

- Python 3.8 or higher
- GTK 4.0 or higher
- Libadwaita 1.0 or higher
- PyGObject
- GtkSourceView 5
- WebKit2 (for PDF preview)
- A LaTeX distribution (TeX Live, MiKTeX, etc.)

### Running from Source

1. Clone this repository:
   ```bash
   git clone https://github.com/example/silktex.git
   cd silktex
   ```

2. Run the application:
   ```bash
   python3 -m src.main
   ```

### Installation with Meson (Recommended)

1. Install build dependencies (Ubuntu example):
   ```bash
   sudo apt install python3-pip meson ninja-build
   ```

2. Build and install:
   ```bash
   meson setup _build
   meson compile -C _build
   meson install -C _build
   ```

## Usage

### Basic Operations

- Create a new document: <kbd>Ctrl</kbd> + <kbd>N</kbd>
- Open a document: <kbd>Ctrl</kbd> + <kbd>O</kbd>
- Save: <kbd>Ctrl</kbd> + <kbd>S</kbd>
- Save As: <kbd>Ctrl</kbd> + <kbd>Shift</kbd> + <kbd>S</kbd>
- Compile: <kbd>F5</kbd>
- Toggle sidebar: <kbd>F9</kbd>

### Document Structure

The sidebar shows the document structure based on LaTeX sectioning commands (chapters, sections, subsections, etc.). Click on any entry to navigate to that section in your document.

### Compilation

SilkTex supports different LaTeX engines:
- pdflatex (default)
- xelatex
- lualatex

You can select the engine in the preferences dialog. Compilation can be set to run automatically when saving a document.

### Preview

The integrated PDF preview shows the compiled document. It updates automatically when the document is compiled. You can adjust the preview refresh settings in the preferences dialog.

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## License

This project is licensed under the GPL-3.0 License - see the LICENSE file for details.
## Features

- Modern GTK4 and libadwaita interface
- Dark mode support
- Responsive design
- Blueprint UI definitions
- TeX editing and compilation

## Dependencies

### Required Packages

- Python 3.8+
- GTK 4 (>= 4.0.0)
- libadwaita (>= 1.0.0)
- PyGObject (Python GObject Introspection)
- Blueprint compiler (for development)
- Meson (>= 0.59.0)
- Ninja build system
- GtkSourceView 5 (>= 5.0.0)

### Package Installation

#### Fedora/RHEL/CentOS (DNF)

For Fedora, RHEL, CentOS and other distributions using dnf, copy and paste this command:

```bash
sudo dnf install -y \
  python3-3.8* \
  gtk4 \
  gtk4-devel \
  libadwaita \
  libadwaita-devel \
  gtksourceview5 \
  gtksourceview5-devel \
  python3-gobject \
  python3-cairo \
  meson \
  ninja-build \
  blueprint-compiler \
  gettext \
  appstream \
  desktop-file-utils