test_exe = executable(
    'test_pure',
    'test_pure.c',
    link_with:    silktex_lib,
    dependencies: project_dependencies,
    c_args:       build_args,
    link_args:    build_link_args,
)

bench_exe = executable(
    'bench_pure',
    'bench_pure.c',
    link_with:    silktex_lib,
    dependencies: project_dependencies,
    c_args:       build_args,
    link_args:    build_link_args,
)

test('pure functions', test_exe,
     env: ['G_SLICE=always-malloc', 'G_DEBUG=gc-friendly'])

benchmark('speed', bench_exe)
