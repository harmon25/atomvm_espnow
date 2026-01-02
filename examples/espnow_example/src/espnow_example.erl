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

-define(DISCOVER_REQ, 1).
-define(DISCOVER_RESP, 2).

%% @doc ESPNOW node discovery example.
%%
%% Each node periodically broadcasts a discovery request and responds to
%% discovery requests with a unicast reply.
%%
%% Run this program on 2+ devices on the same WiFi channel to see discovery.
start() ->
    %% For discovery to work reliably, all nodes must share a channel.
    %% Pick a fixed channel here (e.g. 1/6/11). Channel 0 means "leave unchanged".
    Channel = 1,
    {ok, Port} = espnow:open([{channel, Channel}]),

    NodeId = node_id16(),
    io:format("espnow_example: started (node_id=~p channel=~p)~n", [NodeId, Channel]),

    %% Kick off discovery immediately, then repeat.
    self() ! discover_tick,
    loop(Port, NodeId, []).

loop(Port, NodeId, Peers) ->
    receive
        discover_tick ->
            Nonce = nonce16(),
            ok = espnow:send(Port, broadcast, encode_discover_req(Nonce, NodeId)),
            erlang:send_after(3000, self(), discover_tick),
            loop(Port, NodeId, Peers);

        {espnow, rx, FromMac, Data} ->
            case decode_discovery(Data) of
                {discover_req, Nonce, PeerId} ->
                    %% Ensure we can unicast back to the sender.
                    _ = espnow:add_peer(Port, FromMac, 0),
                    _ = espnow:send(Port, FromMac, encode_discover_resp(Nonce, NodeId)),
                    NewPeers = maybe_add_peer(FromMac, PeerId, Peers),
                    loop(Port, NodeId, NewPeers);

                {discover_resp, _Nonce, PeerId} ->
                    NewPeers = maybe_add_peer(FromMac, PeerId, Peers),
                    loop(Port, NodeId, NewPeers);

                ignore ->
                    loop(Port, NodeId, Peers)
            end;

        {espnow, tx, _To, _Status} ->
            %% Optional: uncomment for TX status logging.
            %% io:format("tx ~p status=~p~n", [To, Status]),
            loop(Port, NodeId, Peers);

        {espnow, error, Err} ->
            io:format("espnow error: ~p~n", [Err]),
            loop(Port, NodeId, Peers)
    end.

maybe_add_peer(Mac, PeerId, Peers) ->
    case lists:keyfind(Mac, 1, Peers) of
        false ->
            io:format("discovered peer id=~p mac=~s~n", [PeerId, mac_to_string(Mac)]),
            [{Mac, PeerId} | Peers];
        {Mac, PeerId} ->
            Peers;
        {Mac, _OldId} ->
            io:format("peer updated id=~p mac=~s~n", [PeerId, mac_to_string(Mac)]),
            lists:keyreplace(Mac, 1, Peers, {Mac, PeerId})
    end.

%% Discovery message format (5 bytes total):
%% - 1 byte: type (req=1, resp=2)
%% - 2 bytes: nonce (little-endian u16)
%% - 2 bytes: node_id (little-endian u16)

encode_discover_req(Nonce, NodeId) ->
    <<?DISCOVER_REQ:8, Nonce:16/unsigned-little, NodeId:16/unsigned-little>>.

encode_discover_resp(Nonce, NodeId) ->
    <<?DISCOVER_RESP:8, Nonce:16/unsigned-little, NodeId:16/unsigned-little>>.

decode_discovery(<<?DISCOVER_REQ:8, Nonce:16/unsigned-little, PeerId:16/unsigned-little, _/binary>>) ->
    {discover_req, Nonce, PeerId};
decode_discovery(<<?DISCOVER_RESP:8, Nonce:16/unsigned-little, PeerId:16/unsigned-little, _/binary>>) ->
    {discover_resp, Nonce, PeerId};
decode_discovery(_) ->
    ignore.

node_id16() ->
    atomvm:random() band 16#FFFF.

nonce16() ->
    atomvm:random() band 16#FFFF.

mac_to_string(<<A:8, B:8, C:8, D:8, E:8, F:8>>) ->
    lists:flatten(io_lib:format("~2.16.0B:~2.16.0B:~2.16.0B:~2.16.0B:~2.16.0B:~2.16.0B", [A, B, C, D, E, F]));
mac_to_string(_) ->
    "<invalid-mac>".
