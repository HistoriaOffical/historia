#include "amount.h"

bool IsIpfsPeerIdValid(const std::string&, CAmount collateralAmount = -1);
bool IsIpfsPeerIdValidWithoutCollateral(const std::string&);
bool IsIpfsIdValid(const std::string&);
bool IsIpfsCidTypeValid(const std::string&);
bool IsIpfsIdDuplicate(const std::string& ipfsId);
bool IsIdentityValidWithCollateral(std::string identity, CAmount CollateralAmount = -1);
bool IsIdentityValidWithOutCollateral(std::string identity);
bool validateHigh(const std::string& identity);
bool validateLow(const std::string& identity);
bool validateDomainName(const std::string& label);
const std::string identityAllowedChars =
    "-abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
