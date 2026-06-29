//
// Created by hscloud on 25. 6. 30.
//

#include "SshStream.h"
#include "Coroutine/Awaitable.h"
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
		int code = libssh2_session_last_error(_session, &msg, nullptr, 0);
		auto err = ne::OsError{ static_cast<ne::ulong_t>(code), msg ? msg : "libssh2 error" };
		err.Context(_ctx);
		return err;
	}

	static ne::Task<ne::Result<void, ne::OsError>> WaitSocket(
		socket_t _fd, IIoEngine& _engine, LIBSSH2_SESSION* _session)
	{
		const int dir = libssh2_session_block_directions(_session);
		if (dir & LIBSSH2_SESSION_BLOCK_INBOUND)
		{
			if (auto r = co_await RecvAwaitable{ _fd, _engine }; r.IsError()) co_return ne::Result<void, ne::OsError>::Error(std::move(r.Error()));
		}
		if (dir & LIBSSH2_SESSION_BLOCK_OUTBOUND)
		{
			if (auto r = co_await SendAwaitable{ _fd, _engine }; r.IsError()) co_return ne::Result<void, ne::OsError>::Error(std::move(r.Error()));
		}
		co_return ne::Result<void, ne::OsError>::Ok();
	}



	SshStream::SshStream(Socket&& _socket, IIoEngine& _engine, void* _session, void* _channel) noexcept
		: socket(std::move(_socket))
		, engine(&_engine)
		, session(_session)
		, channel(_channel) {}

	SshStream::SshStream(SshStream&& _other) noexcept
		: socket(std::move(_other.socket))
		, engine(_other.engine)
		, session(std::exchange(_other.session, nullptr))
		, channel(std::exchange(_other.channel, nullptr)) {}

	SshStream& SshStream::operator=(SshStream&& _other) noexcept
	{
		if (this != &_other)
		{
			(void)Close();
			socket = std::move(_other.socket);
			engine = _other.engine;
			session = std::exchange(_other.session, nullptr);
			channel = std::exchange(_other.channel, nullptr);
		}

		return *this;
	}

	SshStream::~SshStream()
	{
		if (channel) libssh2_channel_free(static_cast<LIBSSH2_CHANNEL*>(channel));
		if (session)
		{
			libssh2_session_disconnect(static_cast<LIBSSH2_SESSION*>(session), "Bye");
			libssh2_session_free(static_cast<LIBSSH2_SESSION*>(session));
		}
	}



	ne::Task<ne::Result<SshStream, ne::OsError>> SshStream::Connect(Socket&& _socket, IIoEngine& _engine, const SshConfig& _config)
	{
		libssh2_init(0);

		LIBSSH2_SESSION* sess = libssh2_session_init();
		if (!sess)
			co_return ne::Result<SshStream, ne::OsError>::Error(
				ne::OsError{ 0, "libssh2_session_init failed" });

		libssh2_session_set_blocking(sess, 0);

		// SSH 계층 핸드셰이크
		while (true)
		{
			int rc = libssh2_session_handshake(sess,
												static_cast<libssh2_socket_t>(_socket.Handle()));
			if (rc == 0) break;
			if (rc == LIBSSH2_ERROR_EAGAIN)
			{
				if (auto r = co_await WaitSocket(_socket.Handle(), _engine, sess); r.IsError())
				{
					libssh2_session_free(sess);
					co_return ne::Result<SshStream, ne::OsError>::Error(std::move(r.Error()));
				}
			}
			else
			{
				auto e = Ssh2Error(sess, "[SshStream/Connect/Handshake]");
				libssh2_session_free(sess);
				co_return ne::Result<SshStream, ne::OsError>::Error(std::move(e));
			}
		}

		// 인증 — 공개키 우선, 없으면 비밀번호
		if (!_config.privateKeyFile.empty())
		{
			while (true)
			{
				int rc = libssh2_userauth_publickey_fromfile(
					sess,
					_config.username.c_str(),
					_config.publicKeyFile.empty() ? nullptr : _config.publicKeyFile.c_str(),
					_config.privateKeyFile.c_str(),
					nullptr);
				if (rc == 0) break;
				if (rc == LIBSSH2_ERROR_EAGAIN)
				{
					if (auto r = co_await WaitSocket(_socket.Handle(), _engine, sess); r.IsError())
					{
						libssh2_session_free(sess);
						co_return ne::Result<SshStream, ne::OsError>::Error(std::move(r.Error()));
					}
				}
				else
				{
					auto e = Ssh2Error(sess, "[SshStream/Connect/PubkeyAuth]");
					libssh2_session_free(sess);
					co_return ne::Result<SshStream, ne::OsError>::Error(std::move(e));
				}
			}
		}
		else if (!_config.password.empty())
		{
			while (true)
			{
				int rc = libssh2_userauth_password(sess,
													_config.username.c_str(), _config.password.c_str());
				if (rc == 0) break;
				if (rc == LIBSSH2_ERROR_EAGAIN)
				{
					if (auto r = co_await WaitSocket(_socket.Handle(), _engine, sess); r.IsError())
					{
						libssh2_session_free(sess);
						co_return ne::Result<SshStream, ne::OsError>::Error(std::move(r.Error()));
					}
				}
				else
				{
					auto e = Ssh2Error(sess, "[SshStream/Connect/PasswordAuth]");
					libssh2_session_free(sess);
					co_return ne::Result<SshStream, ne::OsError>::Error(std::move(e));
				}
			}
		}

		// 채널 열기
		LIBSSH2_CHANNEL* ch = nullptr;
		while (true)
		{
			ch = libssh2_channel_open_session(sess);
			if (ch) break;
			if (libssh2_session_last_errno(sess) == LIBSSH2_ERROR_EAGAIN)
			{
				if (auto r = co_await WaitSocket(_socket.Handle(), _engine, sess); r.IsError())
				{
					libssh2_session_free(sess);
					co_return ne::Result<SshStream, ne::OsError>::Error(std::move(r.Error()));
				}
			}
			else
			{
				auto e = Ssh2Error(sess, "[SshStream/Connect/OpenChannel]");
				libssh2_session_free(sess);
				co_return ne::Result<SshStream, ne::OsError>::Error(std::move(e));
			}
		}

		// exec 또는 shell 요청
		if (!_config.command.empty())
		{
			while (true)
			{
				int rc = libssh2_channel_exec(ch, _config.command.c_str());
				if (rc == 0) break;
				if (rc == LIBSSH2_ERROR_EAGAIN)
				{
					if (auto r = co_await WaitSocket(_socket.Handle(), _engine, sess); r.IsError())
					{
						libssh2_channel_free(ch);
						libssh2_session_free(sess);
						co_return ne::Result<SshStream, ne::OsError>::Error(std::move(r.Error()));
					}
				}
				else
				{
					auto e = Ssh2Error(sess, "[SshStream/Connect/Exec]");
					libssh2_channel_free(ch);
					libssh2_session_free(sess);
					co_return ne::Result<SshStream, ne::OsError>::Error(std::move(e));
				}
			}
		}
		else
		{
			// pty 요청 후 shell (실패해도 shell은 시도)
			bool ptyOk = false;
			while (!ptyOk)
			{
				int rc = libssh2_channel_request_pty(ch, "xterm");
				if (rc == 0)
				{
					ptyOk = true;
					break;
				}
				if (rc == LIBSSH2_ERROR_EAGAIN)
				{
					if (auto r = co_await WaitSocket(_socket.Handle(), _engine, sess); r.IsError())
					{
						libssh2_channel_free(ch);
						libssh2_session_free(sess);
						co_return ne::Result<SshStream, ne::OsError>::Error(std::move(r.Error()));
					}
				}
				else break; // pty 실패 무시, shell은 시도
			}

			while (true)
			{
				int rc = libssh2_channel_shell(ch);
				if (rc == 0) break;
				if (rc == LIBSSH2_ERROR_EAGAIN)
				{
					if (auto r = co_await WaitSocket(_socket.Handle(), _engine, sess); r.IsError())
					{
						libssh2_channel_free(ch);
						libssh2_session_free(sess);
						co_return ne::Result<SshStream, ne::OsError>::Error(std::move(r.Error()));
					}
				}
				else
				{
					auto e = Ssh2Error(sess, "[SshStream/Connect/Shell]");
					libssh2_channel_free(ch);
					libssh2_session_free(sess);
					co_return ne::Result<SshStream, ne::OsError>::Error(std::move(e));
				}
			}
		}

		co_return ne::Result<SshStream, ne::OsError>::Ok(
			SshStream{ std::move(_socket), _engine, sess, ch });
	}



	ne::Task<ne::Result<std::size_t, ne::OsError>> SshStream::Send(std::span<const byte_t> _data)
	{
		if (!IsOpen()) co_return ne::Result<std::size_t, ne::OsError>::Error(ne::OsError{ 0, "SSH stream closed" });

		auto* sess = static_cast<LIBSSH2_SESSION*>(session);
		auto* ch = static_cast<LIBSSH2_CHANNEL*>(channel);
		std::size_t totalSent = 0;

		while (totalSent < _data.size())
		{
			libssh2_ssize_t n = libssh2_channel_write(ch,
													reinterpret_cast<const char*>(_data.data() + totalSent),
													_data.size() - totalSent);

			if (n >= 0)
			{
				totalSent += static_cast<std::size_t>(n);
			}
			else if (n == LIBSSH2_ERROR_EAGAIN)
			{
				if (auto r = co_await WaitSocket(socket.Handle(), *engine, sess); r.IsError()) co_return ne::Result<std::size_t, ne::OsError>::Error(std::move(r.Error()));
			}
			else
			{
				co_return ne::Result<std::size_t, ne::OsError>::Error(
					Ssh2Error(sess, "[SshStream/Send]"));
			}
		}
		co_return ne::Result<std::size_t, ne::OsError>::Ok(totalSent);
	}

	ne::Task<ne::Result<std::size_t, ne::OsError>> SshStream::Receive(std::span<byte_t> _data)
	{
		if (!IsOpen()) co_return ne::Result<std::size_t, ne::OsError>::Error(ne::OsError{ 0, "SSH stream closed" });

		auto* sess = static_cast<LIBSSH2_SESSION*>(session);
		auto* ch = static_cast<LIBSSH2_CHANNEL*>(channel);

		while (true)
		{
			libssh2_ssize_t n = libssh2_channel_read(ch,
													reinterpret_cast<char*>(_data.data()), _data.size());

			if (n > 0) co_return ne::Result<std::size_t, ne::OsError>::Ok(static_cast<std::size_t>(n));
			if (n == 0 || libssh2_channel_eof(ch)) co_return ne::Result<std::size_t, ne::OsError>::Ok(0);
			if (n == LIBSSH2_ERROR_EAGAIN)
			{
				if (auto r = co_await WaitSocket(socket.Handle(), *engine, sess); r.IsError()) co_return ne::Result<std::size_t, ne::OsError>::Error(std::move(r.Error()));
			}
			else
			{
				co_return ne::Result<std::size_t, ne::OsError>::Error(
					Ssh2Error(sess, "[SshStream/Receive]"));
			}
		}
	}

	ne::Result<void, ne::OsError> SshStream::Close()
	{
		if (!IsOpen()) return ne::Result<void, ne::OsError>::Ok();

		auto* sess = static_cast<LIBSSH2_SESSION*>(session);
		auto* ch = static_cast<LIBSSH2_CHANNEL*>(channel);

		libssh2_channel_send_eof(ch);
		libssh2_channel_free(ch);
		channel = nullptr;

		libssh2_session_disconnect(sess, "Bye");
		libssh2_session_free(sess);
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



	SshStream::SshStream(Socket&&, IIoEngine&, void*, void*) noexcept {}
	SshStream::~SshStream() = default;



	ne::Task<ne::Result<SshStream, ne::OsError>> SshStream::Connect(Socket&&, IIoEngine&, const SshConfig&)
	{
		co_return ne::Result<SshStream, ne::OsError>::Error(NoLibssh2("[SshStream/Connect]"));
	}



	ne::Task<ne::Result<std::size_t, ne::OsError>> SshStream::Send(std::span<const byte_t>)
	{
		co_return ne::Result<std::size_t, ne::OsError>::Error(NoLibssh2("[SshStream/Send]"));
	}

	ne::Task<ne::Result<std::size_t, ne::OsError>> SshStream::Receive(std::span<byte_t>)
	{
		co_return ne::Result<std::size_t, ne::OsError>::Error(NoLibssh2("[SshStream/Receive]"));
	}

	ne::Result<void, ne::OsError> SshStream::Close()
	{
		return ne::Result<void, ne::OsError>::Ok();
	}

#endif // NEBULA_WITH_LIBSSH2

END_NS
