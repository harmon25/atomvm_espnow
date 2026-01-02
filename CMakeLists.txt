#
# This file is part of AtomVM.
#
# Copyright 2022 Fred Dushin <fred@dushin.net>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0 OR LGPL-2.1-or-later
#

set(ATOMVM_ESPNOW_COMPONENT_SRCS
    "nifs/atomvm_espnow.c"
    "nifs/espnow_driver.c"
)

if(IDF_VERSION_MAJOR LESS 5 OR (IDF_VERSION_MAJOR EQUAL 5 AND IDF_VERSION_MINOR LESS 5))
    message(FATAL_ERROR "atomvm_espnow requires ESP-IDF >= 5.5")
endif()

idf_component_register(
    SRCS ${ATOMVM_ESPNOW_COMPONENT_SRCS}
    INCLUDE_DIRS "nifs/include"
    PRIV_REQUIRES "libatomvm" "avm_sys" "esp_wifi" "esp_netif" "esp_event" "nvs_flash"
    WHOLE_ARCHIVE
)
