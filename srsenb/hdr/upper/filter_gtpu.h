#include <map>
#include <vector>
#include <netinet/in.h>
#include "srslte/common/threads.h"

#ifndef FILTER_GTPU_H
#define FILTER_GTPU_H

namespace srsenb {

class filter_gtpu
{
	public:

		void init(char* path_file);
		bool match_rule(uint8_t* pkt, uint32_t pkt_len);

	private:

		bool parse_file();

		pthread_mutex_t mutex;
		char path[256];
		std::vector<struct in_addr> addrs;

};

class rrt:
	public thread
{
	private:
		bool running;
		bool run_enable;

};

} //namespace srsenb

#endif