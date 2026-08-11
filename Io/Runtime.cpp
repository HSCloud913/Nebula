//
// Created by hscloud on 26. 7. 24.
//

#include "Io/Runtime.h"

#include "Io/Engine.h"
#include "Time/Timer/TimerWheel.h"



namespace ne::io
{
	Runtime::Runtime(const EngineType _type)
		: engine(MakeEngine(_type))
		, timerWheel(std::make_unique<ne::time::TimerWheel>())
		, context(std::make_unique<Context>(*engine, timerWheel.get()))
	{
	}

	Runtime::~Runtime() = default;



	bool_t Runtime::IsValid() const noexcept { return engine != nullptr && engine->IsValid(); }
}
