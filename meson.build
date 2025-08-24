project('reactor', ['c','cpp'], version: '1.0.0', default_options: ['c_std=c18','cpp_std=c++23','cpp_args=-Wno-subobject-linkage'])

# Find Boost dependency
#boost_dep = dependency('boost', required: true)
#boost_dep = dependency('boost', modules: ['url'], required: true)
boost_compile_args = [
    '-DBOOST_ASIO_DISABLE_THREADS',
    '-DBOOST_ALL_NO_LIB',
    '-DBOOST_SYSTEM_NO_DEPRECATED',
    '-DBOOST_ERROR_CODE_HEADER_ONLY',
    '-DBOOST_COROUTINES_NO_DEPRECATION_WARNING',
]
boost_dep = declare_dependency(
    dependencies: dependency('boost',modules: ['url'], required: true),
    compile_args: boost_compile_args,
)
openssl_dep = dependency('openssl', required: true)
zlib_dep = dependency('zlib')

nlohmann_json_dep = dependency(
    'nlohmann_json',
    version: '>=3.11.3',
    fallback: ['nlohmann_json', 'nlohmann_json_dep'],
    include_type: 'system',
)


if not nlohmann_json_dep.found()
    nlohmann_json_proj = subproject('nlohmann_json', required: true)
    nlohmann_json_dep = nlohmann_json_proj.get_variable('nlohmann_json_dep')
endif

sdeventplus_dep = dependency('sdeventplus')
reactor_inc = include_directories('include')
reactor_dep = declare_dependency(
    include_directories: reactor_inc,
    dependencies: [boost_dep,nlohmann_json_dep, openssl_dep, zlib_dep, sdeventplus_dep]
)
reactorhead_dep = declare_dependency(
    include_directories: reactor_inc
)
cxx = meson.get_compiler('cpp')

# Add dependencies
#add_project_arguments(
    #cxx.get_supported_arguments(['-DBOOST_ASIO_DISABLE_THREADS']),language: 'cpp')
subdir('examples')


# Install headers
install_subdir('include', install_dir: 'include/reactor',strip_directory: true)

# Create and install pkg-config file
pkgconfig = import('pkgconfig')

pkgconfig.generate(
  name: 'reactor',
  description: 'Reactor library',
  version: meson.project_version(),
  libraries: reactorhead_dep,
  install_dir: join_paths(get_option('libdir'), 'pkgconfig')
)
