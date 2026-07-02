//
// Created by hscloud on 25. 6. 30.
//

#include "SshStream.h"
#include "Stream/Awaitable.h"
#include <utility>

#ifdef NEBULA_WITH_LIBSSH2
#	include <libssh2.h>
#	if defined(_WIN32)
#		include <winsock2.h>
#	else
#		include <sys/socket.h>
#	endif
#endif



BEGIN_NS(ne::network)
#ifdef NEBULA_WITH_LIBSSH2
	static ne::OsError Ssh2Error(LIBSSH2_SESSION* _session, string_view_t _ctx) noexcept
	{
		char* msg = nullptr;
		const int code = libssh2_session_last_error(_session, &msg, nullptr, 0);

		auto error = ne::OsError{ static_cast<ne::ulong_t>(code), msg ? msg : "libssh2 error" };
		error.Context(_ctx);

		return error;
	}

	static ne::Task<ne::Result<void, ne::OsError>> WaitSocket(const socket_t _fd, ne::io::IIoEngine& _engine, LIBSSH2_SESSION* _session)
	{
		const int dir = libssh2_session_block_directions(_session);
		if (dir & LIBSSH2_SESSION_BLOCK_INBOUND)
		{
			if (auto result = co_await ne::io::RecvAwaitable{ _fd, _engine }; result.IsError()) co_return ne::Result<void, ne::OsError>::Error(std::move(result.Error()));
		}
		if (dir & LIBSSH2_SESSION_BLOCK_OUTBOUND)
		{
			if (auto result = co_await ne::io::SendAwaitable{ _fd, _engine }; result.IsError()) co_return ne::Result<void, ne::OsError>::Error(std::move(result.Error()));
		}

		co_return ne::Result<void, ne::OsError>::Ok();
	}



	SshStream::SshStream(Socket&& _socket, ne::io::IIoEngine& _engine, void* _session, void* _channel, ne::memory::IAllocator* _allocator) noexcept
		: socket(std::move(_socket))
		, engine(&_engine)
		, allocator(_allocator)
		, session(_session)
		, channel(_channel) {}

	SshStream::SshStream(SshStream&& _other) noexcept
		: socket(std::move(_other.socket))
		, engine(_other.engine)
		, sshConfig(std::move(_other.sshConfig))
		, allocator(std::exchange(_other.allocator, nullptr))
		, session(std::exchange(_other.session, nullptr))
		, channel(std::exchange(_other.channel, nullptr)) {}

	SshStream& SshStream::operator=(SshStream&& _other) noexcept
	{
		if (this != &_other)
		{
			(void)Close();
			socket    = std::move(_other.socket);
			engine    = _other.engine;
			sshConfig = std::move(_other.sshConfig);
			allocator = std::exchange(_other.allocator, nullptr);
			session   = std::exchange(_other.session, nullptr);
			channel   = std::exchange(_other.channel, nullptr);
		}

		return *this;
	}

	SshStream::~SshStream()
	{
		if (channel)
		{
			libssh2_channel_free(static_cast<LIBSSH2_CHANNEL*>(channel));
		}

		if (session)
		{
			libssh2_session_disconnect(static_cast<LIBSSH2_SESSION*>(session), "Bye");
			libssh2_session_free(static_cast<LIBSSH2_SESSION*>(session));
		}
	}



	ne::Task<ne::Result<SshStream, ne::OsError>> SshStream::Connect(Socket&& _socket, ne::io::IIoEngine& _engine, const SshConfig& _config, ne::memory::IAllocator* _allocator)
	{
		libssh2_init(0);

		LIBSSH2_SESSION* tempSession = libssh2_session_init();
		if (!tempSession)
			co_return ne::Result<SshStream, ne::OsError>::Error(
				ne::OsError{ 0, "libssh2_session_init failed" });

		libssh2_session_set_blocking(tempSession, 0);

		SshStream stream(std::move(_socket), _engine, tempSession, nullptr, _allocator);
		stream.sshConfig = _config;

		if (auto result = co_await stream.Handshake(); result.IsError()) co_return ne::Result<SshStream, ne::OsError>::Error(std::move(result.Error()));

		co_return ne::Result<SshStream, ne::OsError>::Ok(std::move(stream));
	}



	ne::Task<ne::Result<void, ne::OsError>> SshStream::Handshake()
	{
		auto* nativeSession = static_cast<LIBSSH2_SESSION*>(session);

		// SSH 계층 핸드셰이크
		while (true)
		{
			int sshResult = libssh2_session_handshake(nativeSession, socket.Handle());
			if (sshResult == 0) break;

			if (sshResult == LIBSSH2_ERROR_EAGAIN)
			{
				if (auto result = co_await WaitSocket(socket.Handle(), *engine, nativeSession); result.IsError()) co_return ne::Result<void, ne::OsError>::Error(std::move(result.Error()));
			}
			else
			{
				co_return ne::Result<void, ne::OsError>::Error(
					Ssh2Error(nativeSession, "[SshStream/Handshake/SessionHandshake]"));
			}
		}

		// 인증 — 공개키 우선, 없으면 비밀번호
		if (!sshConfig.privateKeyFile.empty())
		{
			while (true)
			{
				int sshResult = libssh2_userauth_publickey_fromfile(nativeSession, sshConfig.username.c_str(), sshConfig.publicKeyFile.empty() ? nullptr : sshConfig.publicKeyFile.c_str(), sshConfig.privateKeyFile.c_str(), nullptr);
				if (sshResult == 0) break;

				if (sshResult == LIBSSH2_ERROR_EAGAIN)
				{
					if (auto result = co_await WaitSocket(socket.Handle(), *engine, nativeSession); result.IsError()) co_return ne::Result<void, ne::OsError>::Error(std::move(result.Error()));
				}
				else
				{
					co_return ne::Result<void, ne::OsError>::Error(
						Ssh2Error(nativeSession, "[SshStream/Handshake/PubkeyAuth]"));
				}
			}
		}
		else if (!sshConfig.password.empty())
		{
			while (true)
			{
				int sshResult = libssh2_userauth_password(nativeSession, sshConfig.username.c_str(), sshConfig.password.c_str());
				if (sshResult == 0) break;

				if (sshResult == LIBSSH2_ERROR_EAGAIN)
				{
					if (auto result = co_await WaitSocket(socket.Handle(), *engine, nativeSession); result.IsError()) co_return ne::Result<void, ne::OsError>::Error(std::move(result.Error()));
				}
				else
				{
					co_return ne::Result<void, ne::OsError>::Error(
						Ssh2Error(nativeSession, "[SshStream/Handshake/PasswordAuth]"));
				}
			}
		}

		LIBSSH2_CHANNEL* tempChannel = nullptr;
		while (true)
		{
			tempChannel = libssh2_channel_open_session(nativeSession);
			if (tempChannel) break;

			if (libssh2_session_last_errno(nativeSession) == LIBSSH2_ERROR_EAGAIN)
			{
				if (auto result = co_await WaitSocket(socket.Handle(), *engine, nativeSession); result.IsError()) co_return ne::Result<void, ne::OsError>::Error(std::move(result.Error()));
			}
			else
			{
				co_return ne::Result<void, ne::OsError>::Error(
					Ssh2Error(nativeSession, "[SshStream/Handshake/OpenChannel]"));
			}
		}

		if (!sshConfig.command.empty())
		{
			while (true)
			{
				int sshResult = libssh2_channel_exec(tempChannel, sshConfig.command.c_str());
				if (sshResult == 0) break;

				if (sshResult == LIBSSH2_ERROR_EAGAIN)
				{
					if (auto result = co_await WaitSocket(socket.Handle(), *engine, nativeSession); result.IsError())
					{
						libssh2_channel_free(tempChannel);
						co_return ne::Result<void, ne::OsError>::Error(std::move(result.Error()));
					}
				}
				else
				{
					auto e = Ssh2Error(nativeSession, "[SshStream/Handshake/Exec]");
					libssh2_channel_free(tempChannel);
					co_return ne::Result<void, ne::OsError>::Error(std::move(e));
				}
			}
		}
		else
		{
			bool isPtyOk = false;
			while (!isPtyOk)
			{
				int sshResult = libssh2_channel_request_pty(tempChannel, "xterm");
				if (sshResult == 0)
				{
					isPtyOk = true;
					break;
				}

				if (sshResult == LIBSSH2_ERROR_EAGAIN)
				{
					if (auto result = co_await WaitSocket(socket.Handle(), *engine, nativeSession); result.IsError())
					{
						libssh2_channel_free(tempChannel);
						co_return ne::Result<void, ne::OsError>::Error(std::move(result.Error()));
					}
				}
				else
				{
					break;
				}
			}

			while (true)
			{
				int sshResult = libssh2_channel_shell(tempChannel);
				if (sshResult == 0) break;

				if (sshResult == LIBSSH2_ERROR_EAGAIN)
				{
					if (auto result = co_await WaitSocket(socket.Handle(), *engine, nativeSession); result.IsError())
					{
						libssh2_channel_free(tempChannel);
						co_return ne::Result<void, ne::OsError>::Error(std::move(result.Error()));
					}
				}
				else
				{
					auto sshError = Ssh2Error(nativeSession, "[SshStream/Handshake/Shell]");
					libssh2_channel_free(tempChannel);

					co_return ne::Result<void, ne::OsError>::Error(std::move(sshError));
				}
			}
		}

		channel = tempChannel;

		co_return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Task<ne::Result<std::size_t, ne::OsError>> SshStream::Send(BufferView _data)
	{
		if (!IsOpen()) co_return ne::Result<std::size_t, ne::OsError>::Error(ne::OsError{ 0, "SSH stream closed" });

		auto* nativeSession = static_cast<LIBSSH2_SESSION*>(session);
		auto* nativeChannel = static_cast<LIBSSH2_CHANNEL*>(channel);
		const auto dataSpan = _data.Span();
		std::size_t totalSent = 0;

		while (totalSent < dataSpan.size())
		{
			const auto bytes = libssh2_channel_write(nativeChannel, reinterpret_cast<const char*>(dataSpan.data() + totalSent), dataSpan.size() - totalSent);
			if (bytes >= 0)
			{
				totalSent += static_cast<std::size_t>(bytes);
			}
			else if (bytes == LIBSSH2_ERROR_EAGAIN)
			{
				if (auto result = co_await WaitSocket(socket.Handle(), *engine, nativeSession); result.IsError()) co_return ne::Result<std::size_t, ne::OsError>::Error(std::move(result.Error()));
			}
			else
			{
				co_return ne::Result<std::size_t, ne::OsError>::Error(
					Ssh2Error(nativeSession, "[SshStream/Send]"));
			}
		}

		co_return ne::Result<std::size_t, ne::OsError>::Ok(totalSent);
	}

	ne::Task<ne::Result<std::size_t, ne::OsError>> SshStream::Sendv(const BufferChain& _chain)
	{
		if (!IsOpen()) co_return ne::Result<std::size_t, ne::OsError>::Error(ne::OsError{ 0, "SSH stream closed" });
		if (!allocator) co_return ne::Result<std::size_t, ne::OsError>::Error(ne::OsError{ 0, "no allocator for SshStream::Sendv" });

		const auto flat = _chain.Flatten(*allocator);
		if (!flat.IsValid())
			co_return ne::Result<std::size_t, ne::OsError>::Error(ne::OsError{ 0, "BufferChain::Flatten failed" });

		auto result = co_await Send(flat);
		flat.owner->Release();

		co_return result;
	}

	ne::Task<ne::Result<std::size_t, ne::OsError>> SshStream::Receive(BufferView _data)
	{
		if (!IsOpen()) co_return ne::Result<std::size_t, ne::OsError>::Error(ne::OsError{ 0, "SSH stream closed" });

		auto* nativeSession = static_cast<LIBSSH2_SESSION*>(session);
		auto* nativeChannel = static_cast<LIBSSH2_CHANNEL*>(channel);

		while (true)
		{
			const auto bytes = libssh2_channel_read(nativeChannel, reinterpret_cast<char*>(_data.ptr), _data.length);
			if (bytes > 0)
			{
				co_return ne::Result<std::size_t, ne::OsError>::Ok(static_cast<std::size_t>(bytes));
			}
			if (bytes == 0 || libssh2_channel_eof(nativeChannel))
			{
				co_return ne::Result<std::size_t, ne::OsError>::Ok(0);
			}

			if (bytes == LIBSSH2_ERROR_EAGAIN)
			{
				if (auto result = co_await WaitSocket(socket.Handle(), *engine, nativeSession); result.IsError()) co_return ne::Result<std::size_t, ne::OsError>::Error(std::move(result.Error()));
			}
			else
			{
				co_return ne::Result<std::size_t, ne::OsError>::Error(
					Ssh2Error(nativeSession, "[SshStream/Receive]"));
			}
		}
	}

	ne::Task<ne::Result<void, ne::OsError>> SshStream::Shutdown()
	{
		(void)Close();
		co_return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<void, ne::OsError> SshStream::Close()
	{
		if (!IsOpen()) return ne::Result<void, ne::OsError>::Ok();

		auto* nativeSession = static_cast<LIBSSH2_SESSION*>(session);
		auto* nativeChannel = static_cast<LIBSSH2_CHANNEL*>(channel);

		libssh2_channel_send_eof(nativeChannel);
		libssh2_channel_free(nativeChannel);
		channel = nullptr;

		libssh2_session_disconnect(nativeSession, "Bye");
		libssh2_session_free(nativeSession);
		session = nullptr;

		(void)engine->Unwatch(socket.Handle());
		[[maybe_unused]] auto closing = std::move(socket);

		return ne::Result<void, ne::OsError>::Ok();
	}

#else // NEBULA_WITH_LIBSSH2 not defined
	static ne::OsError NoLibssh2(string_view_t _ctx)
	{
		auto err = ne::OsError{ 0, "built without libssh2 (define NEBULA_WITH_LIBSSH2)" };
		err.Context(_ctx);
		return err;
	}



	SshStream::SshStream(Socket&&, ne::io::IIoEngine&, void*, void*, ne::memory::IAllocator*) noexcept {}
	SshStream::SshStream(SshStream&&) noexcept = default;
	SshStream& SshStream::operator=(SshStream&&) noexcept = default;
	SshStream::~SshStream() = default;



	ne::Task<ne::Result<SshStream, ne::OsError>> SshStream::Connect(Socket&&, ne::io::IIoEngine&, const SshConfig&, ne::memory::IAllocator*)
	{
		co_return ne::Result<SshStream, ne::OsError>::Error(NoLibssh2("[SshStream/Connect]"));
	}

	ne::Task<ne::Result<void, ne::OsError>> SshStream::Handshake()
	{
		co_return ne::Result<void, ne::OsError>::Error(NoLibssh2("[SshStream/Handshake]"));
	}

	ne::Task<ne::Result<std::size_t, ne::OsError>> SshStream::Send(BufferView)
	{
		co_return ne::Result<std::size_t, ne::OsError>::Error(NoLibssh2("[SshStream/Send]"));
	}

	ne::Task<ne::Result<std::size_t, ne::OsError>> SshStream::Receive(BufferView)
	{
		co_return ne::Result<std::size_t, ne::OsError>::Error(NoLibssh2("[SshStream/Receive]"));
	}

	ne::Task<ne::Result<std::size_t, ne::OsError>> SshStream::Sendv(const BufferChain&)
	{
		co_return ne::Result<std::size_t, ne::OsError>::Error(NoLibssh2("[SshStream/Sendv]"));
	}

	ne::Task<ne::Result<void, ne::OsError>> SshStream::Shutdown()
	{
		co_return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<void, ne::OsError> SshStream::Close()
	{
		return ne::Result<void, ne::OsError>::Ok();
	}

#endif // NEBULA_WITH_LIBSSH2

END_NS
