#
# Remove trailing white spaces recursively from the file or directory
#
# www.yvtechnologies.com
#

# This function remove trailing white spaces from the single file
#
# Parameters:
# * file - file to clean from trailing whitespaces
def rmwhite_file(strfile)
  print("Cleaning: " + strfile + "\n")
  
  # Read the file
  begin
    lines = File.open(strfile).readlines()
  rescue
    print("Cannot open: " + strfile + "\n")
    return
  end
  
  # Write back
  begin 
    File.open(strfile, "w") do |file|
      lines.each { |line| file.puts(line.rstrip()) }
    end
  rescue => e
    print(e.class.to_s + ": Cannot write back to: " + strfile + "\n")
  end
end

# This function executes clean up of file or directory
#
# Parameters:
# * file - file or directory path
# * patterns - file pattern to search
def rmwhite_file_or_dir(file, patterns)
  if(File.directory?(file))
    current_dir = Dir.pwd()
    Dir.chdir(file)  
#    print ("changed dir to: " + file + "\n")
    rmwhite_directory(file, patterns)
    Dir.chdir(current_dir)
#    print ("changed dir to: " + current_dir + "\n")
  else
    rmwhite_file(file)
  end
end

# Recursive function that walks over all directories and files according to the predefined pattern
#
# Parameters:
# * file - file or directory path
# * patterns - file pattern to search
def rmwhite_directory(dir, patterns)
  printf("Cleaning: " + dir + "\n")
    
  patterns.each do |pattern|
 #   print("Cleaning pattern: " + pattern + "  current dir:" + Dir.pwd() + "\n")
    arr_entries = Dir[pattern]
    arr_entries.each { |entry| rmwhite_file_or_dir(entry, patterns) }
  end
end

if((str_file = ARGV[0].to_s) == "")
  str_file = "."
else
  str_file = ARGV[0].to_s
end

# File patterns that will be cleared from white spaces
FILE_PATTERNS = ["*.txt", "*.rc", "*.c", "*.h", "*.cpp", "*.vbs", "*/"]

if(not File.exist?(str_file))
  print "error: file doesn't exits\n"
  exit -1
end

current_dir = Dir.pwd()
rmwhite_file_or_dir(str_file, FILE_PATTERNS)
Dir.chdir(current_dir)
