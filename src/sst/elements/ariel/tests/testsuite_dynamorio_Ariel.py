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
