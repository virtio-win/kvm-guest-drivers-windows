#!/usr/bin/ruby

if ARGV[0].to_s == ""
  print "error: syntax netperf_log_parser <filename>\n"
  exit
end

print "Parsing: " + ARGV[0] + "\n"

file = File.open(ARGV[0], "r")

if(!file)
  print "error: cannot open " + ARGV[0]
  exit
end

line_count = 0
test_type =  "TCP"
print test_type + "\n"
print "size, throughput\n"

while(line = file.gets)
  #print line
  if(line["Executing"]) #reset line scan
    line_count = 0  
    if(line["UDP"]) #this is UDP test
      if(test_type ==  "TCP" )
        print "UDP" + "\n"
        print "size, throughput\n"
      end
      test_type =  "UDP"      
    else
      test_type = "TCP"
    end
    
    if(size_index = line.index("-m"))
      print line[size_index + 3 ... line.size].strip + ",\t"
    else
      print "default,\t"
    end
    
else
  line_count = line_count + 1
  if(line_count == 7) # results are on the 7th line
    results = line.split;
    if(results != nil)
      if(test_type == "TCP")
        print results[4] +"\n"
      else
        print results[3] +"\n"  
      end
    end
  end
end
  
end

file.close
