package maxscale.java;

import java.io.File;

/**
 * MaxScale configuration class
 *
 * Configures MaxScale for testing
 */
public class MaxScaleConfiguration {
    
    private static final String TEST_DIR = "/home/turenko/1.4/MaxScale/maxscale-system-test";
    private static final String CONFIG_COMMAND = TEST_DIR + "/configure_maxscale.sh";
    private static final int DEFAULT_SLEEP = 10;
    public MaxScaleConfiguration(String test) throws Exception
    {
        ProcessBuilder pb = new ProcessBuilder(CONFIG_COMMAND ,  test);
        pb.inheritIO();
        pb.directory(new File(TEST_DIR));
        pb.environment().put("test_dir", TEST_DIR);
        Process process = pb.start();
        process.waitFor();
        System.out.println("Sleeping for " + DEFAULT_SLEEP + " seconds.");
        Thread.sleep(DEFAULT_SLEEP * 1000);
    }
}
