/*
 * Copyright 2017, Andrej Kislovskij
 *
 * This is PUBLIC DOMAIN software so use at your own risk as it comes
 * with no warranties. This code is yours to share, use and modify without
 * any restrictions or obligations.
 *
 * For more information see conwrap/LICENSE or refer refer to http://unlicense.org
 *
 * Author: gimesketvirtadieni at gmail dot com (Andrej Kislovskij)
 */

#pragma once

#include <conwrap2/ProcessorProxy.hpp>
#include <chrono>
#include <memory>
#include <thread>
#include <type_safe/optional_ref.hpp>
#include <vector>

#include "slim/ContainerBase.hpp"
#include "slim/log/log.hpp"


namespace slim
{
	namespace ts = type_safe;

	template <class ProducerType>
	class Multiplexor
	{
		public:
			Multiplexor(conwrap2::ProcessorProxy<std::unique_ptr<ContainerBase>> pp, std::vector<std::unique_ptr<ProducerType>> pr)
			: producers{std::move(pr)} {}

			// using Rule Of Zero
			~Multiplexor() = default;
			Multiplexor(const Multiplexor&) = delete;             // non-copyable
			Multiplexor& operator=(const Multiplexor&) = delete;  // non-assignable
			Multiplexor(Multiplexor&& rhs) = delete;              // non-movable
			Multiplexor& operator=(Multiplexor&& rhs) = delete;   // non-move-assignable

			inline bool isRunning()
			{
				auto result{false};

				for (auto& producerPtr : producers)
				{
					if (producerPtr->isRunning())
					{
						result = true;
						break;
					}
				}

				return result;
			}

			template<typename ConsumerType>
			inline ts::optional<std::chrono::milliseconds> produceChunk(const ConsumerType& consumer)
			{
				auto result{ts::optional<std::chrono::milliseconds>{ts::nullopt}};

				// setting up a producer if needed
				if (!currentProducer.has_value())
				{
					switchToNextProducer();
				}

				ts::with(currentProducer, [&](auto& producer)
				{
					result = producer.produceChunk(consumer);
				});

				// switching to the next producer if there were no chunks produced
				if (!result.has_value())
				{
					switchToNextProducer();

					// counting 'empty switches' to a different producer to issue pauses once none of the producers produce anything
					if ((++emptySwitches) >= producers.size())
					{
						result        = 100;
						emptySwitches = 0;
					}
				}
				else
				{
					emptySwitches = 0;
				}

				return result;
			}

			inline ts::optional<std::chrono::milliseconds> skipChunk()
			{
				auto result{ts::optional<std::chrono::milliseconds>{ts::nullopt}};

				ts::with(currentProducer, [&](auto& producer)
				{
					result = producer.skipChunk();
				});

				return result;
			}

			inline void start()
			{
				for (auto& producerPtr : producers)
				{
					// starting PCM data producer thread for Real-Time processing
					std::thread producerThread
					{
						[&producer = *producerPtr]
						{
							LOG(DEBUG) << LABELS{"slim"} << "PCM data capture thread was started (id=" << std::this_thread::get_id() << ")";

							try
							{
								producer.start();
							}
							catch (const Exception& error)
							{
								LOG(ERROR) << LABELS{"slim"} << "Error in producer thread: " << error;
							}
							catch (const std::exception& error)
							{
								LOG(ERROR) << LABELS{"slim"} << "Error in producer thread: " << error.what();
							}

							LOG(DEBUG) << LABELS{"slim"} << "PCM data capture thread was stopped (id=" << std::this_thread::get_id() << ")";
						}
					};

					// making sure it is up and running
					while (producerThread.joinable() && !producerPtr->isRunning())
					{
						std::this_thread::sleep_for(std::chrono::milliseconds{1});
					}

					// keeping producer's thread in dedicated vector
					threads.push_back(std::move(producerThread));
				}
			}

			inline void stop(bool gracefully = true)
			{
				// signalling all threads to stop processing
				for (auto& producerPtr : producers)
				{
					producerPtr->stop(gracefully);
				}

				// waiting for all threads to terminate
				for (auto& thread : threads)
				{
					if (thread.joinable())
					{
						thread.join();
					}
				}
			}

		protected:
			inline void switchToNextProducer()
			{
				if ((++currentProducerIndex) >= producers.size())
				{
					currentProducerIndex = 0;
				}
				
				if (producers.size() > 0)
				{
					currentProducer = ts::ref(*producers.at(currentProducerIndex));
				}
				else
				{
					currentProducer.reset();
				}
			}

		private:
			std::vector<std::unique_ptr<ProducerType>> producers;
			unsigned int                               currentProducerIndex{0};
			ts::optional_ref<ProducerType>             currentProducer{ts::nullopt};
			std::vector<std::thread>                   threads;
			unsigned int                               emptySwitches{0};
	};
}
