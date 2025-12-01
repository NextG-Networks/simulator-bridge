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


#include "xapp_rmr.hpp"

XappRmr::XappRmr(std::string port, int rmrattempts){

	_proto_port = port;
	_nattempts = rmrattempts;
	_xapp_rmr_ctx = NULL;
	_xapp_received_buff = NULL;
	_xapp_send_buff =NULL;
	_rmr_is_ready = false;
	_listen = false;

};

XappRmr::~XappRmr(void){
	// free memory
	if(_xapp_received_buff)
		rmr_free_msg(_xapp_received_buff);

	if(_xapp_send_buff)
		rmr_free_msg(_xapp_send_buff);

	if (_xapp_rmr_ctx){
		rmr_close(_xapp_rmr_ctx);
	}
};

//Get RMR Context.
void XappRmr::xapp_rmr_init(bool rmr_listen){


	// Initialize the RMR context
	_xapp_rmr_ctx = rmr_init(const_cast<char*>(_proto_port.c_str()), RMR_MAX_RCV_BYTES, RMRFL_NONE);

	if ( _xapp_rmr_ctx == NULL){
		mdclog_write(MDCLOG_ERR,"Error Initializing RMR, file= %s, line=%d",__FILE__,__LINE__);
	}
	while( ! rmr_ready(_xapp_rmr_ctx) ) {
		mdclog_write(MDCLOG_INFO,">>> waiting for RMR, file= %s, line=%d",__FILE__,__LINE__);
		sleep(1);
	}
	_rmr_is_ready = true;
	mdclog_write(MDCLOG_INFO,"RMR Context is Ready, file= %s, line=%d",__FILE__,__LINE__);
	
	// Log RMR_SEED_RT for debugging
	const char* rmr_seed_rt = getenv("RMR_SEED_RT");
	if (rmr_seed_rt) {
		mdclog_write(MDCLOG_INFO,"RMR_SEED_RT=%s", rmr_seed_rt);
	} else {
		mdclog_write(MDCLOG_WARN,"RMR_SEED_RT environment variable not set");
	}

	//Set the listener requirement
	_listen = rmr_listen;
	return;

}

bool XappRmr::rmr_header(xapp_rmr_header *hdr){

	_xapp_send_buff->mtype  = hdr->message_type;
	_xapp_send_buff->len = hdr->payload_length;
	_xapp_send_buff->sub_id = -1;
	rmr_str2meid(_xapp_send_buff, hdr->meid);


	return true;
}

//RMR Send with payload and header.
bool XappRmr::xapp_rmr_send(xapp_rmr_header *hdr, void *payload){

	// Get the thread id
	std::thread::id my_id = std::this_thread::get_id();
	std::stringstream thread_id;
	std::stringstream ss;

	thread_id << my_id;
	mdclog_write(MDCLOG_INFO, "Sending thread %s",  thread_id.str().c_str());


	int rmr_attempts = _nattempts;

	if( _xapp_send_buff == NULL ) {
		_xapp_send_buff = rmr_alloc_msg(_xapp_rmr_ctx, RMR_DEF_SIZE);
	}

	bool res = rmr_header(hdr);
	if(!res){
		mdclog_write(MDCLOG_ERR,"RMR HEADERS were incorrectly populated, file= %s, line=%d",__FILE__,__LINE__);
		return false;
	}

	memcpy(_xapp_send_buff->payload, payload, hdr->payload_length);
	_xapp_send_buff->len = hdr->payload_length;

	if(!_rmr_is_ready) {
		mdclog_write(MDCLOG_ERR,"RMR Context is Not Ready in SENDER, file= %s, line=%d",__FILE__,__LINE__);
		return false;
	}

	while(rmr_attempts > 0){

		_xapp_send_buff = rmr_send_msg(_xapp_rmr_ctx,_xapp_send_buff);
		if(!_xapp_send_buff) {
			mdclog_write(MDCLOG_ERR,"Error In Sending Message , file= %s, line=%d, attempt=%d",__FILE__,__LINE__,rmr_attempts);
			rmr_attempts--;
		}
		else if (_xapp_send_buff->state == RMR_OK){
			mdclog_write(MDCLOG_INFO,"Message Sent: RMR State = RMR_OK");
			rmr_attempts = 0;
			_xapp_send_buff = NULL;
			return true;
		}
		else
		{
			// Log more details about the error
			const char* state_str = "UNKNOWN";
			if (_xapp_send_buff->state == RMR_ERR_NOENDPT) state_str = "RMR_ERR_NOENDPT (no endpoint found)";
			else if (_xapp_send_buff->state == RMR_ERR_RETRY) state_str = "RMR_ERR_RETRY";
			else if (_xapp_send_buff->state == RMR_ERR_SENDFAILED) state_str = "RMR_ERR_SENDFAILED";
			else if (_xapp_send_buff->state == RMR_ERR_TIMEOUT) state_str = "RMR_ERR_TIMEOUT";
			
			mdclog_write(MDCLOG_INFO,"Need to retry RMR: state=%d (%s), mtype=%d, attempt=%d, file=%s, line=%d",
			             _xapp_send_buff->state, state_str, _xapp_send_buff->mtype, rmr_attempts,__FILE__,__LINE__);
			if(_xapp_send_buff->state == RMR_ERR_RETRY){
				usleep(1);
			}
			rmr_attempts--;
		}
		sleep(1);
	}
	return false;
}

//----------------------------------------
// Some get/set methods
//---------------------------------------
bool XappRmr::get_listen(void){
  return _listen;
}


void XappRmr::set_listen(bool listen){
  _listen = listen;
}

int XappRmr::get_is_ready(void){
  return _rmr_is_ready;
}

bool XappRmr::get_isRunning(void){
  return _listen;
}


void * XappRmr::get_rmr_context(void){
  return _xapp_rmr_ctx;
}


void init_logger(const char  *AppName, mdclog_severity_t log_level)
{
    mdclog_attr_t *attr;
    mdclog_attr_init(&attr);
    mdclog_attr_set_ident(attr, AppName);
    mdclog_init(attr);
    mdclog_level_set(log_level);
    mdclog_attr_destroy(attr);
}

