#include "ipfs-utils.h"
#include "governance.h"
#include "governance-validators.h"

#include "json.hpp"
#include <regex>


bool IsIpfsPeerIdValid(const std::string& ipfsId, CAmount collateralAmount)
{
    /** All alphanumeric characters except for "0", "I", "O", and "l" */
    std::string base58chars =
        "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

    if (collateralAmount == 100) // Skip voting nodes, as they don't use an IPFS peer cid
        return true;

    // CID v0 validation
    if (ipfsId.size() == 46 && ipfsId[0] == 'Q' && ipfsId[1] == 'm') {
        for (char c : ipfsId) {
            if (base58chars.find(c) == std::string::npos) {
                return false;
            }
        }
        return true;
    } else {
        // Assuming new IPFS peer id length constraints
        if (ipfsId.size() >= 46 && ipfsId.size() <= 90) {
            for (char c : ipfsId) {
                if (base58chars.find(c) == std::string::npos) {
                    return false;
                }
            }
            return true;
        }
    }
    return false;
}

bool IsIpfsPeerIdValidWithoutCollateral(const std::string& ipfsId)
{
    /** All alphanumeric characters except for "0", "I", "O", and "l" */
    std::string base58chars =
        "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

    // CID v0 validation
    if (ipfsId.size() == 46 && ipfsId[0] == 'Q' && ipfsId[1] == 'm') {
        for (char c : ipfsId) {
            if (base58chars.find(c) == std::string::npos) {
                return false;
            }
        }
        return true;
    } else {
        // Assuming new IPFS peer id length constraints
        if (ipfsId.size() >= 46 && ipfsId.size() <= 90) {
            for (char c : ipfsId) {
                if (base58chars.find(c) == std::string::npos) {
                    return false;
                }
            }
            return true;
        }
    }
    return false;
}

bool IsIpfsIdValid(const std::string& ipfsId)
{
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

bool IsIpfsCidTypeValid(const std::string& ipfsCidType)
{
	/** All digits */
	std::string digits =
	    "0123456789";

	if (ipfsCidType.size() != 1) {
		return false;
	}

	int l = ipfsCidType.length();
	for (int i = 0; i < l; i++)
		if (digits.find(ipfsCidType[i]) == -1)
			return false;

	return true;
}

bool IsIpfsIdDuplicate(const std::string& ipfsId)
{
    std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(0);
    std::string ipfsCid;

    for (auto& pGovObj : objs) {
        std::string const plainData = pGovObj->GetDataAsPlainString();
        nlohmann::json jsonData = nlohmann::json::parse(plainData);
        nlohmann::json ipfsCid = jsonData["ipfscid"];
        if (ipfsCid == ipfsId) {
            return true;
        }
    }
    return false;
}


bool IsIdentityValidWithCollateral(std::string identity, CAmount CollateralAmount)
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

    switch (CollateralAmount) {
    case 5000:
        valid = validateHigh(identity);
        break;
    case 100:
        valid = validateLow(identity);
        break;
    default:
        valid = false;
        break;
    }

    return valid;
}

bool IsIdentityValidWithOutCollateral(std::string identity)
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

    return true;
}


bool validateHigh(const std::string& identity)
{
    const char delim = '.';
    std::string label;
    size_t labelend = identity.find(delim);
    size_t labelstart = 0;

    while (labelend != std::string::npos) {
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

bool validateLow(const std::string& identity)
{
    if (identity.find_first_not_of(identityAllowedChars) != std::string::npos)
        return false;

    return true;
}

bool validateDomainName(const std::string& label)
{
    if (label.size() < 1 || label.size() > 63)
        return false;
    if (label.find_first_not_of(identityAllowedChars) != std::string::npos)
        return false;

    return true;
}
