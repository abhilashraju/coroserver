project('coroserver', 'cpp', version: '1.0.0', default_options: ['cpp_std=c++20'])

# Find Boost dependency
boost_dep = dependency('boost', required: true)
openssl_dep = dependency('openssl', required: true)
sdbusplus_dep = dependency('sdbusplus', required: true, include_type: 'system')
coroserver_dep = declare_dependency(
    include_directories:['include']
)
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

cxx = meson.get_compiler('cpp')
# Add dependencies
add_project_arguments(
    cxx.get_supported_arguments(['-DBOOST_ASIO_DISABLE_THREADS']),language: 'cpp')
subdir('examples')