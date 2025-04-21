# Install UI files
ui_dir = get_option('datadir') / 'silktex' / 'ui'

# Install the compiled UI files from the parent directory targets
# This connects the UI files generated in the parent build file to this subdirectory
foreach ui_file : ui_targets
  # The install is already handled in the parent directory's custom_target
  # Just ensure the dependency is set correctly
  meson.add_install_script('touch', ui_file.full_path())
endforeach

# Install any additional static UI resources
install_data(
  [
    # List any static UI files here
    # 'style.css',
    # 'icons.css',
  ],
  install_dir: ui_dir
)