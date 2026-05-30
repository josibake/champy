// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoinkernel.h>

#include <cstddef>
#include <cstdint>

int main()
{
    btck_Context* context{nullptr};
    btck_ContextOptions* context_options{nullptr};
    btck_ChainParameters* chain_parameters{nullptr};
    btck_ChainstateManager* chainman{nullptr};
    btck_ChainstateManagerOptions* chainman_options{nullptr};
    btck_BlockTreeEntry* block_entry{nullptr};

    return context == nullptr &&
                   context_options == nullptr &&
                   chain_parameters == nullptr &&
                   chainman == nullptr &&
                   chainman_options == nullptr &&
                   block_entry == nullptr ?
               0 :
               1;
}
