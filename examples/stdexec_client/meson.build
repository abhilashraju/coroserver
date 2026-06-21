# Try to find stdexec via pkg-config first
stdexec_dep = dependency('stdexec', required: false)

# If not found via pkg-config, try to find headers manually
if not stdexec_dep.found()
    compiler = meson.get_compiler('cpp')
    
    # Check common installation paths
    stdexec_paths = [
        '/usr/local/include',
        '/usr/include',
        get_option('prefix') / 'include'
    ]
    
    foreach path : stdexec_paths
        if compiler.has_header('stdexec/execution.hpp', args: ['-I' + path])
            stdexec_dep = declare_dependency(
                include_directories: include_directories(path)
            )
            message('Found stdexec headers in: ' + path)
            break
        endif
    endforeach
endif

# Build example only if stdexec is available
if stdexec_dep.found()
    executable(
        'webclient_stdexec_example',
        'webclient_stdexec_example.cpp',
        dependencies: [reactor_dep, stdexec_dep],
        install: false
    )
    message('Building webclient_stdexec_example')
else
    message('stdexec not found - skipping webclient_stdexec_example')
    message('To install stdexec, see: examples/stdexec_client/INSTALL_STDEXEC.md')
endif