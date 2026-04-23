## Latex editor based on GUMMI

- Its working on macOS (till i update to GTK 4) :)
- Snippets to autocomplete via Keybinds.
- Real time Preview
- fast compilation
- GTK4-Adwaita framework
- sharper on HIdpi Displays 

## Meson buildable

```
$ git clone https://github.com/DERK0CHER/SilkTex.git
$ cd SilkTex
$ meson setup bild
$ ninja -C build
```
### ⚠️ TODO 
- migrate remaining GTK3 runtime code paths to GTK4/Libadwaita APIs
- better Luatex integration

## Fork:
[credits]: https://github.com/alexandervdm/gummi.git

[Gummi][credits]
