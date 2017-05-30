/********************************

Plays back MIDI message received
Acts as a producer and consumer at the same time.

This part is a.k.a. "NDN Module"

Note:
For received interest packet:
					-3		-2 		-1
(topology-prefix)/<user>/midi-ndn/<proj_name>
if we add in device then it's -4

For received data packet / sent interest packet:
user position = user position in received interest - 1 = -4

********************************/

#include <ndn-cxx/face.hpp>
#include <ndn-cxx/interest.hpp>
#include <ndn-cxx/data.hpp>
#include <ndn-cxx/security/key-chain.hpp>

#include <iostream>
#include <string>
#include <map>

#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "RtMidi.h"

// Platform-dependent sleep routines.
#if defined(__WINDOWS_MM__)
  #include <windows.h>
  #define SLEEP( milliseconds ) Sleep( (DWORD) milliseconds ) 
#else // Unix variants
  #include <unistd.h>
  #define SLEEP( milliseconds ) usleep( (unsigned long) (milliseconds * 1000.0) )
#endif

#define PREWARM_AMOUNT 5

struct MIDIControlBlock
{
	int minSeqNo;
	int maxSeqNo;
};

class PlaybackModule
{
public:
	PlaybackModule(ndn::Face& face, const std::string& hostname, const std::string& projname)
		: m_face(face)
		, m_baseName(ndn::Name("/topo-prefix/" + hostname + "/midi-ndn/" + projname))
	{
		m_face.setInterestFilter(m_baseName,
								 std::bind(&PlaybackModule::onInterest, this, _2),
								 std::bind([] {
									std::cerr << "Prefix registered" << std::endl;
								 }),
								 [] (const ndn::Name& prefix, const std::string& reason) {
									std::cerr << "Failed to register prefix: " << reason << std::endl;
								 });
	}

private:
	void
	onInterest(const ndn::Interest& interest)
	{
		/*** check if connection already exist ***/

		// placeholder: maybe device name in the future
		std::string remoteName = interest.getName().get(-3).toUri();;

		if (m_lookup.count(remoteName) > 0)
		{
			std::cerr << "connection request dropped: " << interest << std::endl;
			return;
		}

		/*** accept new connection ***/

		m_lookup[remoteName] = {0,0};

		std::cerr << "connection accepted: " << interest << std::endl;

		/*** respond to connection request ***/

		// create data packet with the same name as interest
		std::shared_ptr<ndn::Data> data = std::make_shared<ndn::Data>(interest.getName());

		// prepare and assign content of the data packet
		std::string content = "ACCEPTED";
		data->setContent(reinterpret_cast<const uint8_t*>(content.c_str()), content.size());

		// set metainfo parameters
		data->setFreshnessPeriod(ndn::time::seconds(1)); 

		// sign data packet
		m_keyChain.sign(*data);

		// make data packet available for fetching
		m_face.put(*data);

		/*** start sending out interest for next seq ***/

		// prewarm the channel with some interests
		for (int i = 0; i < PREWARM_AMOUNT; ++i)
		{
			requestNext(remoteName);
		}
	}

	void
	onData(const ndn::Data& data)
	{
		int seqNo = data.getName().get(-1).toSequenceNumber();
		// placeholder: maybe device name in the future?
		std::string remoteName = data.getName().get(-4).toUri();

		// CHECKPOINT 1: connection actually exist
		if (m_lookup.count(remoteName) == 0)
		{
			// the connection doesn't exist!!
			std::cerr << "Connection for remote user \""
					  << remoteName << "\" doesn't exist!"
					  << std::endl;
			return;
		}

		// CHECKPOINT 2: sequence number agrees
		if (m_lookup[remoteName].minSeqNo >= m_lookup[remoteName].maxSeqNo)
		{
			// behavior yet to be defined......
			std::cerr << "Corrupted block: minSeqNo >= maxSeqNo"
					  << std::endl;
		}
		if (m_lookup[remoteName].minSeqNo != seqNo)
		{
			// behavior yet to be defined
			std::cerr << "Sequence number out of order --> "
					  << "sent: " << m_lookup[remoteName].minSeqNo
					  << "  rcvd: " << seqNo
					  << std::endl;
		}

		// CHECKPOINT 3: data is in correct format
		char buffer[30];
		int dataSize = data.getContent().value_size();
		// if (data.getContent().value_size() != 3)
		// {
		// 	// incorrect data format
		// 	// behavior yet to be defined
		// 	std::cerr << "Incorrect data format: len = "
		// 			  << data.getContent().value_size()
		// 			  << " (expected 3)"
		// 			  << std::endl;
		// }

		/**
		 * Starting here all check points are passed
		 * copy data and increment sequence number
		 */
		memcpy(buffer, data.getContent().value(), dataSize);
		m_lookup[remoteName].minSeqNo++;

		// debug
		
		std::cout << "Received data: \n\t";
		for (int j = 0; j < dataSize/3; ++j){
		for (int i = 0; i < 3; ++i)
		{
			std::cout << " " << (int)buffer[i+(j*3)];
			// for midi message
			this->message[i] = (unsigned char)buffer[i+(j*3)];

		}
		std::cout << "\n\t";


		// Playback of midi
		if (this->message.size()==3){
			this->midiout->sendMessage(&this->message);
		}

		// currently using a special message to shutdown... 
		if (buffer[0] == 0 && buffer[1] == 0 && buffer[2] == 0)
		{
			std::cerr << "Deleting table entry of: " << remoteName << std::endl;
			m_lookup.erase(remoteName);
			return;
		}
	}
		//std::cout << std::endl;
		std::cout << "\t[seq range = (" << m_lookup[remoteName].minSeqNo
			<< "," << m_lookup[remoteName].maxSeqNo << ")]" << std::endl;

		/**
		 * TODO: process data
		 */
		
		requestNext(remoteName);
	}

	
	void
	onTimeout(const ndn::Interest& interest)
	{
		// re-express interest
		std::cerr << "Timeout for: " << interest << std::endl;
		m_face.expressInterest(interest.getName(),
								std::bind(&PlaybackModule::onData, this, _2),
								std::bind(&PlaybackModule::onTimeout, this, _1));
	}
	

private:
	void
	requestNext(std::string remoteName)
	{
		if (m_lookup.count(remoteName) == 0)
		{
			// weird, maybe connection is closed or something
			// or people trying to be malicious (LOL)
			std::cerr << "Attempted to request from non-existent remote: "
					  << remoteName
					  << " - DROPPED"
					  << std::endl;
			return;
		}

		int nextSeqNo = m_lookup[remoteName].maxSeqNo;
		

		/** Send interest without specifying interest lifetime 

		ndn::Name nextName = ndn::Name(m_baseName).appendSequenceNumber(nextSeqNo);
		m_face.expressInterest(ndn::Interest(nextName).setMustBeFresh(true),
								std::bind(&PlaybackModule::onData, this, _2),
								std::bind(&PlaybackModule::onTimeout, this, _1));
		**/

		// Send interest with long interest lifetime
		ndn::Name nextName = ndn::Name(m_baseName).appendSequenceNumber(nextSeqNo);
		ndn::Interest nextNameInterest = ndn::Interest(nextName);
		nextNameInterest.setInterestLifetime(ndn::time::seconds(10));
		nextNameInterest.setMustBeFresh(true);
		m_face.expressInterest(nextNameInterest,
								std::bind(&PlaybackModule::onData, this, _2),
								//std::bind(&PlaybackModule::onNack, this, _1, _2),
								std::bind(&PlaybackModule::onTimeout, this, _1));

		m_lookup[remoteName].maxSeqNo++;

		// debug
		std::cerr << "Sending out interest: " << nextName << std::endl;
	}

private:
	ndn::Face& m_face;
	ndn::KeyChain m_keyChain;
	ndn::Name m_baseName;

	// maps foreign hostname (remoteName) to a control block
	std::map<std::string, MIDIControlBlock> m_lookup;

public:
	RtMidiOut *midiout;
	std::vector<unsigned char> message;
};

bool chooseMidiPort( RtMidiOut *rtmidi );

int main(int argc, char *argv[])
{
	// get unique user name
	char namebuf[64];
	gethostname(namebuf, 64);
	std::string hostname = namebuf;

	

	// get project name: default is tmp-proj
	std::string projname = "tmp-proj";
	if (argc > 1)
	{
		projname = argv[1];
	}

	try {
		// create Face instance
		ndn::Face face;

		// create server instance
		PlaybackModule ndnModule(face, hostname, projname);
		
		// RtMidiOut setup
		ndnModule.midiout = new RtMidiOut();
		chooseMidiPort( ndnModule.midiout );

		ndnModule.message.push_back( 192 );
    	ndnModule.message.push_back( 5 );
    	ndnModule.midiout->sendMessage( &ndnModule.message );

		SLEEP( 500 );

  		ndnModule.message[0] = 0xF1;
  		ndnModule.message[1] = 60;
  		ndnModule.midiout->sendMessage( &ndnModule.message );

  		// Control Change: 176, 7, 100 (volume)
  		ndnModule.message[0] = 176;
  		ndnModule.message[1] = 7;
  		ndnModule.message.push_back( 100 );
  		ndnModule.midiout->sendMessage( &ndnModule.message );

  		// Note On: 144, 64, 90
  		ndnModule.message[0] = 144;
  		ndnModule.message[1] = 64;
  		ndnModule.message[2] = 90;
  		ndnModule.midiout->sendMessage( &ndnModule.message );

  		SLEEP( 500 );

  		// Note Off: 128, 64, 40
  		ndnModule.message[0] = 144;
  		ndnModule.message[1] = 64;
  		ndnModule.message[2] = 0;
  		ndnModule.midiout->sendMessage( &ndnModule.message );

  		SLEEP( 500 );

  		// // Note Off: 128, 64, 40
  		// ndnModule.message[0] = 128;
  		// ndnModule.message[1] = 64;
  		// ndnModule.message[2] = 40;
  		// ndnModule.midiout->sendMessage( &ndnModule.message );

  		// SLEEP( 500 );


		// start processing loop (it will block forever)
		face.processEvents();
	}
	catch (const std::exception& e) {
		std::cerr << "ERROR: " << e.what() << std::endl;
	}

	return 0;
}

bool chooseMidiPort( RtMidiOut *rtmidi )
{
  

  std::cout << "\nWould you like to open a virtual output port? [y/N] ";

  std::string keyHit;
  std::getline( std::cin, keyHit);
  std::string keyHit2 = "NDN";
  if ( keyHit == "y" ) {
  	//std::cout << "Name your port: ";
  	//std::getline( std::cin, keyHit2);
    rtmidi->openVirtualPort(keyHit2);
    return true;
  }
 

  std::string portName;
  unsigned int i = 0, nPorts = rtmidi->getPortCount();
  if ( nPorts == 0 ) {
    std::cout << "No output ports available!" << std::endl;
    return false;
  }

  if ( nPorts == 1 ) {
    std::cout << "\nOpening " << rtmidi->getPortName() << std::endl;
  }
  else {
    for ( i=0; i<nPorts; i++ ) {
      portName = rtmidi->getPortName(i);
      std::cout << "  Output port #" << i << ": " << portName << '\n';
    }

    do {
      std::cout << "\nChoose a port number: ";
      std::cin >> i;
    } while ( i >= nPorts );
  }

  std::cout << "\n";
  rtmidi->openPort( i );

  return true;
}
