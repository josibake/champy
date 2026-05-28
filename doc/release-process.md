Release Process
====================

## Branch updates

### Before every release candidate

* Update release candidate version in `CMakeLists.txt` (`CLIENT_VERSION_RC`).
* Update manpages (after rebuilding the binaries), see [gen-manpages.py](/contrib/devtools/README.md#gen-manpagespy).
* Update bitcoin.conf and commit changes if they exist, see [gen-bitcoin-conf.sh](/contrib/devtools/README.md#gen-bitcoin-confsh).

### Before every major and minor release

* Update [bips.md](bips.md) to account for changes since the last release.
* Update version in `CMakeLists.txt` (don't forget to set `CLIENT_VERSION_RC` to `0`).
* Update manpages (see previous section)
* Write release notes (see "Write the release notes" below) in doc/release-notes.md. If necessary,
  archive the previous release notes as doc/release-notes/release-notes-${VERSION}.md.

### Before every major release

* On both the master branch and the new release branch:
  - update `CLIENT_VERSION_MAJOR` in [`CMakeLists.txt`](../CMakeLists.txt)
* On the new release branch in [`CMakeLists.txt`](../CMakeLists.txt)(see [this commit](https://github.com/bitcoin/bitcoin/commit/742f7dd)):
  - set `CLIENT_VERSION_MINOR` to `0`
  - set `CLIENT_VERSION_BUILD` to `0`
  - set `CLIENT_VERSION_IS_RELEASE` to `true`
* Check with the security team whether there is any security advisory to pre-announce.

#### Before branch-off

* Update hardcoded [seeds](/contrib/seeds/README.md), see [this pull request](https://github.com/bitcoin/bitcoin/pull/27488) for an example.
* Update embedded asmap data at `/src/node/data/ip_asn.dat`, see [asmap data documentation](./asmap-data.md).
* Update the following variables in [`src/kernel/chainparams.cpp`](/src/kernel/chainparams.cpp) for mainnet, testnet, and signet:
  - `m_assumed_blockchain_size` and `m_assumed_chain_state_size` with the current size plus some overhead (see
    [this](#how-to-calculate-assumed-blockchain-and-chain-state-size) for information on how to calculate them).
  - The following updates should be reviewed with `reindex-chainstate` and `assumevalid=0` to catch any defect
    that causes rejection of blocks in the past history.
  - `chainTxData` with statistics about the transaction count and rate. Use the output of the `getchaintxstats` RPC with an
    `nBlocks` of 4096 (28 days) and a `bestblockhash` of RPC `getbestblockhash`; see
    [this pull request](https://github.com/bitcoin/bitcoin/pull/28591) for an example. Reviewers can verify the results by running
    `getchaintxstats <window_block_count> <window_final_block_hash>` with the `window_block_count` and `window_final_block_hash` from your output.
  - `defaultAssumeValid` with the output of RPC `getblockhash` using the `height` of `window_final_block_height` above
    (and update the block height comment with that height), taking into account the following:
    - On mainnet, the selected value must not be orphaned, so it may be useful to set the height two blocks back from the tip.
    - Testnet should be set with a height some tens of thousands back from the tip, due to reorgs there.
  - `nMinimumChainWork` with the "chainwork" value of RPC `getblockheader` using the same height as that selected for the previous step.
  - `m_assumeutxo_data` array should be appended to with the values returned by calling `bitcoin-cli -rpcclienttimeout=0 -named dumptxoutset utxo.dat rollback=<height or hash>`
    The same height considerations for `defaultAssumeValid` apply.
* Consider updating the headers synchronization tuning parameters to account for the chainparams updates.
  The optimal values change very slowly, so this isn't strictly necessary every release, but doing so doesn't hurt.
  - Update configuration variables in [`contrib/devtools/headerssync-params.py`](/contrib/devtools/headerssync-params.py):
    - Set `TIME` to the software's expected supported lifetime -- after this time, its ability to defend against a high bandwidth timewarp attacker will begin to degrade.
    - Set `MINCHAINWORK_HEADERS` to the height used for the `nMinimumChainWork` calculation above.
    - Check that the other variables still look reasonable.
  - Run the script. It works fine in CPython, but PyPy is much faster (seconds instead of minutes): `pypy3 contrib/devtools/headerssync-params.py`.
  - Paste the output defining the header `commitment_period` and `redownload_buffer_size` into the mainnet section of [`src/kernel/chainparams.cpp`](/src/kernel/chainparams.cpp).
- Clear the release notes and move them to the wiki (see "Write the release notes" below).

#### After branch-off (on the major release branch)

- Update the versions.
- Create the draft, named "*version* Release Notes Draft", as a [collaborative wiki](https://github.com/bitcoin-core/bitcoin-devwiki/wiki/_new).
- Clear the release notes: `cp doc/release-notes-empty-template.md doc/release-notes.md`
- Create a pinned meta-issue for testing the release candidate (see [this issue](https://github.com/bitcoin/bitcoin/issues/27621) for an example) and provide a link to it in the release announcements where useful.

#### Before final release

- Merge the release notes from [the wiki](https://github.com/bitcoin-core/bitcoin-devwiki/wiki/) into the branch.
- Ensure the "Needs release note" label is removed from all relevant pull
  requests and issues:
  https://github.com/bitcoin/bitcoin/issues?q=label%3A%22Needs+release+note%22

#### Tagging a release (candidate)

To tag the version (or release candidate) in git, use the `make-tag.py` script from [bitcoin-maintainer-tools](https://github.com/bitcoin-core/bitcoin-maintainer-tools). From the root of the repository run:

    ../bitcoin-maintainer-tools/make-tag.py v(new version, e.g. 25.0)

This will perform a few last-minute consistency checks in the build system files, and if they pass, create a signed tag.

## Release notes

### Write the release notes

Open a draft of the release notes for collaborative editing at https://github.com/bitcoin-core/bitcoin-devwiki/wiki.

For the period during which the notes are being edited on the wiki, the version on the branch should be wiped and replaced with a link to the wiki which should be used for all announcements until `-final`.

Generate list of authors:

    git log --format='- %aN' v(current version, e.g. 25.0)..v(new version, e.g. 25.1) | grep -v 'merge-script' | sort -fiu

## Building and signing

Release binaries, package metadata, dependency pinning, hardening policy, and
codesigning flows are maintained in the external packaging repository. For each
release candidate or final release:

- Point the packaging repository at the signed source tag.
- Record the packaging repository revision used for the build.
- Build the supported platform artifacts from the packaging repository.
- Generate and sign checksums for the resulting artifacts.
- Have independent builders verify that their artifacts and checksums match.
- Publish the artifacts and signed checksums together.

- After uploading release candidate binaries, notify the relevant mailing lists
  that a release candidate is available for testing. Include a link to the
  release notes draft.

- Archive the release notes for the new version to `doc/release-notes/release-notes-${VERSION}.md`
  (branch `master` and branch of the release).

- Update the bitcoincore.org website

  - blog post

  - maintained versions [table](https://github.com/bitcoin-core/bitcoincore.org/commits/master/_includes/posts/maintenance-table.md)

  - RPC documentation update

      - See https://github.com/bitcoin-core/bitcoincore.org/blob/master/contrib/doc-gen/


- Update repositories

  - Delete post-EOL [release branches](https://github.com/bitcoin/bitcoin/branches/all) and create a tag `v${branch_name}-final`.

  - Delete ["Needs backport" labels](https://github.com/bitcoin/bitcoin/labels?q=backport) for non-existing branches.

  - Update the external packaging repository with the new source tag and any
    packaging metadata changes.

  - Create a [new GitHub release](https://github.com/bitcoin/bitcoin/releases/new) with a link to the archived release notes

- Announce the release, along with any security advisory pre-announcements:

  - bitcoin-dev and bitcoin-core-dev mailing list

  - Bitcoin Core announcements list https://bitcoincore.org/en/list/announcements/join/

  - Bitcoin Core Twitter https://twitter.com/bitcoincoreorg

  - Celebrate

### Additional information

#### <a name="how-to-calculate-assumed-blockchain-and-chain-state-size"></a>How to calculate `m_assumed_blockchain_size` and `m_assumed_chain_state_size`

Both variables are used as a guideline for how much space the user needs on their drive in total, not just strictly for the blockchain.
Note that all values should be taken from a **fully synced** node and have an overhead of 5-10% added on top of its base value.

To calculate `m_assumed_blockchain_size`, take the size in GiB of these directories:
- For `mainnet` -> the data directory, excluding the `/testnet3`, `/testnet4`, `/signet`, and `/regtest` directories and any overly large files, e.g. a huge `debug.log`
- For `testnet` -> `/testnet3`
- For `testnet4` -> `/testnet4`
- For `signet` -> `/signet`

To calculate `m_assumed_chain_state_size`, take the size in GiB of these directories:
- For `mainnet` -> `/chainstate`
- For `testnet` -> `/testnet3/chainstate`
- For `testnet4` -> `/testnet4/chainstate`
- For `signet` -> `/signet/chainstate`

Notes:
- When taking the size for `m_assumed_blockchain_size`, there's no need to exclude the `/chainstate` directory since it's a guideline value and an overhead will be added anyway.
- The expected overhead for growth may change over time. Consider whether the percentage needs to be changed in response; if so, update it here in this section.
