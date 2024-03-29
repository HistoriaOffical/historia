// Copyright (c) 2014-2019 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MASTERNODE_UTILS_H
#define MASTERNODE_UTILS_H

#include "evo/deterministicmns.h"

class CConnman;

class CMasternodeUtils
{
public:
    typedef std::pair<arith_uint256, CDeterministicMNCPtr> score_pair_t;
    typedef std::vector<score_pair_t> score_pair_vec_t;
    typedef std::pair<int, const CDeterministicMNCPtr> rank_pair_t;
    typedef std::vector<rank_pair_t> rank_pair_vec_t;

public:
    static bool GetMasternodeScores(const uint256& nBlockHash, score_pair_vec_t& vecMasternodeScoresRet);
    static bool GetMasternodeRank(const COutPoint &outpoint, int& nRankRet, uint256& blockHashRet, int nBlockHeight = -1);

    static void ProcessMasternodeConnections(CConnman& connman);
    static void DoMaintenance(CConnman &connman);
    bool IsIdentityValid(std::string, CAmount CollateralAmount = -1);
    bool IsIpfsIdValidWithCollateral(const std::string& ipfsId, CAmount collateralAmount);
    bool IsIpfsIdValidWithoutCollateral(const std::string& ipfsId);
    bool CheckMasternodeDNS(std::string ExternalIP, std::string DNSName);

private:
    bool validateHigh(const std::string&);
    bool validateLow(const std::string&);
    bool validateDomainName(const std::string& label);
    const std::string identityAllowedChars =
      "-abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
	
};

#endif//MASTERNODE_UTILS_H
