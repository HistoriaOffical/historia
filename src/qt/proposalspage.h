// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_PROPOSALSPAGE_H
#define BITCOIN_QT_PROPOSALSPAGE_H

#include "amount.h"
#include "arith_uint256.h"
#include "governance-vote.h"

#include <QWidget>
#include <QStringListModel>
#include <QStringListModel>
#include <QPushButton>

#include <memory>
#include <map>

class ClientModel;
class TransactionFilterProxy;
class TxViewDelegate;
class PlatformStyle;
class WalletModel;

namespace Ui {
    class ProposalsPage;
}

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

enum VoteButton {
    VOTE_YES,
    VOTE_NO,
    VOTE_ABSTAIN
};

/** Proposals ("home") page widget */
class ProposalsPage : public QWidget
{
    Q_OBJECT

public:
    explicit ProposalsPage(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~ProposalsPage();

    void setClientModel(ClientModel *clientModel);
    void setWalletModel(WalletModel *walletModel);

public Q_SLOTS:
    QStringList listProposals();
    void tabSelected(int tabIndex);

    void LaunchHLWAButtonClick();
    void sendVote(std::string, const std::string&, QPushButton*);

private:
    QTimer *timer;
    Ui::ProposalsPage *ui;
    ClientModel *clientModel;
    WalletModel *walletModel;
    QStringListModel *model;
    CAmount currentBalance;
    CAmount currentUnconfirmedBalance;
    CAmount currentImmatureBalance;
    CAmount currentAnonymizedBalance;
    CAmount currentWatchOnlyBalance;
    CAmount currentWatchUnconfBalance;
    CAmount currentWatchImmatureBalance;
    int nDisplayUnit;
    bool fShowAdvancedPSUI;

    TxViewDelegate *txdelegate;
    std::map<int, std::string> proposalsRow;

    void SetupTransactionList(int nNumItems);
    void DisablePrivateSendCompletely();
    void onProposalClicked();
    vote_outcome_enum_t findPreviousVote(const std::string&);
    void updateVotingButtons(std::string &);
private Q_SLOTS:
    void handleProposalClicked(const QModelIndex& index);
    void handleVoteButtonClicked(VoteButton, const std::string&, QPushButton *);
};

#endif // BITCOIN_QT_PROPOSALSPAGE_H
