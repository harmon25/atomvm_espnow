%%
%% Copyright (c) 2021 dushin.net
%% All rights reserved.
%%
%% Licensed under the Apache License, Version 2.0 (the "License");
%% you may not use this file except in compliance with the License.
%% You may obtain a copy of the License at
%%
%%     http://www.apache.org/licenses/LICENSE-2.0
%%
%% Unless required by applicable law or agreed to in writing, software
%% distributed under the License is distributed on an "AS IS" BASIS,
%% WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
%% See the License for the specific language governing permissions and
%% limitations under the License.
%%

-module(espnow_example).


-export([start/0]).

%% @doc Minimal ESPNOW example.
%%
%% Initializes ESPNOW and then sleeps forever.
%%
%% Notes:
%% - This example is intentionally minimal while the driver is under development.
%% - Receive/send callbacks currently log from C (see nifs/espnow_driver.c).
start() ->
    %% Channel 0 means "do not change channel".
    Handle = espnow:init(0),
    _Poller = espnow:active(Handle, self()),
    timer:sleep(infinity).
