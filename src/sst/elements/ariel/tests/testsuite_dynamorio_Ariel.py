# -*- coding: utf-8 -*-

from sst_unittest import *
from sst_unittest_support import *
import os


def is_DynamoRIO_loaded() -> bool:
    dr_path = os.environ.get('DYNAMORIO_ROOT')
    if dr_path is None:
        return False
    return os.path.isfile(os.path.join(dr_path, "bin64", "drrun"))


class testcase_Ariel_DynamoRIO(SSTTestCase):

    dynamorio_loaded = is_DynamoRIO_loaded()
    pin_equivalent_cases = [
        ("runstream", "runstream", "", 240),
        ("runstreamSt", "runstreamSt", "", 240),
        ("runstreamNB", "runstreamNB", "", 240),
        ("memHstream", "memHstream", "", 300),
        ("ariel_ivb", "ariel_ivb", "", 300),
        ("ariel_snb", "ariel_snb", "", 300),
        ("ariel_snb_mlm", "ariel_snb_mlm", "stream_mlm", 300),
    ]

    @unittest.skipIf(not dynamorio_loaded, "Ariel: DynamoRIO tests require DYNAMORIO_ROOT with bin64/drrun.")
    def test_Ariel_dynamorio_threads(self):
        test_path = self.get_testsuite_dir()
        outdir = self.get_test_output_run_dir()

        ariel_dir = os.path.abspath("{0}/../".format(test_path))
        dyn_dir = "{0}/tests/testDynamoRIO".format(ariel_dir)

        build = os_command("make", set_cwd=dyn_dir).run()
        self.assertTrue(build.result() == 0,
                        "dyntrace_threads build failed.\nstdout:\n{0}\nstderr:\n{1}".format(build.output(), build.error()))

        os.environ["ARIEL_DYNAMORIO_TEST_APP"] = "{0}/dyntrace_threads".format(dyn_dir)
        os.environ["ARIEL_DYNAMORIO_TEST_CORES"] = "2"

        sdlfile = "{0}/dyntrace_test.py".format(dyn_dir)
        outfile = "{0}/test_Ariel_dynamorio_threads.out".format(outdir)
        errfile = "{0}/test_Ariel_dynamorio_threads.err".format(outdir)
        mpiout = "{0}/test_Ariel_dynamorio_threads.testfile".format(outdir)

        self.run_sst(sdlfile, outfile, errfile, set_cwd=dyn_dir,
                     mpi_out_files=mpiout, timeout_sec=120)

        testing_remove_component_warning_from_file(outfile)

        cmd = 'grep "FATAL" {0} '.format(outfile)
        grep_result = os.system(cmd) != 0
        self.assertTrue(grep_result, "Output file {0} contains the word 'FATAL'".format(outfile))

    @unittest.skipIf(not dynamorio_loaded, "Ariel: DynamoRIO tests require DYNAMORIO_ROOT with bin64/drrun.")
    def test_Ariel_pin_equivalent_stream_suite(self):
        test_path = self.get_testsuite_dir()
        outdir = self.get_test_output_run_dir()

        ariel_dir = os.path.abspath("{0}/../".format(test_path))
        stream_dir = "{0}/frontend/simple/examples/stream".format(ariel_dir)
        omp_dir = "{0}/testopenMP/ompmybarrier".format(test_path)
        api_dir = "{0}/api".format(ariel_dir)

        elements_libdir = sstsimulator_conf_get_value('SST_ELEMENT_LIBRARY', 'SST_ELEMENT_LIBRARY_LIBDIR', str)
        libdir = os.path.join(elements_libdir, '..')

        stream_make = os_command("make LDFLAGS=-L{0}".format(libdir), set_cwd=stream_dir).run()
        self.assertTrue(stream_make.result() == 0,
                        "stream apps build failed.\nstdout:\n{0}\nstderr:\n{1}".format(
                            stream_make.output(), stream_make.error()))

        omp_make = os_command("make", set_cwd=omp_dir).run()
        self.assertTrue(omp_make.result() == 0,
                        "ompmybarrier build failed.\nstdout:\n{0}\nstderr:\n{1}".format(
                            omp_make.output(), omp_make.error()))

        stream_app = "{0}/stream".format(stream_dir)
        omp_app = "{0}/ompmybarrier".format(omp_dir)
        stream_mlm_app = "{0}/stream_mlm".format(stream_dir)

        os.environ["ARIEL_TEST_FRONTEND"] = "dynamorio"
        os.environ["ARIEL_TEST_STREAM_APP"] = stream_app
        os.environ["ARIEL_DYNAMORIO_TEST_CORES"] = "8"

        libpath = os.environ.get("LD_LIBRARY_PATH", "")
        if libpath:
            os.environ["LD_LIBRARY_PATH"] = api_dir + ":" + libdir + ":" + libpath
        else:
            os.environ["LD_LIBRARY_PATH"] = api_dir + ":" + libdir

        frontend_dir = "{0}/frontend/simple".format(ariel_dir)
        malloc_dst = "{0}/malloc.txt".format(frontend_dir)
        if (not os.path.islink(malloc_dst)) and (not os.path.exists(malloc_dst)):
            os_symlink_file(stream_dir, frontend_dir, "malloc.txt")

        for (case_name, sdl_name, app_mode, timeout_sec) in self.pin_equivalent_cases:
            if app_mode == "stream_mlm":
                os.environ["OMP_EXE"] = stream_mlm_app
            else:
                os.environ["OMP_EXE"] = omp_app

            sdlfile = "{0}/{1}.py".format(stream_dir, sdl_name)
            outfile = "{0}/test_Ariel_dynamorio_{1}.out".format(outdir, case_name)
            errfile = "{0}/test_Ariel_dynamorio_{1}.err".format(outdir, case_name)
            mpiout = "{0}/test_Ariel_dynamorio_{1}.testfile".format(outdir, case_name)

            self.run_sst(sdlfile, outfile, errfile, set_cwd=stream_dir,
                         mpi_out_files=mpiout, timeout_sec=timeout_sec)

            testing_remove_component_warning_from_file(outfile)

            cmd = 'grep "FATAL" {0} '.format(outfile)
            grep_result = os.system(cmd) != 0
            self.assertTrue(grep_result,
                            "Output file {0} contains the word 'FATAL'".format(outfile))

    @unittest.skipIf(not dynamorio_loaded, "Ariel: DynamoRIO tests require DYNAMORIO_ROOT with bin64/drrun.")
    def test_Ariel_testio_equivalent_suite(self):
        test_path = self.get_testsuite_dir()
        outdir = self.get_test_output_run_dir()

        ariel_dir = os.path.abspath("{0}/../".format(test_path))
        testio_dir = "{0}/tests/testIO".format(ariel_dir)
        api_dir = "{0}/api".format(ariel_dir)

        build = os_command("make testio", set_cwd=testio_dir).run()
        self.assertTrue(build.result() == 0,
                        "testio build failed.\nstdout:\n{0}\nstderr:\n{1}".format(
                            build.output(), build.error()))

        os.environ["ARIEL_TEST_FRONTEND"] = "dynamorio"
        os.environ["ARIEL_EXE"] = "{0}/testio".format(testio_dir)
        os.environ["ARIEL_DYNAMORIO_TEST_CORES"] = "4"

        libpath = os.environ.get("LD_LIBRARY_PATH", "")
        if libpath:
            os.environ["LD_LIBRARY_PATH"] = api_dir + ":" + libpath
        else:
            os.environ["LD_LIBRARY_PATH"] = api_dir

        cases = [
            "redirect_out",
            "redirect_err",
            "redirect_out redirect_err",
            "redirect_in",
            "redirect_in redirect_out",
            "redirect_in redirect_err",
            "append_redirect_out",
            "append_redirect_err",
        ]

        for idx, args in enumerate(cases):
            testcase = "testio_{0:02d}".format(idx + 1)
            sdlfile = "{0}/runtestio.py".format(testio_dir)
            outfile = "{0}/test_Ariel_dynamorio_{1}.out".format(outdir, testcase)
            errfile = "{0}/test_Ariel_dynamorio_{1}.err".format(outdir, testcase)
            mpiout = "{0}/test_Ariel_dynamorio_{1}.testfile".format(outdir, testcase)
            other_args = '--model-options="{0}"'.format(args)

            self.run_sst(sdlfile, outfile, errfile, set_cwd=testio_dir,
                         mpi_out_files=mpiout, timeout_sec=120, other_args=other_args)

            testing_remove_component_warning_from_file(outfile)

            cmd = 'grep "FATAL" {0} '.format(outfile)
            grep_result = os.system(cmd) != 0
            self.assertTrue(grep_result,
                            "Output file {0} contains the word 'FATAL'".format(outfile))
