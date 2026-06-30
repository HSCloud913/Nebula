//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include <cstddef>
#include "Coroutine/Task.h"
#include "Result.h"
#include "Error.h"
#include "NetworkType.h"
#include "Type.h"

BEGIN_NS(ne::io)
	// zero-copy 파일→소켓 전송.
	// Linux: sendfile(2), Windows: TransmitFile
	[[nodiscard]] ne::Task<ne::Result<std::size_t, ne::OsError>>
	SendFile(ne::network::socket_t _sockFd, int _fileFd, std::size_t _offset, std::size_t _size);
END_NS
