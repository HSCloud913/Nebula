//
// Created by hscloud on 26. 6. 30.
//

#include <gtest/gtest.h>
#include "Stream/TlsStream.h"

using namespace ne::network;

TEST(TlsConfigTest, DefaultVerifyPeerIsTrue)
{
	TlsConfig config;
	EXPECT_TRUE(config.verifyPeer);
}

TEST(TlsConfigTest, DefaultFieldsAreEmpty)
{
	TlsConfig config;
	EXPECT_TRUE(config.caFile.empty());
	EXPECT_TRUE(config.certFile.empty());
	EXPECT_TRUE(config.keyFile.empty());
	EXPECT_TRUE(config.pfxPassword.empty());
}

TEST(TlsConfigTest, AssignFields)
{
	TlsConfig config;
	config.verifyPeer  = false;
	config.caFile      = "ca.pem";
	config.certFile    = "cert.pfx";
	config.pfxPassword = "secret";

	EXPECT_FALSE(config.verifyPeer);
	EXPECT_EQ(config.caFile, "ca.pem");
	EXPECT_EQ(config.certFile, "cert.pfx");
	EXPECT_EQ(config.pfxPassword, "secret");
}
