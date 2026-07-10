//
// Created by csw on 26. 6. 30..
//

#include <cstdint>
#include "Base/Type.h"

BEGIN_NS (ne::network::sftp)

struct Entry
{
	string_t name;
	bool_t isDirectory{ false };
	uint64_t size{ 0 };
};

END_NS
