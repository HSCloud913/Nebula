//
// Created by nebula on 24. 11. 10.
//

#pragma once
#include <functional>
#include <ranges>
#include <memory>
#include <unordered_set>
#include "Base/Type.h"

BEGIN_NS(ne)
	/**
	 * @class IObserver
	 * @brief Observer 패턴의 관찰자 인터페이스입니다.
	 *
	 * 고유 id로 식별되며, ISubject::Notify()를 통해 Update()가 호출됩니다.
	 */
	class IObserver
	{
	public:
		explicit IObserver(const uint_t _id)
			: id(_id) {};
		virtual ~IObserver() = default;

	protected:
		uint_t id;

	public:
		[[nodiscard]] uint_t GetID() const { return id; }

		virtual void_t Update(const std::any& _data) = 0;
	};

	/**
	 * @class ISubject
	 * @brief Observer 패턴의 관찰 대상(주체)입니다.
	 *
	 * IObserver를 id 기준으로 등록/해제(Attach/Detach)하고, Notify()로 전체 또는
	 * 지정한 id 집합에만 선택적으로 이벤트를 전파합니다.
	 */
	class ISubject
	{
	public:
		ISubject() = default;
		virtual ~ISubject() = default;

	protected:
		std::unordered_map<uint_t, std::shared_ptr<IObserver>> observers;

	public:
		void_t Attach(const std::shared_ptr<IObserver>& _observer) { observers[_observer->GetID()] = _observer; }
		void_t Detach(const uint_t _id) { observers.erase(_id); }

		void_t Notify(const std::any& _data) { for (const auto& observer : observers | std::views::values) { observer->Update(_data); } }
		void_t Notify(const std::unordered_set<uint_t>& _ids, const std::any& _data)
		{
			auto ObserverFilter = [&_ids](const auto& _pair) { return _ids.contains(_pair.first); };

			for (auto& [id, observer] : observers | std::views::filter(ObserverFilter)) { observer->Update(_data); }
		}
	};

END_NS
