%%
%% AtomVM ESPNOW Erlang wrapper (WIP skeleton)
%%

-module(espnow).

-export([
    init/0, init/1, deinit/1,
    add_peer/3, send/3,
    recv/1, poll/1,
    active/1, active/2, active_stop/1
]).

%% NIF entrypoints (resolved by AtomVM)
-export([nif_init/1, nif_deinit/1, nif_add_peer/3, nif_send/3, nif_recv/1, nif_poll/1]).

-define(NIF_STUB, erlang:nif_error(nif_not_loaded)).

%% Public API

init() ->
    init(0).

%% @doc Initialize ESPNOW. Channel 0 means "leave unchanged".
init(Channel) when is_integer(Channel) ->
    %% Note: current implementation is singleton; returns {error, busy} if already initialized.
    nif_init(Channel).

deinit(Handle) when is_binary(Handle) ->
    nif_deinit(Handle).

%% @doc Add a peer. PeerMac is a 6-byte binary; Channel 0 means "current".
add_peer(Handle, PeerMac, Channel)
        when is_binary(Handle), is_binary(PeerMac), is_integer(Channel) ->
    nif_add_peer(Handle, PeerMac, Channel).

%% @doc Send data to a peer or broadcast.
%% To = broadcast | <<Mac:6/binary>>
send(Handle, To, Data)
        when is_binary(Handle), (To =:= broadcast orelse is_binary(To)), is_binary(Data) ->
    nif_send(Handle, To, Data).

%% @doc Non-blocking receive: returns {ok, FromMacBin, DataBin} | none.
recv(Handle) when is_binary(Handle) ->
    nif_recv(Handle).

%% @doc Non-blocking poll: returns one of:
%% - none
%% - {rx, FromMacBin, DataBin}
%% - {tx, broadcast | <<Mac:6/binary>>, StatusInt}
poll(Handle) when is_binary(Handle) ->
    nif_poll(Handle).

%% @doc Start active mode for the calling process.
%% Returns the spawned poller pid.
active(Handle) ->
    active(Handle, self()).

%% @doc Start active mode and forward messages to OwnerPid.
%% Messages have the shape:
%% - {espnow, rx, FromMacBin, DataBin}
%% - {espnow, tx, broadcast | <<Mac:6/binary>>, StatusInt}
active(Handle, OwnerPid) when is_binary(Handle), is_pid(OwnerPid) ->
    spawn(fun() -> active_loop(Handle, OwnerPid) end).

active_loop(Handle, OwnerPid) ->
    Ref = erlang:monitor(process, OwnerPid),
    active_loop(Handle, OwnerPid, Ref).

active_loop(Handle, OwnerPid, Ref) ->
    receive
        stop ->
            erlang:demonitor(Ref, [flush]),
            ok;
        {'DOWN', Ref, process, OwnerPid, _Reason} ->
            %% Best-effort cleanup when owner is gone.
            _ = catch deinit(Handle),
            ok
    after 0 ->
        %% Drain all available events without sleeping.
        case poll(Handle) of
            none ->
                timer:sleep(10),
                active_loop(Handle, OwnerPid, Ref);
            {rx, From, Data} ->
                OwnerPid ! {espnow, rx, From, Data},
                active_loop(Handle, OwnerPid, Ref);
            {tx, To, Status} ->
                OwnerPid ! {espnow, tx, To, Status},
                active_loop(Handle, OwnerPid, Ref);
            {error, _} = Err ->
                OwnerPid ! {espnow, error, Err},
                timer:sleep(100),
                active_loop(Handle, OwnerPid, Ref)
        end
    end.

%% @doc Stop an active-mode poller started by active/1,2.
active_stop(PollerPid) when is_pid(PollerPid) ->
    PollerPid ! stop,
    ok.

%% NIF stubs

nif_init(_Channel) -> ?NIF_STUB.

nif_deinit(_Handle) -> ?NIF_STUB.

nif_add_peer(_Handle, _PeerMac, _Channel) -> ?NIF_STUB.

nif_send(_Handle, _To, _Data) -> ?NIF_STUB.

nif_recv(_Handle) -> ?NIF_STUB.

nif_poll(_Handle) -> ?NIF_STUB.

