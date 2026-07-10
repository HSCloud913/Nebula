//
// Created by hscloud on 24. 9. 8.
//

#include <gtest/gtest.h>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <thread>
#include <vector>
#include "Log/Logger.h"



class LoggerTest :public ::testing::Test
{
protected:
	void TearDown() override
	{
		std::filesystem::remove("log_level.txt");
		std::filesystem::remove("open_close.txt");
		std::filesystem::remove("write.txt");
		std::filesystem::remove("multi_threaded_log.txt");
		std::filesystem::remove("concurrent_open_close.txt");
		std::filesystem::remove("threadsafe_set_log_level.txt");
		std::filesystem::remove("flush_on_destruct.txt");
		std::filesystem::remove("non_blocking.txt");
	}
};



TEST_F(LoggerTest, LogLevel)
{
	ne::Logger logger("log_level.txt");
	logger.SetLogLevel(ne::LogLevel::NE_DEBUG);

	EXPECT_EQ(ne::LogLevel::NE_DEBUG, logger.GetLogLevel());
	EXPECT_TRUE(logger.Close());
}

TEST_F(LoggerTest, OpenClose)
{
	ne::Logger logger("open_close.txt");

	EXPECT_TRUE(logger.Close());
}

// 블록 스코프 종료로 소멸자가 FlushPending()을 보장한 뒤 파일 검증.
TEST_F(LoggerTest, Write)
{
	{
		ne::Logger logger("write.txt");
		logger.Trace("Test message");
		logger.Debug("Test message");
		logger.Info("Test message");
		logger.Warning("Test message");
		logger.Error("Test message");
		logger.Fatal("Test message");
		// 소멸자: running=false → join → FlushPending → Close
	}

	std::ifstream logFile("write.txt");
	ASSERT_TRUE(logFile.is_open());

	int lineCount = 0;
	std::string line;
	while (std::getline(logFile, line))
	{
		EXPECT_FALSE(line.empty());
		++lineCount;
	}
	EXPECT_EQ(lineCount, 6);
}

TEST_F(LoggerTest, MultiThreadedLogging)
{
	constexpr std::size_t numThreads = 10;
	constexpr int msgsPerThread = 10;

	{
		ne::Logger logger("multi_threaded_log.txt");
		std::vector<std::thread> threads;

		for (std::size_t i = 0; i < numThreads; ++i)
		{
			threads.emplace_back([&logger, i]() { for (int j = 0; j < msgsPerThread; ++j) logger.Info("Thread " + std::to_string(i) + " log " + std::to_string(j)); });
		}

		for (auto& t : threads)
			if (t.joinable()) t.join();
		// 소멸자가 FlushPending으로 전부 씀
	}

	std::ifstream logFile("multi_threaded_log.txt");
	ASSERT_TRUE(logFile.is_open());

	int lineCount = 0;
	std::string line;
	while (std::getline(logFile, line))
	{
		EXPECT_FALSE(line.empty());
		++lineCount;
	}
	EXPECT_EQ(lineCount, static_cast<int>(numThreads * msgsPerThread));
}

TEST_F(LoggerTest, ConcurrentOpenClose)
{
	ne::Logger logger("concurrent_open_close.txt");
	EXPECT_TRUE(logger.Close());

	auto OpenCloseTask = [&logger]()
	{
		for (int i = 0; i < 10; ++i)
		{
			EXPECT_TRUE(logger.Open("concurrent_open_close.txt"));
			EXPECT_TRUE(logger.Close());
		}
	};

	std::thread t1(OpenCloseTask);
	std::thread t2(OpenCloseTask);

	t1.join();
	t2.join();

	EXPECT_TRUE(logger.Close());
}

TEST_F(LoggerTest, ThreadSafeSetLogLevel)
{
	ne::Logger logger("threadsafe_set_log_level.txt");

	auto SetLogLevelTask = [&logger]()
	{
		for (int i = 0; i < 10; ++i)
		{
			logger.SetLogLevel(ne::LogLevel::NE_TRACE);
			logger.Trace("Setting log level to TRACE");

			logger.SetLogLevel(ne::LogLevel::NE_ERROR);
			logger.Error("Setting log level to ERROR");

			logger.SetLogLevel(ne::LogLevel::NE_DEBUG);
			logger.Info("Setting log level to DBG");
		}
	};

	std::thread t1(SetLogLevelTask);
	std::thread t2(SetLogLevelTask);

	t1.join();
	t2.join();

	EXPECT_TRUE(logger.Close());
}

// 8스레드 × 1000 = 8000줄 — 소멸자의 FlushPending이 유실 없이 동작하는지 검증.
TEST_F(LoggerTest, FlushOnDestruct)
{
	constexpr std::size_t numThreads = 8;
	constexpr int msgsPerThread = 1000;

	{
		ne::Logger logger("flush_on_destruct.txt");
		std::vector<std::thread> threads;

		for (std::size_t i = 0; i < numThreads; ++i)
		{
			threads.emplace_back([&logger, i]() { for (int j = 0; j < msgsPerThread; ++j) logger.Info("Thread " + std::to_string(i) + " msg " + std::to_string(j)); });
		}

		for (auto& t : threads)
			if (t.joinable()) t.join();
	}

	std::ifstream logFile("flush_on_destruct.txt");
	ASSERT_TRUE(logFile.is_open());

	int lineCount = 0;
	std::string line;
	while (std::getline(logFile, line)) { if (!line.empty()) ++lineCount; }
	EXPECT_EQ(lineCount, static_cast<int>(numThreads * msgsPerThread));
}

// Info() 1000회 호출이 즉시 반환되는지 타이밍으로 간접 검증.
// 동기 디스크 I/O 1000회라면 100ms를 훨씬 초과함.
TEST_F(LoggerTest, InfoCallIsNonBlocking)
{
	ne::Logger logger("non_blocking.txt");

	const auto start = std::chrono::steady_clock::now();
	for (int i = 0; i < 1000; ++i) logger.Info("Non-blocking test message " + std::to_string(i));
	const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();

	EXPECT_LT(elapsed, 100) << "Info() calls took " << elapsed << "ms — likely blocking";
}
