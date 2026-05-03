test_exe = executable(
    'test_pure',
    'test_pure.c',
    link_with:    silktex_lib,
    dependencies: project_dependencies,
    c_args:       build_args,
)

test('pure functions', test_exe)
