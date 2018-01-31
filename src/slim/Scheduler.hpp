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

#include <conwrap/ProcessorProxy.hpp>
#include <memory>
#include <thread>
#include <vector>

#include "slim/ContainerBase.hpp"
#include "slim/Pipeline.hpp"


namespace slim
{
	template<typename Source, typename Destination>
	class Scheduler
	{
		public:
			explicit Scheduler(std::vector<Pipeline<Source, Destination>> p)
			: pipelines{std::move(p)}
			, processorProxyPtr{nullptr} {}

			// using Rule Of Zero
			~Scheduler() = default;
			Scheduler(const Scheduler&) = delete;             // non-copyable
			Scheduler& operator=(const Scheduler&) = delete;  // non-assignable
			Scheduler(Scheduler&& rhs) = default;
			Scheduler& operator=(Scheduler&& rhs) = default;

			void setProcessorProxy(conwrap::ProcessorProxy<ContainerBase>* p)
			{
				processorProxyPtr = p;

				// propogating processor to all pipelines
				for (auto& pipeline : pipelines)
				{
					pipeline.setProcessorProxy(processorProxyPtr);
				}
			}

			void start()
			{
				for (auto& pipeline : pipelines)
				{
					auto producerTask = [&]
					{
						LOG(DEBUG) << "Starting PCM data capture thread (id=" << this << ")";

						try
						{
							pipeline.start();
						}
						catch (const slim::Exception& error)
						{
							LOG(ERROR) << error;
						}

						LOG(DEBUG) << "Stopping PCM data capture thread (id=" << this << ")";
					};

					// starting producer and making sure it is up and running before creating a consumer
					std::thread producerThread{producerTask};
					while(producerThread.joinable() && !pipeline.isProducing())
					{
						std::this_thread::sleep_for(std::chrono::milliseconds{10});
					}

					// adding producer thread for Real-Time processing
					threads.emplace_back(std::move(producerThread));
				}

				// creating one single thread for consuming PCM data for all pipelines
				threads.emplace_back(std::thread
				{
					[&]
					{
						LOG(DEBUG) << "Starting streamer thread (id=" << this << ")";

						stream();

						LOG(DEBUG) << "Stopping streamer thread (id=" << this << ")";
					}
				});
			}

			void stop(bool gracefully = true)
			{
				// signalling all pipelines to stop processing
				for (auto& pipeline : pipelines)
				{
					try
					{
						pipeline.stop(gracefully);
					}
					catch (const slim::Exception& error)
					{
						LOG(ERROR) << error;
					}
				}

				// waiting for all pipelines' threads to terminate
				for (auto& thread : threads)
				{
					if (thread.joinable())
					{
						thread.join();
					}
				}
			}

		protected:
			inline void stream()
			{
				for(auto producing{true}, available{true}; producing;)
				{
					producing = false;
					available = false;

					for (auto& pipeline : pipelines)
					{
						// if there is PCM data ready to be read
						if ((producing = pipeline.isProducing()) && (available = pipeline.isAvailable()))
						{
							// submitting a task to the process which would process X chunks at most
							processorProxyPtr->process([&]
							{
								// TODO: calculate maxChunks per processing quantum
								// TODO: consider exit reason for scheduling tasks
								pipeline.processChunks(5);
							});
						}
					}

					// if no PCM data is available in any of the pipelines then pause processing
					if (!available)
					{
						// TODO: cruise control should be implemented
						std::this_thread::sleep_for(std::chrono::milliseconds{20});
					}
				}
			}

		private:
			std::vector<Pipeline<Source, Destination>> pipelines;
			std::vector<std::thread>                   threads;
			conwrap::ProcessorProxy<ContainerBase>*    processorProxyPtr{nullptr};
	};
}
