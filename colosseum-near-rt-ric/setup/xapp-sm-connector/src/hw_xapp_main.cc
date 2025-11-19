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
#include "xapp-mgmt/ai_config_receiver.h"
#include "xapp-mgmt/ns3_control_writer.h"
#include "xapp-mgmt/ai_tcp_client.h"
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
	
	// Enable E2 control sender for sending commands back to RAN via E2 CONTROL REQUEST
	mp_handler->set_control_sender(
	  [x = hw_xapp.get()](const std::string& text, const std::string& meid) {
	    x->send_control_text(text, meid);
	  }
	);
	
	hw_xapp->start_xapp_receiver(std::ref(*mp_handler));

	sleep(1);

	// Setup AI config receiver BEFORE startup (non-blocking, runs in background thread)
	mdclog_write(MDCLOG_INFO, "[MAIN] Setting up AI config receiver...");
	
	std::string ns3_control_dir = []() {
		const char* dir = std::getenv("NS3_CONTROL_DIR");
		return dir ? std::string(dir) : std::string("/tmp/ns3-control");
	}();
	
	int config_port = []() {
		const char* port = std::getenv("AI_CONFIG_PORT");
		return port ? std::atoi(port) : 5001; // Default port 5001 for configs
	}();
	
	// Create NS3 control writer (lightweight, just sets up paths)
	std::unique_ptr<Ns3ControlWriter> ns3_writer = std::make_unique<Ns3ControlWriter>(ns3_control_dir);
	
	// Start config receiver server (runs in background thread, non-blocking)
	AiConfigReceiver::ConfigHandler handler = [&ns3_writer](const std::string& config_json) -> bool {
		return ns3_writer->WriteControl(config_json);
	};
	
	std::unique_ptr<AiConfigReceiver> config_receiver = std::make_unique<AiConfigReceiver>(config_port, handler);
	config_receiver->Start();
	
	mdclog_write(MDCLOG_INFO, "[MAIN] Started AI config receiver on port %d, writing to %s", 
	             config_port, ns3_control_dir.c_str());

	// Setup reactive control command listener
	GetAiTcpClient().StartControlCommandListener(
		[handler = mp_handler.get()](const std::string& meid, const std::string& cmd_json) -> bool {
			handler->send_control(cmd_json, meid);
			return true;
		}
	);

	// Now start the xApp (this will send subscriptions and start receiving KPIs)
	hw_xapp->startup(std::ref(*sub_handler));

	//xapp->shutdown();

 	while(1){
	 			sleep(1);
	 		 }

	return 0;
}



