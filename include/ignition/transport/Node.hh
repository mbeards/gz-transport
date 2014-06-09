/*
 * Copyright (C) 2014 Open Source Robotics Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
*/

#ifndef __IGN_TRANSPORT_NODE_HH_INCLUDED__
#define __IGN_TRANSPORT_NODE_HH_INCLUDED__

#include <google/protobuf/message.h>
#include <uuid/uuid.h>
#include <algorithm>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "ignition/transport/NodePrivate.hh"
#include "ignition/transport/Packet.hh"
#include "ignition/transport/RepHandler.hh"
#include "ignition/transport/RepStorage.hh"
#include "ignition/transport/ReqHandler.hh"
#include "ignition/transport/ReqStorage.hh"
#include "ignition/transport/SubscriptionHandler.hh"
#include "ignition/transport/SubscriptionStorage.hh"
#include "ignition/transport/TransportTypes.hh"

namespace ignition
{
  namespace transport
  {
    /// \class Node Node.hh
    /// \brief A transport node to send and receive data using a
    /// publication/subscription paradigm.
    class Node
    {
      /// \brief Constructor.
      /// \param[in] _verbose true for enabling verbose mode.
      public: Node(bool _verbose = false);

      /// \brief Destructor.
      public: virtual ~Node();

      /// \brief Advertise a new topic.
      /// \param[in] _topic Topic to be advertised.
      /// \param[in] _scope Topic scope.
      public: void Advertise(const std::string &_topic,
                             const Scope &_scope = Scope::All);

      /// \brief Unadvertise a topic.
      /// \param[in] _topic Topic to be unadvertised.
      public: void Unadvertise(const std::string &_topic);

      /// \brief Publish a message.
      /// \param[in] _topic Topic to be published.
      /// \param[in] _message protobuf message.
      /// \return 0 when success.
      public: int Publish(const std::string &_topic,
                          const ProtoMsg &_msg);

      /// \brief Subscribe to a topic registering a callback. In this version
      /// the callback is a free function.
      /// \param[in] _topic Topic to be subscribed.
      /// \param[in] _cb Pointer to the callback function.
      public: template<typename T> void Subscribe(
          const std::string &_topic,
          void(*_cb)(const std::string &, const T &))
      {
        std::lock_guard<std::recursive_mutex> lock(this->dataPtr->mutex);

        // Create a new subscription handler.
        std::shared_ptr<SubscriptionHandler<T>> subscrHandlerPtr(
            new SubscriptionHandler<T>(this->nUuidStr));

        // Insert the callback into the handler.
        subscrHandlerPtr->SetCallback(_cb);

        // Store the subscription handler. Each subscription handler is
        // associated with a topic. When the receiving thread gets new data,
        // it will recover the subscription handler associated to the topic and
        // will invoke the callback.
        this->dataPtr->localSubscriptions.AddSubscriptionHandler(
          _topic, this->nUuidStr, subscrHandlerPtr);

        // Add the topic to the list of subscribed topics (if it was not before)
        if (std::find(this->topicsSubscribed.begin(),
          this->topicsSubscribed.end(), _topic) == this->topicsSubscribed.end())
        {
          this->topicsSubscribed.push_back(_topic);
        }

        // Discover the list of nodes that publish on the topic.
        this->dataPtr->discovery->Discover(false, _topic);
      }

      /// \brief Subscribe to a topic registering a callback. In this version
      /// the callback is a member function.
      /// \param[in] _topic Topic to be subscribed.
      /// \param[in] _cb Pointer to the callback member function.
      /// \param[in] _obj Instance.
      public: template<typename C, typename T> void Subscribe(
          const std::string &_topic,
          void(C::*_cb)(const std::string &, const T &), C* _obj)
      {
        std::lock_guard<std::recursive_mutex> lock(this->dataPtr->mutex);

        // Create a new subscription handler.
        std::shared_ptr<SubscriptionHandler<T>> subscrHandlerPtr(
          new SubscriptionHandler<T>(this->nUuidStr));

        // Insert the callback into the handler by creating a free function.
        subscrHandlerPtr->SetCallback(
          std::bind(_cb, _obj, std::placeholders::_1, std::placeholders::_2));

        // Store the subscription handler. Each subscription handler is
        // associated with a topic. When the receiving thread gets new data,
        // it will recover the subscription handler associated to the topic and
        // will invoke the callback.
        this->dataPtr->localSubscriptions.AddSubscriptionHandler(
          _topic, this->nUuidStr, subscrHandlerPtr);

        // Add the topic to the list of subscribed topics (if it was not before)
        if (std::find(this->topicsSubscribed.begin(),
          this->topicsSubscribed.end(), _topic) == this->topicsSubscribed.end())
        {
          this->topicsSubscribed.push_back(_topic);
        }

        // Discover the list of nodes that publish on the topic.
        this->dataPtr->discovery->Discover(false, _topic);
      }

      /// \brief Unsubscribe to a topic.
      /// \param[in] _topic Topic to be unsubscribed.
      public: void Unsubscribe(const std::string &_topic);

      /// \brief Advertise a new service call.
      /// \param[in] _topic Topic name associated to the service call.
      /// \param[in] _cb Callback to handle the service request.
      /// \param[in] _scope Topic scope.
      public: template<typename T1, typename T2> void Advertise(
        const std::string &_topic,
        void(*_cb)(const std::string &, const T1 &, T2 &, bool &),
        const Scope &_scope = Scope::All)
      {
        std::lock_guard<std::recursive_mutex> lock(this->dataPtr->mutex);

        // Add the topic to the list of advertised service calls
        if (std::find(this->srvsAdvertised.begin(), this->srvsAdvertised.end(),
              _topic) == this->srvsAdvertised.end())
        {
          this->srvsAdvertised.push_back(_topic);
        }

        // Create a new service reply handler.
        std::shared_ptr<RepHandler<T1, T2>> repHandlerPtr(
          new RepHandler<T1, T2>(this->nUuidStr));

        // Insert the callback into the handler.
        repHandlerPtr->SetCallback(_cb);

        // Store the replier handler. Each replier handler is
        // associated with a topic. When the receiving thread gets new requests,
        // it will recover the replier handler associated to the topic and
        // will invoke the service call.
        this->dataPtr->repliers.AddRepHandler(
          _topic, this->nUuidStr, repHandlerPtr);

        // Notify the discovery service to register and advertise my responser.
        this->dataPtr->discovery->Advertise(AdvertiseType::Srv, _topic,
          this->dataPtr->myReplierAddress, "", this->nUuidStr, _scope);
      }

      /// \brief Request a new service call using a non-blocking call.
      /// \param[in] _topic Topic requested.
      /// \param[in] _req Protobuf message containing the request's parameters.
      /// \param[in] _cb Pointer to the callback function executed when the
      /// response arrives.
      /// \return 0 when success.
      public: template<typename T1, typename T2> void Request(
        const std::string &_topic,
        const T1 &_req,
        void(*_cb)(const std::string &_topic, const T2 &, bool))
      {
        std::lock_guard<std::recursive_mutex> lock(this->dataPtr->mutex);

        // If the responser is within my process.
        IRepHandler_M repHandlers;
        this->dataPtr->repliers.GetRepHandlers(_topic, repHandlers);
        if (!repHandlers.empty())
        {
          // There is a responser in my process, let's use it.
          T2 rep;
          bool result;
          IRepHandlerPtr repHandler = repHandlers.begin()->second;
          repHandler->RunLocalCallback(_topic, _req, rep, result);
          _cb(_topic, rep, result);
          return;
        }

        // Create a new request handler.
        std::shared_ptr<ReqHandler<T1, T2>> reqHandlerPtr(
          new ReqHandler<T1, T2>(this->nUuidStr));

        // Insert the request's parameters.
        reqHandlerPtr->SetMessage(_req);

        // Insert the callback into the handler.
        reqHandlerPtr->SetCallback(_cb);

        // Store the request handler.
        this->dataPtr->requests.AddReqHandler(
          _topic, this->nUuidStr, reqHandlerPtr);

        // If the responser's address is known, make the request.
        Addresses_M addresses;
        if (this->dataPtr->discovery->GetTopicAddresses(_topic, addresses))
          this->dataPtr->SendPendingRemoteReqs(_topic);
        else
        {
          // Discover the service call responser.
          this->dataPtr->discovery->Discover(true, _topic);
        }
      }

      /// \brief Request a new service call using a blocking call.
      /// \param[in] _topic Topic requested.
      /// \param[in] _req Protobuf message containing the request's parameters.
      /// \param[in] _timeout The request will timeout after '_timeout' ms.
      /// \param[out] _res Protobuf message containing the response.
      /// \param[out] _result Result of the service call.
      /// \return true when the request did not timeout.
      public: template<typename T1, typename T2> bool Request(
        const std::string &_topic,
        const T1 &_req,
        const unsigned int &_timeout,
        T2 &_rep,
        bool &_result)
      {
        std::unique_lock<std::recursive_mutex> lk(this->dataPtr->mutex);

        // If the responser is within my process.
        IRepHandler_M repHandlers;
        this->dataPtr->repliers.GetRepHandlers(_topic, repHandlers);
        if (!repHandlers.empty())
        {
          // There is a responser in my process, let's use it.
          IRepHandlerPtr repHandler = repHandlers.begin()->second;
          repHandler->RunLocalCallback(_topic, _req, _rep, _result);
          return true;
        }

        // Create a new request handler.
        std::shared_ptr<ReqHandler<T1, T2>> reqHandlerPtr(
          new ReqHandler<T1, T2>(this->nUuidStr));

        // Insert the request's parameters.
        reqHandlerPtr->SetMessage(_req);

        // Store the request handler.
        this->dataPtr->requests.AddReqHandler(
          _topic, this->nUuidStr, reqHandlerPtr);

        // If the responser's address is known, make the request.
        Addresses_M addresses;
        if (this->dataPtr->discovery->GetTopicAddresses(_topic, addresses))
          this->dataPtr->SendPendingRemoteReqs(_topic);
        else
        {
          // Discover the service call responser.
          this->dataPtr->discovery->Discover(true, _topic);
        }

        auto now = std::chrono::system_clock::now();

        // Wait until the REP is available.
        bool executed = reqHandlerPtr->condition.wait_until
          (lk, now + std::chrono::milliseconds(_timeout),
           [&reqHandlerPtr]
           {
             return reqHandlerPtr->repAvailable;
           });

        if (executed)
        {
          if (reqHandlerPtr->result)
            _rep.ParseFromString(reqHandlerPtr->rep);

          _result = reqHandlerPtr->result;
        }

        lk.unlock();

        return executed;
      }

      /// \brief The transport captures SIGINT and SIGTERM (czmq does) and
      /// the function will return true in that case. All the task threads
      /// will terminate.
      /// \return true if SIGINT or SIGTERM has been captured.
      public: bool Interrupted();

      /// \internal
      /// \brief Shared pointer to private data.
      protected: NodePrivatePtr dataPtr;

      /// \brief The list of topics subscribed by this node.
      private: std::vector<std::string> topicsSubscribed;

      /// \brief The list of topics advertised by this node.
      private: std::vector<std::string> topicsAdvertised;

      /// \brief The list of service calls advertised by this node.
      private: std::vector<std::string> srvsAdvertised;

      /// \brief Node UUID. This ID is unique for each node.
      private: uuid_t nUuid;

      /// \brief Node UUID in string format.
      private: std::string nUuidStr;
    };
  }
}
#endif
