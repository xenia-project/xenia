def main(ctx):
    return [
        pipeline_lint(),
        pipeline_linux_desktop('x86_64-linux-clang', image_linux_x86_64(), 'amd64', 'clang', True),
        pipeline_linux_desktop('x86_64-linux-gcc', image_linux_x86_64(), 'amd64', 'gcc', False), # GCC release linking is really slow
        pipeline_android('x86_64-android', image_linux_x86_64(), 'amd64', 'Android-x86_64'),
        pipeline_android('aarch64-android', image_linux_x86_64(), 'amd64', 'Android-ARM64'),
    ]

def image_linux_x86_64():
    return 'xeniaproject/buildenv:2022-07-15'

def volume_build(toolchain, path='/drone/src/build'):
    return {
        'name': 'build-' + toolchain,
        'path': path,
    }

def command_cc(cc):
    # set CC, CXX, ...
    return 'export $(cat /{}.env | sed \'s/#.*//g\' | xargs)'.format(cc)

def command_ndk_build(platform, configuration, target):
    return '$ANDROID_NDK_ROOT/build/ndk-build NDK_PROJECT_PATH:=./bin/{configuration} NDK_APPLICATION_MK:=./xenia.Application.mk PREMAKE_ANDROIDNDK_PLATFORMS:={platform} PREMAKE_ANDROIDNDK_CONFIGURATIONS:={configuration} -j$(nproc) {target}'.format(platform=platform, configuration=configuration, target=target)

def xenia_base_tests_filters():
    # https://github.com/xenia-project/xenia/issues/2036
    return 'exclude:"Wait on Timer" exclude:"Wait on Multiple Timers" exclude:"HighResolutionTimer"'

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

def pipeline_linux_desktop(name, image, arch, cc, build_release_all):
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
                      cmake -S . -B cmake-$c -G Ninja -DCMAKE_BUILD_TYPE=$c
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
                'name': 'build-premake-debug-tests',
                'image': image,
                'volumes': [volume_build('premake')],
                'commands': [
                    command_cc(cc),
                    './xenia-build build --no_premake -j$(nproc) --config=Debug --target=xenia-base-tests',
                ],
                'depends_on': ['toolchain-premake'],
            },
            {
                'name': 'build-premake-debug-all',
                'image': image,
                'volumes': [volume_build('premake')],
                'commands': [
                    command_cc(cc),
                    './xenia-build build --no_premake -j$(nproc) --config=Debug',
                ],
                'depends_on': ['build-premake-debug-tests'],
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
        ] + ([
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
        ] if build_release_all else []) + [

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
        ] + ([
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
        ] if build_release_all else []) + [


            #
            # Tests
            #

            {
                'name': 'test-premake-debug-valgrind',
                'image': image,
                'volumes': [volume_build('premake')],
                'commands': [
                    'valgrind --error-exitcode=99 ./build/bin/Linux/Debug/xenia-base-tests --durations yes ' + xenia_base_tests_filters(),
                ],
                'depends_on': ['build-premake-debug-tests'],
            },

            {
                'name': 'test-premake-release',
                'image': image,
                'volumes': [volume_build('premake')],
                'commands': [
                    './build/bin/Linux/Release/xenia-base-tests --success --durations yes ' + xenia_base_tests_filters(),
                ],
                'depends_on': ['build-premake-release-tests'],
            },

            {
                'name': 'test-cmake-release',
                'image': image,
                'volumes': [volume_build('cmake')],
                'commands': [
                    './build/bin/Linux/Release/xenia-base-tests --success --durations yes ' + xenia_base_tests_filters(),
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
                    'build-cmake-debug-all',
                ] + ([
                    'build-premake-release-all',
                    'build-cmake-release-all',
                ] if build_release_all else [
                    'build-premake-release-tests',
                    'build-cmake-release-tests',
                ]),
            },
        ],
    }


def pipeline_android(name, image, arch, platform):
    return {
        'kind': 'pipeline',
        'type': 'docker',
        'name': name,
        'platform': {
            'os': 'linux',
            'arch': arch,
        },

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
            # Build premake and generate NDK makefiles
            #

            # NDK Makefiles
            {
                'name': 'toolchain',
                'image': image,
                'commands': [
                    'c++ --version',
                    'python3 --version',
                    './xenia-build premake --target_os android',
              ],
              'depends_on': ['clone-submodules'],
            },


            #
            # Building
            #
            {
                'name': 'build-debug',
                'image': image,
                'commands': [
                    'cd build',
                    command_ndk_build(platform, 'Debug', 'all'),
                ],
                'depends_on': ['toolchain'],
            },

            {
                'name': 'build-release',
                'image': image,
                'commands': [
                    'cd build',
                    command_ndk_build(platform, 'Release', 'all'),
                ],
                'depends_on': ['toolchain'],
            },


            #
            # Stat
            #
            {
                'name': 'stat',
                'image': image,
                'commands': [
                    '''
                    header() {
                        SEP='============================================================'
                        echo
                        echo $SEP
                        echo $@
                        echo $SEP
                    }

                    for c in Debug Release
                    do
                        header $c
                        p=build/bin/$c/obj/local/*
                        ls -la $p
                        sha256sum $p/* || true
                    done
                    '''
                ],
                'depends_on': [
                  'build-debug',
                  'build-release',
                ],
            },
        ],
    }
