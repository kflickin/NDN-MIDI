/********************************

PlaybackModuleMIDI.cpp
Requires NFD, ndn-cxx, RtMidi.cpp, and RtMidi.h to compile

Receives and plays back MIDI messages received from ControllerMIDI
on user designated MIDI port.

Receives interest and sends data for connection setup and heartbeat messages
Sends interest for MIDI messages

Currently compatible and tested on MacOS 10.13.2
Currently untested but should work on Linux distros with minimal changes

********************************/

#include <ndn-cxx/face.hpp>
#include <ndn-cxx/interest.hpp>
#include <ndn-cxx/data.hpp>
#include <ndn-cxx/security/key-chain.hpp>

#include <iostream>
#include <string>
#include <map>
#include <thread>

#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "RtMidi.h"

// Define platform-dependent sleep routines.
#if defined(__WINDOWS_MM__)
  #include <windows.h>
  #define SLEEP( milliseconds ) Sleep( (DWORD) milliseconds ) 
#else // Unix variants
  #include <unistd.h>
  #define SLEEP( milliseconds ) usleep( (unsigned long) (milliseconds * 1000.0) )
#endif

// Define number of interests sent once connection is made with ControllerMIDI
#define PREWARM_AMOUNT 5

// Define maximum time for connection with ControllerMIDI to be inactive 
#define MAX_INACTIVE_TIME 5

// Define maximum number of MIDI channels
#define MAX_CHANNELS 16

// MIDI message information for a single connection
struct MIDIControlBlock
{
	int minSeqNo;
	int maxSeqNo;
	int inactiveTime;
	int channel;
};


class PlaybackModule
{
public:
	PlaybackModule(ndn::Face& face, const std::string& hostname, const std::string& projname)
		: m_face(face)
		, m_baseName(ndn::Name("/topo-prefix/" + hostname + "/midi-ndn/" + projname))
		, m_projName(projname)
	{
		// Set interest filter for connection setup
		m_face.setInterestFilter(m_baseName,
								 std::bind(&PlaybackModule::onInterest, this, _2),
								 std::bind([] {
									std::cerr << "Prefix registered" << std::endl;

								 }),
								 [] (const ndn::Name& prefix, const std::string& reason) {
									std::cerr << "Failed to register prefix: " << reason << std::endl;
								 });

		// Thread to check for and remove stale connections
		cbMonitor = std::thread(&PlaybackModule::controlBlockMonitoring, this);

		setupComplete = true;

	}

	bool
	getSetupComplete()
	{
		return setupComplete;
	}

	bool
	getViewingMenu()
	{
		return viewingMenu;
	}

	void
	setViewingMenu()
	{
		viewingMenu = true;
	}

	void
	unsetViewingMenu()
	{
		viewingMenu = false;
	}

	std::set <std::string>
	getAllowedDevices()
	{
		return allowedDevices;
	}

	bool
	getVerboseMode()
	{
		return verboseMode;
	}

	void
	setVerboseMode()
	{
		verboseMode = true;
	}

	void
	unsetVerboseMode()
	{
		verboseMode = false;
	}

	void
	toggleVerboseMode()
	{
		if (verboseMode)
		{
			verboseMode = false;
		}
		else 
		{
			verboseMode = true;
		}
	}

	// Print connected devices menu 
	void
	printConnections()
	{
		bool noConnections = true;
		std::cout
		<< " ____________________________________\n"
		<< "|      ----- Connections -----       |\n"
		<< "|                                    |\n";
		for (int i = 0; i < MAX_CHANNELS; i++)
		{
			if (channelList[i] != "")
			{
				std::cout << "| Channel "
					<< i
					<< ": "
					<< channelList[i];
				int extraSpace = 24 - channelList[i].size();
				for (int i = 0; i < extraSpace; i++) {
					std::cout << " ";
				}
				std::cout << "|" << std::endl;
				noConnections = false;
			}
		}
		if (noConnections) {
			std::cout << "| No connections                     |\n";
		}
		printNavFooter();
	}

	// Print footer for menus
	void
	printNavFooter()
	{
		std::cout 
		<< "|                                    |\n"
		<< "| Main Menu: m                       |\n"
		<< "| Quit: q                            |\n"
		<< "|____________________________________|\n"
		<< std::endl
		<< "Enter selection: ";
	}

	void
	printAllowedDevices()
	{
		std::cout
			<< " ____________________________________\n"
			<< "|     ----- Allowed Devices ----     |\n"
			<< "|                                    |\n";
		if (allowedDevices.empty())
		{
			std::cout << "| All Devices Allowed                |\n";;
		}
		else 
		{
			std::cout << "| Allowed Devices:                   |\n";
			std::set <std::string> :: iterator itr;
			for (itr = allowedDevices.begin(); itr != allowedDevices.end(); ++ itr)
			{
				std::cout << "|     " << *itr;
				int spaces = 31 - (*itr).size();
				for (int i = 0; i < spaces; i++) {
					std::cout << " ";
				}
				std::cout << "|" << std::endl;
			}
		}
	}

	void
	printProhibitedDevices()
	{
		std::cout
			<< " ____________________________________\n"
			<< "|    ----- Prohibited Devices ----   |\n"
			<< "|                                    |\n";
		if (prohibitedDevices.empty())
		{
			std::cout << "| No devices Prohibited              |\n";
		}
		else 
		{
			std::cout << "| Prohibited Devices:                |\n";
			std::set <std::string> :: iterator itr;
			for (itr = prohibitedDevices.begin(); itr != prohibitedDevices.end(); ++ itr)
			{
				std::cout << "|     " << *itr;
				int spaces = 31 - (*itr).size();
				for (int i = 0; i < spaces; i++) {
					std::cout << " ";
				}
				std::cout << "|" << std::endl;
			}
		}
	}

	void
	printVerboseMode()
	{
		std::cout << std::endl;
		if (verboseMode)
		{
			std::cout << "Verbose mode on. ";
		}
		else
		{
			std::cout << "Verbose mode off. ";
		}
		std::cout << std::endl;
	}

	// Clear all connections to external controllers
	void
	clearAllConnections()
	{
		m_lookup.clear();
		for (int i = 0; i < MAX_CHANNELS; i++)
		{
			if (channelList[i] != "")
			{
				closeConnection(channelList[i]);
			}
			this->channelList[i] = "";
		}
		printConnections();
	}

	// Interface to set allowed and prohibited devices
	void
	specifyConnections()
	{
			std::cout << "\nWould you like to specify which devices can connect? [y/N] ";

			std::string keyHit;
			std::string keyHit2;
	  		std::getline( std::cin, keyHit);
			while ( keyHit == "y" ) {
				std::cout << "\nEnter device name: ";
				std::getline( std::cin, keyHit2);
				allowedDevices.insert(keyHit2);
				std::cout << "\nWould you like to specify another device? [y/N] ";
				std::getline( std::cin, keyHit); 
	  		}

	  		std::cout << "\nWould you like to specify which devices are prohibited? [y/N] ";

	  		std::getline( std::cin, keyHit);
			while ( keyHit == "y" ) {
				std::cout << "\nEnter device name: ";
				std::getline( std::cin, keyHit2);
				prohibitedDevices.insert(keyHit2);
				std::cout << "\nWould you like to specify another device? [y/N] ";
				std::getline( std::cin, keyHit); 
	  		}

	}


private:
		
	// Respond to interest as heartbeat message or connection setup	
	void
	onInterest(const ndn::Interest& interest)
	{
		// Check if interest is for heartbeat/connection setup or throw away
		if (interest.getName().get(-1).toUri() != "heartbeat")
			return;

		// Check if connection already exist
		bool isHeartbeat = false;
		bool connectionSuccess = true;
		std::string content = "ACCEPTED";

		// Get name of remote sending device
		std::string remoteName = interest.getName().get(-2).toUri();

		// Check if device is allowed
		// Close connection if not allowed
		if (!allowedDevices.empty()) 
		{
			if (allowedDevices.find(remoteName) == allowedDevices.end())
			{
				if (!viewingMenu)
				{
					std::cerr << "Connection denied: Device not allowed: " << remoteName << std::endl;
				}
				closeConnection(remoteName);
				return;
			}
		}

		// Check if device is prohibited
		// Close connection if prohibited
		if (!prohibitedDevices.empty()) 
		{
			if (prohibitedDevices.find(remoteName) != prohibitedDevices.end())
			{
				if (!viewingMenu)
				{
					std::cerr << "Connection denied: Device prohibited." << remoteName << std::endl;
				}
				closeConnection(remoteName);
				return;
			}
		}

		// Check if connection already exists
		if (m_lookup.count(remoteName) > 0)
		{
			if (verboseMode && !viewingMenu) {
				std::cerr << "Received heartbeat message: " << interest << std::endl;
			}
			isHeartbeat = true;
			m_lookup[remoteName].inactiveTime = 0;
		}

		// Accept and create new connection
		if (!isHeartbeat)
		{
			int controllerChannel = MAX_CHANNELS;
			// Set channel to first available channel
			for (int i = 0; i < MAX_CHANNELS; i++) 
			{
				if (channelList[i] == "") {
					controllerChannel = i;
					channelList[i] = remoteName;
					break;
				}
			}

			// Return error if no availble channels
			if (controllerChannel == MAX_CHANNELS) {
				std::cerr << "Connection denied: No available MIDI channels." << std::endl;
				connectionSuccess = false;
				content = "DENIED";
			}
		
			// Create MIDI control block for new connection
			if (connectionSuccess) 
			{
				m_lookup[remoteName] = {0,0,0,controllerChannel};
				if (verboseMode && !viewingMenu)
				{
					std::cerr << "Connection accepted: " << interest << std::endl;
				}
			}
		}

		/*** Respond to connection request ***/

		// Create data packet with the same name as the interest packet
		std::shared_ptr<ndn::Data> data = std::make_shared<ndn::Data>(interest.getName());

		// Prepare and assign content of the data packet
		data->setContent(reinterpret_cast<const uint8_t*>(content.c_str()), content.size());

		// Set metainfo parameters
		data->setFreshnessPeriod(ndn::time::seconds(1)); 

		// Sign data packet
		m_keyChain.sign(*data);

		// Make data packet available for fetching
		m_face.put(*data);

		if (!isHeartbeat)
		{
			SLEEP(20);
			// "Prewarm the channel" with some interest packets to avoid initial playback latency
			for (int i = 0; i < PREWARM_AMOUNT; ++i)
			{
				requestNext(remoteName);
			}
		}
	}

	void
	onData(const ndn::Data& data)
	{
		// Exit is data packet is a heartbeat message
		if (data.getName().get(-1).toUri() == "heartbeat")
			return;

		// Get sequence number of data packet
		int seqNo = data.getName().get(-1).toSequenceNumber();

		// Set name of remote MIDI controller from data packet
		std::string remoteName = data.getName().get(-4).toUri();

		// Verify connection exists
		if (m_lookup.count(remoteName) == 0)
		{
			// the connection doesn't exist!!
			std::cerr << "Connection for remote user \""
					  << remoteName << "\" doesn't exist!"
					  << std::endl;
			return;
		}

		// Possibly for future: CHECKPOINT 2: sequence number agrees
		//if (m_lookup[remoteName].minSeqNo >= m_lookup[remoteName].maxSeqNo)
		//{
		//	// behavior yet to be defined......
		//	std::cerr << "Corrupted block: minSeqNo >= maxSeqNo"
		//			  << std::endl;
		//}
		//if (m_lookup[remoteName].minSeqNo != seqNo)
		//{
		//	// behavior yet to be defined
		//	std::cerr << "Sequence number out of order --> "
		//			  << "sent: " << m_lookup[remoteName].minSeqNo
		//			  << "  rcvd: " << seqNo
		//			  << std::endl;
		//}

		char buffer[30];
		int dataSize = data.getContent().value_size();
		// Possibly got future:
		// if (data.getContent().value_size() != 3)
		// {
		// 	// incorrect data format
		// 	// behavior yet to be defined
		// 	std::cerr << "Incorrect data format: len = "
		// 			  << data.getContent().value_size()
		// 			  << " (expected 3)"
		// 			  << std::endl;
		// }

		// Copy data to buffer and increment sequence number
		memcpy(buffer, data.getContent().value(), dataSize);
		
		// Get connection information
		MIDIControlBlock cb = m_lookup[remoteName];

		// Check for valid sequence number
		if (cb.minSeqNo > seqNo)
		{
			// out-of-date data, drop
			if (verboseMode && !viewingMenu)
			{
				std::cerr << "Received out-of-date packet... Dropped" << std::endl;
			}
			return;
		}
		else if (cb.maxSeqNo < seqNo)
		{
			if (verboseMode && !viewingMenu)
			{
				std::cerr << "Received packet w/ seq# somehow larger than "
						  << "expected max value: " << seqNo
						  << " (" << cb.maxSeqNo << ")" << std::endl;
			}
			return;
		}

		// Adjust sequence number window
		int diff = seqNo - cb.minSeqNo + 1;
		m_lookup[remoteName].minSeqNo += diff;

		// Create MIDI message for playback from data packet
		std::string receivedData = "Received data:";
		//std::cout << "Received data:";
		for (int j = 0; j < dataSize/3; ++j){
				receivedData = receivedData + " [" + std::to_string(((int)buffer[(j*3)] >> 4) & 15);
				//std::cout << " [" << (int)buffer[(j*3)];
				// for midi message
				this->message[0] = ((unsigned char)buffer[(j*3)] & 0b11110000) | cb.channel;
			for (int i = 1; i < 3; ++i)
			{
				receivedData = receivedData + " " + std::to_string((int)buffer[i+(j*3)]);
				//std::cout << " " << (int)buffer[i+(j*3)];
				// for midi message
				this->message[i] = (unsigned char)buffer[i+(j*3)];

			}
			receivedData = receivedData + " Channel: " + std::to_string(cb.channel) + "]";
			//std::cout << " Channel: " << cb.channel << "]";
			//std::cout << "\n\t";

			// Playback of MIDI message
			if (this->message.size()==3){
				this->midiout->sendMessage(&this->message);
			}

			// Special MIDI message for shutdown
			// TODO: Implement a way to send this message 
			if (buffer[0] == 0 && buffer[1] == 0 && buffer[2] == 0)
			{
				std::cerr << "Deleting table entry of: " << remoteName << std::endl;
				channelList[cb.channel] = "";
				m_lookup.erase(remoteName);
				return;
			}
		}
		
		// Print sequence range
		receivedData = receivedData + "\t[seq range = (" + std::to_string(m_lookup[remoteName].minSeqNo) + "," + std::to_string(m_lookup[remoteName].maxSeqNo) + ")]\n";
		// std::cout << "\t[seq range = (" << m_lookup[remoteName].minSeqNo
		// 	<< "," << m_lookup[remoteName].maxSeqNo << ")]" << std::endl;
		if (!getViewingMenu())
		{
			std::cout << receivedData;
		}
		// Request next data packets based on window size
		for (int i = 0; i < diff; ++i)
		{
			requestNext(remoteName);
		}
	}

	
	void
	onTimeout(const ndn::Interest& interest)
	{
		// For future: Possibly more than a message
		if (verboseMode && !viewingMenu)
		{
			std::cerr << "Timeout for: " << interest << std::endl;
		}
		//m_face.expressInterest(interest,
		//						std::bind(&PlaybackModule::onData, this, _2),
		//						std::bind(&PlaybackModule::onTimeout, this, _1));
	}

	void 
	onNack(const ndn::Interest& interest)
	{
		// For future: Possibly more than a message
		if (verboseMode && !viewingMenu)
		{
			std::cerr << "Nack received for: " << interest << std::endl;
		}
	}
	

private:
	void
	requestNext(std::string remoteName)
	{
		// Check if connection exists
		if (m_lookup.count(remoteName) == 0)
		{
			if (verboseMode && !viewingMenu)
			{
				std::cerr << "Attempted to request from non-existent remote: "
						  << remoteName
						  << " - DROPPED"
						  << std::endl;
			}
			return;
		}

		int nextSeqNo = m_lookup[remoteName].maxSeqNo;
		
		// Possible implementation without specifying interest lifetime
		/** Send interest without specifying interest lifetime 

		ndn::Name nextName = ndn::Name(m_baseName).appendSequenceNumber(nextSeqNo);
		m_face.expressInterest(ndn::Interest(nextName).setMustBeFresh(true),
								std::bind(&PlaybackModule::onData, this, _2),
								std::bind(&PlaybackModule::onTimeout, this, _1));
		**/

		// Create and send next interest with long interest lifetime
		ndn::Name nextName = ndn::Name("/topo-prefix/" + remoteName + "/midi-ndn/" + m_projName)
				.appendSequenceNumber(nextSeqNo);
		ndn::Interest nextNameInterest = ndn::Interest(nextName);
		nextNameInterest.setInterestLifetime(ndn::time::seconds(3600));
		nextNameInterest.setMustBeFresh(true);
		m_face.expressInterest(nextNameInterest,
								std::bind(&PlaybackModule::onData, this, _2),
								std::bind(&PlaybackModule::onNack, this, _1),
								std::bind(&PlaybackModule::onTimeout, this, _1));

		// Increment max sequence number 
		m_lookup[remoteName].maxSeqNo++;

		//std::cerr << "Sending out interest: " << nextName << std::endl;
	}

	// Close the connection with remoteName
	private:
	void
	closeConnection(std::string remoteName)
	{
		// Create and send next interest with long interest lifetime
		ndn::Name nextName = ndn::Name("/topo-prefix/" + remoteName + "/midi-ndn/" + m_projName + "/shutdown");
		ndn::Interest nextNameInterest = ndn::Interest(nextName);
		nextNameInterest.setInterestLifetime(ndn::time::seconds(10));
		nextNameInterest.setMustBeFresh(true);
		m_face.expressInterest(nextNameInterest,
								std::bind(&PlaybackModule::onData, this, _2),
								std::bind(&PlaybackModule::onNack, this, _1),
								std::bind(&PlaybackModule::onTimeout, this, _1));

	}

	// Check and update/remove all control blocks every second
	void
	controlBlockMonitoring()
	{
		while (true)
		{
			SLEEP(1000);
			std::vector<std::string> rmList;
			for (std::map<std::string, MIDIControlBlock>::iterator it = m_lookup.begin();
				it != m_lookup.end(); ++it)
			{
				if (++it->second.inactiveTime > MAX_INACTIVE_TIME)
				{
					rmList.push_back(it->first);
				}
			}

			for (std::string& remoteName : rmList)
			{
				std::cerr << "Deleting connection because it is not active: "
						  << remoteName << std::endl;
				channelList[m_lookup[remoteName].channel] = "";
				m_lookup.erase(remoteName);
			}
		}
	}

private:
	ndn::Face& m_face;
	ndn::KeyChain m_keyChain;
	ndn::Name m_baseName;
	std::string m_projName;

	// Devices that are explicity stated as allowed
	std::set <std::string> allowedDevices;

	// Devices that are explicity stated as prohibited 
	std::set <std::string> prohibitedDevices;

	// Maps remote hostname (remoteName) to a control block
	std::map<std::string, MIDIControlBlock> m_lookup;

	// Thread to monitor control blocks and add/remove as necessary
	std::thread cbMonitor;

	// List of MIDI channels
	std::string channelList[16] = {};

	bool setupComplete = false;

	bool viewingMenu = false;

	bool verboseMode = false;

public:
	RtMidiOut *midiout;
	std::vector<unsigned char> message;
};

void
printTitle()
{
	std::cout
		<< " _________________________\n"
		<< "|                         |\n"
		<< "|         NDN-MIDI        |\n"  
		<< "|     Playback Module     |\n"
		<< "|_________________________|\n";
}

void
printMenu()
{
	std::cout 
		<< std::endl
		<< " ____________________________________\n"
		<< "|       ------ Main Menu ------      |\n"
		<< "|                                    |\n"
		<< "| View Connections: 0                |\n"
		<< "| Clear Connections: 1               |\n"
		<< "| Allowed Devices: 2                 |\n"
		<< "| Prohibited Devices: 3              |\n"
		<< "|                                    |\n"
		<< "| Toggle verbose mode: v             |\n"
		<< "| Exit: q                            |\n"
		<< "|____________________________________|\n"
		<< std::endl
		<< "Please select an option: ";
}

void
menuListener(PlaybackModule& playbackModule)
{
	while(playbackModule.getSetupComplete()) {
		std::string listener = "";
		char menuOption;
		getline (std::cin, listener);
		if (listener == "menu") {
			playbackModule.setViewingMenu();
			mainMenu:
				printMenu();
				//getline (std::cin, menuOption);
				std::cin >> menuOption;
				switch (menuOption) {
					case '0' :
						playbackModule.printConnections();
						std::cin >> menuOption;
						switch (menuOption) {
							case 'm' :
								goto mainMenu;
							default :
								break;
						}
						break;
					case '1' :
						playbackModule.clearAllConnections();
						break;
					case '2' :
						playbackModule.printAllowedDevices();
						playbackModule.printNavFooter();
						std::cin >> menuOption;
						switch (menuOption) {
							case 'm' :
								goto mainMenu;
							default :
								break;
						}
						break;
					case '3' :
						playbackModule.printProhibitedDevices();
						playbackModule.printNavFooter();
						std::cin >> menuOption;
						switch (menuOption) {
							case 'm' :
								goto mainMenu;
							default :
								break;
						}
						break;
					case 'v' :
						playbackModule.toggleVerboseMode();
						playbackModule.printVerboseMode();
						goto mainMenu;
					case 'm' :
						goto mainMenu;
					default :
						break;

				}
			playbackModule.unsetViewingMenu();
		}
	}
	return;
}


bool chooseMidiPort( RtMidiOut *rtmidi );

int main(int argc, char *argv[])
{
	if (argc <= 1)
	{
		std::cerr << "Need to specify your identifier name" << std::endl;
		exit(1);
	}

	// TODO: Add check for hostname format
	std::string hostname = argv[1];

	// get project name: default is tmp-proj
	std::string projname = "tmp-proj";
	if (argc > 2)
	{
		// TODO: Add check for projname format
		projname = argv[2];
	}

	printTitle();

	try {
		// Create Face instance
		ndn::Face face;

		// Create server instance
		PlaybackModule ndnModule(face, hostname, projname);

		ndnModule.specifyConnections();
		
		// RtMidiOut setup
		ndnModule.midiout = new RtMidiOut();
		chooseMidiPort( ndnModule.midiout );

		// // TODO: Remove if unnecessary
		ndnModule.message.push_back( 192 );
    	ndnModule.message.push_back( 5 );
    	ndnModule.midiout->sendMessage( &ndnModule.message );

		SLEEP( 500 );


		// Control Change: 176, 7, 100 (volume)
  		ndnModule.message[0] = 176;
  		ndnModule.message[1] = 7;
  		ndnModule.message.push_back( 100 );
  		ndnModule.midiout->sendMessage( &ndnModule.message );

  		SLEEP( 500 );

  		std::thread menuThread(menuListener, std::ref(ndnModule));

		// Start processing loop (it will block forever)
		face.processEvents();
	}
	catch (const std::exception& e) {
		std::cerr << "ERROR: " << e.what() << std::endl;
	}

	return 0;
}

bool chooseMidiPort( RtMidiOut *rtmidi )
{
  

  std::cout << "\nWould you like to open a virtual NDN-MIDI output port? [y/N] ";

  std::string keyHit;
  std::getline( std::cin, keyHit);
  std::string keyHit2 = "NDN-MIDI Playback";
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
