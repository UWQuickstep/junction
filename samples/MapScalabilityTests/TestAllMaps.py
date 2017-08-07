#!/usr/bin/env python
import os
import subprocess
import sys

# Default CMake command is just 'cmake' but you can override it by setting
# the CMAKE environment variable:
CMAKE = os.getenv('CMAKE', 'cmake')

ALL_MAPS = [
    ('leapfrog', 'junction/extra/impl/MapAdapter_Leapfrog.h', [], ['-i10000', '-c200']),
    ('grampa', 'junction/extra/impl/MapAdapter_Grampa.h', [], ['-i10000', '-c200']),
   ('cuckoo', 'junction/extra/impl/MapAdapter_LibCuckoo.h', ['-DJUNCTION_WITH_LIBCUCKOO=1', '-DTURF_WITH_EXCEPTIONS=1'], ['-c20', '-i5000'])
]
RW_RATIOS = [(['-r0', '-w1'], "0R_100W"), (['-r1', '-w4'], "20R_80W"), (['-r1', '-w1'], "50R_50W"), (['-r4', 'w1'], "80R_20W"), (['-r1', '-w0'], "100R_0W")]
# 0R 100W, 20R 80W, 50R 50W, 80R 20W, 100R 0W
#RW_RATIOS = [(['-r1', '-w1'], "50R_50W")]
IR_RATIOS = [(['-n1', '-d1'], "50I_50R"), (['-n4', '-d1'], "80I_20R"), (['-n1', '-d0'], "100I_0R")]
#IR_RATIOS = [(['-n1', '-d0'], "100I_0R")]

# Scan arguments for path to CMakeLists.txt and args to pass through.
passThroughArgs = []
pathArgs = []
for arg in sys.argv[1:]:
    if arg.startswith('-'):
        passThroughArgs.append(arg)
    else:
        pathArgs.append(arg)
if len(pathArgs) != 1:
    sys.stderr.write('You must provide exactly one path argument.\n')
    exit(1)

listFilePath = os.path.abspath(pathArgs[0])
for suffix, include, cmakeOpts, runtimeOpts in ALL_MAPS:
    success = True
    subdir = 'build-%s' % suffix
    if not os.path.exists(subdir):
        os.mkdir(subdir)
    os.chdir(subdir)
    print('Configuring in %s...' % subdir)
    with open('junction_userconfig.h.in', 'w') as f:
        f.write('#define JUNCTION_IMPL_MAPADAPTER_PATH "%s"\n' % include)
    userConfigCMakePath = os.path.abspath('junction_userconfig.h.in')
    if os.sep != '/':
        userConfigCMakePath = userConfigCMakePath.replace(os.sep, '/')
    if subprocess.call([CMAKE, listFilePath, '-DCMAKE_BUILD_TYPE=RelWithDebInfo', '-DCMAKE_INSTALL_PREFIX=TestAllMapsInstallFolder',
    #if subprocess.call([CMAKE, listFilePath, '-DCMAKE_BUILD_TYPE=Debug', '-DCMAKE_INSTALL_PREFIX=TestAllMapsInstallFolder',
                       '-DJUNCTION_USERCONFIG=%s' % userConfigCMakePath] + passThroughArgs + cmakeOpts) == 0:
        subprocess.check_call([CMAKE, '--build', '.', '--target', 'install', '--config', 'RelWithDebInfo'])
        print('Running in %s...' % subdir)
        # run each rw ratio
        for ratio, rw_suffix in RW_RATIOS:
            for ir_ratio, ir_suffix in IR_RATIOS:
                #print(rw_suffix)
                runpath = [os.path.join('TestAllMapsInstallFolder', 'bin', 'MapScalabilityTests')] + runtimeOpts + ratio + ir_ratio
                #print(runpath)
                results = subprocess.check_output(runpath)
                outfile = "results_" + rw_suffix + "_" + ir_suffix + ".txt"
                with open(outfile, 'wb') as f:
                    f.write(results)
    os.chdir('..')
