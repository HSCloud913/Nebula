#include <iostream>
#include <fstream>

#include "Base64/Base64.h"
#include "Hash/Factory/HashFactory.h"

int main()
{
	//auto hash = NebulaHash::Create(NebulaHashType::SHA2_256);

	//auto get = hash->GetHashFromString("asdflkjhwioruq2troui7qt23ri8712t3498");

	auto result = NebulaHash::Create(NebulaHashType::SHA1)->GetHashFromString("asdflkjhwioruq2troui7qt23ri8712t3498");
	auto result1 = NebulaHash::Create(NebulaHashType::SHA2_224)->GetHashFromString("asdflkjhwioruq2troui7qt23ri8712t3498");
	auto result2 = NebulaHash::Create(NebulaHashType::SHA2_256)->GetHashFromString("asdflkjhwioruq2troui7qt23ri8712t3498");
	auto result3 = NebulaHash::Create(NebulaHashType::SHA2_384)->GetHashFromString("asdflkjhwioruq2troui7qt23ri8712t3498");
	auto result4 = NebulaHash::Create(NebulaHashType::SHA2_512)->GetHashFromString("asdflkjhwioruq2troui7qt23ri8712t3498");
	auto result5 = NebulaHash::Create(NebulaHashType::SHA3_224)->GetHashFromString("asdflkjhwioruq2troui7qt23ri8712t3498");
	auto result6 = NebulaHash::Create(NebulaHashType::SHA3_256)->GetHashFromString("asdflkjhwioruq2troui7qt23ri8712t3498");
	auto result7 = NebulaHash::Create(NebulaHashType::SHA3_384)->GetHashFromString("asdflkjhwioruq2troui7qt23ri8712t3498");
	auto result8 = NebulaHash::Create(NebulaHashType::SHA3_512)->GetHashFromString("asdflkjhwioruq2troui7qt23ri8712t3498");

	auto a = NebulaBase64::Encode("123123123");
	auto b = NebulaBase64::Decode(std::move(a));

	return 0;
}