defmodule ESPNowExample do
  import Bitwise

  @moduledoc """
  ESPNOW node discovery example for AtomVM Elixir.

  Each node periodically broadcasts a discovery request and responds to discovery
  requests with a unicast reply.

  Messages received from the port:
    - {:espnow, :rx, from_mac_bin, data_bin}
    - {:espnow, :tx, :broadcast | mac_bin, status_int}
  """

  @discover_req 1
  @discover_resp 2

  def start() do
    IO.puts("ESPNowExample starting...")

    # Open the ESPNOW port with channel 1
    channel = 1
    {:ok, port} = :espnow.open([{:channel, channel}])
    IO.puts("ESPNOW port opened on channel #{channel}")

    node_id = node_id()
    IO.puts("Node ID: #{node_id}")

    # Kick off discovery immediately
    send(self(), :discover_tick)

    loop(port, node_id, %{})
  end

  defp loop(port, node_id, peers) do
    receive do
      :discover_tick ->
        nonce = nonce()

        case :espnow.send(port, :broadcast, encode_discover_req(nonce, node_id)) do
          :ok ->
            :ok

          {:error, reason} ->
            IO.puts("broadcast send failed: #{inspect(reason)}")
        end

        Process.send_after(self(), :discover_tick, 3_000)
        loop(port, node_id, peers)

      {:espnow, :rx, from, data} when is_binary(from) and is_binary(data) ->
        case decode_discovery(data) do
          {:discover_req, nonce, peer_id} ->
            _ = :espnow.add_peer(port, from, 0)
            _ = :espnow.send(port, from, encode_discover_resp(nonce, node_id))
            loop(port, node_id, maybe_add_peer(from, peer_id, peers))

          {:discover_resp, _nonce, peer_id} ->
            loop(port, node_id, maybe_add_peer(from, peer_id, peers))

          :ignore ->
            loop(port, node_id, peers)
        end

      {:espnow, :tx, _to, _status} ->
        # TX confirmation - can be ignored or logged
        loop(port, node_id, peers)

      other ->
        IO.puts("Unknown message: #{inspect(other)}")
        loop(port, node_id, peers)
    after
      1000 ->
        loop(port, node_id, peers)
    end
  end

  defp maybe_add_peer(mac, peer_id, peers) when is_binary(mac) and is_integer(peer_id) do
    case Map.get(peers, mac) do
      nil ->
        IO.puts("discovered peer id=#{peer_id} mac=#{mac_to_string(mac)}")
        Map.put(peers, mac, peer_id)

      ^peer_id ->
        peers

      _old ->
        IO.puts("peer updated id=#{peer_id} mac=#{mac_to_string(mac)}")
        Map.put(peers, mac, peer_id)
    end
  end

  # Discovery message format (5 bytes total):
  # - 1 byte: type (req=1, resp=2)
  # - 2 bytes: nonce (little-endian u16)
  # - 2 bytes: node_id (little-endian u16)

  defp encode_discover_req(nonce, node_id)
       when is_integer(nonce) and is_integer(node_id) do
    <<@discover_req, nonce::little-unsigned-16, node_id::little-unsigned-16>>
  end

  defp encode_discover_resp(nonce, node_id)
       when is_integer(nonce) and is_integer(node_id) do
    <<@discover_resp, nonce::little-unsigned-16, node_id::little-unsigned-16>>
  end

  defp decode_discovery(
         <<@discover_req, nonce::little-unsigned-16, peer_id::little-unsigned-16, _rest::binary>>
       ) do
    {:discover_req, nonce, peer_id}
  end

  defp decode_discovery(
         <<@discover_resp, nonce::little-unsigned-16, peer_id::little-unsigned-16, _rest::binary>>
       ) do
    {:discover_resp, nonce, peer_id}
  end

  defp decode_discovery(_), do: :ignore

  defp node_id() do
    # 16-bit node ID (0-65535)
    :atomvm.random() &&& 0xFFFF
  end

  defp nonce() do
    # 16-bit nonce
    :atomvm.random() &&& 0xFFFF
  end

  defp mac_to_string(<<a, b, c, d, e, f>>) do
    :io_lib.format(~c"~2.16.0B:~2.16.0B:~2.16.0B:~2.16.0B:~2.16.0B:~2.16.0B", [a, b, c, d, e, f])
    |> :erlang.iolist_to_binary()
  end

  defp mac_to_string(_), do: "<invalid-mac>"
end
