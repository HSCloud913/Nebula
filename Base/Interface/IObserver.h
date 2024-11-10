//
// Created by hsclo on 24. 11. 10.
//

#ifndef IOBSERVER_H
#define IOBSERVER_H

#include <functional>
#include <list>
#include <ranges>
#include <memory>
#include <unordered_set>

#include "Type.h"

BEGIN_NS(ne)
	class IObserver
	{
	public:
		explicit IObserver(const uint_t _id)
			: id(_id)
		{
		};
		virtual ~IObserver() = default;

	protected:
		uint_t id;

	public:
		[[nodiscard]] uint_t GetID() const
		{
			return id;
		}

		virtual void Update(const std::any& _data) = 0;
	};

	class ISubject
	{
	public:
		ISubject() = default;
		virtual ~ISubject() = default;

	protected:
		std::unordered_map<uint_t, std::shared_ptr<IObserver>> observers;

	public:
		void Attach(const std::shared_ptr<IObserver>& _observer)
		{
			observers[_observer->GetID()] = _observer;
		}

		void Detach(const uint_t _id)
		{
			observers.erase(_id);
		}

		void Notify(const std::any& _data)
		{
			for (auto& [id, observer] : observers)
			{
				observer->Update(_data);
			}
		}

		void Notify(const std::unordered_set<uint_t>& _ids, const std::any& _data)
		{
			auto ObserverFilter = [&_ids](const auto& _pair)
			{
				return _ids.contains(_pair.first);
			};

			for (auto& [id, observer] : observers | std::views::filter(ObserverFilter))
			{
				observer->Update(_data);
			}
		}
	};

END_NS

#endif //IOBSERVER_H
