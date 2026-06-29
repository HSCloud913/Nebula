//
// Created by nebula on 24. 5. 29.
//

#ifndef HTTPCLIENTRESPONSE_H
#define HTTPCLIENTRESPONSE_H

#include <functional>
#include <variant>
#include <array>

#include "Http/Interface/IParseHeaders.h"
#include "StringFormat.h"

#include "Socket.h"
#include "ConnectionPool.h"

BEGIN_NS(ne::protocol::Http::Client)
	class Parser;

	class ResponseRaw final
	{
		friend class Parser;

	public:
		explicit constexpr ResponseRaw(const std::span<const std::byte> _data, const std::size_t _dataStart) noexcept;

	public:
		std::span<const std::byte> data;
		std::size_t dataStart;

	private:
		bool_t isStopped;

	public:
		constexpr void_t Stop() noexcept
		{
			isStopped = true;
		}
	};

	class ResponseHeaders final :public IParseHeader
	{
		NEBULA_NON_COPYABLE_MOVABLE(ResponseHeaders)

	public:
		ResponseHeaders() = delete;
		~ResponseHeaders() = default;

		ResponseHeaders(const ResponseRaw _raw, const ResponseData& _responseData);

	public:
		ResponseRaw raw;

	private:
		const ResponseData& responseData;

	public:
		[[nodiscard]] virtual constexpr const ResponseData& GetParseData() const noexcept override;
		constexpr void_t Stop() noexcept
		{
			raw.Stop();
		}
	};

	class ResponseBody final :public IParseHeader
	{
		NEBULA_NON_COPYABLE_MOVABLE(ResponseBody)

	public:
		ResponseBody() = delete;
		~ResponseBody() = default;

		ResponseBody(const ResponseRaw _raw, const ResponseData& _responseData, std::span<const std::byte> _body, const std::optional<std::size_t> _bodySize);

	public:
		ResponseRaw raw;
		std::span<const std::byte> body;
		std::optional<std::size_t> bodySize;

	private:
		const ResponseData& responseData;

	public:
		[[nodiscard]] virtual constexpr const ResponseData& GetParseData() const noexcept override;
		constexpr void_t Stop() noexcept
		{
			raw.Stop();
		}
	};

	class Response final :public IParseHeader
	{
		NEBULA_NON_COPYABLE(Response)

	public:
		NEBULA_DEFAULT_MOVE(Response)

	public:
		Response() = default;
		~Response() = default;

		Response(ResponseData&& _responseData, string_t&& _url);

	private:
		ResponseData responseData;
		string_t url;

	public:
		[[nodiscard]] virtual constexpr const ResponseData& GetParseData() const noexcept override;

		[[nodiscard]] string_view_t GetUrl() const;
		[[nodiscard]] std::span<const std::byte> GetBody() const;
		[[nodiscard]] string_view_t GetBodyString() const;
	};

	struct ResponseCallbacks
	{
		std::function<void_t(ResponseRaw&)> handleRaw;
		std::function<void_t(ResponseHeaders&)> handleHeaders;
		std::function<void_t(ResponseBody&)> handleBody;
		std::function<void_t(Response&)> handle;
		std::function<void_t()> handleStop;
	};

	/*--------------------------------------------------*/

	class ChunkBodyParser final
	{
	private:
		static constexpr auto newline = string_view_t{ "\r\n" };

	private:
		std::vector<std::byte> result;
		bool_t isFinished = false;
		bool_t hasReturnedResult = false;
		std::size_t offset{};
		string_t chunkSizeBuffer;
		std::size_t chunkSize{};

	public:
		[[nodiscard]] std::span<const std::byte> GetResult() const;
		[[nodiscard]] std::optional<std::vector<std::byte>> Parse(const std::span<const std::byte> _data);

	private:
		[[nodiscard]] std::size_t ParseNext(const std::span<const std::byte> _data);
		[[nodiscard]] std::size_t ParseChunkBody(const std::span<const std::byte> _data);
		[[nodiscard]] std::size_t ParseSeparator(const std::span<const std::byte> _data);
		void_t ParseChunkSize(const string_view_t _string);
	};

	class Parser final
	{
	public:
		Parser() = default;
		Parser(ResponseCallbacks& _callbacks);

	private:
		std::vector<std::byte> buffer;
		ResponseData responseData;

		bool_t isFinish = false;
		std::size_t bodyStart{};
		std::size_t bodySize{};
		std::optional<ChunkBodyParser> chunkBodyParser;
		std::optional<ResponseCallbacks*> callbacks;

	public:
		[[nodiscard]] std::optional<ResponseData> Parse(const std::span<const std::byte> _data);

	private:
		void_t ParseHeader(const std::size_t _dataStart);
		void_t ParseRegularBody(const std::size_t _dataStart);
		void_t ParseChunkBody(const std::size_t _dataStart);
		void_t Stop();

		[[nodiscard]] std::optional<std::size_t> GetBodySize() const;
		[[nodiscard]] std::optional<string_view_t> ExtractHeaderString(const std::size_t _dataStart);
	};



	[[nodiscard]]
	inline bool_t IsKeepAliveEligible(const Response& _response)
	{
		if (const auto connection = _response.GetHeaderValue("Connection"))
		{
			return StringFormat::Lower(string_t(*connection)) != "close";
		}

		return _response.GetHttpVersion() == "HTTP/1.1";
	}

	template <std::size_t bufferSize = std::size_t{ 1 } << 12>
	[[nodiscard]]
	Response ReceiveResponse(NebulaHttpClientSocket&& _socket, string_t&& _url, ResponseCallbacks&& _callbacks, string_t _connectionKey)
	{
		auto hasStopped = false;
		_callbacks.handleStop = [&hasStopped]
		{
			hasStopped = true;
		};

		auto parser = Parser(_callbacks);
		auto buffer = std::array<std::byte, bufferSize>();

		while (!hasStopped)
		{
			if (const auto result = _socket.Read(buffer); result >= 0)
			{
				if (auto parseResult = parser.Parse(std::span(buffer).first(static_cast<std::size_t>(result))))
				{
					auto response = Response(std::move(*parseResult), std::move(_url));
					if (_callbacks.handle) _callbacks.handle(response);

					if (!_connectionKey.empty() && IsKeepAliveEligible(response) && _socket.IsAlive())
					{
						ConnectionPool::GetInstance().Release(_connectionKey, std::move(_socket));
					}

					return response;
				}
			}
			else
			{
				throw ne::Exception("[ReceiveResponse]", "The peer closed the connection unexpectedly");
			}
		}

		throw ne::Exception("[ReceiveResponse]", "Reached an unreachable code path, exiting");
	}

END_NS

#endif //HTTPCLIENTRESPONSE_H
