//
// Created by hscloud on 24. 9. 8.
//

#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>
#include <vector>
#include "Log/Logger.h"
#include "Log/Sink/ConsoleSink.h"
#include "Log/Sink/FileSink.h"
#include "Log/Sink/RotatingFileSink.h"

namespace
{
	int CountNonEmptyLines(const std::string& _path)
	{
		std::ifstream file(_path);
		if (!file.is_open()) return -1;

		int lines = 0;
		std::string line;
		while (std::getline(file, line))
			if (!line.empty()) ++lines;

		return lines;
	}
}

class LoggerTest :public ::testing::Test
{
protected:
	void TearDown() override
	{
		for (const auto* f : { "log_level.txt", "write.txt", "multi_threaded_log.txt", "threadsafe.txt", "flush_on_destruct.txt", "non_blocking.txt", "explicit_flush.txt", "sink_a.txt", "sink_b.txt" })
			std::filesystem::remove(f);
		for (const auto* f : { "rot.log", "rot.1.log", "rot.2.log" })
			std::filesystem::remove(f);
	}
};



TEST_F(LoggerTest, LogLevel)
{
	ne::log::Logger logger("log_level.txt");
	logger.SetLogLevel(ne::log::LogLevel::NE_DEBUG);

	EXPECT_EQ(ne::log::LogLevel::NE_DEBUG, logger.GetLogLevel());
}

// 블록 스코프 종료로 소멸자가 드레인+flush 를 보장한 뒤 파일 검증.
TEST_F(LoggerTest, Write)
{
	{
		ne::Logger logger("write.txt"); // 루트 별칭 + 편의 FileSink 생성자
		logger.Trace("Test message");
		logger.Debug("Test message");
		logger.Info("Test message");
		logger.Warning("Test message");
		logger.Error("Test message");
		logger.Fatal("Test message");
	}

	EXPECT_EQ(CountNonEmptyLines("write.txt"), 6);
}

// Flush(): 소멸 없이도 지금까지의 로그가 파일에 반영된다.
TEST_F(LoggerTest, ExplicitFlush)
{
	ne::Logger logger("explicit_flush.txt");
	for (int i = 0; i < 50; ++i) logger.Info("line " + std::to_string(i));

	logger.Flush();

	EXPECT_EQ(CountNonEmptyLines("explicit_flush.txt"), 50);
}

// 멀티 sink: 하나의 Logger 가 두 파일 sink 에 동시에 기록한다.
TEST_F(LoggerTest, MultiSink)
{
	{
		std::vector<std::unique_ptr<ne::log::ISink>> sinks;
		sinks.push_back(std::make_unique<ne::log::FileSink>("sink_a.txt"));
		sinks.push_back(std::make_unique<ne::log::FileSink>("sink_b.txt"));

		ne::Logger logger(std::move(sinks));
		for (int i = 0; i < 10; ++i) logger.Info("dup " + std::to_string(i));
		logger.Flush();
	}

	EXPECT_EQ(CountNonEmptyLines("sink_a.txt"), 10);
	EXPECT_EQ(CountNonEmptyLines("sink_b.txt"), 10);
}

// 크기 기반 회전: 많은 로그를 쓰면 백업 파일(rot.1.log 등)이 생긴다.
TEST_F(LoggerTest, RotatingFileSink)
{
	{
		std::vector<std::unique_ptr<ne::log::ISink>> sinks;
		sinks.push_back(std::make_unique<ne::log::RotatingFileSink>("rot.log", 256, 3)); // 작은 최대 크기로 회전 유도

		ne::Logger logger(std::move(sinks));
		for (int i = 0; i < 200; ++i) logger.Info("rotating log line " + std::to_string(i));
		logger.Flush();
	}

	EXPECT_TRUE(std::filesystem::exists("rot.log"));
	EXPECT_TRUE(std::filesystem::exists("rot.1.log")); // 최소 한 번은 회전됨
}

// ConsoleSink 는 구성/기록이 크래시 없이 동작한다(출력 검증은 생략).
TEST_F(LoggerTest, ConsoleSinkSmoke)
{
	ne::Logger logger(std::make_unique<ne::log::ConsoleSink>());
	logger.Info("console sink smoke");
	logger.Flush();
	SUCCEED();
}

TEST_F(LoggerTest, MultiThreadedLogging)
{
	constexpr std::size_t numThreads = 10;
	constexpr int msgsPerThread = 10;

	{
		ne::Logger logger("multi_threaded_log.txt");
		std::vector<std::thread> threads;
		for (std::size_t i = 0; i < numThreads; ++i)
			threads.emplace_back([&logger, i] { for (int j = 0; j < msgsPerThread; ++j) logger.Info("Thread " + std::to_string(i) + " log " + std::to_string(j)); });

		for (auto& t : threads) if (t.joinable()) t.join();
	}

	EXPECT_EQ(CountNonEmptyLines("multi_threaded_log.txt"), static_cast<int>(numThreads * msgsPerThread));
}

TEST_F(LoggerTest, ThreadSafeSetLogLevel)
{
	ne::Logger logger("threadsafe.txt");

	auto task = [&logger]
	{
		for (int i = 0; i < 100; ++i)
		{
			logger.SetLogLevel(ne::log::LogLevel::NE_TRACE);
			logger.Trace("t");
			logger.SetLogLevel(ne::log::LogLevel::NE_ERROR);
			logger.Error("e");
		}
	};

	std::thread t1(task);
	std::thread t2(task);
	t1.join();
	t2.join();

	logger.Flush(); // 크래시 없이 완료되면 성공
	SUCCEED();
}

// 8스레드 × 1000 = 8000줄 — 소멸자의 최종 드레인이 유실 없이 동작하는지 검증.
TEST_F(LoggerTest, FlushOnDestruct)
{
	constexpr std::size_t numThreads = 8;
	constexpr int msgsPerThread = 1000;

	{
		ne::Logger logger("flush_on_destruct.txt");
		std::vector<std::thread> threads;
		for (std::size_t i = 0; i < numThreads; ++i)
			threads.emplace_back([&logger, i] { for (int j = 0; j < msgsPerThread; ++j) logger.Info("Thread " + std::to_string(i) + " msg " + std::to_string(j)); });

		for (auto& t : threads) if (t.joinable()) t.join();
	}

	EXPECT_EQ(CountNonEmptyLines("flush_on_destruct.txt"), static_cast<int>(numThreads * msgsPerThread));
}

// Info() 호출이 동기 디스크 I/O 없이 즉시 반환되는지 타이밍으로 간접 검증.
TEST_F(LoggerTest, InfoCallIsNonBlocking)
{
	ne::Logger logger("non_blocking.txt");

	const auto start = std::chrono::steady_clock::now();
	for (int i = 0; i < 1000; ++i) logger.Info("Non-blocking test message " + std::to_string(i));
	const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();

	EXPECT_LT(elapsed, 100) << "Info() calls took " << elapsed << "ms — likely blocking";
}
