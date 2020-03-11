/*
 * Copyright 2019, Proyectos y Sistemas de Mantenimiento SL (eProsima).
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

#ifndef EPROSIMA_DDS_SUB_DETAIL_RANK_HPP_
#define EPROSIMA_DDS_SUB_DETAIL_RANK_HPP_

//#include <dds/sub/detail/TRankImpl.hpp>

#include <cstdint>

/**
 * @cond
 * Ignore this file in the API
 */

namespace dds {
namespace sub {
namespace detail {

struct Rank
{
    //!Preview of the samples that follow within the sequence returned by the read or take operations.
    int32_t sample_rank = 0;

    //!Preview of the samples that follow within the sequence returned by the read or take operations.
    int32_t generation_rank = 0;

    //!Preview of what is available within DataReader.
    int32_t absolute_generation_rank = 0;
};

} //namespace detail
} //namespace sub
} //namespace dds

/** @endcond */

#endif //EPROSIMA_DDS_SUB_DETAIL_RANK_HPP_
