#include <iostream>
#include <string>

#include <unistd.h>

#include <ndn-cxx/face.hpp>
#include <ndn-cxx/interest.hpp>
#include <ndn-cxx/data.hpp>

#define MAX_HOSTNAME_LEN 64

int main(int argc, char const *argv[])
{
	char namebuf[MAX_HOSTNAME_LEN];
	gethostname(namebuf, MAX_HOSTNAME_LEN);
	std::string hostname = namebuf;

	std::cout << "Hostname: " << hostname << std::endl;

	if (argc <= 1)
	{
		std::cerr << "Must specify a project name!" << std::endl;
		return 1;
	}
	std::string projname = argv[1];

	ndn::Name name("/topo-prefix/" + hostname + "/midi-ndn/" + projname);
	std::cout << name.toUri() << std::endl;

	return 0;
}
