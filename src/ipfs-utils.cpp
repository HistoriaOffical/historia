#include "ipfs-utils.h"

bool IsIpfsIdValid(const std::string& ipfsId, CAmount collateralAmount) {
    /** All alphanumeric characters except for "0", "I", "O", and "l" */
    std::string base58chars =
	"123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

    if (ipfsId == "0" && collateralAmount != 100 * COIN)
	return false;
    
    /** https://docs.ipfs.io/guides/concepts/cid/ CID v0 */
    if (ipfsId.size() != 46 || ipfsId[0] !='Q' || ipfsId[1] !='m') {
	return false;
    }

    int l = ipfsId.length();
    for (int i = 0; i < l; i++)
	if (base58chars.find(ipfsId[i]) == -1)
	    return false;

    return true;
}

