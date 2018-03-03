#!/usr/bin/env python3
# Copyright 2017 Justin Moore. All Rights Reserved.
import argparse
import glob
import itertools
from multiprocessing.dummy import Pool
import os
import time
import subprocess
import sys


def parallel_arg_type(value):
    ivalue = int(value)
    if ivalue < 1:
        raise argparse.ArgumentError(
            "%s is an invalid number of processes (>= 1)" % ivalue)

    return ivalue


def file_type(value):
    if not os.path.isfile(value):
        raise argparse.ArgumentError("%s is an invalid filename" % value)

    return value


def run_dumper(exe_path, input_name):
    start_time = time.perf_counter()

    p = subprocess.Popen([
        exe_path,
        input_name,
        '--log_file=stdout',
    ], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    p.wait()
    elapsed_time = time.perf_counter() - start_time
    print("%.3f seconds, code %2d (%s)" %
            (elapsed_time, p.returncode, input_name))


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument(
        'exepath', help='path to the trace dump executable', type=file_type)
    parser.add_argument('files', nargs='*')
    parser.add_argument('-j', type=parallel_arg_type,
                        help='number of parallel processes', default=1)
    args = parser.parse_args(argv[1:])
    exepath = args.exepath

    files = []
    for wildcard in args.files:
        files.extend(glob.glob(wildcard))

    if len(files) == 0:
        print("No input files found!")
        return 1

    # Test the executable first.
    valid = False
    try:
        p = subprocess.Popen([
            exepath,
            '--log_file=stdout',
        ], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        p.wait()
        if p.returncode == 5:
            # code 5 = invalid trace file / trace file unspecified
            valid = True
    except OSError:
        pass

    if not valid:
        print("Trace executable invalid!")
        return 1

    print("Processing...")
    pool = Pool(args.j)

    start_time = time.perf_counter()
    results = pool.starmap(run_dumper, zip(itertools.repeat(exepath), files))
    pool.close()
    pool.join()

    elapsed_time = time.perf_counter() - start_time
    print("entire runtime took %.3f seconds" % elapsed_time)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
