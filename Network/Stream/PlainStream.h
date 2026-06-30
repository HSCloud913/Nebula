//
// Created by hscloud on 25. 6. 29.
//

#pragma once
#include <cstddef>
#include "IStream.h"
#include "Socket/Socket.h"
#include "IoEngine/IIoEngine.h"

BEGIN_NS(ne::network)
	// 암호화 없는 TCP 스트림.
	// Socket + IIoEngine 을 받아 코루틴 기반 비동기 Send/Recv 제공.
	class PlainStream final :public IStream
	{
	public:
		PlainStream(PlainStream&& _other) noexcept;
		PlainStream& operator=(PlainStream&& _other) noexcept;
		virtual ~PlainStream() override = default;
		NEBULA_NON_COPYABLE(PlainStream)

	private:
		explicit PlainStream(Socket&& _socket, IIoEngine& _engine, ne::memory::IAllocator* _allocator) noexcept;

	public:
		[[nodiscard]] static ne::Result<PlainStream, ne::OsError> Create(Socket&& _socket, IIoEngine& _engine, ne::memory::IAllocator* _allocator = nullptr) noexcept;

	private:
		Socket socket;
		IIoEngine* engine;
		ne::memory::IAllocator* allocator{ nullptr };

	public:
		virtual ne::Task<ne::Result<void, ne::OsError>> Handshake() override;
		virtual Task<Result<std::size_t, OsError>> Send(BufferView _data) override;
		virtual Task<Result<std::size_t, OsError>> Sendv(const BufferChain& _chain) override;
		virtual Task<Result<std::size_t, OsError>> Receive(BufferView _data) override;
		virtual ne::Task<ne::Result<void, ne::OsError>> Shutdown() override;
		virtual Result<void, OsError> Close() override;

	public:
		[[nodiscard]] virtual bool_t IsOpen() const noexcept override { return socket.IsValid(); }
	};

END_NS
