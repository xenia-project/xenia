def main(ctx):
    return [
        pipeline_lint(),
        pipeline_linux_desktop('x86_64-linux-clang', image_linux_x86_64(), 'amd64', 'clang'),
        pipeline_linux_desktop('x86_64-linux-gcc', image_linux_x86_64(), 'amd64', 'gcc'),
    ]

def image_linux_x86_64():
    return 'xeniaproject/buildenv:2022-01-01'

def volume_build(toolchain, path='/drone/src/build'):
    return {
        'name': 'build-' + toolchain,
        'path': path,
    }

def command_cc(cc):
    # set CC, CXX, ...
    return 'export $(cat /{}.env | sed \'s/#.*//g\' | xargs)'.format(cc)

# Run lint in a separate pipeline so that it will try building even if lint fails
def pipeline_lint():
    return {
        'kind': 'pipeline',
        'type': 'docker',
        'name': 'lint',
        'steps': [
            {
                'name': 'lint',
                'image': image_linux_x86_64(),
                'commands': [
                  'clang-format --version',
                  './xenia-build lint --all',
                ],
            },
        ],
    }

def pipeline_linux_desktop(name, image, arch, cc):
    return {
        'kind': 'pipeline',
        'type': 'docker',
        'name': name,
        'platform': {
            'os': 'linux',
            'arch': arch,
        },
        # These volumes will be mounted at the build directory, allowing to
        # run different premake toolchains from the same source tree
        'volumes': [
          {
            'name': 'build-premake',
            'temp': {},
          },
          {
            'name': 'build-cmake',
            'temp': {},
          },
        ],

        'steps': [
            #
            # Setup the source tree
            #
            {
                'name': 'clone-submodules',
                  'image': image,
                  'commands': [
                      'pwd',
                      # May miss recursive submodules (but faster than xb setup)
                      'git submodule update --init --depth 1 -j $(nproc)',
                  ],
              },


            #
            # Setup the two build systems
            #

            # Native premake Makefiles for production
            {
                'name': 'toolchain-premake',
                'image': image,
                'volumes': [volume_build('premake')],
                'commands': [
                    command_cc(cc),
                    '$CXX --version',
                    'python3 --version',
                    './xenia-build premake --cc={}'.format(cc),
              ],
              'depends_on': ['clone-submodules'],
            },

            # Development toolchain
            {
                'name': 'toolchain-cmake',
                'image': image,
                'volumes': [volume_build('cmake')],
                'commands': [
                    command_cc(cc),
                    '''
                    ./xenia-build premake --cc={} --devenv=cmake
                    cd build
                    for c in Debug Release
                    do
                      mkdir cmake-$c
                      cd cmake-$c
                      cmake -DCMAKE_BUILD_TYPE=$c ..
                      cd ..
                    done
                    '''.format(cc),
                ],
                # Premake itself needs to be build first:
                'depends_on': ['toolchain-premake'],
            },

            #
            # Building
            #
            {
                'name': 'build-premake-debug-all',
                'image': image,
                'volumes': [volume_build('premake')],
                'commands': [
                    command_cc(cc),
                    './xenia-build build --no_premake -j$(nproc) --config=Debug',
                ],
                'depends_on': ['toolchain-premake'],
            },

            {
                'name': 'build-premake-release-tests',
                'image': image,
                'volumes': [volume_build('premake')],
                'commands': [
                    command_cc(cc),
                    './xenia-build build --no_premake -j$(nproc) --config=Release --target=xenia-base-tests',
                ],
                'depends_on': ['toolchain-premake'],
            },

            {
                'name': 'build-premake-release-all',
                'image': image,
                'volumes': [volume_build('premake')],
                'commands': [
                    command_cc(cc),
                    './xenia-build build --no_premake -j$(nproc) --config=Release',
                ],
                'depends_on': ['build-premake-release-tests'],
            },

            {
                'name': 'build-cmake-debug-all',
                'image': image,
                'volumes': [volume_build('cmake')],
                'commands': [
                    command_cc(cc),
                    'cd build/cmake-Debug',
                    'cmake --build . -j$(nproc)',
                ],
                'depends_on': ['toolchain-cmake'],
            },

            {
                'name': 'build-cmake-release-tests',
                'image': image,
                'volumes': [volume_build('cmake')],
                'commands': [
                    command_cc(cc),
                    'cd build/cmake-Release',
                    'cmake --build . -j$(nproc) --target xenia-base-tests',
                ],
                'depends_on': ['toolchain-cmake'],
            },

            {
                'name': 'build-cmake-release-all',
                'image': image,
                'volumes': [volume_build('cmake')],
                'commands': [
                    command_cc(cc),
                    'cd build/cmake-Release',
                    'cmake --build . -j$(nproc)',
                ],
                'depends_on': ['build-cmake-release-tests'],
            },


            #
            # Tests
            #
            {
                'name': 'test-premake',
                'image': image,
                'volumes': [volume_build('premake')],
                'commands': [
                    './build/bin/Linux/Release/xenia-base-tests',
                ],
                'depends_on': ['build-premake-release-tests'],
            },

            {
                'name': 'test-cmake',
                'image': image,
                'volumes': [volume_build('cmake')],
                'commands': [
                    './build/bin/Linux/Release/xenia-base-tests',
                ],
                'depends_on': ['build-cmake-release-tests'],
            },


            #
            # Stat
            #
            {
                'name': 'stat',
                'image': image,
                'volumes': [
                  volume_build('premake', '/build-premake'),
                  volume_build('cmake', '/build-cmake'),
                ],
                'commands': [
                    '''
                    header() {
                        SEP='============================================================'
                        echo
                        echo $SEP
                        echo $@
                        echo $SEP
                    }

                    for v in premake cmake
                    do
                        for c in Debug Release
                        do
                            header $v $c
                            p=/build-$v/bin/Linux/$c
                            ls -la $p
                            sha256sum $p/*
                        done
                    done
                    '''
                ],
                'depends_on': [
                  'build-premake-debug-all',
                  'build-premake-release-all',
                  'build-cmake-debug-all',
                  'build-cmake-release-all',
                ],
            },
        ],
    }
