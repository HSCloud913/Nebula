//
// Created by hscloud on 26. 7. 23.
//

#pragma once
#include <filesystem>
#include <fstream>
#include "Base/Type.h"
#include "Log/LogRecord.h"
#include "Log/Sink/ISink.h"

namespace ne::log
{
	/** @brief 단일 파일 sink(append 모드). 부모 디렉터리가 없으면 생성한다. */
	class FileSink final : public ISink
	{
	public:
		explicit FileSink(const string_t& _path)
		{
			namespace fs = std::filesystem;
			try
			{
				if (const fs::path path(_path); path.has_parent_path() && !path.parent_path().empty() && !fs::exists(path.parent_path())) fs::create_directories(path.parent_path());

				os.open(_path, std::ios_base::out | std::ios_base::app | std::ios_base::binary);
			}
			catch (const fs::filesystem_error&) {}
		}

	private:
		std::ofstream os;

	public:
		virtual void_t Write(const LogRecord& _record) override { if (os.is_open()) os << FormatRecord(_record) << '\n'; }
		virtual void_t Flush() override { if (os.is_open()) os.flush(); }

	public:
		[[nodiscard]] bool_t IsOpen() const { return os.is_open(); }
	};
}
