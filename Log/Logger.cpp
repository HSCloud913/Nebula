//
// Created by nebula on 24. 5. 17.
//

#include "Log/Logger.h"

#include "Log/Sink/FileSink.h"



namespace ne::log
{
	Logger::Logger(std::unique_ptr<ISink> _sink)
	{
		sinks.push_back(std::move(_sink));
		backendThread = std::thread(&Logger::BackendLoop, this);
	}

	Logger::Logger(std::vector<std::unique_ptr<ISink>> _sinks)
		: sinks(std::move(_sinks))
	{
		backendThread = std::thread(&Logger::BackendLoop, this);
	}

	Logger::Logger(const string_t& _fileName)
	{
		sinks.push_back(std::make_unique<FileSink>(_fileName));
		backendThread = std::thread(&Logger::BackendLoop, this);
	}

	Logger::~Logger()
	{
		// running 변경은 wakeMutex(백엔드 wait 술어가 읽는 락) 하에서 하고 깨운다 — lost-wakeup 방지.
		{
			std::lock_guard<std::mutex> lock(wakeMutex);
			isRunning.store(false, std::memory_order_relaxed);
		}
		wake.notify_one();

		if (backendThread.joinable()) backendThread.join();

		DrainToSinks(); // 백엔드 종료 후 남은 레코드 최종 기록(파괴 중 동시 로깅은 계약 위반)
		FlushSinks();
	}



	void_t Logger::Write(const LogLevel _logLevel, const string_t& _message)
	{
		if (_logLevel < logLevel.load(std::memory_order_relaxed)) return;

		queue.Enqueue(LogRecord{ _logLevel, _message, std::chrono::system_clock::now() });
		// Enqueue 완료 후 pending 세팅 → 백엔드가 놓쳐도(false-empty) 다음 wait 술어에서 재드레인.
		isPending.store(true, std::memory_order_release);
		wake.notify_one();
	}

	void_t Logger::Flush()
	{
		ulonglong_t generation = 0;
		{
			std::lock_guard<std::mutex> lock(wakeMutex);
			generation = flushRequest.fetch_add(1, std::memory_order_acq_rel) + 1;
		}
		wake.notify_one();

		// 백엔드가 이 세대까지 드레인+flush 를 끝낼 때까지 대기.
		for (ulonglong_t done = flushComplete.load(std::memory_order_acquire); done < generation; done = flushComplete.load(std::memory_order_acquire))
			flushComplete.wait(done, std::memory_order_acquire);
	}

	void_t Logger::BackendLoop()
	{
		ulonglong_t lastFlush = 0;
		while (isRunning.load(std::memory_order_relaxed))
		{
			// 유휴 시 스핀 대신 condvar 로 블록. pending.exchange 로 lost-wakeup 을 흡수하고, flush 요청도 깨움 조건에 포함.
			{
				std::unique_lock<std::mutex> lock(wakeMutex);
				wake.wait(lock, [this, lastFlush]
				{
					return !isRunning.load(std::memory_order_relaxed)
						|| isPending.exchange(false, std::memory_order_acq_rel)
						|| flushRequest.load(std::memory_order_acquire) != lastFlush;
				});
			}

			DrainToSinks();

			if (const ulonglong_t request = flushRequest.load(std::memory_order_acquire); request != lastFlush)
			{
				FlushSinks();
				lastFlush = request;
				flushComplete.store(request, std::memory_order_release);
				flushComplete.notify_all();
			}
		}
	}

	void_t Logger::DrainToSinks()
	{
		LogRecord record;
		while (queue.Dequeue(record))
		{
			for (const auto& sink : sinks)
			{
				sink->Write(record);
				if (record.level >= LogLevel::NE_FATAL) sink->Flush(); // FATAL 은 즉시 내보내 크래시 시 유실 방지
			}
		}
	}

	void_t Logger::FlushSinks()
	{
		for (const auto& sink : sinks) sink->Flush();
	}
}
