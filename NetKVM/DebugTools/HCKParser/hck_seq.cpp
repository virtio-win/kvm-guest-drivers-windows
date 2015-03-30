#include <iostream>
#include <vector>
#include <algorithm>
#include <pcap.h>
#include <inttypes.h>

using namespace std;

char errbuf[PCAP_ERRBUF_SIZE]; 

typedef vector<u_char> byte_array;
u_char ndis_bytes[] =  {'N', 'D', 'I', 'S'};

uint16_t sessions[256];

int main(int argc, char **argv)
{
  pcap_t *handle;
  const u_char *packet;
  struct pcap_pkthdr header;
  unsigned long counter = 0;

  if (argc != 2) {
    cerr << "Unexpected args" << endl <<"Usage: hck_seq <pcap file>" << endl;
    return -1;
  }
  
  handle = pcap_open_offline(argv[1], errbuf);
  if (handle == NULL) {
    cerr << argv[1] << ":" << errbuf << endl;
    return -1;
  }
  while (packet = pcap_next(handle,&header)) {
    byte_array content(packet, packet + header.caplen);

    byte_array::iterator it = search(content.begin(), content.end(), ndis_bytes, ndis_bytes + 4);

    if (it != content.end() && (it-content.begin() + 22) < content.size()) {
      advance(it, 12);
      uint8_t b1 = *it++;
      uint8_t b2 = *it;
      uint16_t sequence_number = b2 << 8 | b1;
      
      advance(it, 9);
      
      b1 = *it++;
      b2 = *it;
      uint16_t session_number = b2 << 8 | b1;
      
      if (sequence_number != sessions[session_number]) {
	if (sequence_number == 0) {
	  cout << counter << ": "<< "Session " << session_number << " reset" << endl;
	} else {
	  cout << counter << ": " << "Sequence mismatch for session " << session_number <<
	    ": expected " << sessions[session_number] << " got " <<
	    sequence_number << endl;
	}
      }

      sessions[session_number] = sequence_number + 1;
    }
    counter ++;
  }

  pcap_close(handle);
  return 0;
}
    
