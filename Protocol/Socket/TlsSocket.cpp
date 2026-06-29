//
// Created by nebula on 24. 5. 29.
//

#include "TlsSocket.h"

#include <cerrno>
#include <cstring>
#include <fstream>
#if defined(_WIN32)
#	include <array>
#	include "Windows/Secure/SchannelServer.h"
#	include "Windows/Secure/SchannelClient.h"
#elif defined(__linux__)
#	include <sys/epoll.h>
#elif defined(__APPLE__)
#	include <sys/event.h>
#endif



BEGIN_NS(ne::protocol)
	TlsSocket::TlsSocket(const string_view_t _server, const int_t _port)
		: socket(std::make_unique<TcpSocket>(_server, _port))
		, server(_server)
	{
#if defined(IS_POSIX)
		InitializeClientContext();
#endif
	}

	TlsSocket::TlsSocket(const socket_t _socket, const string_view_t _server)
		: socket(std::make_unique<TcpSocket>(_socket))
		, server(_server)
	{
#if defined(IS_POSIX)
		InitializeClientContext();
#endif
	}

#if defined(IS_POSIX)
	void_t TlsSocket::InitializeClientContext()
	{
		tlsContext = []
		{
			if (const auto method = TLS_client_method())
			{
				if (const auto tls = SSL_CTX_new(method))
				{
					return TlsContext{ tls };
				}
			}

			throw ne::Exception("[TlsSocket/TlsClientInit]", OpenSslError());
		}();

		tlsConnection = [this]
		{
			if (const auto tlsConnection = SSL_new(tlsContext.get()))
			{
				return TlsConnection{ tlsConnection };
			}

			throw ne::Exception("[TlsSocket/TlsClientInit]", OpenSslError());
		}();
	}
#endif



	void_t TlsSocket::Connect()
	{
		socket->Connect();
		Handshake();
	}

	void_t TlsSocket::Handshake()
	{
#if defined(_WIN32)
		std::tie(securityContextHandle, tlsBuffer) = SchannelClient(socket.get(), server)();
		if (const auto result = SspiWrapper::GetInstance().functions->QueryContextAttributesW(&securityContextHandle, SECPKG_ATTR_STREAM_SIZES, &streamSizes); result != SEC_E_OK)
		{
			throw ne::Exception("[TlsSocket/Handshake]", std::format("Failed to query schannel security context stream sizes (result: {})", result));
		}
		tlsBuffer.Resize(streamSizes.cbHeader + streamSizes.cbMaximumMessage + streamSizes.cbTrailer);
#elif defined(IS_POSIX)
		if (SSL_CTX_set_default_verify_paths(tlsContext.get()) != 1)
		{
			throw ne::Exception("[TlsSocket/Handshake]", OpenSslError());
		}

		SSL_CTX_set_read_ahead(tlsContext.get(), true);

		const auto hostName = server.data();
		if (SSL_set_tlsext_host_name(tlsConnection.get(), reinterpret_cast<void_t*>(const_cast<char_t*>(hostName))) != 1)
		{
			throw ne::Exception("[TlsSocket/Handshake]", OpenSslError());
		}

		if (SSL_set1_host(tlsConnection.get(), hostName) != 1)
		{
			throw ne::Exception("[TlsSocket/Handshake]", OpenSslError());
		}

		if (SSL_set_fd(tlsConnection.get(), socket->GetHandle()) != 1)
		{
			throw ne::Exception("[TlsSocket/Handshake]", OpenSslError());
		}

		SSL_connect(tlsConnection.get());
		if (const auto certificate = SSL_get_peer_certificate(tlsConnection.get()))
		{
			X509_free(certificate);
		}
		else
		{
			throw ne::Exception("[TlsSocket/Handshake]", OpenSslError());
		}

		if (SSL_get_verify_result(tlsConnection.get()) != X509_V_OK)
		{
			throw ne::Exception("[TlsSocket/Handshake]", OpenSslError());
		}
#endif
	}
	void_t TlsSocket::Reconnect()
	{
#if defined(_WIN32)
		socket->Reconnect();

		std::tie(securityContextHandle, tlsBuffer) = SchannelClient(socket.get(), server)();
		if (const auto result = SspiWrapper::GetInstance().functions->QueryContextAttributesW(&securityContextHandle, SECPKG_ATTR_STREAM_SIZES, &streamSizes); result != SEC_E_OK)
		{
			throw ne::Exception("[TlsSocket/Reconnect]", std::format("Failed to query schannel security context stream sizes (result: {})", result));
		}
		tlsBuffer.Resize(streamSizes.cbHeader + streamSizes.cbMaximumMessage + streamSizes.cbTrailer);
#elif defined(IS_POSIX)
		socket->Reconnect();

		if (SSL_set_fd(tlsConnection.get(), socket->GetHandle()) != 1)
		{
			throw ne::Exception("[TlsSocket/Write]", OpenSslError());
		}
#endif
	}



#if defined(_WIN32)
	void_t TlsSocket::Handshake(PCCERT_CONTEXT _certContext)
	{
		std::tie(securityContextHandle, tlsBuffer) = SchannelServer(socket.get(), _certContext)();
		if (const auto result = SspiWrapper::GetInstance().functions->QueryContextAttributesW(&securityContextHandle, SECPKG_ATTR_STREAM_SIZES, &streamSizes); result != SEC_E_OK)
		{
			throw ne::Exception("[TlsSocket/Handshake]", std::format("Failed to query schannel security context stream sizes (result: {})", result));
		}
		tlsBuffer.Resize(streamSizes.cbHeader + streamSizes.cbMaximumMessage + streamSizes.cbTrailer);
	}
#elif defined(IS_POSIX)
	void_t TlsSocket::Certificates(string_view_t _crt, string_view_t _key)
	{
		tlsContext = []
		{
			if (const auto method = TLS_server_method())
			{
				if (const auto tls = SSL_CTX_new(method))
				{
					return TlsContext{ tls };
				}
			}

			throw ne::Exception("[TlsSocket/Init]", OpenSslError());
		}();

		SSL_CTX_set_cipher_list(tlsContext.get(), "ALL:eNULL");

		if (SSL_CTX_load_verify_locations(tlsContext.get(), _crt.data(), _key.data()) != 1)
		{
			throw ne::Exception("[TlsSocket/LoadCertificates]", OpenSslError());
		}

		//SSL_CTX_set_verify(tlsContext.get(), SSL_VERIFY_PEER, nullptr);

		if (SSL_CTX_set_default_verify_paths(tlsContext.get()) != 1)
		{
			throw ne::Exception("[TlsSocket/LoadCertificates]", OpenSslError());
		}

		if (SSL_CTX_use_certificate_file(tlsContext.get(), _crt.data(), SSL_FILETYPE_PEM) <= 0)
		{
			throw ne::Exception("[TlsSocket/LoadCertificates]", OpenSslError());
		}

		if (SSL_CTX_use_PrivateKey_file(tlsContext.get(), _key.data(), SSL_FILETYPE_PEM) <= 0)
		{
			throw ne::Exception("[TlsSocket/LoadCertificates]", OpenSslError());
		}

		if (!SSL_CTX_check_private_key(tlsContext.get()))
		{
			throw ne::Exception("[TlsSocket/LoadCertificates]", "Private key does not match the certificatepublic key");
		}
	}

	void_t TlsSocket::Handshake(SSL_CTX* _tlsContext)
	{
		tlsConnection = [this, _tlsContext]
		{
			if (const auto tlsConnection = SSL_new(_tlsContext))
			{
				return TlsConnection{ tlsConnection };
			}

			throw ne::Exception("[TlsSocket/Handshake]", OpenSslError());
		}();

		SSL_set_fd(tlsConnection.get(), socket->GetHandle());
		if (SSL_accept(tlsConnection.get()) <= 0)
		{
			throw ne::Exception("[TlsSocket/Handshake]", OpenSslError());
		}

		// X509* client_cert = SSL_get_peer_certificate(tlsConnection.get());
		// if (client_cert == nullptr)
		// {
		// 	throw ne::Exception("[TlsSocket/Handshake]", OpenSslError());
		// }
	}
#endif



	longlong_t TlsSocket::Read(const std::span<std::byte> _buffer)
	{
#if defined(_WIN32)
		if (decryptedMessage.empty())
		{
			const auto buffer = tlsBuffer.GetBuffer();
			auto offset = std::size_t{};

			while (true)
			{
				if (const auto result = ReadEncryptedData(offset); result < 0) { return result; }
				else if (!DecryptMessage(buffer.first(offset + result))) { offset += result; }

				break;
			}
		}

		if (decryptedMessage.empty()) return longlong_t{};

		const auto size = static_cast<longlong_t>(std::min(decryptedMessage.size(), _buffer.size()));
		std::ranges::copy(decryptedMessage.first(size), _buffer.begin());
		decryptedMessage = decryptedMessage.subspan(size);

		return size;
#elif defined(IS_POSIX)
		if (isClosed) return -1;

		socket->SetSocketMode(false);
		if (const auto result = ::SSL_read(tlsConnection.get(), _buffer.data(), static_cast<int_t>(_buffer.size())); result >= 0)
		{
			if (result == 0)
			{
				isClosed = true;
				return -1;
			}

			return static_cast<longlong_t>(result);
		}

		throw ne::Exception("[TlsSocket/Read]", OpenSslError());
#endif
	}

	bool_t TlsSocket::Write(std::span<const std::byte> _data)
	{
#if defined(_WIN32)
		while (!_data.empty())
		{
			const auto length = std::min(_data.size(), static_cast<std::size_t>(streamSizes.cbMaximumMessage));
			const auto buffer = EncryptMessage(_data.first(length));
			if (!socket->Write(buffer)) return false;

			_data = _data.subspan(length);
		}
#elif defined(IS_POSIX)
		if (!socket->IsConnected()) Reconnect();

		if (::SSL_write(tlsConnection.get(), _data.data(), static_cast<int_t>(_data.size())) == -1)
		{
			throw ne::Exception("[TlsSocket/Write]", OpenSslError());
		}
#endif

		return true;
	}



#if defined(_WIN32)
	void_t TlsSocket::Iocp()
	{
		struct ReadContext
		{
			OVERLAPPED overlapped{};
			WSABUF wsaBuf{};
		};

		const auto rawSocket = socket->GetHandle();
		const auto iocpFd = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
		::CreateIoCompletionPort(reinterpret_cast<HANDLE>(rawSocket), iocpFd, 0, 0);

		auto bufferOffset = std::size_t{};

		if (!tlsBuffer.data.empty())
		{
			const auto extraSize = tlsBuffer.data.size();
			std::memmove(tlsBuffer.GetBuffer().data(), tlsBuffer.data.data(), extraSize);
			tlsBuffer.data = {};
			bufferOffset = extraSize;
		}

		const auto postRead = [this, rawSocket, &bufferOffset](ReadContext* _ctx) -> bool_t
		{
			const auto encBuf = tlsBuffer.GetBuffer();
			_ctx->overlapped = {};
			_ctx->wsaBuf.buf = reinterpret_cast<CHAR*>(encBuf.data()) + bufferOffset;
			_ctx->wsaBuf.len = static_cast<ULONG>(encBuf.size() - bufferOffset);
			DWORD flags = 0;
			if (::WSARecv(rawSocket, &_ctx->wsaBuf, 1, nullptr, &flags, &_ctx->overlapped, nullptr) == SOCKET_ERROR) return ::WSAGetLastError() == WSA_IO_PENDING;
			return true;
		};

		const auto tryDecrypt = [this, &bufferOffset]() -> bool_t
		{
			if (!DecryptMessage(tlsBuffer.GetBuffer().first(bufferOffset))) return false;

			bufferOffset = 0;

			if (!decryptedMessage.empty())
			{
				auto plaintext = std::vector<std::byte>(decryptedMessage.begin(), decryptedMessage.end());
				decryptedMessage = {};
				if (readHandler) readHandler(std::move(plaintext));
			}

			if (!tlsBuffer.data.empty())
			{
				const auto extraSize = tlsBuffer.data.size();
				std::memmove(tlsBuffer.GetBuffer().data(), tlsBuffer.data.data(), extraSize);
				tlsBuffer.data = {};
				bufferOffset = extraSize;
			}

			return true;
		};

		auto* pending = new ReadContext{};

		if (bufferOffset > 0)
		{
			auto didDecrypt = true;
			while (didDecrypt && bufferOffset > 0) didDecrypt = tryDecrypt();
		}

		if (!postRead(pending))
		{
			delete pending;
			::CloseHandle(iocpFd);
			throw ne::Exception("[TlsSocket/Iocp]", std::format("Failed to post initial WSARecv (error: {})", ::WSAGetLastError()));
		}

		while (socket->IsConnected())
		{
			DWORD bytes = 0;
			ULONG_PTR key = 0;
			OVERLAPPED* pOv = nullptr;

			const auto ok = ::GetQueuedCompletionStatus(iocpFd, &bytes, &key, &pOv, INFINITE);
			auto ctx = std::unique_ptr<ReadContext>(reinterpret_cast<ReadContext*>(pOv));
			pending = nullptr;

			if (!ok)
			{
				if (pOv && exceptionHandler) exceptionHandler(std::format("[TlsSocket/Iocp] GQCS error ({})", ::GetLastError()));
				break;
			}
			if (bytes == 0) break;

			bufferOffset += bytes;

			auto didDecrypt = true;
			while (didDecrypt && bufferOffset > 0) didDecrypt = tryDecrypt();

			if (!postRead(ctx.get()))
			{
				if (exceptionHandler) exceptionHandler(std::format("[TlsSocket/Iocp] WSARecv error ({})", ::WSAGetLastError()));
				break;
			}
			pending = ctx.release();
		}

		if (pending)
		{
			::CancelIoEx(reinterpret_cast<HANDLE>(rawSocket), &pending->overlapped);
			DWORD b;
			ULONG_PTR k;
			OVERLAPPED* ov = nullptr;
			::GetQueuedCompletionStatus(iocpFd, &b, &k, &ov, 500);
			if (ov) delete reinterpret_cast<ReadContext*>(ov);
			else delete pending;
		}

		::CloseHandle(iocpFd);
	}
#elif defined(__linux__)
	void_t TlsSocket::Epoll()
	{
		const auto rawFd = socket->GetHandle();
		const auto epollFd = ::epoll_create1(EPOLL_CLOEXEC);
		if (epollFd == -1) throw ne::Exception("[TlsSocket/Epoll]", std::format("Failed to create epoll fd (error: {})", errno));

		epoll_event ev{};
		ev.events = EPOLLIN | EPOLLERR | EPOLLHUP;
		ev.data.fd = rawFd;
		if (::epoll_ctl(epollFd, EPOLL_CTL_ADD, rawFd, &ev) == -1)
		{
			::close(epollFd);
			throw ne::Exception("[TlsSocket/Epoll]", std::format("Failed to register socket with epoll (error: {})", errno));
		}

		std::array<epoll_event, 16> events{};
		auto stop = false;
		while (!stop && !isClosed && socket->IsConnected())
		{
			const auto count = ::epoll_wait(epollFd, events.data(), static_cast<int>(events.size()), -1);
			if (count < 0)
			{
				if (errno == EINTR) continue;
				break;
			}

			for (int i = 0; i < count && !stop; ++i)
			{
				if (events[i].events & (EPOLLERR | EPOLLHUP))
				{
					if (exceptionHandler) exceptionHandler(std::format("[TlsSocket/Epoll] Socket error ({})", errno));
					stop = true;
				}
				else if (events[i].events & EPOLLIN)
				{
					auto data = std::vector<std::byte>(4096);
					const auto result = ::SSL_read(tlsConnection.get(), data.data(), static_cast<int>(data.size()));
					if (result <= 0)
					{
						isClosed = true;
						stop = true;
					}
					else
					{
						data.resize(static_cast<std::size_t>(result));
						if (readHandler) readHandler(std::move(data));
					}
				}
			}
		}

		::close(epollFd);
	}
#elif defined(__APPLE__)
	void_t TlsSocket::Kqueue()
	{
		const auto rawFd = socket->GetHandle();
		const auto kqFd = ::kqueue();
		if (kqFd == -1) throw ne::Exception("[TlsSocket/Kqueue]", std::format("Failed to create kqueue fd (error: {})", errno));

		struct kevent change;
		EV_SET(&change, rawFd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
		if (::kevent(kqFd, &change, 1, nullptr, 0, nullptr) == -1)
		{
			::close(kqFd);
			throw ne::Exception("[TlsSocket/Kqueue]", std::format("Failed to register socket with kqueue (error: {})", errno));
		}

		std::array<struct kevent, 16> events{};
		auto stop = false;
		while (!stop && !isClosed && socket->IsConnected())
		{
			const auto count = ::kevent(kqFd, nullptr, 0, events.data(), static_cast<int>(events.size()), nullptr);
			if (count < 0)
			{
				if (errno == EINTR) continue;
				break;
			}

			for (int i = 0; i < count && !stop; ++i)
			{
				if (events[i].flags & EV_ERROR)
				{
					if (exceptionHandler) exceptionHandler(std::format("[TlsSocket/Kqueue] Socket error ({})", events[i].data));
					stop = true;
				}
				else if (events[i].filter == EVFILT_READ)
				{
					auto data = std::vector<std::byte>(4096);
					const auto result = ::SSL_read(tlsConnection.get(), data.data(), static_cast<int>(data.size()));
					if (result <= 0)
					{
						isClosed = true;
						stop = true;
					}
					else
					{
						data.resize(static_cast<std::size_t>(result));
						if (readHandler) readHandler(std::move(data));
					}
				}
			}
		}

		::close(kqFd);
	}
#endif



#if defined(_WIN32)
	std::vector<std::byte> TlsSocket::EncryptMessage(const std::span<const std::byte> _data)
	{
		auto buffer = std::vector<std::byte>(streamSizes.cbHeader + _data.size() + streamSizes.cbTrailer);
		std::ranges::copy(_data, buffer.begin() + streamSizes.cbHeader);

		auto buffers = std::array
		{
			SecBuffer
			{
				.cbBuffer = streamSizes.cbHeader,
				.BufferType = SECBUFFER_STREAM_HEADER,
				.pvBuffer = buffer.data(),
			},
			SecBuffer
			{
				.cbBuffer = static_cast<ulong_t>(_data.size()),
				.BufferType = SECBUFFER_DATA,
				.pvBuffer = buffer.data() + streamSizes.cbHeader,
			},
			SecBuffer
			{
				.cbBuffer = streamSizes.cbTrailer,
				.BufferType = SECBUFFER_STREAM_TRAILER,
				.pvBuffer = buffer.data() + streamSizes.cbHeader + _data.size(),
			},
			SecBuffer{}
		};
		auto buffersDescription = CreateBufferDescription(buffers);

		if (const auto result = SspiWrapper::GetInstance().functions->EncryptMessage(&securityContextHandle, 0, &buffersDescription, 0); result != SEC_E_OK) { throw ne::Exception("[TlsSocket/EncryptMessage]", std::format("Failed to encrypt tls message (result: {})", result)); }

		return buffer;
	}

	bool_t TlsSocket::DecryptMessage(const std::span<std::byte> _message)
	{
		auto buffers = std::array
		{
			SecBuffer
			{
				.cbBuffer = static_cast<unsigned long>(_message.size()),
				.BufferType = SECBUFFER_DATA,
				.pvBuffer = _message.data(),
			},
			SecBuffer{},
			SecBuffer{},
			SecBuffer{}
		};
		auto buffersDescription = CreateBufferDescription(buffers);

		if (const auto result = SspiWrapper::GetInstance().functions->DecryptMessage(&securityContextHandle, &buffersDescription, 0, nullptr); result == SEC_E_OK)
		{
			decryptedMessage = { static_cast<const std::byte*>(buffers[1].pvBuffer), static_cast<std::size_t>(buffers[1].cbBuffer) };

			if (buffers[3].BufferType == SECBUFFER_EXTRA) { tlsBuffer.data = _message.last(buffers[3].cbBuffer); }
			return true;
		}
		else if (result == SEC_E_INCOMPLETE_MESSAGE) { return false; }
		else if (result == SEC_I_CONTEXT_EXPIRED)
		{
			decryptedMessage = {};
			return true;
		}
		else { throw ne::Exception("[TlsSocket/DecryptMessage]", std::format("Failed to decrypt a received tls message (result: {})", result)); }
	}

	longlong_t TlsSocket::ReadEncryptedData(const std::size_t _offset)
	{
		const auto buffer = tlsBuffer.GetBuffer();

		if (tlsBuffer.data.empty()) return socket->Read(buffer.subspan(_offset));
		if (_offset == 0) return 0;

		const auto dataSize = static_cast<longlong_t>(tlsBuffer.data.size());
		std::ranges::copy_backward(tlsBuffer.data, buffer.begin() + dataSize);
		tlsBuffer.data = {};

		return dataSize;
	}
#endif

END_NS
