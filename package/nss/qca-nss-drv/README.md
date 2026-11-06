# qca-nss-drv (override)

A copy of `qca-nss-drv` from kuncy7's `nss-packages` feed (branch
`ipq50xx-rebase`), carried here so this tree can add a patch of its own:

- `0150-nss_rmnet_rx-let-paged-frags-through.patch`: the raw-IP WAN bind
  handler sends paged frags to the firmware as a scatter-gather chain
  (what a deaggregation by reference would hand it; not in this tree),
  leaves GRO-merged segments to the stack (they came back without their
  checksum state and were dropped), and offers the injected buffers to
  the firmware for reuse as its empty buffers (`rmnet_reusable`, bounded
  by `rmnet_reuse_max`); kernel patch 0918 sizes rmnet's deaggregated
  copies so that they qualify.

Refresh from the feed by copying `Makefile`, `Config.in`, `files/` and
`patches/` over this directory and re-adding the patch above; it is also
meant to go upstream as a pull request to kuncy7/nss-packages.
