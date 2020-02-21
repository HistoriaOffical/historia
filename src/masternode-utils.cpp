// Copyright (c) 2014-2019 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "masternode-utils.h"

#include "init.h"
#include "masternode-sync.h"
#ifdef ENABLE_WALLET
#include "privatesend-client.h"
#endif
#include "validation.h"

struct CompareScoreMN
{
    bool operator()(const std::pair<arith_uint256, const CDeterministicMNCPtr&>& t1,
                    const std::pair<arith_uint256, const CDeterministicMNCPtr&>& t2) const
    {
        return (t1.first != t2.first) ? (t1.first < t2.first) : (t1.second->collateralOutpoint < t2.second->collateralOutpoint);
    }
};

bool CMasternodeUtils::GetMasternodeScores(const uint256& nBlockHash, score_pair_vec_t& vecMasternodeScoresRet)
{
    vecMasternodeScoresRet.clear();

    auto mnList = deterministicMNManager->GetListAtChainTip();
    auto scores = mnList.CalculateScores(nBlockHash);
    for (const auto& p : scores) {
        vecMasternodeScoresRet.emplace_back(p.first, p.second);
    }

    std::sort(vecMasternodeScoresRet.rbegin(), vecMasternodeScoresRet.rend(), CompareScoreMN());
    return !vecMasternodeScoresRet.empty();
}

bool CMasternodeUtils::GetMasternodeRank(const COutPoint& outpoint, int& nRankRet, uint256& blockHashRet, int nBlockHeight)
{
    nRankRet = -1;

    if (!masternodeSync.IsBlockchainSynced())
        return false;

    // make sure we know about this block
    blockHashRet = uint256();
    if (!GetBlockHash(blockHashRet, nBlockHeight)) {
        LogPrintf("CMasternodeUtils::%s -- ERROR: GetBlockHash() failed at nBlockHeight %d\n", __func__, nBlockHeight);
        return false;
    }

    score_pair_vec_t vecMasternodeScores;
    if (!GetMasternodeScores(blockHashRet, vecMasternodeScores))
        return false;

    int nRank = 0;
    for (const auto& scorePair : vecMasternodeScores) {
        nRank++;
        if(scorePair.second->collateralOutpoint == outpoint) {
            nRankRet = nRank;
            return true;
        }
    }

    return false;
}


void CMasternodeUtils::ProcessMasternodeConnections(CConnman& connman)
{
    std::vector<CDeterministicMNCPtr> vecDmns; // will be empty when no wallet
#ifdef ENABLE_WALLET
    privateSendClient.GetMixingMasternodesInfo(vecDmns);
#endif // ENABLE_WALLET

    connman.ForEachNode(CConnman::AllNodes, [&](CNode* pnode) {
        if (pnode->fMasternode && !connman.IsMasternodeQuorumNode(pnode)) {
#ifdef ENABLE_WALLET
            bool fFound = false;
            for (const auto& dmn : vecDmns) {
                if (pnode->addr == dmn->pdmnState->addr) {
                    fFound = true;
                    break;
                }
            }
            if (fFound) return; // do NOT disconnect mixing masternodes
#endif // ENABLE_WALLET
            LogPrintf("Closing Masternode connection: peer=%d, addr=%s\n", pnode->id, pnode->addr.ToString());
            pnode->fDisconnect = true;
        }
    });
}

void CMasternodeUtils::DoMaintenance(CConnman& connman)
{
    if(fLiteMode) return; // disable all Historia specific functionality

    if(!masternodeSync.IsBlockchainSynced() || ShutdownRequested())
        return;

    static unsigned int nTick = 0;

    nTick++;

    if(nTick % 60 == 0) {
        ProcessMasternodeConnections(connman);
    }
}

bool CMasternodeUtils::IsIdentityValid(std::string identity, CAmount CollateralAmount)
{
    bool valid = false;
    
    if (identity.size() == 0 || identity.size() > 255 || identity == "" || identity == "0")
	return false;

    auto mnList = deterministicMNManager->GetListAtChainTip();
    auto identities = mnList.GetIdentitiesInUse();
    for (const auto& p : identities) {
        if (p.c_str() == identity) {
            valid = false;
            return valid;
        }
    }

    switch(CollateralAmount) {
        case 5000 * COIN:
	        valid = validateHigh(identity);
	        break;
        case 100 * COIN:
	        valid = validateLow(identity);
	        break;
        default:
	        valid = false;
	        break;
    }
	
    return valid;
}

bool CMasternodeUtils::IsIpfsIdValidWithCollateral(const std::string& ipfsId, CAmount collateralAmount)
{
    //Check for in use IPFS Peer ID
    auto mnList = deterministicMNManager->GetListAtChainTip();
    auto ipfspeerids = mnList.GetIPFSPeerIdInUse();
    for (const auto& p : ipfspeerids) {
        if (p.c_str() == ipfsId && ipfsId != "0" && ipfsId != "") {
            return false;
        }
    }
    /** All alphanumeric characters except for "0", "I", "O", and "l" */
    std::string base58chars =
        "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

    if (collateralAmount == 100 * COIN)
        return true;

    /** https://docs.ipfs.io/guides/concepts/cid/ CID v0 */ 
    if (ipfsId.size() != 46 || ipfsId[0] != 'Q' || ipfsId[1] != 'm') {
        return false;
    }

    int l = ipfsId.length();
    for (int i = 0; i < l; i++)
        if (base58chars.find(ipfsId[i]) == -1)
            return false;

    return true;
}

bool CMasternodeUtils::IsIpfsIdValidWithoutCollateral(const std::string& ipfsId)
{
    auto mnList = deterministicMNManager->GetListAtChainTip();
    auto ipfspeerids = mnList.GetIPFSPeerIdInUse();
    for (const auto& p : ipfspeerids) {
        if (p.c_str() == ipfsId && ipfsId != "0" && ipfsId != "") {
            return false;
        }
    }
    /** All alphanumeric characters except for "0", "I", "O", and "l" */
    std::string base58chars =
        "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

    /** https://docs.ipfs.io/guides/concepts/cid/ CID v0 */
    if (ipfsId.size() != 46 || ipfsId[0] != 'Q' || ipfsId[1] != 'm') {
        return false;
    }

    int l = ipfsId.length();
    for (int i = 0; i < l; i++)
        if (base58chars.find(ipfsId[i]) == -1)
            return false;

    return true;
}

bool CMasternodeUtils::validateHigh(const std::string& identity)
{
    const char delim = '.';
    std::string label;
    size_t labelend = identity.find(delim);
    size_t labelstart = 0;

    while (labelend != std::string::npos)
    {
	label = identity.substr(labelstart, labelend - labelstart);
	if (!validateDomainName(label)) return false;

	labelstart = labelend + 1;
	labelend = identity.find(delim, labelstart);
    }
    // Last chunk
    label = identity.substr(labelstart);
    if (!validateDomainName(label)) return false;

    return true;
}

bool CMasternodeUtils::validateLow(const std::string& identity)
{
    if (identity.find_first_not_of(identityAllowedChars) != std::string::npos) 
	return false;
    
    return true;
}

bool CMasternodeUtils::validateDomainName(const std::string& label)
{    
    if (label.size() < 1 || label.size() > 63)
	return false;
    if (label.find_first_not_of(identityAllowedChars) != std::string::npos)
	return false;

    return true;
}
