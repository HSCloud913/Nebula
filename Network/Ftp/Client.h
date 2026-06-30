//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include <functional>
#include <span>
#include <utility>
#include <vector>
#include "FtpBase.h"

BEGIN_NS(ne::network::ftp)
	// Async FTP client layered over ne::network::IStream.
	// Control channel: provided by caller (PlainStream or TlsStream).
	// Data channels:   created on demand via DataStreamFactory_t (PASV mode only).
	class Client
	{
	public:
		explicit Client(std::unique_ptr<ne::network::IStream> _control, DataStreamFactory_t _dataFactory) noexcept;
		~Client();
		NEBULA_NON_COPYABLE(Client)
		NEBULA_DEFAULT_MOVE(Client)

	private:
		using SinkFn_t = std::function<void(std::span<const ne::byte_t>)>;

		std::unique_ptr<ne::network::IStream> controlStream;
		DataStreamFactory_t dataFactory;
		string_t receiveBuffer;

	public:
		// Reads the 220 banner then authenticates with _config credentials.
		[[nodiscard]] static ne::Task<ne::Result<Client, ne::Error>> Connect(std::unique_ptr<ne::network::IStream> _control, DataStreamFactory_t _dataFactory, const FtpConfig& _config);

	public:
		[[nodiscard]] ne::Task<ne::Result<void, ne::Error>> Login(string_view_t _user, string_view_t _password);
		[[nodiscard]] ne::Task<ne::Result<void, ne::Error>> Quit();
		[[nodiscard]] ne::Task<ne::Result<void, ne::Error>> SetTransferType(TransferType _type);
		[[nodiscard]] ne::Task<ne::Result<string_t, ne::Error>> Pwd();
		[[nodiscard]] ne::Task<ne::Result<void, ne::Error>> Cwd(string_view_t _path);
		[[nodiscard]] ne::Task<ne::Result<void, ne::Error>> Mkd(string_view_t _path);
		[[nodiscard]] ne::Task<ne::Result<void, ne::Error>> Rmd(string_view_t _path);
		[[nodiscard]] ne::Task<ne::Result<void, ne::Error>> Dele(string_view_t _path);
		[[nodiscard]] ne::Task<ne::Result<void, ne::Error>> Rename(string_view_t _from, string_view_t _to);
		[[nodiscard]] ne::Task<ne::Result<std::vector<FtpEntry>, ne::Error>> List(string_view_t _path = {});
		[[nodiscard]] ne::Task<ne::Result<void, ne::Error>> Get(string_view_t _remote, const SinkFn_t& _sink, uint64_t _offset = 0);
		[[nodiscard]] ne::Task<ne::Result<void, ne::Error>> Put(string_view_t _remote, std::span<const ne::byte_t> _data, uint64_t _offset = 0);

	private:
		[[nodiscard]] ne::Task<ne::Result<FtpReply, ne::Error>> ReadReply();
		[[nodiscard]] ne::Task<ne::Result<FtpReply, ne::Error>> SendCommand(string_view_t _cmd);
		[[nodiscard]] ne::Task<ne::Result<FtpReply, ne::Error>> SendCommand(string_view_t _cmd, string_view_t _arg);
		[[nodiscard]] ne::Task<ne::Result<std::pair<string_t, uint16_t>, ne::Error>> Pasv();
	};

END_NS
