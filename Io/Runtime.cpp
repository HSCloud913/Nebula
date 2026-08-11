//
// Created by hscloud on 26. 7. 24.
//

#include "Io/Runtime.h"

#include "Io/Engine.h"
#include "Time/TimerQueue.h"



namespace ne::io
{
	Runtime::Runtime(const EngineType _type)
		: engine(MakeEngine(_type))
		, timer(std::make_unique<ne::time::TimerQueue>())
		, context(std::make_unique<Context>(*engine, timer.get()))
	{
	}

	Runtime::~Runtime() = default;



	bool_t Runtime::IsValid() const noexcept { return engine != nullptr && engine->IsValid(); }
}
