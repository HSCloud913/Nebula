//
// Created by hscloud on 26. 6. 30.
//

#include "Network/Protocol/Ftp/Client.h"
#include "Network/Protocol/Ftp/FtpUtil.h"
#include <array>
#include <format>

BEGIN_NS(ne::network::ftp)
	Client::Client(std::unique_ptr<ne::network::IStream> _control,
		DataStreamFactory_t _dataFactory) noexcept
		: controlStream(std::move(_control))
		, dataFactory(std::move(_dataFactory)) {}

	Client::~Client()
	{
		if (controlStream)
		{
			(void)controlStream->Close();
		}
	}



	ne::Task<ne::Result<Client, ne::Error>> Client::Connect(std::unique_ptr<ne::network::IStream> _control, DataStreamFactory_t _dataFactory, const FtpConfig& _config)
	{
		Client client(std::move(_control), std::move(_dataFactory));

		auto result = co_await client.ReadReply();
		if (result.IsError())
			co_return ne::Result<Client, ne::Error>::Error(std::move(result.Error()));
		if (result.Value().code != 220)
			co_return ne::Result<Client, ne::Error>::Error(
				ne::Error(std::format("unexpected banner: {}", result.Value().code))
				.Context("[FtpClient/Connect]"));

		if (auto loginResult = co_await client.Login(_config.username, _config.password); loginResult.IsError())
			co_return ne::Result<Client, ne::Error>::Error(std::move(loginResult.Error()));

		co_return ne::Result<Client, ne::Error>::Ok(std::move(client));
	}



	ne::Task<ne::Result<FtpReply, ne::Error>> Client::ReadReply()
	{
		// Reads one complete FTP reply (handles multi-line: "NNN-..." until "NNN ...")
		auto readLine = [this]() -> ne::Task<ne::Result<string_t, ne::Error>>
		{
			while (true)
			{
				const auto crlf = receiveBuffer.find("\r\n");
				if (crlf != string_t::npos)
				{
					string_t line = receiveBuffer.substr(0, crlf);
					receiveBuffer.erase(0, crlf + 2);
					co_return ne::Result<string_t, ne::Error>::Ok(std::move(line));
				}

				std::array<ne::byte_t, 4096> buffer{};
				auto result = co_await controlStream->Receive(ne::io::BufferView{ nullptr, buffer.data(), buffer.size() });
				if (result.IsError())
					co_return ne::Result<string_t, ne::Error>::Error(
						ne::Error(result.Error().What()).Context("[FtpClient/ReadReply]"));

				if (result.Value() == 0)
					co_return ne::Result<string_t, ne::Error>::Error(
						ne::Error("connection closed").Context("[FtpClient/ReadReply]"));

				receiveBuffer.append(reinterpret_cast<const char*>(buffer.data()), result.Value());
			}
		};

		auto result = co_await readLine();
		if (result.IsError())
			co_return ne::Result<FtpReply, ne::Error>::Error(std::move(result.Error()));

		const string_t& first = result.Value();
		if (first.size() < 3)
			co_return ne::Result<FtpReply, ne::Error>::Error(ne::Error("malformed FTP reply"));

		int code = 0;
		for (int i = 0; i < 3; ++i)
		{
			if (first[i] < '0' || first[i] > '9')
				co_return ne::Result<FtpReply, ne::Error>::Error(ne::Error("malformed FTP reply code"));

			code = code * 10 + (first[i] - '0');
		}

		// Single-line: 4th char is space or string ends at 3
		if (first.size() < 4 || first[3] != '-')
		{
			co_return ne::Result<FtpReply, ne::Error>::Ok(
				FtpReply{ code, first.size() > 4 ? first.substr(4) : string_t{} });
		}

		// Multi-line: consume lines until "NNN " (same code + space)
		const string_t codeStr = first.substr(0, 3);
		while (true)
		{
			auto lr = co_await readLine();
			if (lr.IsError()) co_return ne::Result<FtpReply, ne::Error>::Error(std::move(lr.Error()));
			const string_t& line = lr.Value();
			if (line.size() >= 4 && line.substr(0, 3) == codeStr && line[3] == ' ')
				co_return ne::Result<FtpReply, ne::Error>::Ok(
					FtpReply{ code, line.size() > 4 ? line.substr(4) : string_t{} });
		}
	}

	ne::Task<ne::Result<FtpReply, ne::Error>> Client::SendCommand(string_view_t _cmd)
	{
		string_t buffer{ _cmd };
		buffer += "\r\n";

		auto result = co_await controlStream->Send(ne::io::BufferView{ nullptr, const_cast<ne::byte_t*>(reinterpret_cast<const ne::byte_t*>(buffer.data())), buffer.size() });
		if (result.IsError())
			co_return ne::Result<FtpReply, ne::Error>::Error(
				ne::Error(result.Error().What()).Context("[FtpClient/SendCommand]"));
		co_return co_await ReadReply();
	}

	ne::Task<ne::Result<FtpReply, ne::Error>> Client::SendCommand(string_view_t _cmd, string_view_t _arg)
	{
		string_t combined{ _cmd };
		combined += ' ';
		combined += _arg;
		co_return co_await SendCommand(combined);
	}

	ne::Task<ne::Result<std::pair<string_t, uint16_t>, ne::Error>> Client::Pasv()
	{
		auto r = co_await SendCommand("PASV");
		if (r.IsError()) co_return ne::Result<std::pair<string_t, uint16_t>, ne::Error>::Error(std::move(r.Error()));
		if (r.Value().code != 227)
			co_return ne::Result<std::pair<string_t, uint16_t>, ne::Error>::Error(
				ne::Error(std::format("PASV failed: {}", r.Value().message))
				.Context("[FtpClient/Pasv]"));

		auto parsed = ParsePassiveReply(r.Value().message);
		if (!parsed)
			co_return ne::Result<std::pair<string_t, uint16_t>, ne::Error>::Error(
				ne::Error("failed to parse PASV response").Context("[FtpClient/Pasv]"));

		co_return ne::Result<std::pair<string_t, uint16_t>, ne::Error>::Ok(std::move(*parsed));
	}

	// ── public methods ────────────────────────────────────────────────────────────

	ne::Task<ne::Result<void, ne::Error>> Client::Login(string_view_t _user, string_view_t _password)
	{
		auto ur = co_await SendCommand("USER", _user);
		if (ur.IsError()) co_return ne::Result<void, ne::Error>::Error(std::move(ur.Error()));
		if (ur.Value().code == 230) co_return ne::Result<void, ne::Error>::Ok();
		if (ur.Value().code != 331)
			co_return ne::Result<void, ne::Error>::Error(
				ne::Error(std::format("USER failed: {}", ur.Value().message))
				.Context("[FtpClient/Login]"));

		auto pr = co_await SendCommand("PASS", _password);
		if (pr.IsError()) co_return ne::Result<void, ne::Error>::Error(std::move(pr.Error()));
		if (pr.Value().code != 230)
			co_return ne::Result<void, ne::Error>::Error(
				ne::Error(std::format("PASS failed: {}", pr.Value().message))
				.Context("[FtpClient/Login]"));

		co_return ne::Result<void, ne::Error>::Ok();
	}

	ne::Task<ne::Result<void, ne::Error>> Client::Quit()
	{
		(void)co_await SendCommand("QUIT");

		if (controlStream)
			(void)controlStream->Close();

		co_return ne::Result<void, ne::Error>::Ok();
	}

	ne::Task<ne::Result<void, ne::Error>> Client::SetTransferType(TransferType _type)
	{
		const string_view_t argument = (_type == TransferType::Binary) ? "I" : "A";

		auto result = co_await SendCommand("TYPE", argument);
		if (result.IsError())
			co_return ne::Result<void, ne::Error>::Error(std::move(result.Error()));

		if (result.Value().code != 200)
			co_return ne::Result<void, ne::Error>::Error(
				ne::Error(std::format("TYPE failed: {}", result.Value().message))
				.Context("[FtpClient/SetTransferType]"));

		co_return ne::Result<void, ne::Error>::Ok();
	}

	ne::Task<ne::Result<string_t, ne::Error>> Client::Pwd()
	{
		auto result = co_await SendCommand("PWD");
		if (result.IsError())
			co_return ne::Result<string_t, ne::Error>::Error(std::move(result.Error()));

		if (result.Value().code != 257)
			co_return ne::Result<string_t, ne::Error>::Error(
				ne::Error(std::format("PWD failed: {}", result.Value().message))
				.Context("[FtpClient/Pwd]"));

		const auto& message = result.Value().message;
		const auto q1 = message.find('"');
		const auto q2 = message.rfind('"');
		if (q1 == string_t::npos || q1 == q2)
			co_return ne::Result<string_t, ne::Error>::Ok(message);

		co_return ne::Result<string_t, ne::Error>::Ok(message.substr(q1 + 1, q2 - q1 - 1));
	}

	ne::Task<ne::Result<void, ne::Error>> Client::Cwd(string_view_t _path)
	{
		auto result = co_await SendCommand("CWD", _path);
		if (result.IsError())
			co_return ne::Result<void, ne::Error>::Error(std::move(result.Error()));

		if (result.Value().code != 250)
			co_return ne::Result<void, ne::Error>::Error(
				ne::Error(std::format("CWD failed: {}", result.Value().message))
				.Context("[FtpClient/Cwd]"));

		co_return ne::Result<void, ne::Error>::Ok();
	}

	ne::Task<ne::Result<void, ne::Error>> Client::Mkd(string_view_t _path)
	{
		auto result = co_await SendCommand("MKD", _path);
		if (result.IsError())
			co_return ne::Result<void, ne::Error>::Error(std::move(result.Error()));

		if (result.Value().code != 257)
			co_return ne::Result<void, ne::Error>::Error(
				ne::Error(std::format("MKD failed: {}", result.Value().message))
				.Context("[FtpClient/Mkd]"));

		co_return ne::Result<void, ne::Error>::Ok();
	}

	ne::Task<ne::Result<void, ne::Error>> Client::Rmd(string_view_t _path)
	{
		auto result = co_await SendCommand("RMD", _path);
		if (result.IsError())
			co_return ne::Result<void, ne::Error>::Error(std::move(result.Error()));

		if (result.Value().code != 250)
			co_return ne::Result<void, ne::Error>::Error(
				ne::Error(std::format("RMD failed: {}", result.Value().message))
				.Context("[FtpClient/Rmd]"));

		co_return ne::Result<void, ne::Error>::Ok();
	}

	ne::Task<ne::Result<void, ne::Error>> Client::Dele(string_view_t _path)
	{
		auto result = co_await SendCommand("DELE", _path);
		if (result.IsError())
			co_return ne::Result<void, ne::Error>::Error(std::move(result.Error()));

		if (result.Value().code != 250)
			co_return ne::Result<void, ne::Error>::Error(
				ne::Error(std::format("DELE failed: {}", result.Value().message))
				.Context("[FtpClient/Dele]"));

		co_return ne::Result<void, ne::Error>::Ok();
	}

	ne::Task<ne::Result<void, ne::Error>> Client::Rename(string_view_t _from, string_view_t _to)
	{
		auto result = co_await SendCommand("RNFR", _from);
		if (result.IsError())
			co_return ne::Result<void, ne::Error>::Error(std::move(result.Error()));

		if (result.Value().code != 350)
			co_return ne::Result<void, ne::Error>::Error(
				ne::Error(std::format("RNFR failed: {}", result.Value().message))
				.Context("[FtpClient/Rename]"));

		result = co_await SendCommand("RNTO", _to);
		if (result.IsError())
			co_return ne::Result<void, ne::Error>::Error(std::move(result.Error()));

		if (result.Value().code != 250)
			co_return ne::Result<void, ne::Error>::Error(
				ne::Error(std::format("RNTO failed: {}", result.Value().message))
				.Context("[FtpClient/Rename]"));

		co_return ne::Result<void, ne::Error>::Ok();
	}

	static string_t ReadDataChannel(const string_t& _raw, std::vector<FtpEntry>& _out, bool_t _mlsd)
	{
		// Returns empty string on success (no unused tail).
		std::size_t pos = 0;
		while (pos < _raw.size())
		{
			const auto nl = _raw.find('\n', pos);
			const auto end = (nl == string_t::npos) ? _raw.size() : nl;
			const string_view_t line(_raw.data() + pos, end - pos);
			const auto entry = _mlsd ? ParseMlsdLine(line) : ParseUnixListLine(line);
			if (entry) _out.push_back(*entry);

			pos = (nl == string_t::npos) ? _raw.size() : nl + 1;
		}

		return {};
	}

	ne::Task<ne::Result<std::vector<FtpEntry>, ne::Error>> Client::List(string_view_t _path)
	{
		auto pasvR = co_await Pasv();
		if (pasvR.IsError()) co_return ne::Result<std::vector<FtpEntry>, ne::Error>::Error(std::move(pasvR.Error()));

		auto [dHost, dPort] = std::move(pasvR.Value());
		auto dataR = co_await dataFactory(dHost, dPort);
		if (dataR.IsError())
			co_return ne::Result<std::vector<FtpEntry>, ne::Error>::Error(
				ne::Error(dataR.Error().What()).Context("[FtpClient/List/connect]"));
		auto dataStream = std::move(dataR.Value());

		// Try MLSD (RFC 3659); fall back to LIST on 500/502
		auto cmdR = _path.empty()
						? co_await SendCommand("MLSD")
						: co_await SendCommand("MLSD", _path);
		if (cmdR.IsError()) co_return ne::Result<std::vector<FtpEntry>, ne::Error>::Error(std::move(cmdR.Error()));

		bool_t useMlsd = true;
		if (cmdR.Value().code == 500 || cmdR.Value().code == 502)
		{
			useMlsd = false;
			(void)dataStream->Close();

			auto p2 = co_await Pasv();
			if (p2.IsError()) co_return ne::Result<std::vector<FtpEntry>, ne::Error>::Error(std::move(p2.Error()));
			auto [dh2, dp2] = std::move(p2.Value());
			auto dr2 = co_await dataFactory(dh2, dp2);
			if (dr2.IsError())
				co_return ne::Result<std::vector<FtpEntry>, ne::Error>::Error(
					ne::Error(dr2.Error().What()).Context("[FtpClient/List/fallback/connect]"));
			dataStream = std::move(dr2.Value());

			auto listR = _path.empty()
							? co_await SendCommand("LIST")
							: co_await SendCommand("LIST", _path);
			if (listR.IsError()) co_return ne::Result<std::vector<FtpEntry>, ne::Error>::Error(std::move(listR.Error()));
			if (listR.Value().code != 125 && listR.Value().code != 150)
				co_return ne::Result<std::vector<FtpEntry>, ne::Error>::Error(
					ne::Error(std::format("LIST failed: {}", listR.Value().message))
					.Context("[FtpClient/List]"));
		}
		else if (cmdR.Value().code != 125 && cmdR.Value().code != 150)
		{
			co_return ne::Result<std::vector<FtpEntry>, ne::Error>::Error(
				ne::Error(std::format("MLSD failed: {}", cmdR.Value().message))
				.Context("[FtpClient/List]"));
		}

		string_t raw;
		while (true)
		{
			std::array<ne::byte_t, 8192> tmp{};
			auto r = co_await dataStream->Receive(ne::io::BufferView{ nullptr, tmp.data(), tmp.size() });
			if (r.IsError() || r.Value() == 0) break;
			raw.append(reinterpret_cast<const char*>(tmp.data()), r.Value());
		}
		(void)dataStream->Close();
		(void)co_await ReadReply(); // 226 Transfer complete

		std::vector<FtpEntry> entries;
		ReadDataChannel(raw, entries, useMlsd);
		co_return ne::Result<std::vector<FtpEntry>, ne::Error>::Ok(std::move(entries));
	}

	ne::Task<ne::Result<void, ne::Error>> Client::Get(string_view_t _remote, const SinkFn_t& _sink, uint64_t _offset)
	{
		auto pasvR = co_await Pasv();
		if (pasvR.IsError()) co_return ne::Result<void, ne::Error>::Error(std::move(pasvR.Error()));

		auto [dHost, dPort] = std::move(pasvR.Value());
		auto dataR = co_await dataFactory(dHost, dPort);
		if (dataR.IsError())
			co_return ne::Result<void, ne::Error>::Error(
				ne::Error(dataR.Error().What()).Context("[FtpClient/Get/connect]"));
		auto dataStream = std::move(dataR.Value());

		if (_offset > 0)
		{
			auto restR = co_await SendCommand("REST", std::to_string(_offset));
			if (restR.IsError()) co_return ne::Result<void, ne::Error>::Error(std::move(restR.Error()));
			if (restR.Value().code != 350)
				co_return ne::Result<void, ne::Error>::Error(
					ne::Error(std::format("REST failed: {}", restR.Value().message))
					.Context("[FtpClient/Get]"));
		}

		auto retrR = co_await SendCommand("RETR", _remote);
		if (retrR.IsError()) co_return ne::Result<void, ne::Error>::Error(std::move(retrR.Error()));
		if (retrR.Value().code != 125 && retrR.Value().code != 150)
			co_return ne::Result<void, ne::Error>::Error(
				ne::Error(std::format("RETR failed: {}", retrR.Value().message))
				.Context("[FtpClient/Get]"));

		while (true)
		{
			std::array<ne::byte_t, 65536> tmp{};
			auto r = co_await dataStream->Receive(ne::io::BufferView{ nullptr, tmp.data(), tmp.size() });
			if (r.IsError() || r.Value() == 0) break;
			_sink(std::span<const ne::byte_t>(tmp.data(), r.Value()));
		}
		(void)dataStream->Close();
		(void)co_await ReadReply(); // 226
		co_return ne::Result<void, ne::Error>::Ok();
	}

	ne::Task<ne::Result<void, ne::Error>> Client::Put(string_view_t _remote, std::span<const ne::byte_t> _data, uint64_t _offset)
	{
		auto pasvR = co_await Pasv();
		if (pasvR.IsError()) co_return ne::Result<void, ne::Error>::Error(std::move(pasvR.Error()));

		auto [dHost, dPort] = std::move(pasvR.Value());
		auto dataR = co_await dataFactory(dHost, dPort);
		if (dataR.IsError())
			co_return ne::Result<void, ne::Error>::Error(
				ne::Error(dataR.Error().What()).Context("[FtpClient/Put/connect]"));
		auto dataStream = std::move(dataR.Value());

		if (_offset > 0)
		{
			auto restR = co_await SendCommand("REST", std::to_string(_offset));
			if (restR.IsError()) co_return ne::Result<void, ne::Error>::Error(std::move(restR.Error()));
			if (restR.Value().code != 350)
				co_return ne::Result<void, ne::Error>::Error(
					ne::Error(std::format("REST failed: {}", restR.Value().message))
					.Context("[FtpClient/Put]"));
		}

		auto storR = co_await SendCommand("STOR", _remote);
		if (storR.IsError()) co_return ne::Result<void, ne::Error>::Error(std::move(storR.Error()));
		if (storR.Value().code != 125 && storR.Value().code != 150)
			co_return ne::Result<void, ne::Error>::Error(
				ne::Error(std::format("STOR failed: {}", storR.Value().message))
				.Context("[FtpClient/Put]"));

		std::size_t sent = 0;
		while (sent < _data.size())
		{
			auto r = co_await dataStream->Send(ne::io::BufferView{ nullptr, const_cast<ne::byte_t*>(_data.data() + sent), _data.size() - sent });
			if (r.IsError())
				co_return ne::Result<void, ne::Error>::Error(
					ne::Error(r.Error().What()).Context("[FtpClient/Put/send]"));
			sent += r.Value();
		}
		(void)dataStream->Close();
		(void)co_await ReadReply(); // 226
		co_return ne::Result<void, ne::Error>::Ok();
	}

END_NS
