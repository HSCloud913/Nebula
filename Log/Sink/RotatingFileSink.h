//
// Created by hscloud on 26. 7. 23.
//

#pragma once
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include "Base/Type.h"
#include "Log/LogRecord.h"
#include "Log/Sink/ISink.h"

namespace ne::log
{
	/**
	 * @class RotatingFileSink
	 * @brief 크기 기반 회전 파일 sink.
	 *
	 * 현재 파일이 _maxBytes 를 넘을 참이면 base.ext → base.1.ext → … 로 밀고 현재 파일을 새로 시작하며,
	 * 최대 _maxFiles 개(현재 파일 포함)를 유지한다(초과분은 삭제).
	 */
	class RotatingFileSink final : public ISink
	{
	public:
		RotatingFileSink(string_t _path, const std::size_t _maxBytes, const std::size_t _maxFiles)
			: path(std::move(_path))
			, maxBytes(_maxBytes == 0 ? 1 : _maxBytes)
			, maxFiles(_maxFiles == 0 ? 1 : _maxFiles) { Open(); }

	private:
		string_t path;
		std::size_t maxBytes;
		std::size_t maxFiles;
		std::size_t written{ 0 };
		std::ofstream os;

	public:
		virtual void_t Write(const LogRecord& _record) override
		{
			if (!os.is_open()) return;

			const string_t line = FormatRecord(_record) + "\n";
			if (written > 0 && written + line.size() > maxBytes) Rotate();

			os << line;
			written += line.size();
		}

		virtual void_t Flush() override { if (os.is_open()) os.flush(); }

	private:
		void_t Open()
		{
			namespace fs = std::filesystem;
			try
			{
				const fs::path p(path);
				if (p.has_parent_path() && !p.parent_path().empty() && !fs::exists(p.parent_path())) fs::create_directories(p.parent_path());

				os.open(path, std::ios_base::out | std::ios_base::app | std::ios_base::binary);

				std::error_code ec;
				written = fs::exists(p, ec) ? static_cast<std::size_t>(fs::file_size(p, ec)) : 0;
			}
			catch (const fs::filesystem_error&) {}
		}

		void_t Rotate()
		{
			namespace fs = std::filesystem;
			os.close();

			std::error_code ec;
			if (maxFiles > 1)
			{
				fs::remove(RotatedName(maxFiles - 1), ec);
				for (std::size_t i = maxFiles - 1; i > 1; --i) fs::rename(RotatedName(i - 1), RotatedName(i), ec);
				fs::rename(path, RotatedName(1), ec);
			}

			written = 0;
			os.open(path, std::ios_base::out | std::ios_base::trunc | std::ios_base::binary);
		}

		[[nodiscard]] string_t RotatedName(const std::size_t _index) const
		{
			const std::filesystem::path p(path);
			return (p.parent_path() / p.stem()).string() + "." + std::to_string(_index) + p.extension().string();
		}
	};
}
