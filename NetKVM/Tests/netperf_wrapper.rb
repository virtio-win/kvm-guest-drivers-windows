#!/usr/bin/ruby

#Test constants
NETPERF_CLIENT_WINDOWS = "netperf_x86.exe"  # Name of the test app executable
#NETPERF_CLIENT_WINDOWS = "netclient.exe"  # Name of the test app executable
NETPERF_CLIENT_LINUX = "netperf"  # Name of the test app executable
TEST_CASES = [" -t TCP_STREAM", " -t UDP_STREAM"] # Test cases (UDP|TCP)
HOST_PREFIX = " -H "                    # Server address prefix
TEST_TIME_PREFIX = " -l "            # Test time prefix
ADDITIONAL_PARAMS = " -- "          # Additional test params go after this prefix
PACKET_SIZE_PREFIX = " -m "         # Packet size prefix
PACKET_SIZES = [32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536] # packer sizes


def Log(log_file)
  print yield           # print to screen 
  log_file.print yield # print to log file
end

#Default values for command line parameters
test_loops = 3                            # How many time to execute each test case
test_time = 10                            #  time to run each test
test_server = "10.35.16.57"       # Server address

# First parameter to the script is a server host
if (ARGV[0].to_s.strip() != "")
  test_server = ARGV[0]
end

# Second parameter to the script is a time to run each test
if (ARGV[1].to_s.strip() != "")
  test_time = ARGV[1].to_i
end

#Third parameter how much times to run each test
if (ARGV[2].to_s.strip() != "")
  test_loops = ARGV[2].to_i
end

if(ENV['OS'].to_s.include? "Windows")
  netperf_client = NETPERF_CLIENT_WINDOWS 
else
  netperf_client = NETPERF_CLIENT_LINUX
end

start_time = Time.now

# Format log file name - add date and time
File.open("test_log__#{start_time.strftime("%d_%m_%Y-%H_%M_%S")}.log", "w") { |logFile|

Log(logFile) { "Test started: " + start_time.strftime("%d/%m/%Y %H:%M:%S\n") }

TEST_CASES.each { |test|
PACKET_SIZES.each { |size| 
test_loops.times {

# construct test command line
netperf_command_line = netperf_client +
                                  test +
                                  HOST_PREFIX + test_server +
                                  TEST_TIME_PREFIX + test_time.to_s +
                                  ADDITIONAL_PARAMS +
                                  PACKET_SIZE_PREFIX + size.to_s

# log test command line
Log(logFile) { "\nExecuting: " + netperf_command_line + "\n" }

# If the client runs again Windows server - the server need to be reexecuted after each run
# Therefore the client should wait for the server to restart
sleep(1)

# run the test
result = `#{netperf_command_line}`

# log result
Log(logFile) { result }

} # TEST_LOOPS
} # packet_size
} # TEST_CASE
} # log file
