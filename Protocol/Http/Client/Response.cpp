//
// Created by nebula on 24. 5. 29.
//

#include "Response.h"
#include "StringFormat.h"



BEGIN_NS(ne::protocol::Http::Client)
	constexpr ResponseRaw::ResponseRaw(const std::span<const std::byte> _data, const std::size_t _dataStart) noexcept
		: data(_data)
		, dataStart(_dataStart)
		, isStopped(false)
	{
	}

	ResponseHeaders::ResponseHeaders(const ResponseRaw _raw, const ResponseData& _responseData)
		: raw(_raw)
		, responseData(_responseData)
	{
	}

	ResponseBody::ResponseBody(const ResponseRaw _raw, const ResponseData& _responseData, std::span<const std::byte> _body, const std::optional<std::size_t> _bodySize)
		: raw(_raw)
		, body(_body)
		, bodySize(_bodySize)
		, responseData(_responseData)
	{
	}

	Response::Response(ResponseData&& _responseData, string_t&& _url)
		: responseData(std::move(_responseData))
		, url(std::move(_url))
	{
	}



	constexpr const ResponseData& ResponseHeaders::GetParseData() const noexcept
	{
		return responseData;
	}

	constexpr const ResponseData& ResponseBody::GetParseData() const noexcept
	{
		return responseData;
	}

	constexpr const ResponseData& Response::GetParseData() const noexcept
	{
		return responseData;
	}

	string_view_t Response::GetUrl() const
	{
		return url;
	}

	std::span<const std::byte> Response::GetBody() const
	{
		return responseData.body;
	}

	string_view_t Response::GetBodyString() const
	{
		return DataToString(GetBody());
	}



	/*--------------------------------------------------*/


	[[nodiscard]]
	inline Method StringToMethod(string_view_t _method)
	{
		using enum Method;
		if (_method == "CONNECT") return CONNECT;
		if (_method == "DELETE") return DEL;
		if (_method == "GET") return GET;
		if (_method == "HEAD") return HEAD;
		if (_method == "OPTIONS") return OPTIONS;
		if (_method == "PATCH") return PATCH;
		if (_method == "POST") return POST;
		if (_method == "PUT") return PUT;
		if (_method == "TRACE") return TRACE;

		throw ne::Exception("[StringToMethod]", "Invalid http method");
	}

	[[nodiscard]]
	inline Status ParseStatus(const string_view_t _line)
	{
		auto status = Status{};

		const auto httpVersionEnd = _line.find(' ');
		if (httpVersionEnd == string_view_t::npos) return status;

		status.httpVersion = _line.substr(0, httpVersionEnd);
		auto cursor = httpVersionEnd + 1;

		const auto statusCodeEnd = _line.find(' ', cursor);
		if (statusCodeEnd == string_view_t::npos) return status;

		if (const auto statusCode = StringToInt<int_t>(_line.substr(cursor, statusCodeEnd)))
		{
			status.statusCode = static_cast<StatusCode>(*statusCode);
		}

		cursor = statusCodeEnd + 1;
		status.statusMessage = _line.substr(cursor, _line.find_last_not_of("\r\n ") + 1 - cursor);

		return status;
	}

	[[nodiscard]]
	constexpr std::optional<Header> ParseHeader(const string_view_t _line)
	{
		auto const colonPos = _line.find(':');
		if (colonPos == string_view_t::npos) return {};

		constexpr auto whitespace_characters = string_view_t{ " \t\r" };

		auto const valueStart = _line.find_first_not_of(whitespace_characters, colonPos + 1);
		if (valueStart == string_view_t::npos) return {};

		auto const valueEnd = _line.find_last_not_of(whitespace_characters);

		return Header{ .name = _line.substr(0, colonPos), .value = _line.substr(valueStart, valueEnd + 1 - valueStart) };
	}

	[[nodiscard]]
	inline std::vector<Header> ParseHeadersString(const string_view_t _headers)
	{
		auto result = std::vector<Header>();
		std::ranges::copy(_headers
						| std::views::split('\n') | std::views::transform(RangeToStringView)
						| std::views::transform(ParseHeader) | Filter | DereferenceMove,
						std::back_inserter(result)
		);

		return result;
	}



	/*--------------------------------------------------*/



	std::span<const std::byte> ChunkBodyParser::GetResult() const
	{
		return result;
	}

	std::optional<std::vector<std::byte>> ChunkBodyParser::Parse(const std::span<const std::byte> _data)
	{
		if (hasReturnedResult) return {};

		if (isFinished)
		{
			hasReturnedResult = true;
			return std::move(result);
		}

		auto cursor = offset;
		while (true)
		{
			if (cursor >= _data.size())
			{
				offset = cursor - _data.size();
				return {};
			}

			if (const auto cursorOffset = ParseNext(_data.subspan(cursor)))
			{
				cursor += cursorOffset;
			}
			else
			{
				hasReturnedResult = true;
				return std::move(result);
			}
		}
	}



	std::size_t ChunkBodyParser::ParseNext(const std::span<const std::byte> _data)
	{
		return (chunkSize) ? ParseChunkBody(_data) : ParseSeparator(_data);
	}

	std::size_t ChunkBodyParser::ParseChunkBody(const std::span<const std::byte> _data)
	{
		if (chunkSize > _data.size())
		{
			chunkSize -= _data.size();
			result.insert(result.end(), _data.begin(), _data.end());

			return _data.size();
		}

		auto data = _data.first(chunkSize);
		result.insert(result.end(), data.begin(), data.end());

		const auto end = chunkSize + newline.size();
		chunkSize = 0;

		return end;
	}

	std::size_t ChunkBodyParser::ParseSeparator(const std::span<const std::byte> _data)
	{
		const auto dataString = DataToString(_data);

		const auto pos = dataString.find(newline[0]);
		if (pos == string_view_t::npos)
		{
			chunkSizeBuffer += dataString;
			return _data.size();
		}

		if (chunkSizeBuffer.empty())
		{
			ParseChunkSize(dataString.substr(0, pos));
		}
		else
		{
			chunkSizeBuffer += dataString.substr(0, pos);
			ParseChunkSize(chunkSizeBuffer);
			chunkSizeBuffer.clear();
		}

		if (chunkSize == 0)
		{
			isFinished = true;
			return 0;
		}

		return pos + newline.size();
	}

	void_t ChunkBodyParser::ParseChunkSize(const string_view_t _string)
	{
		if (const auto result = StringToInt<std::size_t>(_string, 16); result)
		{
			chunkSize = *result;
		}
		else
		{
			throw ne::Exception("[BodyParser/ParseChunkSize]", std::format("Failed parsing http body chunk size (value: {})", _string));
		}
	}



	/*--------------------------------------------------*/



	Parser::Parser(ResponseCallbacks& _callbacks)
		: callbacks(&_callbacks)
	{
	}



	std::optional<ResponseData> Parser::Parse(const std::span<const std::byte> _data)
	{
		if (isFinish) return {};

		const auto dataStart = buffer.size();
		buffer.insert(buffer.end(), _data.begin(), _data.end());

		if (callbacks && (*callbacks)->handleRaw)
		{
			auto progress = ResponseRaw(buffer, dataStart);
			(*callbacks)->handleRaw(progress);
			if (progress.isStopped) Stop();
		}

		if (!isFinish && responseData.headersString.empty())
		{
			ParseHeader(dataStart);
		}

		if (!isFinish && !responseData.headersString.empty())
		{
			(chunkBodyParser) ? ParseChunkBody(dataStart) : ParseRegularBody(dataStart);
		}

		if (isFinish) return std::move(responseData);

		return {};
	}

	void_t Parser::ParseHeader(const std::size_t _dataStart)
	{
		if (const auto headersString = ExtractHeaderString(_dataStart))
		{
			responseData.headersString = *headersString;

			auto statusEnd = responseData.headersString.find_first_of("\r\n");
			if (statusEnd == string_view_t::npos)
			{
				statusEnd = responseData.headersString.size();
			}

			auto line = string_view_t(responseData.headersString).substr(0, statusEnd);
			if (line.starts_with("HTTP/"))
			{
				responseData.status = ParseStatus(line);
			}
			else
			{
				const auto methodEnd = line.find(' ');
				if (methodEnd != string_t::npos)
				{
					responseData.method = StringToMethod(line.substr(0, methodEnd));

					auto cursor = methodEnd + 1;
					const auto pathEnd = line.find(' ', cursor);
					if (pathEnd != string_t::npos)
					{
						string_t path(line.substr(cursor, pathEnd - cursor));

						const auto uriEnd = path.find('?');
						if (uriEnd == string_t::npos)
						{
							responseData.path = path;
							responseData.uri = "";
						}
						else
						{
							responseData.path = path.substr(0, uriEnd);
							responseData.uri = path.substr(uriEnd + 1);
						}

						cursor = pathEnd + 1;
						responseData.status.httpVersion = line.substr(cursor);
					}
				}
			}

			if (responseData.headersString.size() > statusEnd)
			{
				responseData.headers = ParseHeadersString(string_view_t(responseData.headersString).substr(statusEnd));
			}

			if (callbacks && (*callbacks)->handleHeaders)
			{
				auto progress = ResponseHeaders(ResponseRaw(buffer, _dataStart), responseData);
				(*callbacks)->handleHeaders(progress);
				if (progress.raw.isStopped) Stop();
			}

			if (const auto bodySize = GetBodySize())
			{
				this->bodySize = *bodySize;
			}
			else if (const auto transferEncoding = FindHeaderByName(responseData.headers, "transfer-encoding"); transferEncoding && transferEncoding->value == "chunked")
			{
				chunkBodyParser = ChunkBodyParser();
			}
		}
	}

	void_t Parser::Stop()
	{
		isFinish = true;
		if (callbacks && (*callbacks)->handleStop) (*callbacks)->handleStop();
	}



	void_t Parser::ParseRegularBody(const std::size_t _dataStart)
	{
		if (buffer.size() >= bodyStart + bodySize)
		{
			const auto bodyBegin = buffer.begin() + bodyStart;
			responseData.body = std::vector<std::byte>(bodyBegin, bodyBegin + bodySize);

			if (callbacks && (*callbacks)->handleBody)
			{
				auto progress = ResponseBody{
					ResponseRaw(buffer, _dataStart),
					responseData,
					chunkBodyParser->GetResult(),
					bodySize
				};
				(*callbacks)->handleBody(progress);
			}

			Stop();
		}
		else if (callbacks && (*callbacks)->handleBody)
		{
			auto progress = ResponseBody
			{
				ResponseRaw(buffer, _dataStart),
				responseData,
				std::span(buffer).subspan(bodyStart),
				bodySize
			};
			(*callbacks)->handleBody(progress);
			if (progress.raw.isStopped) Stop();
		}
	}

	void_t Parser::ParseChunkBody(const std::size_t _dataStart)
	{
		const auto bodyStart = std::max(_dataStart, this->bodyStart);
		if (auto body = chunkBodyParser->Parse(std::span(buffer).subspan(bodyStart)))
		{
			responseData.body = *std::move(body);

			if (callbacks && (*callbacks)->handleBody)
			{
				auto progress = ResponseBody
				{
					ResponseRaw(buffer, _dataStart),
					responseData,
					responseData.body,
					{}
				};
				(*callbacks)->handleBody(progress);
			}

			Stop();
		}
		else if (callbacks && (*callbacks)->handleBody)
		{
			auto progress = ResponseBody
			{
				ResponseRaw(buffer, _dataStart),
				responseData,
				chunkBodyParser->GetResult(),
				{}
			};
			(*callbacks)->handleBody(progress);
			if (progress.raw.isStopped) Stop();
		}
	}


	std::optional<std::size_t> Parser::GetBodySize() const
	{
		if (const auto contentLength = FindHeaderByName(responseData.headers, "content-length"))
		{
			if (const auto result = StringToInt<std::size_t>(contentLength->value))
			{
				return *result;
			}
		}

		return {};
	}

	std::optional<string_view_t> Parser::ExtractHeaderString(const std::size_t _dataStart)
	{
		for (const string_view_t emptyLine : { "\r\n\r\n", "\n\n" })
		{
			const auto start = _dataStart >= emptyLine.length() - 1 ? _dataStart - (emptyLine.length() - 1) : std::size_t{};
			const auto search = DataToString(std::span(buffer));

			if (const auto pos = search.find(emptyLine, start); pos != string_view_t::npos)
			{
				bodyStart = pos + emptyLine.length();
				return search.substr(0, pos);
			}
		}

		return {};
	}

END_NS
