/*
==================================================================================
        Copyright (c) 2019-2020 AT&T Intellectual Property.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
==================================================================================
*/
/*
 * hw_xapp_main.cc
 * Created on: Dec, 2019
 * Author: Shraboni Jana
 */

#include "xapp.hpp"
#include "xapp-mgmt/ai_tcp_client.h"
#include "xapp-mgmt/ns3_control_writer.h"
#include <thread>
#include <cstdlib>

void signalHandler( int signum ) {
   cout << "Interrupt signal (" << signum << ") received.\n";
   exit(signum);
}

int main(int argc, char *argv[]){

	// Get the thread id
	std::thread::id my_id = std::this_thread::get_id();
	std::stringstream thread_id;
	std::stringstream ss;

	thread_id << my_id;

	mdclog_write(MDCLOG_INFO, "Starting thread %s",  thread_id.str().c_str());

	//get configuration
	XappSettings config;
	//change the priority depending upon application requirement
	config.loadDefaultSettings();
	config.loadEnvVarSettings();
	config.loadCmdlineSettings(argc, argv);

	//Register signal handler to stop
	signal(SIGINT, signalHandler);
	signal(SIGTERM, signalHandler);

	//getting the listening port and xapp name info
	std::string  port = config[XappSettings::SettingName::HW_PORT];
	std::string  name = config[XappSettings::SettingName::XAPP_NAME];

	//initialize rmr
	std::unique_ptr<XappRmr> rmr = std::make_unique<XappRmr>(port);
	rmr->xapp_rmr_init(true);


	//Create Subscription Handler if Xapp deals with Subscription.
	std::unique_ptr<SubscriptionHandler> sub_handler = std::make_unique<SubscriptionHandler>();

	//create HelloWorld Xapp Instance.
	std::unique_ptr<Xapp> hw_xapp;
	hw_xapp = std::make_unique<Xapp>(std::ref(config),std::ref(*rmr));

	mdclog_write(MDCLOG_INFO, "Created Hello World Xapp Instance");

	sleep(1);
	//Startup E2 subscription and A1 policy
	//hw_xapp->startup(std::ref(*sub_handler));
	// auto mp_handler = std::make_unique<XappMsgHandler>(
    //     config[XappSettings::SettingName::XAPP_ID],
    //     std::ref(*sub_handler)
    // );
	

	sleep(1);
	
	//start listener threads and register message handlers.
	int num_threads = std::stoi(config[XappSettings::SettingName::THREADS]);
	mdclog_write(MDCLOG_INFO, "Starting Listener Threads. Number of Workers = %d", num_threads);

	std::unique_ptr<XappMsgHandler> mp_handler = std::make_unique<XappMsgHandler>(config[XappSettings::SettingName::XAPP_ID], std::ref(*sub_handler));
// 	mp_handler->set_control_sender(
//   [x = hw_xapp.get()](const std::string& text, const std::string& meid) {
//     x->send_control_text(text, meid);
//   }
// );
	hw_xapp->start_xapp_receiver(std::ref(*mp_handler));

	sleep(1);

	hw_xapp->startup(std::ref(*sub_handler));

	// Setup bidirectional AI communication: receive configs on same connection
	std::string ns3_control_dir = []() {
		const char* dir = std::getenv("NS3_CONTROL_DIR");
		return dir ? std::string(dir) : std::string("/tmp");
	}();
	
	// Create NS3 control writer
	std::unique_ptr<Ns3ControlWriter> ns3_writer = std::make_unique<Ns3ControlWriter>(ns3_control_dir);
	
	// Start config listener on the existing AI TCP connection
	// AI can send configs as unsolicited messages on the same socket
	GetAiTcpClient().StartConfigListener([&ns3_writer](const std::string& config_json) -> bool {
		return ns3_writer->WriteControl(config_json);
	});
	
	mdclog_write(MDCLOG_INFO, "[MAIN] Started AI config listener on existing connection, writing to %s", 
	             ns3_control_dir.c_str());

	//xapp->shutdown();

 	while(1){
	 			sleep(1);
	 		 }

	return 0;
}



