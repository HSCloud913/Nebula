#include <iostream>
#include <fstream>

//#include "Http/Server/Server.h"
//#include "Http/Client/Request.h"
//#include "Logger.h"
#include <memory>



int main()
{
	//using namespace std::chrono_literals;

	//SetConsoleOutputCP(CP_UTF8);

	// {
	// 	//NebulaHttpServer server("127.0.0.1", 8080, "D:\\ssl\\server.pfx", "");
	// 	//NebulaHttpServer server("127.0.0.1", 8080, "/tmp/Nebula/_bin/ssl/Nebula.crt", "/tmp/Nebula/_bin/ssl/private.key");
	// 	NebulaHttpServer server("127.0.0.1", 8080);
	//
	// 	server.Route(NebulaHttp::Method::GET, "/nebula/test", [](NebulaHttp::Server::Response&& _responseData)
	// 	{
	// 		// add header
	// 		//_responseData.SetHeaders();
	//
	// 		// add body
	// 		string_t json = "{ \"aa\": 1 }";
	// 		_responseData.SetBody(json);
	// 	});
	//
	// 	server.Listen(false);
	// }

	// {
	// 	try
	// 	{
	// 		//auto response = NebulaHttpClient::Post("http://192.168.0.47:3000/save-result")
	// 		auto response = NebulaHttpClient::Post("http://192.168.0.3:7000/save-result")
	// 		//auto response = NebulaHttpClient::Post("https://echo.free.beeceptor.com")
	// 						.AddHeaders(
	// 							NebulaHttp::Header
	// 							{
	// 								.name = "Connection",
	// 								.value = "keep-alive"
	// 							},
	// 							NebulaHttp::Header
	// 							{
	// 								.name = "Cache-Control",
	// 								.value = "no-cache"
	// 							},
	// 							NebulaHttp::Header
	// 							{
	// 								.name = "Content-Type",
	// 								.value = "application/json"
	// 							}
	// 						)
	// 						.SetBody(R"({"aa": 1})")
	// 						.SetHeadersCallback([](NebulaHttpClient::ResponseHeaders& _response)
	// 						{
	// 							std::cout << "Http Status " << static_cast<int_t>(_response.GetStatusCode()) << std::endl;
	// 							std::cout << "Header" << std::endl;
	// 							for (auto header : _response.GetHeaders())
	// 							{
	// 								std::cout << header.name << " " << header.value << '\n';
	// 							}
	// 							std::cout << "------------------------------" << std::endl;
	// 							std::cout << std::endl;
	// 						})
	// 						// .SetBodyCallback([](NebulaHttpClient::ResponseBody& _response)
	// 						// {
	// 						// 	std::cout << "Body" << std::endl;
	// 						//
	// 						// 	if (_response.bodySize > 0)
	// 						// 	{
	// 						// 		std::cout << NebulaHttp::DataToString(_response.body) << std::endl;
	// 						// 	}
	// 						// 	std::cout << "------------------------------" << std::endl;
	// 						// })
	// 						.Send();
	//
	// 		std::cout << response.GetBodyString() << std::endl;
	// 	} catch (const ne::NebulaException& e)
	// 	{
	// 		std::cout << e.what() << std::endl;
	// 	}
	// }

	return 0;
}
