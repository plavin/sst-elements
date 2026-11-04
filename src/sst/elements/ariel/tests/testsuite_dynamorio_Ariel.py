# -*- coding: utf-8 -*-

from sst_unittest import *
from sst_unittest_support import *
from sst_unittest_parameterized import parameterized
import contextlib
import os
import re


def is_DynamoRIO_compiled() -> bool:
    # Ariel was built with the DynamoRIO frontend and drrun is still present
    if sst_elements_config_include_file_get_value(define="HAVE_DYNAMORIO", type=int, default=0, disable_warning=True) != 1:
        return False
    drrun = sst_elements_config_include_file_get_value(define="DYNAMORIO_EXECUTABLE", type=str, default="", disable_warning=True)
    return drrun != "" and os.access(drrun, os.X_OK)


@contextlib.contextmanager
def scoped_environ(updates):
    """Apply environment variable updates and restore the previous values on exit,
    so DynamoRIO settings do not leak into other Ariel test suites."""
    saved = {key: os.environ.get(key) for key in updates}
    try:
        for key, value in updates.items():
            os.environ[key] = value
        yield
    finally:
        for key, value in saved.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value


def prepend_ld_library_path(*paths):
    current = os.environ.get("LD_LIBRARY_PATH", "")
    return ":".join([p for p in paths if p] + ([current] if current else []))


STAT_LINE_RE = re.compile(r"^\s*(\S+)\.(\w+)\.(\d+)\s*:\s*Accumulator\s*:\s*Sum\.u64\s*=\s*(\d+)")


def parse_ariel_console_stats(filename, component="a0"):
    """Return {(stat_name, core_id): sum} for an Ariel component from console statistic output."""
    stats = {}
    with open(filename, 'r') as fp:
        for line in fp:
            m = STAT_LINE_RE.match(line)
            if m and m.group(1) == component:
                stats[(m.group(2), int(m.group(3)))] = int(m.group(4))
    return stats


# (case name, SDL name, OMP_EXE app, timeout, SDL prints Ariel stats to the console)
stream_cases = [
    ("runstream", "runstream", "ompmybarrier", 240, True),
    ("runstreamSt", "runstreamSt", "ompmybarrier", 240, True),
    ("runstreamNB", "runstreamNB", "ompmybarrier", 240, True),
    ("memHstream", "memHstream", "ompmybarrier", 300, False),
    ("ariel_ivb", "ariel_ivb", "ompmybarrier", 300, False),
    ("ariel_snb", "ariel_snb", "ompmybarrier", 300, False),
    ("ariel_snb_mlm", "ariel_snb_mlm", "stream_mlm", 300, False),
]

testio_cases = [
    ("testio_01", "redirect_out"),
    ("testio_02", "redirect_err"),
    ("testio_03", "redirect_out redirect_err"),
    ("testio_04", "redirect_in"),
    ("testio_05", "redirect_in redirect_out"),
    ("testio_06", "redirect_in redirect_err"),
    ("testio_07", "append_redirect_out"),
    ("testio_08", "append_redirect_err"),
]


class testcase_Ariel_DynamoRIO(SSTTestCase):

    dynamorio_compiled = is_DynamoRIO_compiled()

    def _ariel_dir(self):
        return os.path.abspath("{0}/../".format(self.get_testsuite_dir()))

    def _make(self, cmd, cwd, what):
        build = os_command(cmd, set_cwd=cwd).run()
        self.assertTrue(build.result() == 0,
                        "{0} build failed.\nstdout:\n{1}\nstderr:\n{2}".format(what, build.output(), build.error()))

    def _assert_no_fatal(self, outfile):
        testing_remove_component_warning_from_file(outfile)
        with open(outfile, 'r') as fp:
            text = fp.read()
        self.assertFalse("FATAL" in text, "Output file {0} contains the word 'FATAL'".format(outfile))
        return text

    def _assert_trace_completed(self, outfile):
        text = self._assert_no_fatal(outfile)
        self.assertTrue("PROCESSED AN EXIT EVENT" in text,
                        "Output file {0}: Ariel never received the frontend exit event".format(outfile))
        self.assertTrue("Simulation is complete" in text,
                        "Output file {0}: simulation did not complete".format(outfile))
        return text

    def _assert_stat_positive(self, stats, name, core, outfile):
        value = stats.get((name, core), 0)
        self.assertTrue(value > 0, "Output file {0}: a0.{1}.{2} = {3}, expected > 0".format(outfile, name, core, value))
        return value

    #####

    def _run_dyntrace(self, testname, corecount, payload):
        outdir = self.get_test_output_run_dir()
        dyn_dir = "{0}/tests/testDynamoRIO".format(self._ariel_dir())
        self._make("make", dyn_dir, "dyntrace_threads")

        sdlfile = "{0}/dyntrace_test.py".format(dyn_dir)
        outfile = "{0}/{1}.out".format(outdir, testname)
        errfile = "{0}/{1}.err".format(outdir, testname)
        mpiout = "{0}/{1}.testfile".format(outdir, testname)

        env = {
            "ARIEL_DYNAMORIO_TEST_APP": "{0}/dyntrace_threads".format(dyn_dir),
            "ARIEL_DYNAMORIO_TEST_CORES": str(corecount),
            "ARIEL_DYNAMORIO_TEST_PAYLOAD": "1" if payload else "0",
        }
        with scoped_environ(env):
            self.run_sst(sdlfile, outfile, errfile, set_cwd=dyn_dir,
                         mpi_out_files=mpiout, timeout_sec=180)

        text = self._assert_trace_completed(outfile)
        self.assertTrue("dyntrace_threads workers={0}".format(corecount - 1) in text and " PASS" in text,
                        "Output file {0}: traced application did not report PASS".format(outfile))
        self.assertFalse("SSTARIEL-DR: dropping thread" in text,
                         "Output file {0}: a thread was not mapped to an Ariel core".format(outfile))

        # Every thread (main on core 0, one worker per remaining core) must produce
        # memory traffic and no-ops; workers run SP and DP FP loops.
        stats = parse_ariel_console_stats(outfile)
        for core in range(corecount):
            for name in ["instruction_count", "read_requests", "write_requests", "no_ops"]:
                self._assert_stat_positive(stats, name, core, outfile)
            if core > 0:
                for name in ["fp_sp_ins", "fp_dp_ins", "fp_sp_ops", "fp_dp_ops"]:
                    self._assert_stat_positive(stats, name, core, outfile)

    @unittest.skipIf(not dynamorio_compiled, "Ariel: DynamoRIO tests require Ariel to be configured --with-dynamorio.")
    def test_Ariel_dynamorio_threads(self):
        self._run_dyntrace("test_Ariel_dynamorio_threads", corecount=4, payload=False)

    @unittest.skipIf(not dynamorio_compiled, "Ariel: DynamoRIO tests require Ariel to be configured --with-dynamorio.")
    def test_Ariel_dynamorio_write_payload(self):
        self._run_dyntrace("test_Ariel_dynamorio_write_payload", corecount=2, payload=True)

    #####

    @parameterized.expand(stream_cases)
    @unittest.skipIf(not dynamorio_compiled, "Ariel: DynamoRIO tests require Ariel to be configured --with-dynamorio.")
    def test_Ariel_dynamorio_stream(self, case_name, sdl_name, omp_app, timeout_sec, console_stats):
        test_path = self.get_testsuite_dir()
        outdir = self.get_test_output_run_dir()
        ariel_dir = self._ariel_dir()
        stream_dir = "{0}/frontend/simple/examples/stream".format(ariel_dir)
        omp_dir = "{0}/testopenMP/ompmybarrier".format(test_path)
        api_dir = "{0}/api".format(ariel_dir)

        elements_libdir = sstsimulator_conf_get_value('SST_ELEMENT_LIBRARY', 'SST_ELEMENT_LIBRARY_LIBDIR', str)
        libdir = os.path.join(elements_libdir, '..')

        self._make("make LDFLAGS=-L{0}".format(libdir), stream_dir, "stream apps")
        self._make("make", omp_dir, "ompmybarrier")

        frontend_dir = "{0}/frontend/simple".format(ariel_dir)
        malloc_dst = "{0}/malloc.txt".format(frontend_dir)
        if (not os.path.islink(malloc_dst)) and (not os.path.exists(malloc_dst)):
            os_symlink_file(stream_dir, frontend_dir, "malloc.txt")

        omp_exe = "{0}/stream_mlm".format(stream_dir) if omp_app == "stream_mlm" else "{0}/ompmybarrier".format(omp_dir)
        env = {
            "ARIEL_TEST_FRONTEND": "dynamorio",
            "ARIEL_TEST_STREAM_APP": "{0}/stream".format(stream_dir),
            "OMP_EXE": omp_exe,
            "LD_LIBRARY_PATH": prepend_ld_library_path(api_dir, libdir),
        }

        sdlfile = "{0}/{1}.py".format(stream_dir, sdl_name)
        outfile = "{0}/test_Ariel_dynamorio_{1}.out".format(outdir, case_name)
        errfile = "{0}/test_Ariel_dynamorio_{1}.err".format(outdir, case_name)
        mpiout = "{0}/test_Ariel_dynamorio_{1}.testfile".format(outdir, case_name)

        with scoped_environ(env):
            self.run_sst(sdlfile, outfile, errfile, set_cwd=stream_dir,
                         mpi_out_files=mpiout, timeout_sec=timeout_sec)

        self._assert_trace_completed(outfile)
        if console_stats:
            stats = parse_ariel_console_stats(outfile)
            for name in ["read_requests", "write_requests"]:
                self._assert_stat_positive(stats, name, 0, outfile)

    #####

    @parameterized.expand(testio_cases)
    @unittest.skipIf(not dynamorio_compiled, "Ariel: DynamoRIO tests require Ariel to be configured --with-dynamorio.")
    def test_Ariel_dynamorio_testio(self, testcase, args):
        outdir = self.get_test_output_run_dir()
        ariel_dir = self._ariel_dir()
        testio_dir = "{0}/tests/testIO".format(ariel_dir)
        api_dir = "{0}/api".format(ariel_dir)

        self._make("make testio", testio_dir, "testio")

        # Remove output from earlier cases so stale files cannot satisfy the checks
        for stale in ["app_stdout.txt", "app_stderr.txt"]:
            stale_path = "{0}/{1}".format(testio_dir, stale)
            if os.path.exists(stale_path):
                os.remove(stale_path)

        env = {
            "ARIEL_TEST_FRONTEND": "dynamorio",
            "ARIEL_EXE": "{0}/testio".format(testio_dir),
            "LD_LIBRARY_PATH": prepend_ld_library_path(api_dir),
        }

        sdlfile = "{0}/runtestio.py".format(testio_dir)
        outfile = "{0}/test_Ariel_dynamorio_{1}.out".format(outdir, testcase)
        errfile = "{0}/test_Ariel_dynamorio_{1}.err".format(outdir, testcase)
        mpiout = "{0}/test_Ariel_dynamorio_{1}.testfile".format(outdir, testcase)
        other_args = '--model-options="{0}"'.format(args)

        with scoped_environ(env):
            self.run_sst(sdlfile, outfile, errfile, set_cwd=testio_dir,
                         mpi_out_files=mpiout, timeout_sec=120, other_args=other_args)

        self._assert_trace_completed(outfile)
        self._verify_testio_outputs(testio_dir, outfile, errfile, args)

    def _file_contains(self, filename, strings):
        with open(filename, 'r') as file:
            lines = file.readlines()
            for s in strings:
                self.assertTrue(s in lines, "Output {0} does not contain expected line {1}".format(filename, s))

    def _verify_testio_outputs(self, testio_dir, outfile, errfile, args):
        default_testio_stdout = ["STDOUT: This is the default output of the testio program.\n"]
        default_testio_stderr = ["STDERR: This is the default output of the testio program.\n"]
        redirect_testio_stdout = [
            "STDOUT: This file is used to test Ariel IO redirection functionality.\n",
            "STDOUT: This is the second line in the file.\n",
        ]
        redirect_testio_stderr = [
            "STDERR: This file is used to test Ariel IO redirection functionality.\n",
            "STDERR: This is the second line in the file.\n",
        ]

        args_list = args.split()

        if "redirect_out" not in args_list and "append_redirect_out" not in args_list:
            if "redirect_in" in args_list:
                self._file_contains(outfile, redirect_testio_stdout)
            else:
                self._file_contains(outfile, default_testio_stdout)

        if "redirect_err" not in args_list and "append_redirect_err" not in args_list:
            if "redirect_in" in args_list:
                self._file_contains(errfile, redirect_testio_stderr)
            else:
                self._file_contains(errfile, default_testio_stderr)

        gold_stdout = ""
        gold_stderr = ""
        if "redirect_out" in args_list:
            if "redirect_in" in args_list:
                gold_stdout = "{0}/ref/redirect-in_stdout.txt".format(testio_dir)
            else:
                gold_stdout = "{0}/ref/default-in_stdout.txt".format(testio_dir)

        if "redirect_err" in args_list:
            if "redirect_in" in args_list:
                gold_stderr = "{0}/ref/redirect-in_stderr.txt".format(testio_dir)
            else:
                gold_stderr = "{0}/ref/default-in_stderr.txt".format(testio_dir)

        if "append_redirect_out" in args_list:
            gold_stdout = "{0}/ref/default-in_appendstdout.txt".format(testio_dir)

        if "append_redirect_err" in args_list:
            gold_stderr = "{0}/ref/default-in_appendstderr.txt".format(testio_dir)

        if gold_stdout:
            with open(gold_stdout, 'r') as gold:
                self._file_contains("{0}/app_stdout.txt".format(testio_dir), gold.readlines())

        if gold_stderr:
            with open(gold_stderr, 'r') as gold:
                self._file_contains("{0}/app_stderr.txt".format(testio_dir), gold.readlines())
