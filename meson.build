project(
    'wayfire-toplevel-capture',
    'cpp',
    default_options: ['cpp_std=c++17', 'c_std=c11'],
)

add_project_arguments(['-DWAYFIRE_PLUGIN', '-DWLR_USE_UNSTABLE'], language: 'cpp')

wayfire = dependency('wayfire')

shared_module(
    'toplevel-capture',
    'plugin.cpp',
    dependencies: [wayfire, dependency('wlroots-0.20')],
    install: true,
    install_dir: join_paths(get_option('libdir'), 'wayfire')
)

install_data(
    'toplevel-capture.xml',
    install_dir: wayfire.get_variable(pkgconfig: 'metadatadir')
)
