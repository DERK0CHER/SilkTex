FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    clang \
    meson \
    ninja-build \
    pkg-config \
    gettext \
    blueprint-compiler \
    libgtk-4-dev \
    libadwaita-1-dev \
    libgtksourceview-5-dev \
    libpoppler-glib-dev \
    libsynctex-dev \
    texlive-binaries \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

RUN meson setup build && meson compile -C build

CMD ["./build/src/gummi", "--version"]
