project('reactor', 'cpp', version: '1.0.0', default_options: ['cpp_std=c++20'])

# Find Boost dependency
#boost_dep = dependency('boost', required: true)
boost_dep = dependency('boost', modules: ['url'], required: true)

openssl_dep = dependency('openssl', required: true)


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

#boost_url_dep = dependency('boost-url', required: false)
#boost_url_dep = dependency('boost-url', required: false)
#if not boost_url_dep.found()
 #   cmake = import('cmake')
 #  boost_url_proj = cmake.subproject('boost-url', cmake_options: [
 #       '-DBUILD_SHARED_LIBS=OFF',
 #       '-DBOOST_URL_BUILD_EXAMPLES=OFF'
 #   ])
 ##   boost_url_dep = boost_url_proj.dependency('boost-url')
#endif

reactor_dep = declare_dependency(
    include_directories:['include'],
    dependencies:[nlohmann_json_dep,openssl_dep,boost_dep]
)
cxx = meson.get_compiler('cpp')
# Add dependencies
add_project_arguments(
    cxx.get_supported_arguments(['-DBOOST_ASIO_DISABLE_THREADS']),language: 'cpp')
subdir('examples')

# Install headers
#install_headers('include', subdir: 'reactor')

# Create and install pkg-config file
pkgconfig = import('pkgconfig')
pkgconfig.generate(
    name: 'reactor',
    description: 'Reactor project',
    version: '1.0.0',
    libraries: reactor_dep,
    subdirs: 'include',
    filebase: 'reactor'
)