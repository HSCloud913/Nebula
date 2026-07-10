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
