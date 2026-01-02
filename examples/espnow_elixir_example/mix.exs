defmodule ESPNowExample.MixProject do
  use Mix.Project

  def project do
    [
      app: :espnow_example,
      version: "0.1.0",
      elixir: "~> 1.18",
      start_permanent: Mix.env() == :prod,
      deps: deps(),
      atomvm: [
        # Change to Lux.Test for minimal testing without networking
        start: ESPNowExample,
        flash_offset: 0x250000
      ]
    ]
  end

  # Run "mix help compile.app" to learn about applications.
  def application do
    [
      extra_applications: [:logger]
    ]
  end

  # Run "mix help deps" to learn about dependencies.
  defp deps do
    [
      {:pythonx, "~> 0.4.7", runtime: false},
      {:exatomvm, git: "https://github.com/atomvm/ExAtomVM/", runtime: false},
      {:espnow, path: "../../"}

      # {:dep_from_hexpm, "~> 0.3.0"},
      # {:dep_from_git, git: "https://github.com/elixir-lang/my_dep.git", tag: "0.1.0"}
    ]
  end
end
