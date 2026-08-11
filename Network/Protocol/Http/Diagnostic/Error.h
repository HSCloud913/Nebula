//
// Created by hscloud on 26. 7. 20.
//

#pragma once
#include "Base/Type.h"
#include "Base/Error.h"
#include "Base/Result.h"
#include "Io/Diagnostic/Error.h"

namespace ne::network::http
{
	/**
	 * @class HttpErrorKind
	 * @brief HTTP 계층 에러 분류 열거형.
	 */
	enum class HttpErrorKind : byte_t
	{
		TRANSPORT,             // 하위 IStream(Plain/Tls) 에서 올라온 실패
		MALFORMED_MESSAGE,     // 요청/상태 라인, 헤더 형식 오류
		HEADER_TOO_LARGE,
		BODY_TOO_LARGE,
		UNSUPPORTED_VERSION,
		CONNECTION_CLOSED,     // 피어가 메시지 도중 연결을 닫음
		TIMEOUT,               // 요청이 지정한 데드라인 안에 끝나지 않음
	};

	/**
	 * @class HttpError
	 * @brief ne::Error 를 상속한 HTTP 계층 전용 에러 타입입니다(Io/IoError.h 와 동일한 패턴).
	 *
	 * HttpErrorKind 로 에러 종류를 분류하며, 하위 IStream 실패(ne::io::IoError)를 감쌀 경우
	 * TRANSPORT 로 분류하고 원본 메시지와 OS 에러 코드(있다면)를 보존합니다. Context()/What() 및
	 * HttpResult<T> 와 맞물려 값 기반 에러 전파에 쓰입니다.
	 */
	class HttpError final :public ne::Error
	{
	public:
		explicit HttpError(const HttpErrorKind _kind, const string_view_t _message = {})
			: Error(_message.empty() ? DefaultMessage(_kind) : string_t{ _message })
			, kind(_kind) {}

		explicit HttpError(ne::io::IoError&& _ioError)
			: Error(_ioError.What())
			, kind(HttpErrorKind::TRANSPORT)
			, osCode(_ioError.Code()) {}

	private:
		HttpErrorKind kind;
		ne::ulong_t osCode{ 0 }; // 하위 IoError 가 OS 실패에서 왔다면 그 코드(errno/WSA*), 아니면 0

	public:
		HttpError& Context(const string_view_t _context)
		{
			Error::Context(_context);
			return *this;
		}

		[[nodiscard]] HttpErrorKind Kind() const noexcept { return kind; }

		/** @brief 원인이 된 OS 에러 코드(errno/WSA*). TRANSPORT 이외이거나 OS 원인이 없으면 0. */
		[[nodiscard]] ne::ulong_t OsCode() const noexcept { return osCode; }

	private:
		[[nodiscard]] static string_t DefaultMessage(const HttpErrorKind _kind)
		{
			switch (_kind)
			{
				case HttpErrorKind::TRANSPORT:
					return "transport failure";
				case HttpErrorKind::MALFORMED_MESSAGE:
					return "malformed HTTP message";
				case HttpErrorKind::HEADER_TOO_LARGE:
					return "header block too large";
				case HttpErrorKind::BODY_TOO_LARGE:
					return "body too large";
				case HttpErrorKind::UNSUPPORTED_VERSION:
					return "unsupported HTTP version";
				case HttpErrorKind::CONNECTION_CLOSED:
					return "connection closed mid-message";
				case HttpErrorKind::TIMEOUT:
					return "request timed out";
			}

			return "unknown http error";
		}
	};

	template <typename T>
	using HttpResult = ne::Result<T, HttpError>;
}
