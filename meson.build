project('reactor', ['c','cpp'], version: '1.0.0', default_options: ['c_std=c18','cpp_std=c++23','cpp_args=-Wno-subobject-linkage'])

# Find Boost dependency
#boost_dep = dependency('boost', required: true)
boost_dep = dependency('boost', modules: ['url'], required: true)

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
reactor_dep = declare_dependency(
    include_directories:['include'],
    dependencies:[nlohmann_json_dep,openssl_dep,boost_dep,zlib_dep,sdeventplus_dep]
)
cxx = meson.get_compiler('cpp')
add_project_arguments('-DNSNAME=reactor', language: 'cpp')
# Add dependencies
#add_project_arguments(
    #cxx.get_supported_arguments(['-DBOOST_ASIO_DISABLE_THREADS']),language: 'cpp')
subdir('examples')


# Install headers
install_subdir('include', install_dir: 'include/reactor')

# Create and install pkg-config file
pkgconfig = import('pkgconfig')
pkgconfig.generate(
    name: 'reactor',
    description: 'Reactor project',
    version: '1.0.0',
    libraries: reactor_dep,
    subdirs: 'reactor',
    filebase: 'reactor'
)

