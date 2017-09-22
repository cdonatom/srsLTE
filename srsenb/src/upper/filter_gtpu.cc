#include "upper/filter_gtpu.h"
#include <iostream>
#include <fstream>
#include <arpa/inet.h>
#include <string.h>

namespace srsenb {

void filter_gtpu::init(char* path_file)
{
	strcpy(path, path_file);
}

bool filter_gtpu::parse_file()
{
	std::string line;
	std::ifstream myfile (path);
	struct in_addr tmp_addr;
	bool found_ip = false;

	if (myfile.is_open())
	{
		while ( getline (myfile,line) )
		{
			if (line.length() > 1 && inet_pton(AF_INET, (char*) line.c_str(), (void *) &tmp_addr))
			{
				for( unsigned int i = 0; i < addrs.size(); i++)
				{
					if (memcmp((void*)&(addrs.at(i)), (void*) &(tmp_addr), sizeof(struct in_addr)) == 0)
					{
						found_ip = true;
						break;
					}
				}

				if (!found_ip)
				{
					std::cout<<"Added to the list: "<<line<<std::endl;
					addrs.push_back(tmp_addr);
				}

				found_ip = false;
			}
		}
		myfile.close();
		return true;
	}

	addrs.clear();

	return false;
}

bool filter_gtpu::match_rule (uint8_t* pkt, uint32_t pkt_len)
{
	if ( !parse_file() )
	{
		return false;
	}

	for( unsigned int i = 0; i < addrs.size(); i++)
	{
		if (memcmp((void*)&(addrs.at(i)), (void*)&(pkt[16]), sizeof(struct in_addr)) == 0)
		{
			return true;
		}
	}

	return false;
}


} //namespace srsenb