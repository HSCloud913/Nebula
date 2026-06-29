//
// Created by nebula on 24. 5. 29.
//

#ifndef FTPBASE_H
#define FTPBASE_H

#include "Type.h"

BEGIN_NS(ne::protocol::Ftp)
	enum class TransferType
	{
		Ascii,
		Binary
	};

	struct Entry
	{
		string_t name;
		bool_t isDirectory = false;
		ulonglong_t size = 0;

		[[nodiscard]]
		bool_t operator==(const Entry&) const noexcept = default;
	};

	struct Reply
	{
		int_t code = 0;
		string_t message;
	};

	struct PassiveAddress
	{
		string_t host;
		int_t port = 0;
	};

END_NS

#endif //FTPBASE_H
