// Copyright 2016 Proyectos y Sistemas de Mantenimiento SL (eProsima).
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/**
 * @file HelloWorldPublisher.cpp
 *
 */

#include "HelloWorldPublisher.h"
#include <fastrtps/attributes/ParticipantAttributes.h>
#include <fastrtps/attributes/PublisherAttributes.h>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/qos/PublisherQos.hpp>
#include <fastdds/dds/topic/DataWriter.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastrtps/utils/eClock.h>

#include <fastrtps/types/DynamicDataFactory.h>

#include <fastrtps/xmlparser/XMLProfileManager.h>

#include <thread>

using namespace eprosima::fastdds::dds;
using namespace eprosima::fastrtps;
using namespace eprosima::fastrtps::rtps;

HelloWorldPublisher::HelloWorldPublisher():mp_participant(nullptr),
mp_publisher(nullptr)
{
}

bool HelloWorldPublisher::init()
{
    xmlparser::XMLProfileManager::loadXMLFile("example_type.xml");

    types::DynamicType_ptr dyn_type = xmlparser::XMLProfileManager::getDynamicTypeByName("HelloWorld")->build();
    TypeSupport m_type(new types::DynamicPubSubType(dyn_type));
    m_Hello = types::DynamicDataFactory::get_instance()->create_data(dyn_type);

    m_Hello->set_string_value("Hello DDS Dynamic World", 0);
    m_Hello->set_uint32_value(0, 1);
    types::DynamicData* array = m_Hello->loan_value(2);
    array->set_uint32_value(10, array->get_array_index({0}));
    array->set_uint32_value(20, array->get_array_index({1}));
    array->set_uint32_value(30, array->get_array_index({2}));
    array->set_uint32_value(40, array->get_array_index({3}));
    array->set_uint32_value(50, array->get_array_index({4}));
    m_Hello->return_loaned_value(array);

    ParticipantAttributes PParam;
    PParam.rtps.builtin.domainId = 0;
    PParam.rtps.builtin.discovery_config.leaseDuration = c_TimeInfinite;
    PParam.rtps.setName("Participant_pub");
    mp_participant = DomainParticipantFactory::get_instance()->create_participant(PParam);

    if(mp_participant==nullptr)
        return false;

    //REGISTER THE TYPE
    mp_participant->register_type(m_type);

    //CREATE THE PUBLISHER
    //PublisherQos qos;
    PublisherAttributes Wparam;
    Wparam.topic.topicKind = NO_KEY;
    Wparam.topic.topicDataType = "HelloWorld";
    Wparam.topic.topicName = "HelloWorldTopic";
    Wparam.topic.auto_fill_xtypes = true; // Share the type with readers.
    Wparam.qos.m_reliability.kind = RELIABLE_RELIABILITY_QOS;
    //mp_publisher = mp_participant->create_publisher(qos, Wparam, nullptr);
    mp_publisher = mp_participant->create_publisher(PUBLISHER_QOS_DEFAULT, Wparam, nullptr);

    if(mp_publisher == nullptr)
        return false;

    // CREATE THE WRITER
    writer_ = mp_publisher->create_datawriter(Wparam.topic, Wparam.qos, &m_listener);

    if (writer_ == nullptr)
    {
        return false;
    }

    return true;

}

HelloWorldPublisher::~HelloWorldPublisher()
{
    DomainParticipantFactory::get_instance()->delete_participant(mp_participant);
}

void HelloWorldPublisher::PubListener::on_publication_matched(
        eprosima::fastdds::dds::DataWriter*,
        eprosima::fastrtps::rtps::MatchingInfo &info)
{
    if(info.status == MATCHED_MATCHING)
    {
        n_matched++;
        firstConnected = true;
        std::cout << "Publisher matched"<<std::endl;
    }
    else
    {
        n_matched--;
        std::cout << "Publisher unmatched"<<std::endl;
    }
}

void HelloWorldPublisher::runThread(uint32_t samples, uint32_t sleep)
{
    if (samples == 0)
    {
        while(!stop)
        {
            if(publish(false))
            {
                std::string message;
                m_Hello->get_string_value(message, 0);
                uint32_t index;
                m_Hello->get_uint32_value(index, 1);
                std::string aux_array = "[";
                types::DynamicData* array = m_Hello->loan_value(2);
                for (uint32_t i = 0; i < 5; ++i)
                {
                    uint32_t elem;
                    array->get_uint32_value(elem, array->get_array_index({i}));
                    aux_array += std::to_string(elem) + (i == 4 ? "]" : ", ");
                }
                m_Hello->return_loaned_value(array);
                std::cout << "Message: " << message << " with index: " << index
                          << " array: " << aux_array << " SENT" << std::endl;
            }
            eClock::my_sleep(sleep);
        }
    }
    else
    {
        for(uint32_t i = 0;i<samples;++i)
        {
            if(!publish())
                --i;
            else
            {
                std::string message;
                m_Hello->get_string_value(message, 0);
                uint32_t index;
                m_Hello->get_uint32_value(index, 1);
                std::string aux_array = "[";
                types::DynamicData* array = m_Hello->loan_value(2);
                for (uint32_t i = 0; i < 5; ++i)
                {
                    uint32_t elem;
                    array->get_uint32_value(elem, array->get_array_index({i}));
                    aux_array += std::to_string(elem) + (i == 4 ? "]" : ", ");
                }
                m_Hello->return_loaned_value(array);
                std::cout << "Message: " << message << " with index: " << index
                          << " array: " << aux_array << " SENT" << std::endl;
            }
            eClock::my_sleep(sleep);
        }
    }
}

void HelloWorldPublisher::run(uint32_t samples, uint32_t sleep)
{
    stop = false;
    std::thread thread(&HelloWorldPublisher::runThread, this, samples, sleep);
    if (samples == 0)
    {
        std::cout << "Publisher running. Please press enter to stop the Publisher at any time." << std::endl;
        std::cin.ignore();
        stop = true;
    }
    else
    {
        std::cout << "Publisher running " << samples << " samples." << std::endl;
    }
    thread.join();
}

bool HelloWorldPublisher::publish(bool waitForListener)
{
    if(m_listener.firstConnected || !waitForListener || m_listener.n_matched>0)
    {
        uint32_t index;
        m_Hello->get_uint32_value(index, 1);
        m_Hello->set_uint32_value(index+1, 1);

        types::DynamicData* array = m_Hello->loan_value(2);
        array->set_uint32_value(10 + index, array->get_array_index({0}));
        array->set_uint32_value(20 + index, array->get_array_index({1}));
        array->set_uint32_value(30 + index, array->get_array_index({2}));
        array->set_uint32_value(40 + index, array->get_array_index({3}));
        array->set_uint32_value(50 + index, array->get_array_index({4}));
        m_Hello->return_loaned_value(array);

        writer_->write(m_Hello.get());
        return true;
    }
    return false;
}
