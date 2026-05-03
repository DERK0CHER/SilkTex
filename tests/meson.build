test_exe = executable(
    'test_pure',
    'test_pure.c',
    link_with:    silktex_lib,
    dependencies: project_dependencies,
    c_args:       build_args,
)

bench_exe = executable(
    'bench_pure',
    'bench_pure.c',
    link_with:    silktex_lib,
    dependencies: project_dependencies,
    c_args:       build_args,
)

test('pure functions', test_exe)
benchmark('speed',    bench_exe)
