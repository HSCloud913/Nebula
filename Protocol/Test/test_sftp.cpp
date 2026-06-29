//
// Created by nebula on 24. 11. 3.
//

#include <gtest/gtest.h>

#include "Sftp/SftpClient.h"

using ne::protocol::Sftp::Client;

TEST(SftpClientTest, ConstructionResolvesAddressWithoutConnecting)
{
	EXPECT_NO_THROW(Client("127.0.0.1", 22));
}

// A full Connect/Auth/List round trip and VerifyKnownHosts (which requires a live session to
// retrieve the server host key) need a real SSH server (e.g. local OpenSSH) and are not
// exercised here automatically; see the manual verification notes for this phase.
