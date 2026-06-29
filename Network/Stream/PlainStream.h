//
// Created by hscloud on 25. 6. 29.
//

#pragma once
#include <cstddef>
#include <span>
#include "IStream.h"
#include "Socket/Socket.h"
#include "IoEngine/IIoEngine.h"

BEGIN_NS(ne::network)
	// 암호화 없는 TCP 스트림.
	// Socket + IIoEngine 을 받아 코루틴 기반 비동기 Send/Recv 제공.
	class PlainStream final :public IStream
	{
	public:
		NEBULA_NON_COPYABLE(PlainStream)

	private:
		explicit PlainStream(Socket&& _socket, IIoEngine& _engine) noexcept;

	public:
		PlainStream(PlainStream&& _other) noexcept;
		PlainStream& operator=(PlainStream&& _other) noexcept;
		~PlainStream() override = default;

	public:
		[[nodiscard]] static ne::Result<PlainStream, ne::OsError> Create(Socket&& _socket, IIoEngine& _engine) noexcept;

	private:
		Socket socket;
		IIoEngine* engine;

	public:
		virtual ne::Task<ne::Result<std::size_t, ne::OsError>> Send(std::span<const ne::byte_t> _data) override;
		virtual ne::Task<ne::Result<std::size_t, ne::OsError>> Receive(std::span<ne::byte_t> _data) override;
		virtual ne::Result<void, ne::OsError> Close() override;

	public:
		[[nodiscard]] bool_t IsOpen() const noexcept override { return socket.IsValid(); }
	};

END_NS
