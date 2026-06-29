//
// Created by nebula on 24. 5. 29.
//

#ifndef SFTPBASE_H
#define SFTPBASE_H

#include "Type.h"

BEGIN_NS(ne::protocol::Sftp)
	struct Entry
	{
		string_t name;
		bool_t isDirectory = false;
		ulonglong_t size = 0;

		[[nodiscard]]
		bool_t operator==(const Entry&) const noexcept = default;
	};

END_NS

#endif //SFTPBASE_H
