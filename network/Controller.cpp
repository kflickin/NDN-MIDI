#include <ndn-cxx/face.hpp>
#include <ndn-cxx/interest.hpp>
#include <ndn-cxx/data.hpp>
#include <ndn-cxx/security/key-chain.hpp>

#include <iostream>
#include <string>
#include <map>

class Controller
{
public:
	Controller(ndn::Face& face, const std::string& remoteName, const std::string& projname)
		: m_face(face)
		, m_baseName(ndn::Name("/topo-prefix/" + remoteName + "/midi-ndn/" + projname))
		, m_remoteName(remoteName)
	{
		m_connGood = false;
		m_face.setInterestFilter(m_baseName,
								 std::bind(&Controller::onInterest, this, _2),
								 std::bind([] {
									std::cerr << "Prefix registered" << std::endl;
								 }),
								 [] (const ndn::Name& prefix, const std::string& reason) {
									std::cerr << "Failed to register prefix: " << reason << std::endl;
								 });

		requestNext();
	}

private:
	void
	onInterest(const ndn::Interest& interest)
	{
		if (!m_connGood)
		{
			// ya tryin' to troll me?
			std::cerr << "Connection not set up yet!?" << std::endl;
			return;
		}

		/*** send out data of keyboard input ***/

		// create data packet with the same name as interest
		std::shared_ptr<ndn::Data> data = std::make_shared<ndn::Data>(interest.getName());

		// prepare and assign content of the data packet
		std::string content = "xyz";
		data->setContent(reinterpret_cast<const uint8_t*>(content.c_str()), content.size());

		// set metainfo parameters
		data->setFreshnessPeriod(ndn::time::seconds(10));

		// sign data packet
		m_keyChain.sign(*data);

		// make data packet available for fetching
		m_face.put(*data);

		// debug
		std::cout << "Sending data: " << content << std::endl;
	}

	void
	onData(const ndn::Data& data)
	{
		// maybe some checking here...
		// But currently, handshake doesn't contain any data
		m_connGood = true;

		// debug
		std::cout << "Received data: "
				  << std::string(reinterpret_cast<const char*>(data.getContent().value()),
															   data.getContent().value_size())
				  << std::endl;
	}

	void
	onTimeout(const ndn::Interest& interest)
	{
		// re-express interest
		std::cerr << "Timeout for: " << interest << std::endl;
		m_face.expressInterest(interest.getName(),
								std::bind(&Controller::onData, this, _2),
								std::bind(&Controller::onTimeout, this, _1));
	}

private:
	void
	requestNext()
	{
		m_face.expressInterest(ndn::Interest(m_baseName).setMustBeFresh(true),
								std::bind(&Controller::onData, this, _2),
								std::bind(&Controller::onTimeout, this, _1));

		// debug
		std::cerr << "Sending out interest: " << m_baseName << std::endl;
	}

private:
	ndn::Face& m_face;
	ndn::KeyChain m_keyChain;
	ndn::Name m_baseName;

	bool m_connGood;
	std::string m_remoteName;
};

int main(int argc, char *argv[])
{
	// getting remote name and project name
	std::string remoteName = "";
	std::string projname = "tmp-proj";
	
	if (argc > 1)
	{
		remoteName = argv[1];
	}
	else
	{
		std::cerr << "Must specify a remote name!" << std::endl;
		return 1;
	}

	if (argc > 2)
	{
		projname = argv[2];
	}

	try {
		// create Face instance
		ndn::Face face;

		// create server instance
		Controller controller(face, remoteName, projname);

		// start processing loop (it will block forever)
		face.processEvents();
	}
	catch (const std::exception& e) {
		std::cerr << "ERROR: " << e.what() << std::endl;
	}

	return 0;
}
