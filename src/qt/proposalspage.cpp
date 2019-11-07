// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "proposalspage.h"
#include "ui_proposalspage.h"

#include "masternodelist.h"
#include "activemasternode.h"

#include "bitcoinunits.h"
#include "clientmodel.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "init.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "transactionfilterproxy.h"
#include "transactiontablemodel.h"
#include "utilitydialog.h"
#include "walletmodel.h"

#include "instantx.h"
//#include "darksendconfig.h"
#include "masternode-sync.h"
#include "governance.h"
#include "transport-curl.h"
#include "client.h"

#include <QAbstractItemDelegate>
#include <QPainter>
#include <QSettings>
#include <QTimer>
#include <QObject>
#include <QStringListModel>
#include <QDesktopServices>
#include <QUrl>
#include <ctime>
#include <iomanip>

#include "json.hpp"

using json = nlohmann::json;

#define ICON_OFFSET 16
#define DECORATION_SIZE 54
#define NUM_ITEMS 5
#define NUM_ITEMS_ADV 7

/*
class TxViewDelegate : public QAbstractItemDelegate
{
    Q_OBJECT
public:
    TxViewDelegate(const PlatformStyle *_platformStyle, QObject *parent=nullptr):
        QAbstractItemDelegate(parent), unit(BitcoinUnits::HTA),
        platformStyle(_platformStyle)
    {

    }

    inline void paint(QPainter *painter, const QStyleOptionViewItem &option,
                      const QModelIndex &index ) const
    {

    }

    inline QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        return QSize(DECORATION_SIZE, DECORATION_SIZE);
    }

    int unit;
    const PlatformStyle *platformStyle;

};
*/
#include "proposalspage.moc"

ProposalsPage::ProposalsPage(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ProposalsPage),
    clientModel(0),
    walletModel(0),
    currentBalance(-1),
    currentUnconfirmedBalance(-1),
    currentImmatureBalance(-1),
    currentWatchOnlyBalance(-1),
    currentWatchUnconfBalance(-1),
    currentWatchImmatureBalance(-1),
//    txdelegate(new TxViewDelegate(platformStyle, this)),
    timer(nullptr)
{
    ui->setupUi(this);

    int columnNameWidth = 100;
    int columnDateWidth = 200;
    int columnIPFSCIDWidth = 200;
    int columnVoteRatioWidth = 200;
    int columnSummaryWidth = 225;

    //model = new QStringListModel(this);
    //QStringList List = listProposals();
    //model->setStringList(List);
    //ui->listProposals->setModel(model);

    ui->tableWidgetApprovedRecords->setColumnWidth(0, columnDateWidth);
    ui->tableWidgetApprovedRecords->setColumnWidth(1, columnNameWidth);
    ui->tableWidgetApprovedRecords->setColumnWidth(2, columnVoteRatioWidth);
    ui->tableWidgetApprovedRecords->setColumnWidth(3, columnIPFSCIDWidth);
    ui->tableWidgetApprovedRecords->setColumnWidth(4, columnSummaryWidth);


    ui->tableWidgetApprovedProposals->setColumnWidth(0, columnDateWidth);
    ui->tableWidgetApprovedProposals->setColumnWidth(1, columnNameWidth);
    ui->tableWidgetApprovedProposals->setColumnWidth(2, columnVoteRatioWidth);
    ui->tableWidgetApprovedProposals->setColumnWidth(3, columnIPFSCIDWidth);
    ui->tableWidgetApprovedProposals->setColumnWidth(4, columnSummaryWidth);


    std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(0);
    for (const auto& pGovObj : objs) {
        if (pGovObj->GetObjectType() == GOVERNANCE_OBJECT_RECORD) {
        //if (pGovObj->GetObjectType() == GOVERNANCE_OBJECT_RECORD && pGovObj->IsSetRecordLocked()) {
            time_t creationTime = pGovObj->GetCreationTime();
            std::string const plainData = pGovObj->GetDataAsPlainString();
            nlohmann::json jsonData = json::parse(plainData);
            QString voteRatio = QString::number(pGovObj->GetYesCount(VOTE_SIGNAL_FUNDING)) + " / " + QString::number(pGovObj->GetNoCount(VOTE_SIGNAL_FUNDING)) + " / " + QString::number(pGovObj->GetAbstainCount(VOTE_SIGNAL_FUNDING));
	    nlohmann::json summaryData = jsonData["summary"];

            QTableWidgetItem* name = new QTableWidgetItem(QString::fromStdString(jsonData["name"].get<std::string>()));
            QTableWidgetItem* ipfscid = new QTableWidgetItem(QString::fromStdString(jsonData["ipfscid"].get<std::string>()));
            QTableWidgetItem* date = new QTableWidgetItem(QDateTime::fromTime_t(creationTime).toString("MMMM dd, yyyy"));
            QTableWidgetItem* Vote = new QTableWidgetItem(voteRatio);

            ui->tableWidgetApprovedRecords->insertRow(0);
	    std::string summaryName = summaryData["name"].get<std::string>();
	    std::string summaryDesc = summaryData["description"].get<std::string>();
	    QTableWidgetItem* summaryColumn = new QTableWidgetItem(QString::fromStdString(summaryName + ": " + summaryDesc));

	    ui->tableWidgetApprovedRecords->insertRow(0);
            ui->tableWidgetApprovedRecords->setItem(0, 0, date);
            ui->tableWidgetApprovedRecords->setItem(0, 1, name);
            ui->tableWidgetApprovedRecords->setItem(0, 2, Vote);
            ui->tableWidgetApprovedRecords->setItem(0, 3, ipfscid);
	    ui->tableWidgetApprovedRecords->setItem(0, 4, summaryColumn);
        }
    }
    ui->tableWidgetApprovedRecords->sortItems(0, Qt::DescendingOrder);
    ui->tableWidgetApprovedProposals->sortItems(0, Qt::DescendingOrder);
    
    connect(ui->tabWidget, SIGNAL(currentChanged(int)), this, SLOT(tabSelected(int)));
    connect(ui->tableWidgetApprovedRecords, SIGNAL(clicked(QModelIndex)), this,
        SLOT(handleProposalClicked(QModelIndex)));

}

ProposalsPage::~ProposalsPage()
{
    delete ui;
}


void ProposalsPage::setClientModel(ClientModel *model)
{
    this->clientModel = model;

}

void ProposalsPage::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
    /*
    if(model && model->getOptionsModel())
    {
        connect(model, SIGNAL(balanceChanged(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)), this, SLOT(setBalance(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)));
        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));

    }
*/
}

QStringList ProposalsPage::listProposals() {
    QStringList List;
    std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(0);

    for (const auto& pGovObj : objs) {
        QString proposalHash = QString::fromStdString(pGovObj->GetDataAsPlainString());
	    List << proposalHash;
	}

    return List;
}

void ProposalsPage::handleProposalClicked(const QModelIndex& index)
{
    if (!clientModel || ShutdownRequested()) {
        return;
    }

    std::string addr = clientModel->getRandomValidMN();
    std::string urltemp;

    if (ui->tabWidget->currentIndex() == 0) {
        QString ipfscid = ui->tableWidgetApprovedRecords->item(index.row(), 3)->text();
        urltemp = "http://" + addr + "/ipfs/" + ipfscid.toUtf8().constData() + "/Index.html";
    } else if (ui->tabWidget->currentIndex() == 1) {
        QString ipfscid = ui->tableWidgetApprovedProposals->item(index.row(), 3)->text();
        urltemp = "http://" + addr + "/ipfs/" + ipfscid.toUtf8().constData() + "/Index.html";
    }

    QString url = QString::fromUtf8(urltemp.c_str());
    LogPrintf("ProposalsPage::handleProposalClicked %s\n", urltemp);
    QDesktopServices::openUrl(QUrl(url, QUrl::TolerantMode));

}

void ProposalsPage::tabSelected(int tabIndex)
{
    ui->tableWidgetApprovedRecords->clearContents();
    ui->tableWidgetApprovedRecords->setRowCount(0);
    ui->tableWidgetApprovedProposals->clearContents();
    ui->tableWidgetApprovedProposals->setRowCount(0);
    
    if (tabIndex == 0) {
        std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(0);
        for (const auto& pGovObj : objs) {
            if (pGovObj->GetObjectType() == GOVERNANCE_OBJECT_RECORD) {
                //if (pGovObj->GetObjectType() == GOVERNANCE_OBJECT_RECORD && pGovObj->IsSetRecordLocked()) {
                time_t creationTime = pGovObj->GetCreationTime();
                std::string const plainData = pGovObj->GetDataAsPlainString();
                nlohmann::json jsonData = json::parse(plainData);
                QString voteRatio = QString::number(pGovObj->GetYesCount(VOTE_SIGNAL_FUNDING)) + " / " + QString::number(pGovObj->GetNoCount(VOTE_SIGNAL_FUNDING)) + " / " + QString::number(pGovObj->GetAbstainCount(VOTE_SIGNAL_FUNDING));

                QTableWidgetItem* name = new QTableWidgetItem(QString::fromStdString(jsonData["name"].get<std::string>()));
                QTableWidgetItem* ipfscid = new QTableWidgetItem(QString::fromStdString(jsonData["ipfscid"].get<std::string>()));
		nlohmann::json summaryData = jsonData["summary"];                
                QTableWidgetItem* date = new QTableWidgetItem(QDateTime::fromTime_t(creationTime).toString("MMMM dd, yyyy"));
                QTableWidgetItem* Vote = new QTableWidgetItem(voteRatio);
		std::string summaryName = summaryData["name"].get<std::string>();
		std::string summaryDesc = summaryData["description"].get<std::string>();
		QTableWidgetItem* summaryColumn = new QTableWidgetItem(QString::fromStdString(summaryName + ": " + summaryDesc));

                ui->tableWidgetApprovedRecords->insertRow(0);
                ui->tableWidgetApprovedRecords->setItem(0, 0, date);
                ui->tableWidgetApprovedRecords->setItem(0, 1, name);
                ui->tableWidgetApprovedRecords->setItem(0, 2, Vote);
                ui->tableWidgetApprovedRecords->setItem(0, 3, ipfscid);
		ui->tableWidgetApprovedRecords->setItem(0, 4, summaryColumn);

            }
        }

        connect(ui->tableWidgetApprovedProposals, SIGNAL(clicked(QModelIndex)), this,
            SLOT(handleProposalClicked(QModelIndex)));
    } else if (tabIndex == 1) {
        std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(0);
        for (const auto& pGovObj : objs) {
            if (pGovObj->GetObjectType() == GOVERNANCE_OBJECT_PROPOSAL) {
                //if (pGovObj->GetObjectType() == GOVERNANCE_OBJECT_PROPOSAL && pGovObj->IsSetCachedFunding()) {
                time_t creationTime = pGovObj->GetCreationTime();
                std::string const plainData = pGovObj->GetDataAsPlainString();
                nlohmann::json jsonData = json::parse(plainData);
                QString voteRatio = QString::number(pGovObj->GetYesCount(VOTE_SIGNAL_FUNDING)) + " / " + QString::number(pGovObj->GetNoCount(VOTE_SIGNAL_FUNDING)) + " / " + QString::number(pGovObj->GetAbstainCount(VOTE_SIGNAL_FUNDING));

		nlohmann::json summaryData = jsonData["summary"];

                QTableWidgetItem* name = new QTableWidgetItem(QString::fromStdString(jsonData["name"].get<std::string>()));
                QTableWidgetItem* ipfscid = new QTableWidgetItem(QString::fromStdString(jsonData["ipfscid"].get<std::string>()));

                QTableWidgetItem* date = new QTableWidgetItem(QDateTime::fromTime_t(creationTime).toString("MMMM dd, yyyy"));
                QTableWidgetItem* Vote = new QTableWidgetItem(voteRatio);
		std::string summaryName = summaryData["name"].get<std::string>();
		std::string summaryDesc = summaryData["description"].get<std::string>();
		QTableWidgetItem* summaryColumn = new QTableWidgetItem(QString::fromStdString(summaryName + ": " + summaryDesc));

                ui->tableWidgetApprovedProposals->insertRow(0);
                ui->tableWidgetApprovedProposals->setItem(0, 0, date);
                ui->tableWidgetApprovedProposals->setItem(0, 1, name);
                ui->tableWidgetApprovedProposals->setItem(0, 2, Vote);
                ui->tableWidgetApprovedProposals->setItem(0, 3, ipfscid);
		ui->tableWidgetApprovedProposals->setItem(0, 4, summaryColumn);
            }
        }

        connect(ui->tableWidgetApprovedProposals, SIGNAL(clicked(QModelIndex)), this,
            SLOT(handleProposalClicked(QModelIndex)));
    }


}
