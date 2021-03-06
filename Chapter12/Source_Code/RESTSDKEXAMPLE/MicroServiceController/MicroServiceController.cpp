// MicroServiceController.cpp : Defines the entry point for the console application.
//
//
//
//
#include "stdafx.h"

#include <cpprest/http_client.h>
#include <cpprest/filestream.h>
#include <string>
#include <vector>
#include <algorithm>
#include <sstream>
#include <iostream>
#include <fstream>
#include <random>
#include <set>

#include "cpprest/json.h"
#include "cpprest/http_listener.h"
#include "cpprest/uri.h"
#include "cpprest/asyncrt_utils.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
# include <sys/time.h>
#endif


using namespace std;
using namespace web;
using namespace utility;
using namespace http;
using namespace web::http::experimental::listener;

//////////////////////////////
//
// The following code dumps a json to the Console...
void  DisplayJSON(json::value const & jvalue){
	wcout << jvalue.serialize() << endl;
}

///////////////////////////////////////////////
// A Workhorse routine to perform an action on the request data type
// takes a lambda as parameter along with request type
// The Lambda should contain the action logic...whether it is PUT,POST or DELETE
//
void RequeatWorker(
	http_request& request,function<void(json::value const &, json::value &)> handler)
{
	
	auto result = json::value::object();
	
    request.extract_json().then([&result, &handler](pplx::task<json::value> task) {
		
		try{
		   auto const & jvalue = task.get();
		   if (!jvalue.is_null())
			   handler(jvalue, result); // invoke the lambda
		}
		catch (http_exception const & e) {
			//----------- do exception processsing 
			wcout << L"Exception ->" << e.what() << endl;
		}
	}).wait();
    request.reply(status_codes::OK, result);
}

/////////////////////////////////////////
// A Mock data base Engine which Simulates a Key/Value DB
// In Real life, one should use an Industrial strength DB
//
class HttpKeyValueDBEngine {
	//////////////////////////////////
	//----------- Map , which we save,retrieve,  update and 
	//----------- delete data 
	map<utility::string_t, utility::string_t> storage;
public:
	HttpKeyValueDBEngine() {
		storage[L"Praseed"]= L"45";
		storage[L"Peter"] = L"28";
		storage[L"Andrei"] = L"50";
	}
	////////////////////////////////////////////////////////
	// GET - ?Just Iterates through the Map and Stores
	// the data in a JSon Object. IT is emitted to the 
	// Response Stream
	void GET_HANDLER(http_request& request) {
		auto resp_obj = json::value::object();
		for (auto const & p : storage)
			resp_obj[p.first] = json::value::string(p.second);
        request.reply(status_codes::OK, resp_obj);
	}

	//////////////////////////////////////////////////
	// POST - Retrieves a Set of Values from the DB
	// The PAyload should be in ["Key1" , "Key2"...,"Keyn"]
	// format
	void POST_HANDLER(http_request& request) {
		
		RequeatWorker(
			request,
			[&](json::value const & jvalue, json::value & result)
		{
			//---------- Write to the Console for Diagnostics
			DisplayJSON(jvalue);

			for (auto const & e : jvalue.as_array())
			{
				if (e.is_string())
				{
					auto key = e.as_string();
					auto pos = storage.find(key);

					if (pos == storage.end())
					{
						//--- Indicate to the Client that Key is not found
						result[key] = json::value::string(L"notfound");
					}
					else
					{
						//------------- store the key value pair in the result
						//------------- json. The result will be send back to 
						//------------- the client
						result[pos->first] = json::value::string(pos->second);
					}
				}
			}
		});
		
	}
	////////////////////////////////////////////////////////
	// PUT - Updates Data, If new KEy is found 
	//       Otherwise, Inserts it
	// REST Payload should be in 
	//      { Key1..Value1,...,Keyn,Valuen}  format
	//
	//
	void PUT_HANDLER(http_request& request) {
		
			
		
		RequeatWorker(
			request,
			[&](json::value const & jvalue, json::value & result)
		{
			DisplayJSON(jvalue);

			for (auto const & e : jvalue.as_object())
			{
				if (e.second.is_string())
				{
					auto key = e.first;
					auto value = e.second.as_string();

					if (storage.find(key) == storage.end())
					{
						//--- Indicate to the client that we have
						//--- created a new record
						result[key] = json::value::string(L"<put>");
					}
					else
					{
						//--- Indicate to the client that we have
						//--- updated a new record
						result[key] = json::value::string(L"<updated>");
					}

					storage[key] = value;
				}
			}
		});
		
	}
	///////////////////////////////////////////////////
	// DEL - Deletes a Set of Records
	// REST PayLoad should be in
	//      [ Key1,....,Keyn] format
	//
	void DEL_HANDLER(http_request& request)
	{
		

		RequeatWorker(
			request,
			[&](json::value const & jvalue, json::value & result)
		{
			//--------------- We aggregate all keys into this set
			//--------------- and delete in one go
			set<utility::string_t> keys;
			for (auto const & e : jvalue.as_array())
			{
				if (e.is_string())
				{
					auto key = e.as_string();

					auto pos = storage.find(key);
					if (pos == storage.end())
					{
						result[key] = json::value::string(L"<failed>");
					}
					else
					{
						
						result[key] = json::value::string(L"<deleted>");
						//---------- Insert in to the delete list
						keys.insert(key);
					}
				}
			}
			//---------------Erase all
			for (auto const & key : keys)
				storage.erase(key);
		});
	}
};

///////////////////////////////////////////////
//
// Instantiates the Global instance of Key/Value DB
HttpKeyValueDBEngine g_dbengine;

class RestDbServiceServer
{
public:
	RestDbServiceServer(utility::string_t url);

	pplx::task<void> Open() { return m_listener.open(); }
	pplx::task<void> Close() { return m_listener.close(); }

private:

	void HandleGet(http_request message);
	void HandlePut(http_request message);
	void HandlePost(http_request message);
	void HandleDelete(http_request message);

	http_listener m_listener;
};





RestDbServiceServer::RestDbServiceServer(utility::string_t url) : m_listener(url)
{
	 m_listener.support(methods::GET, std::bind(&RestDbServiceServer::HandleGet, this, std::placeholders::_1));
	 m_listener.support(methods::PUT, std::bind(&RestDbServiceServer::HandlePut, this, std::placeholders::_1));
	 m_listener.support(methods::POST, std::bind(&RestDbServiceServer::HandlePost, this, std::placeholders::_1));
	 m_listener.support(methods::DEL, std::bind(&RestDbServiceServer::HandleDelete, this, std::placeholders::_1));


}

void RestDbServiceServer::HandleGet(http_request message)
{
	g_dbengine.GET_HANDLER(message); 
	
};

void RestDbServiceServer::HandlePost(http_request message)
{
	g_dbengine.POST_HANDLER(message);
};

void RestDbServiceServer::HandleDelete(http_request message)
{
	cout << "Called Delete.........inside " << endl;
	g_dbengine.DEL_HANDLER(message);
}

void RestDbServiceServer::HandlePut(http_request message)
{
	cout << "Put.........inside " << endl;

	g_dbengine.PUT_HANDLER(message);
};

std::unique_ptr<RestDbServiceServer> g_http;

void StartServer(const string_t& address)
{
	
    uri_builder uri(address);
	uri.append_path(U("DBDEMO/"));

	auto addr = uri.to_uri().to_string();
	g_http = std::unique_ptr<RestDbServiceServer>(new RestDbServiceServer(addr));
	g_http->Open().wait();

	ucout << utility::string_t(U("Listening for requests at: ")) << addr << std::endl;

	return;
}

void ShutDown()
{
	g_http->Close().wait();
	return;
}

///////////////////////////////
// The EntryPoint fnction
//

int wmain(int argc, wchar_t *argv[])
{
	utility::string_t port = U("34567");
	if (argc == 2)
	{
		port = argv[1];
	}

	utility::string_t address = U("http://localhost:");
	address.append(port);

	StartServer(address);
	std::cout << "Press ENTER to exit." << std::endl;

	std::string line;
	std::getline(std::cin, line);

	ShutDown();
	return 0;
}
