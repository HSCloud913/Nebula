//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include "Stream/IStream.h"
#include "Coroutine/Task.h"
#include "Result.h"
#include "Error.h"
#include "Type.h"

BEGIN_NS(ne::network::ftp)
	enum class TransferType : uint8_t
	{
		Ascii,
		Binary
	};

	struct FtpEntry
	{
		string_t name;
		bool_t isDirectory{ false };
		uint64_t size{ 0 };
	};

	struct FtpReply
	{
		int_t code{ 0 };
		string_t message;
	};

	struct FtpConfig
	{
		string_t username;
		string_t password;
	};

	// Factory invoked per data-channel connection (PASV host/port → async IStream).
	using DataStreamFactory_t = std::function<
		ne::Task<ne::Result<std::unique_ptr<ne::network::IStream>, ne::OsError>>(
			string_view_t, uint16_t)>;

END_NS
