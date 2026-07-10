//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include <cstdint>
#include <unordered_map>
#include "Network/Protocol/Http/Http2/Frame.h"
#include "Base/Type.h"

BEGIN_NS (ne::network::http_2)
// 연결 레벨 + 스트림 레벨 흐름 제어.
// WINDOW_UPDATE 프레임 값을 추적하고 전송 허가량을 관리.
class FlowController
{
public:
	FlowController() noexcept = default;
	~FlowController() = default;

	NEBULA_NON_COPYABLE_MOVABLE(FlowController)

private:
	int32_t connectionWindow{ static_cast<int32_t>(kDefaultWindowSize) };
	std::unordered_map<uint32_t, int32_t> streamWindows;

public:
	[[nodiscard]] int32_t ConnectionWindow() const noexcept { return connectionWindow; }

	[[nodiscard]] int32_t StreamWindow(uint32_t _streamId) const noexcept
	{
		auto it = streamWindows.find(_streamId);
		return it != streamWindows.end() ? it->second : static_cast<int32_t>(kDefaultWindowSize);
	}

	void ConsumeConnectionWindow(int32_t _bytes) { connectionWindow -= _bytes; }

	void ConsumeStreamWindow(uint32_t _streamId, int32_t _bytes) { streamWindows[_streamId] -= _bytes; }

	void UpdateConnectionWindow(int32_t _increment) { connectionWindow += _increment; }

	void UpdateStreamWindow(uint32_t _streamId, int32_t _increment) { streamWindows[_streamId] += _increment; }

	void InitStream(uint32_t _streamId) { streamWindows[_streamId] = static_cast<int32_t>(kDefaultWindowSize); }

	void RemoveStream(uint32_t _streamId) { streamWindows.erase(_streamId); }
};

END_NS
