# qca-nss-drv (override)

A copy of `qca-nss-drv` from kuncy7's `nss-packages` feed (branch
`ipq50xx-rebase`), carried here so this tree can add a patch of its own:

- `0150-nss_rmnet_rx-let-paged-frags-through.patch`: the raw-IP WAN bind
  handler sends paged frags to the firmware as a scatter-gather chain
  (zero-copy rmnet deaggregation) and leaves GRO-merged segments to the
  stack (they came back without their checksum state and were dropped).

Refresh from the feed by copying `Makefile`, `Config.in`, `files/` and
`patches/` over this directory and re-adding the patch above; it is also
meant to go upstream as a pull request to kuncy7/nss-packages.
