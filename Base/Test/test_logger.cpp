//
// Created by hscloud on 24. 9. 8.
//

#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>
#include "Logger.h"



class LoggerTest : public ::testing::Test
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
    }
};



TEST_F(LoggerTest, LogLevel)
{
    ne::Logger logger("log_level.txt");
    logger.SetLogLevel(ne::LogLevel::DBG);

    EXPECT_EQ(ne::LogLevel::DBG, logger.GetLogLevel());
    EXPECT_TRUE(logger.Close());
}

TEST_F(LoggerTest, OpenClose)
{
    ne::Logger logger("open_close.txt");

    EXPECT_TRUE(logger.Close());
}

TEST_F(LoggerTest, Write)
{
    ne::Logger logger("write.txt");

    logger.Trace("Test message");
    logger.Debug("Test message");
    logger.Info("Test message");
    logger.Warning("Test message");
    logger.Error("Test message");
    logger.Fatal("Test message");

    EXPECT_TRUE(logger.Close());

    // verify
    std::ifstream logFile("write.txt");
    ASSERT_TRUE(logFile.is_open());

    std::string line;
    while (std::getline(logFile, line))
    {
        EXPECT_FALSE(line.empty());
    }
}

TEST_F(LoggerTest, MultiThreadedLogging)
{
    const std::size_t numThreads = 10;
    ne::Logger logger("multi_threaded_log.txt");

    std::vector<std::thread> threads;

    auto loggingTask = [&logger](int threadId)
    {
        for (int i = 0; i < 10; ++i)
        {
            logger.Info("Thread " + std::to_string(threadId) + " log message " + std::to_string(i));
        }
    };

    for (std::size_t i = 0; i < numThreads; ++i)
    {
        threads.emplace_back(loggingTask, static_cast<int>(i));
    }

    for (auto& thread : threads)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }

    auto result = logger.Close();
    EXPECT_TRUE(result);

    // verify
    std::ifstream logFile("multi_threaded_log.txt");
    ASSERT_TRUE(logFile.is_open());

    std::string line;
    while (std::getline(logFile, line))
    {
        EXPECT_FALSE(line.empty());
    }
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
            logger.SetLogLevel(ne::LogLevel::TRACE);
            logger.Trace("Setting log level to TRACE");

            logger.SetLogLevel(ne::LogLevel::ERR);
            logger.Error("Setting log level to ERROR");

            logger.SetLogLevel(ne::LogLevel::DBG);
            logger.Info("Setting log level to DBG");
        }
    };

    std::thread t1(SetLogLevelTask);
    std::thread t2(SetLogLevelTask);

    t1.join();
    t2.join();

    EXPECT_TRUE(logger.Close());
}
