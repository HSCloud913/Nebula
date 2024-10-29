#include <iostream>
#include <fstream>

//#include "Http/Server/Server.h"
//#include "Http/Client/Request.h"
//#include "Logger.h"
#include <memory>
#include "Json/Json.h"
//#include "Json/JsonType.h"
ne::lpcstr_t EXAMPLE = "                           {             \
 	\"string_name\"     :    \"string\tvalue and a \\\"quote\\\" and a unicode char \\u00BE and a c:\\\\path\\\\ or a \\/unix\\/path\\/ :D\", \
 	\"bool_name\"     :     true, \
 	\"bool_second\"   :   TrUe, \
 	\"null_name\"  :   nULl, \
	\"nua\"  :  9223372036854775807, \
	\"max\"  :  18446744073709551615, \
	\"oor\"  :  18446744073709551616, \
 	\"negative\"  :  -34.276, \
 	\"sub_object\"  :  { \
 						\"foo\"   :   \"abc\", \
 						 \"bar\"   :   1.35e2, \
 						 \"blah\"   :   { \"a\" : \"A\", \"b\" : \"B\", \"c\" : \"C\"            } \
 					}, \
 	\"array_letters\" : [ \"a\", \"b\", \"c\", [ 1, 2, 3  ]  ] \
 }                                                           ";

// class SharedInt {
// public:
// 	std::shared_ptr<int> ptr;
//
// 	SharedInt() : ptr(std::make_shared<int>(0)) {}  // 기본 생성자
// 	SharedInt(int value) : ptr(std::make_shared<int>(value)) {}  // 값으로 초기화
//
// 	// = 연산자 오버로드
// 	// SharedInt& operator=(int value) {
// 	// 	*ptr = value;  // 포인터가 가리키는 값을 변경
// 	// 	return *this;
// 	// }
// 	//
// 	// // 변환 연산자
// 	// operator int() const {
// 	// 	return *ptr;  // 포인터가 가리키는 값을 반환
// 	// }
// };

int main()
{
	//using namespace std::chrono_literals;

	//SetConsoleOutputCP(CP_UTF8);

	// for (ne::int_t i = 0; i < 1; i++)
	// {
	// 	auto value = NebulaJson::Parse(EXAMPLE);
	// 	if (!value->IsObject()) return 0;
	//
	// 	std::cout << "------------------------------" << std::endl;
	//
	// 	auto root = value->AsObject();
	//
	// 	for (const auto& key : value->ObjectKeys())
	// 	{
	// 		if (root[key]->IsBool())
	// 		{
	// 			if (root[key]->AsBool())
	// 			{
	// 				std::cout << "true" << std::endl;
	// 			}
	// 			else
	// 			{
	// 				std::cout << "false" << std::endl;
	// 			}
	// 		}
	// 		else if (root[key]->IsNumber())
	// 		{
	// 			std::cout << "Number " << root[key]->AsNumber() << std::endl;
	// 		}
	// 		else if (root[key]->IsPositiveNumber())
	// 		{
	// 			std::cout << "PositiveNumber " << root[key]->AsPositiveNumber() << std::endl;
	// 		}
	// 		else if (root[key]->IsLargeNumber())
	// 		{
	// 			std::cout << "LargeNumber " << root[key]->AsLargeNumber() << std::endl;
	// 		}
	// 		else if (root[key]->IsPositiveLargeNumber())
	// 		{
	// 			std::cout << "PositiveLargeNumber " << root[key]->AsPositiveLargeNumber() << std::endl;
	// 		}
	// 		else if (root[key]->IsReal())
	// 		{
	// 			std::cout << root[key]->AsReal() << std::endl;
	// 		}
	// 		else if (root[key]->IsString())
	// 		{
	// 			std::cout << *root[key]->AsString() << std::endl;
	// 		}
	// 		else if (root[key]->IsArray())
	// 		{
	// 			auto array = root[key]->AsArray();
	// 			for (unsigned int i = 0; i < array.size(); i++)
	// 			{
	// 				std::cout << "[" << i << "] " << array[i]->Stringify() << std::endl;
	// 			}
	// 		}
	// 		else if (root[key]->IsObject())
	// 		{
	// 			std::cout << root[key]->Stringify().c_str() << std::endl;
	// 		}
	// 	}
	//
	// 	std::cout << "------------------------------" << std::endl;
	// }

	{
		std::map<ne::string_t, std::shared_ptr<ne::protocol::JsonValue>> data;
		data["1"] = NE_V(false);
		data["2"] = NE_V(1);
		data["3"] = NE_V(4294967295);
		data["4"] = NE_V(9223372036854775807);
		data["5"] = NE_V(18446744073709551614ULL);
		data["6"] = NE_V(0.1f);
		data["7"] = NE_V("string");
		data["8"] = NE_V(std::string("string"));



		int a= 0;
		//NebulaJsonObject root;
		//root["test1"] = false;

		// root["test1"] = V(false);
		// root["test2"] = V(static_cast<ne::longlong_t>(2));
		// root["test3"] = V(0.12);
		// root["test4"] = V("Test");
		//
		// NebulaJsonObject object;
		// object["sub1"] = V(false);
		// object["sub2"] = V(static_cast<ne::longlong_t>(2));
		// object["sub3"] = V(0.12);
		// object["sub4"] = V("Test");
		//
		// root["test5"] = V(object);
		//
		// NebulaJsonArray array;
		// for (ne::longlong_t i = 0; i < 10; i++)
		// {
		// 	array.push_back(V(i));
		// }
		// root["test6"] = V(array);

		//std::cout << NebulaJson::Stringify(root) << std::endl;
	}


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
