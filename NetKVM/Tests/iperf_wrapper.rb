#!/usr/bin/ruby

#Test constants
IPERF_CLIENT = "iperf" 				 # Name of the test app executable
HOST_PREFIX = " -c "                 # Server address prefix
TEST_TIME_PREFIX = " -t "            # Test time prefix
PARALLEL_THREADS = " -P "			 # Number of parallel client threads to run
PACKET_SIZE_PREFIX = " -l "          # Packet size prefix
PACKET_SIZES = [32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536] # packet sizes
TEST_CASES = [" "] # Test cases (TCP)
#TEST_CASES = [" -u"] # Test cases (UDP)

def Log(log_file)
  print yield           # print to screen
  log_file.print yield # print to log file
end

#Default values for command line parameters
test_loops = 3                            # How many time to execute each test case
test_time = 10                            # Time to run each test
test_server = "10.0.0.144"       		  # Server address

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

#Fourth parameter how many parallel threads should be used
if (ARGV[3].to_s.strip() != "")
  parallel_treads = ARGV[3].to_i
else
  parallel_treads = 1
end


if(ENV['OS'].to_s.include? "Windows")
  iperf_client = IPERF_CLIENT + ".exe"
else
  iperf_client = IPERF_CLIENT
end

start_time = Time.now

# Format log file name - add date and time
File.open("test_log__#{start_time.strftime("%d_%m_%Y-%H_%M_%S")}.log", "w") { |logFile|
	Log(logFile) { "Test started: " + start_time.strftime("%d/%m/%Y %H:%M:%S\n") }

	TEST_CASES.each { |test|
		PACKET_SIZES.each { |size|
			test_loops.times {
			# construct test command line
			iperf_command_line = iperf_client + " " +
								 HOST_PREFIX + test_server +
								 TEST_TIME_PREFIX + test_time.to_s +
								 PACKET_SIZE_PREFIX + size.to_s + "b" +
								 PARALLEL_THREADS + parallel_treads.to_s +
								 test

			# log test command line
			Log(logFile) { "\nExecuting: " + iperf_command_line + "\n" }

			# run the test
			result = `#{iperf_command_line}`

			# log result
			Log(logFile) { result }

			} # TEST_LOOPS
		} # packet_size
	} # TEST_CASE
} # log file
