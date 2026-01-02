defmodule EspnowElixirExample do
  @moduledoc """
  Minimal ESPNOW example for AtomVM Elixir.

  Starts ESPNOW and enables active mode delivery to the current process.

  Messages:
    - {:espnow, :rx, from_mac_bin, data_bin}
    - {:espnow, :tx, :broadcast | mac_bin, status_int}
    - {:espnow, :error, {:error, reason}}
  """

  def start() do
    # Channel 0 means "do not change channel".
    handle = :espnow.init(0)

    # Start active mode delivery to this process.
    _poller = :espnow.active(handle, self())

    loop()
  end

  defp loop() do
    receive do
      {:espnow, :rx, from, data} when is_binary(from) and is_binary(data) ->
        # For now, just keep running.
        loop()

      {:espnow, :tx, _to, _status} ->
        loop()

      {:espnow, :error, _err} ->
        loop()
    after
      1000 ->
        loop()
    end
  end
end
