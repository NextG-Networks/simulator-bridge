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
 * msgs_proc.cc
 * Created on: 2019
 * Author: Ashwin Shridharan, Shraboni Jana
 */


#include "msgs_proc.hpp"
#include <stdio.h>
#include <thread>
#include <string>
// #include "xapp.hpp"

#include "ai_tcp_client.h"

// E2SM (HelloWorld) indication decode support available in this repo
#include "../xapp-asn/e2sm/e2sm_indication.hpp"
// E2SM-KPM ASN.1 types (from oran-e2sim)
extern "C" {
#include "asn_application.h"
#include "E2SM-KPM-IndicationHeader.h"
#include "E2SM-KPM-IndicationMessage.h"
#include "E2SM-KPM-IndicationMessage-Format1.h"
#include "E2SM-KPM-IndicationMessage-Format2.h"
}

bool XappMsgHandler::encode_subscription_delete_request(unsigned char* buffer, size_t *buf_len){

	subscription_helper sub_helper;
	sub_helper.set_request(0); // requirement of subscription manager ... ?
	sub_helper.set_function_id(0);

	subscription_delete e2ap_sub_req_del;

	  // generate the delete request pdu

	  bool res = e2ap_sub_req_del.encode_e2ap_subscription(&buffer[0], buf_len, sub_helper);
	  if(! res){
	    mdclog_write(MDCLOG_ERR, "%s, %d: Error encoding subscription delete request pdu. Reason = %s", __FILE__, __LINE__, e2ap_sub_req_del.get_error().c_str());
	    return false;
	  }

	return true;

}

bool XappMsgHandler::decode_subscription_response(unsigned char* data_buf, size_t data_size){

	subscription_helper subhelper;
	subscription_response subresponse;
	bool res = true;
	E2AP_PDU_t *e2pdu = 0;

	asn_dec_rval_t rval;

	ASN_STRUCT_RESET(asn_DEF_E2AP_PDU, e2pdu);

	rval = asn_decode(0,ATS_ALIGNED_BASIC_PER, &asn_DEF_E2AP_PDU, (void**)&e2pdu, data_buf, data_size);
	switch(rval.code)
	{
		case RC_OK:
			   //Put in Subscription Response Object.
			   //asn_fprint(stdout, &asn_DEF_E2AP_PDU, e2pdu);
			   break;
		case RC_WMORE:
				mdclog_write(MDCLOG_ERR, "RC_WMORE");
				res = false;
				break;
		case RC_FAIL:
				mdclog_write(MDCLOG_ERR, "RC_FAIL");
				res = false;
				break;
		default:
				break;
	 }
	ASN_STRUCT_FREE(asn_DEF_E2AP_PDU, e2pdu);
	return res;

}

bool  XappMsgHandler::a1_policy_handler(char * message, int *message_len, a1_policy_helper &helper){

  rapidjson::Document doc;
  if (doc.Parse(message).HasParseError()){
    mdclog_write(MDCLOG_ERR, "Error: %s, %d :: Could not decode A1 JSON message %s\n", __FILE__, __LINE__, message);
    return false;
  }

  //Extract Operation
  rapidjson::Pointer temp1("/operation");
    rapidjson::Value * ref1 = temp1.Get(doc);
    if (ref1 == NULL){
      mdclog_write(MDCLOG_ERR, "Error : %s, %d:: Could not extract policy type id from %s\n", __FILE__, __LINE__, message);
      return false;
    }

   helper.operation = ref1->GetString();

  // Extract policy id type
  rapidjson::Pointer temp2("/policy_type_id");
  rapidjson::Value * ref2 = temp2.Get(doc);
  if (ref2 == NULL){
    mdclog_write(MDCLOG_ERR, "Error : %s, %d:: Could not extract policy type id from %s\n", __FILE__, __LINE__, message);
    return false;
  }
   //helper.policy_type_id = ref2->GetString();
    helper.policy_type_id = to_string(ref2->GetInt());

    // Extract policy instance id
    rapidjson::Pointer temp("/policy_instance_id");
    rapidjson::Value * ref = temp.Get(doc);
    if (ref == NULL){
      mdclog_write(MDCLOG_ERR, "Error : %s, %d:: Could not extract policy type id from %s\n", __FILE__, __LINE__, message);
      return false;
    }
    helper.policy_instance_id = ref->GetString();

    if (helper.policy_type_id == "1" && helper.operation == "CREATE"){
    	helper.status = "OK";
    	Document::AllocatorType& alloc = doc.GetAllocator();

    	Value handler_id;
    	handler_id.SetString(helper.handler_id.c_str(), helper.handler_id.length(), alloc);

    	Value status;
    	status.SetString(helper.status.c_str(), helper.status.length(), alloc);


    	doc.AddMember("handler_id", handler_id, alloc);
    	doc.AddMember("status",status, alloc);
    	doc.RemoveMember("operation");
    	StringBuffer buffer;
    	Writer<StringBuffer> writer(buffer);
    	doc.Accept(writer);
    	strncpy(message,buffer.GetString(), buffer.GetLength());
    	*message_len = buffer.GetLength();
    	return true;
    }
    return false;
 }

 static void dump_hex_ascii(const void* data, size_t len) {
    auto* p = static_cast<const uint8_t*>(data);
    std::string ascii;
    for (size_t i = 0; i < len; ++i) {
        if (i % 16 == 0) {
            if (!ascii.empty()) { mdclog_write(MDCLOG_INFO, "    %s", ascii.c_str()); ascii.clear(); }
            mdclog_write(MDCLOG_INFO, "%04zx: ", i);
        }
        mdclog_write(MDCLOG_INFO, "%02x ", p[i]);
        ascii.push_back((p[i] >= 32 && p[i] <= 126) ? char(p[i]) : '.');
    }
    if (!ascii.empty()) mdclog_write(MDCLOG_INFO, "    %s", ascii.c_str());
}

static inline void PublishKpiToExternal(const std::string& meid,
                                        const std::string& kpi_json)
{
    GetAiTcpClient().SendKpi(meid, kpi_json);
}

static inline std::string RequestRecommendation(const std::string& meid,
                                                const std::string& kpi_json)
{
    std::string cmd;
    if (GetAiTcpClient().GetRecommendation(meid, kpi_json, cmd)) {
        return cmd;    // non-empty JSON command
    }
    return "";         // no action
}



//For processing received messages.XappMsgHandler should mention if resend is required or not.
void XappMsgHandler::operator()(rmr_mbuf_t *message, bool *resend){

	if (message->len > MAX_RMR_RECV_SIZE){
		mdclog_write(MDCLOG_ERR, "Error : %s, %d, RMR message larger than %d. Ignoring ...", __FILE__, __LINE__, MAX_RMR_RECV_SIZE);
		return;
	}
	a1_policy_helper helper;
	bool res=false;
	switch(message->mtype){
		//need to fix the health check.
		case (RIC_HEALTH_CHECK_REQ):
				message->mtype = RIC_HEALTH_CHECK_RESP;        // if we're here we are running and all is ok
				message->sub_id = -1;
				strncpy( (char*)message->payload, "HELLOWORLD OK\n", rmr_payload_size( message) );
				*resend = true;
				break;

		case RIC_INDICATION: {
			mdclog_write(MDCLOG_INFO, "Received RIC indication message of type = %d", message->mtype);

			unsigned char* me_id_null;
			unsigned char* me_id = rmr_get_meid(message, me_id_null);
			if (me_id == nullptr) {
				mdclog_write(MDCLOG_ERR, "RIC_INDICATION missing MEID; ignoring");
				break;
			}

			std::string meid_str(reinterpret_cast<char*>(me_id));
			std::string kpi_blob(reinterpret_cast<char*>(message->payload), message->len);

			// (Optional) keep any existing local processing you had
			process_ric_indication(message->mtype, me_id, message->payload, message->len);

			// 1) Forward KPI to external system (non-blocking)
			PublishKpiToExternal(meid_str, kpi_blob);

			// 2) Ask external system for recommendation on a detached worker
			//    so we never block the RMR receive thread.
			if (send_ctrl_) {
				std::thread([this, meid_str, kpi_blob]() {
					// Blocking call into your external brain; replace stub later
					std::string cmd_json = RequestRecommendation(meid_str, kpi_blob);

					if (!cmd_json.empty()) {
						mdclog_write(MDCLOG_INFO, "[REC←EXT] MEID=%s cmd=%s",
									meid_str.c_str(), cmd_json.c_str());
						// Ship the command over RIC/E2 using the already-registered sender
						send_ctrl_(cmd_json, meid_str);
					} else {
						mdclog_write(MDCLOG_INFO, "[REC←EXT] MEID=%s no-action", meid_str.c_str());
					}
				}).detach();
			} else {
				mdclog_write(MDCLOG_WARN,
							"[HOOK] send_ctrl_ not set; call set_control_sender() before processing messages (this=%p)",
							(void*)this);
			}
			break;
		}

		case (RIC_SUB_RESP): {
        		mdclog_write(MDCLOG_INFO, "Received subscription message of type = %d", message->mtype);

				unsigned char *me_id_null;
				unsigned char *me_id = rmr_get_meid(message, me_id_null);
				mdclog_write(MDCLOG_INFO,"RMR Received MEID: %s",me_id);

				if(_ref_sub_handler !=NULL){
					_ref_sub_handler->manage_subscription_response(message->mtype, me_id, message->payload, message->len);
				} else {
					mdclog_write(MDCLOG_ERR, " Error :: %s, %d : Subscription handler not assigned in message processor !", __FILE__, __LINE__);
				}
				*resend = false;
				break;
		 }

	case A1_POLICY_REQ:

		    mdclog_write(MDCLOG_INFO, "In Message Handler: Received A1_POLICY_REQ.");
			helper.handler_id = xapp_id;

			res = a1_policy_handler((char*)message->payload, &message->len, helper);
			if(res){
				message->mtype = A1_POLICY_RESP;        // if we're here we are running and all is ok
				message->sub_id = -1;
				*resend = true;
			}
			break;

	default:
		{
			mdclog_write(MDCLOG_ERR, "Error :: Unknown message type %d received from RMR", message->mtype);
			*resend = false;
		}
	}

	return;

};

void process_ric_indication(int message_type, transaction_identifier id, const void *message_payload, size_t message_len) {

	std::cout << "In Process RIC indication" << std::endl;
	std::cout << "ID " << id << std::endl;

	// decode received message payload
  E2AP_PDU_t *pdu = nullptr;
  auto retval = asn_decode(nullptr, ATS_ALIGNED_BASIC_PER, &asn_DEF_E2AP_PDU, (void **) &pdu, message_payload, message_len);

  // print decoded payload
  if (retval.code == RC_OK) {
    char *printBuffer;
    size_t size;
    FILE *stream = open_memstream(&printBuffer, &size);
    asn_fprint(stream, &asn_DEF_E2AP_PDU, pdu);
    mdclog_write(MDCLOG_DEBUG, "Decoded E2AP PDU: %s", printBuffer);

    uint8_t res = procRicIndication(pdu, id);
  }
	else {
		std::cout << "process_ric_indication, retval.code " << retval.code << std::endl;
	}
}

/**
 * Handle RIC indication
 * TODO doxy
 */
uint8_t procRicIndication(E2AP_PDU_t *e2apMsg, transaction_identifier gnb_id)
{
   uint8_t idx;
   uint8_t ied;
   uint8_t ret = RC_OK;
   uint32_t recvBufLen;
   RICindication_t *ricIndication;
   RICaction_ToBeSetup_ItemIEs_t *actionItem;

   printf("\nE2AP : RIC Indication received");
   ricIndication = &e2apMsg->choice.initiatingMessage->value.choice.RICindication;

   printf("protocolIEs elements %d\n", ricIndication->protocolIEs.list.count);

   for (idx = 0; idx < ricIndication->protocolIEs.list.count; idx++)
   {
      switch(ricIndication->protocolIEs.list.array[idx]->id)
      {
				case 28:  // RIC indication type
				{
					long ricindicationType = ricIndication->protocolIEs.list.array[idx]-> \
																		 value.choice.RICindicationType;

					printf("ricindicationType %ld\n", ricindicationType);

					break;
				}
				case 26:  // RIC indication message
				{
					size_t payload_size = ricIndication->protocolIEs.list.array[idx]-> \
																		 value.choice.RICindicationMessage.size;

					char* payload = (char*) calloc(payload_size, sizeof(char));
					memcpy(payload, ricIndication->protocolIEs.list.array[idx]-> \
																		 value.choice.RICindicationMessage.buf, payload_size);

					// Helper lambdas
					auto to_hex = [](const unsigned char* data, size_t len) {
						static const char* H = "0123456789abcdef";
						std::string s;
						s.reserve(len * 2);
						for (size_t i = 0; i < len; ++i) {
							unsigned char c = data[i];
							s.push_back(H[(c >> 4) & 0xF]);
							s.push_back(H[c & 0xF]);
						}
						return s;
					};
					auto json_escape = [](const unsigned char* data, size_t len) {
						std::string s;
						s.reserve(len + 8);
						for (size_t i = 0; i < len; ++i) {
							unsigned char c = data[i];
							switch (c) {
								case '\"': s += "\\\""; break;
								case '\\': s += "\\\\"; break;
								case '\b': s += "\\b"; break;
								case '\f': s += "\\f"; break;
								case '\n': s += "\\n"; break;
								case '\r': s += "\\r"; break;
								case '\t': s += "\\t"; break;
								default:
									if (c < 0x20) {
										char buf[7];
										snprintf(buf, sizeof(buf), "\\u%04x", c);
										s += buf;
									} else {
										s.push_back((char)c);
									}
							}
						}
						return s;
					};

					std::string out_json;
					bool decoded_ok = false;

					// 1) Try E2SM-KPM
					E2SM_KPM_IndicationMessage_t *kpm = 0;
					asn_dec_rval_t kpm_res = asn_decode(0,
					                                    ATS_ALIGNED_BASIC_PER,
					                                    &asn_DEF_E2SM_KPM_IndicationMessage,
					                                    (void**)&kpm,
					                                    payload,
					                                    payload_size);
					if (kpm && kpm_res.code == RC_OK) {
						// Minimal JSON summary without walking deep structures
						const char* fmt = "unknown";
						if (kpm->present == E2SM_KPM_IndicationMessage_PR_indicationMessage_Format1) {
							fmt = "F1";
							E2SM_KPM_IndicationMessage_Format1_t* f1 = kpm->choice.indicationMessage_Format1;
							size_t pm_cont_count = f1 ? f1->pm_Containers.list.count : 0;
							out_json = std::string("{\"serviceModel\":\"KPM\",\"format\":\"") + fmt +
							           "\",\"pmContainers\":" + std::to_string(pm_cont_count) + "}";
						} else if (kpm->present == E2SM_KPM_IndicationMessage_PR_indicationMessage_Format2) {
							fmt = "F2";
							E2SM_KPM_IndicationMessage_Format2_t* f2 = kpm->choice.indicationMessage_Format2;
							std::string sub_id = f2 ? std::to_string((long)f2->subscriptID) : "0";
							std::string cell_hex = (f2 && f2->cellObjID) ? to_hex(f2->cellObjID->buf, f2->cellObjID->size) : "";
							std::string gp = (f2 && f2->granulPeriod) ? std::to_string((long)(*f2->granulPeriod)) : "null";
							// We won't traverse measData deeply here; emit counts only
							size_t meas_rec = (f2) ? f2->measData.measDataItem.list.count : 0;
							out_json = std::string("{\"serviceModel\":\"KPM\",\"format\":\"") + fmt +
							           "\",\"subscriptionId\":" + sub_id +
							           ",\"granularity\":" + gp +
							           ",\"cellObjectIdHex\":\"" + cell_hex +
							           "\",\"measurementRecords\":" + std::to_string(meas_rec) + "}";
						} else {
							out_json = "{\"serviceModel\":\"KPM\",\"format\":\"unknown\"}";
						}
						decoded_ok = true;
						mdclog_write(MDCLOG_INFO, "Decoded KPM E2SM message (present=%d)", kpm->present);
					}

					// 2) If KPM not decoded, try HelloWorld
					if (!decoded_ok) {
						E2SM_HelloWorld_IndicationMessage_t *hw_msg = 0;
						asn_dec_rval_t dres = asn_decode(0,
						                                 ATS_ALIGNED_BASIC_PER,
						                                 &asn_DEF_E2SM_HelloWorld_IndicationMessage,
						                                 (void**)&hw_msg,
						                                 payload,
						                                 payload_size);
						if (dres.code == RC_OK && hw_msg != nullptr) {
							e2sm_indication helper_iface;
							e2sm_indication_helper decoded;
							if (helper_iface.get_fields(hw_msg, decoded)) {
								std::string msg_str = json_escape(decoded.message, decoded.message_len);
								out_json = std::string("{\"serviceModel\":\"HelloWorld\",\"indicationMessage\":\"") +
								           msg_str + "\"}";
								decoded_ok = true;
								mdclog_write(MDCLOG_INFO, "Decoded HelloWorld E2SM message, len=%zu", decoded.message_len);
							} else {
								mdclog_write(MDCLOG_WARN, "HelloWorld decode get_fields failed");
							}
						} else {
							mdclog_write(MDCLOG_WARN, "E2SM HelloWorld decode failed (code=%d)", dres.code);
						}
						if (hw_msg) {
							ASN_STRUCT_FREE(asn_DEF_E2SM_HelloWorld_IndicationMessage, hw_msg);
						}
					}

					// send decoded JSON if available, otherwise raw payload, to external agent over TCP
					std::string agent_ip = find_agent_ip_from_gnb(gnb_id);
					if (decoded_ok && !out_json.empty()) {
						send_socket((char*)out_json.c_str(), out_json.size(), agent_ip);
					} else {
						send_socket(payload, payload_size, agent_ip);
					}

					if (kpm) {
						ASN_STRUCT_FREE(asn_DEF_E2SM_KPM_IndicationMessage, kpm);
					}
					free(payload);

					break;
				}
      }
   }
   return ret; // TODO update ret value in case of errors
}
