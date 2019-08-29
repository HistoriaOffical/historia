#!/bin/bash
# use testnet settings,  if you need mainnet,  use ~/.historiacore/historiad.pid file instead
historia_pid=$(<~/.historiacore/testnet3/historiad.pid)
sudo gdb -batch -ex "source debug.gdb" historiad ${historia_pid}
