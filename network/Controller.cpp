#include <ndn-cxx/face.hpp>
#include <ndn-cxx/interest.hpp>
#include <ndn-cxx/data.hpp>
#include <ndn-cxx/security/key-chain.hpp>

#include <iostream>
#include <string>
#include <map>
#include <chrono>
#include <thread>

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
								 std::bind(&Controller::onSuccess, this, _1),
								 [] (const ndn::Name& prefix, const std::string& reason) {
									std::cerr << "Failed to register prefix: " << reason << std::endl;
								 });
	}

private:
	void
	onSuccess(const ndn::Name& prefix)
	{
		std::cerr << "Prefix registered" << std::endl;
		requestNext();
	}

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
		sendData(interest.getName(), "xyz", 3);
	}

	void
	onData(const ndn::Data& data)
	{
		if (m_connGood)
		{
			std::cerr << "Connection already set up!" << std::endl;
			return;
		}

		// maybe some checking here...
		// But currently, "handshake" doesn't contain any data
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

	// respond interest with data
	void
	sendData(const ndn::Name& dataName, const char *buf, size_t size)
	{
		// create data packet with the same name as interest
		std::shared_ptr<ndn::Data> data = std::make_shared<ndn::Data>(dataName);

		// prepare and assign content of the data packet
		data->setContent(reinterpret_cast<const uint8_t*>(buf), size);

		// set metainfo parameters
		data->setFreshnessPeriod(ndn::time::seconds(10));

		// sign data packet
		m_keyChain.sign(*data);

		// make data packet available for fetching
		m_face.put(*data);

		// debug
		std::cout << "Sending data: " << std::string(buf, size) << std::endl;
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
