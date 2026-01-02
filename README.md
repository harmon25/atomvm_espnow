# AtomVM ESPNOW Driver/Library

An AtomVM Erlang/Elixir library + ESP-IDF component implementing ESP-NOW peer-to-peer wireless communication.

## Features

- Port-based driver with OTP-style message passing
- Singleton driver model: only one active port at a time
- Async RX/TX notifications delivered to owner process
- Peer management: add, modify, delete, check existence

### API

```erlang
%% Open/close
{ok, Port} = espnow:open([{channel, 1}]).
ok = espnow:close(Port).

%% Send data (broadcast or unicast)
ok = espnow:send(Port, broadcast, <<"hello">>).
ok = espnow:send(Port, <<Mac:6/binary>>, <<"hello">>).

%% Peer management
ok = espnow:add_peer(Port, Mac, Channel).
ok = espnow:mod_peer(Port, Mac, NewChannel).
ok = espnow:del_peer(Port, Mac).
true | false = espnow:peer_exists(Port, Mac).
```

### Messages

The owner process receives async messages:

```erlang
{espnow, rx, FromMacBin, DataBin}   %% Received data
{espnow, tx, broadcast | MacBin, StatusInt}  %% TX confirmation
```

### Options

| Option | Default | Description |
|--------|---------|-------------|
| `{channel, 0..14}` | `0` | WiFi channel (0 = don't change) |
| `{owner, pid()}` | `self()` | Process to receive RX/TX messages |

## Requirements

- ESP-IDF `>= 5.0`
- AtomVM with ESP-IDF support

## Quick Start

### Erlang

```erlang
start() ->
    {ok, Port} = espnow:open([{channel, 1}]),
    ok = espnow:send(Port, broadcast, <<"discovery">>),
    loop(Port).

loop(Port) ->
    receive
        {espnow, rx, FromMac, Data} ->
            io:format("Received ~p from ~p~n", [Data, FromMac]),
            %% Add peer for unicast reply
            _ = espnow:add_peer(Port, FromMac, 0),
            ok = espnow:send(Port, FromMac, <<"ack">>),
            loop(Port);
        {espnow, tx, _To, _Status} ->
            loop(Port)
    end.
```

### Elixir

```elixir
def start do
  {:ok, port} = :espnow.open([{:channel, 1}])
  :ok = :espnow.send(port, :broadcast, "discovery")
  loop(port)
end

defp loop(port) do
  receive do
    {:espnow, :rx, from_mac, data} ->
      IO.puts("Received #{inspect(data)} from #{inspect(from_mac)}")
      _ = :espnow.add_peer(port, from_mac, 0)
      :ok = :espnow.send(port, from_mac, "ack")
      loop(port)

    {:espnow, :tx, _to, _status} ->
      loop(port)
  end
end
```

## Building

For instructions on building AtomVM with this component, see the [AtomVM Build Instructions](https://doc.atomvm.net/build-instructions.html).

Add this component to your AtomVM ESP32 build's `components/` directory.

## Resources

- [ESP-IDF ESP-NOW Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_now.html)
- [AtomVM Documentation](https://doc.atomvm.net/)

## License

Apache License 2.0
