desktop_file = i18n.merge_file(
    input: 'org.silktex.gnome.desktop.in',
    output: 'org.silktex.gnome.desktop',
    type: 'desktop',
    po_dir: '../po',
    install: true,
    install_dir: get_option('datadir') / 'applications'
)

desktop_utils = find_program('desktop-file-validate', required: false)
if desktop_utils.found()
  test('Validate desktop file', desktop_utils, args: [desktop_file])
endif

appstream_file = i18n.merge_file(
    input: 'org.silktex.gnome.metainfo.xml.in',
    output: 'org.silktex.gnome.metainfo.xml',
    po_dir: '../po',
    install: true,
    install_dir: get_option('datadir') / 'metainfo'
)

appstreamcli = find_program('appstreamcli', required: false, disabler: true)
test('Validate appstream file', appstreamcli,
     args: ['validate', '--no-net', '--explain', appstream_file])

install_data('org.silktex.gnome.gschema.xml',
    install_dir: get_option('datadir') / 'glib-2.0' / 'schemas'
)

compile_schemas = find_program('glib-compile-schemas', required: false, disabler: true)
test('Validate schema file',
     compile_schemas,
     args: ['--strict', '--dry-run', meson.current_source_dir()])

service_conf = configuration_data()
service_conf.set('bindir', get_option('prefix') / get_option('bindir'))
configure_file(
    input: 'org.silktex.gnome.service.in',
    output: 'org.silktex.gnome.service',
    configuration: service_conf,
    install_dir: get_option('datadir') / 'dbus-1' / 'services'
)

blueprint_compiler = find_program('blueprint-compiler', required: true)

# Define blueprint input files and corresponding output ui files
blueprint_sources = [
    'silktex',
    'main',
    'toolbar',
    'window'
]

ui_targets = []
ui_files = []

# Process each blueprint file
foreach name : blueprint_sources
    blp_file = 'ui/' + name + '.blp'
    ui_file = name + '.ui'

    ui_target = custom_target(ui_file,
        input: blp_file,
        output: ui_file,
        build_by_default: true,  # Ensure the files are built by default
        command: [blueprint_compiler, 'compile', '--output', '@OUTPUT@', '@INPUT@'],
        install: true,
        install_dir: get_option('datadir') / 'silktex' / 'ui'
    )

    ui_targets += ui_target
    ui_files += ui_file
endforeach

# Make UI targets available to subdirectories
# This helps ensure the files are properly generated before installing
meson.override_dependency('silktex-ui-files', declare_dependency(sources: ui_targets))

subdir('icons')
subdir('ui')